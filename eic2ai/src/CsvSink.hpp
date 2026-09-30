// CsvSink.hpp — one CSV output file: buffered lines, plain / zip / gzip backends, tmp + rename.
//
// Used by Writer (one sink per output). Lines are buffered (64 KB) and pushed to the backend:
// the zip backend streams into one deflate entry of a ZIP64 archive (minizip-ng), the gz
// backend into a gzip stream (zlib), the plain backend into the file itself.
//
// The sink writes "<final>.tmp" and renames it to <final> in finish(). A sink destroyed without
// finish() removes the .tmp, so an aborted run leaves no half-written deliverable.
// Trap: consumers read the zip with pandas (compression='zip', exactly one entry) and with
// zipfile.ZipFile(name): keep one entry, named "<stem>.<role>.csv".
#pragma once

#include "Options.hpp"

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

class CsvSink {
public:
    /// Opens <final_path>.tmp at once. zip_entry_name names the entry inside a zip archive;
    /// compression_level is the deflate level (1 to 9) for the zip and gz backends.
    CsvSink(std::filesystem::path final_path, Compression compression, int compression_level, std::string zip_entry_name);
    ~CsvSink();
    CsvSink(const CsvSink&) = delete;
    CsvSink& operator=(const CsvSink&) = delete;

    /// Appends the line and a newline.
    void write_line(std::string_view line);
    /// Flushes, closes the backend and renames .tmp → final. Throws on any I/O error.
    void finish();
    /// Closes and deletes the .tmp; the final path never appears. Never throws.
    void discard();

    const std::filesystem::path& path() const { return final_path_; }
    uint64_t lines() const { return lines_; }
    uint64_t bytes() const { return bytes_; }   // uncompressed bytes handed to the backend

private:
    void open_backend();
    void write_backend(const char* data, size_t size);
    void close_backend();
    void flush_buffer();

    std::filesystem::path final_path_;
    std::filesystem::path tmp_path_;
    Compression compression_;
    int compression_level_;
    std::string zip_entry_name_;
    std::string buffer_;
    uint64_t lines_ = 0;
    uint64_t bytes_ = 0;
    bool backend_open_ = false;
    bool closed_ = false;                // finish() or discard() ran
    std::FILE* plain_file_ = nullptr;    // Compression::none
    gzFile gz_file_ = nullptr;           // Compression::gz
    void* zip_writer_ = nullptr;         // Compression::zip, a minizip-ng writer handle
};
