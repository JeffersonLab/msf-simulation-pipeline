// CsvSink.cpp — the three backends behind one write_line().
#include "CsvSink.hpp"

#include <fmt/core.h>
#include <mz.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include <sys/stat.h>

#include <cerrno>
#include <climits>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <system_error>

namespace {

constexpr size_t buffer_flush_size = 64 * 1024;

[[noreturn]] void fail(const std::filesystem::path& path, const std::string& what) {
    throw std::runtime_error(fmt::format("{}: {}", path.string(), what));
}

/// minizip-ng creates its file with mode 0640; give the finished file the mode fopen() would
/// have produced (0666 masked by the process umask), so every backend's output has the same permissions.
void apply_default_file_mode(const std::filesystem::path& path) {
    const mode_t current_umask = umask(0);
    umask(current_umask);
    std::error_code ignored;
    std::filesystem::permissions(path, static_cast<std::filesystem::perms>(0666 & ~current_umask), ignored);
}

}  // namespace

CsvSink::CsvSink(std::filesystem::path final_path, Compression compression, int compression_level, std::string zip_entry_name)
    : final_path_(std::move(final_path)),
      tmp_path_(final_path_.string() + ".tmp"),
      compression_(compression),
      compression_level_(compression_level),
      zip_entry_name_(std::move(zip_entry_name)) {
    buffer_.reserve(buffer_flush_size + 4096);
    open_backend();
}

CsvSink::~CsvSink() {
    if (!closed_) discard();
}

void CsvSink::write_line(std::string_view line) {
    buffer_.append(line);
    buffer_.push_back('\n');
    ++lines_;
    if (buffer_.size() >= buffer_flush_size) flush_buffer();
}

void CsvSink::finish() {
    if (closed_) return;
    flush_buffer();
    close_backend();
    std::error_code error;
    std::filesystem::rename(tmp_path_, final_path_, error);
    if (error) fail(tmp_path_, fmt::format("cannot rename to {}: {}", final_path_.string(), error.message()));
    if (compression_ == Compression::zip) apply_default_file_mode(final_path_);
    closed_ = true;
}

void CsvSink::discard() {
    if (closed_) return;
    closed_ = true;
    try {
        close_backend();
    } catch (const std::exception&) {
        // The file is deleted next; a close error on a discarded output changes nothing.
    }
    std::error_code ignored;
    std::filesystem::remove(tmp_path_, ignored);
}

void CsvSink::flush_buffer() {
    if (buffer_.empty()) return;
    write_backend(buffer_.data(), buffer_.size());
    bytes_ += buffer_.size();
    buffer_.clear();
}

void CsvSink::open_backend() {
    const std::filesystem::path directory = final_path_.parent_path();
    if (!directory.empty()) {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) fail(directory, fmt::format("cannot create the output directory: {}", error.message()));
    }

    switch (compression_) {
        case Compression::none:
            plain_file_ = std::fopen(tmp_path_.c_str(), "wb");
            if (!plain_file_) fail(tmp_path_, fmt::format("cannot open for writing: {}", std::strerror(errno)));
            break;

        case Compression::gz:
            gz_file_ = gzopen(tmp_path_.c_str(), fmt::format("wb{}", compression_level_).c_str());
            if (!gz_file_) fail(tmp_path_, fmt::format("cannot open for gzip writing: {}", std::strerror(errno)));
            break;

        case Compression::zip: {
            zip_writer_ = mz_zip_writer_create();
            if (!zip_writer_) fail(tmp_path_, "minizip: cannot create a zip writer");
            mz_zip_writer_set_compress_method(zip_writer_, MZ_COMPRESS_METHOD_DEFLATE);
            mz_zip_writer_set_compress_level(zip_writer_, static_cast<int16_t>(compression_level_));
            int32_t status = mz_zip_writer_open_file(zip_writer_, tmp_path_.c_str(), 0, 0);
            if (status != MZ_OK) fail(tmp_path_, fmt::format("minizip: cannot open the archive for writing (error {})", status));

            mz_zip_file entry{};
            entry.filename = zip_entry_name_.c_str();
            entry.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
            entry.modified_date = std::time(nullptr);
            entry.version_madeby = MZ_VERSION_MADEBY;
            entry.external_fa = 0100644u << 16;   // a regular file with mode rw-r--r-- in the POSIX bits
            // Sizes are unknown while streaming and exceed 4 GB for trk_hits: force the ZIP64
            // record layout so every archive has the same structure whatever its size.
            entry.zip64 = MZ_ZIP64_FORCE;
            status = mz_zip_writer_entry_open(zip_writer_, &entry);
            if (status != MZ_OK) fail(tmp_path_, fmt::format("minizip: cannot open the entry {} (error {})", zip_entry_name_, status));
            break;
        }
    }
    backend_open_ = true;
}

void CsvSink::write_backend(const char* data, size_t size) {
    switch (compression_) {
        case Compression::none:
            if (std::fwrite(data, 1, size, plain_file_) != size) {
                fail(tmp_path_, fmt::format("write failed: {}", std::strerror(errno)));
            }
            break;

        case Compression::gz:
            while (size > 0) {
                const unsigned chunk = size > (1u << 30) ? (1u << 30) : static_cast<unsigned>(size);
                const int written = gzwrite(gz_file_, data, chunk);
                if (written <= 0) {
                    int error_number = 0;
                    const char* message = gzerror(gz_file_, &error_number);
                    fail(tmp_path_, fmt::format("gzip write failed: {}", message ? message : "unknown error"));
                }
                data += written;
                size -= static_cast<size_t>(written);
            }
            break;

        case Compression::zip:
            while (size > 0) {
                const int32_t chunk = size > static_cast<size_t>(INT32_MAX) ? INT32_MAX : static_cast<int32_t>(size);
                const int32_t written = mz_zip_writer_entry_write(zip_writer_, data, chunk);
                if (written <= 0) fail(tmp_path_, fmt::format("minizip: entry write failed (error {})", written));
                data += written;
                size -= static_cast<size_t>(written);
            }
            break;
    }
}

void CsvSink::close_backend() {
    if (!backend_open_) return;
    backend_open_ = false;
    switch (compression_) {
        case Compression::none: {
            const int status = std::fclose(plain_file_);
            plain_file_ = nullptr;
            if (status != 0) fail(tmp_path_, fmt::format("close failed: {}", std::strerror(errno)));
            break;
        }
        case Compression::gz: {
            const int status = gzclose(gz_file_);
            gz_file_ = nullptr;
            if (status != Z_OK) fail(tmp_path_, fmt::format("gzip close failed (zlib error {})", status));
            break;
        }
        case Compression::zip: {
            const int32_t entry_status = mz_zip_writer_entry_close(zip_writer_);
            const int32_t archive_status = mz_zip_writer_close(zip_writer_);
            mz_zip_writer_delete(&zip_writer_);
            if (entry_status != MZ_OK) fail(tmp_path_, fmt::format("minizip: entry close failed (error {})", entry_status));
            if (archive_status != MZ_OK) fail(tmp_path_, fmt::format("minizip: archive close failed (error {})", archive_status));
            break;
        }
    }
}
