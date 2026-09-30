// Writer.cpp — the part of a writer that is the same for every role: output file, header, failure marking, summary.
#include "Writer.hpp"

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <cstdio>
#include <stdexcept>
#include <system_error>

namespace {

/// Opens <out_dir>/<csv_name>[.zip|.gz] as a .tmp and writes the header line.
std::unique_ptr<CsvSink> open_sink(const Options& options, const std::string& csv_name, const std::string& header) {
    std::string file_name = csv_name;
    if (options.compression == Compression::zip) file_name += ".zip";
    if (options.compression == Compression::gz) file_name += ".gz";
    auto sink = std::make_unique<CsvSink>(std::filesystem::path(options.out_dir) / file_name, options.compression, options.zip_level, csv_name);
    sink->write_line(header);
    return sink;
}

}  // namespace

void Writer::begin(const Options& options) {
    sink_ = open_sink(options, options.stem + "." + role() + ".csv", csv_header());
    for (const ExtraOutput& extra : extra_outputs()) {
        extra_sinks_.emplace_back(extra.suffix, open_sink(options, options.stem + "." + role() + "_" + extra.suffix + ".csv", extra.header));
    }
}

void Writer::event(const EventInputs& inputs) {
    if (failed_) return;
    try {
        write_event(inputs);
    } catch (const std::exception& error) {
        failed_ = true;
        failure_ = fmt::format("entry {}: {}", inputs.entry_index, error.what());
        fmt::print(stderr, "writer {} failed at {}; its output is discarded, the other writers continue\n", role(), failure_);
    }
}

void Writer::end() {
    if (!sink_) return;
    // A close or rename failure (a full /scratch at the end of a long run) marks this writer
    // failed and must not throw out of main's loop: the other writers still have to finish.
    if (!failed_) {
        try {
            sink_->finish();
            for (auto& [suffix, sink] : extra_sinks_) sink->finish();
        } catch (const std::exception& error) {
            failed_ = true;
            failure_ = fmt::format("closing the output: {}", error.what());
            fmt::print(stderr, "writer {} failed while {}; its output is discarded\n", role(), failure_);
        }
    }
    if (failed_) {
        sink_->discard();
        for (auto& [suffix, sink] : extra_sinks_) sink->discard();
    }
}

std::string Writer::missing_inputs(const Options& options) const {
    const Needs needed = needs();
    std::string missing;
    if (needed.reco && !options.reco_file) missing = "--reco";
    if (needed.sim && !options.sim_file) missing += missing.empty() ? "--sim" : " and --sim";
    return missing;
}

void Writer::write_row(const std::string& line) {
    sink_->write_line(line);
}

void Writer::write_extra_row(const std::string& suffix, const std::string& line) {
    for (auto& [declared_suffix, sink] : extra_sinks_) {
        if (declared_suffix == suffix) {
            sink->write_line(line);
            return;
        }
    }
    throw std::logic_error(fmt::format("writer {}: extra output '{}' is not declared in extra_outputs()", role(), suffix));
}

SystemInfo Writer::system_of(uint64_t cell_id) {
    SystemInfo info = system_of_cell(cell_id);
    if (!info.known) unknown_system_ids_.insert(info.id);
    return info;
}

uint64_t Writer::rows_written() const {
    return sink_ && sink_->lines() > 0 ? sink_->lines() - 1 : 0;   // the header is not a row
}

std::filesystem::path Writer::output_path() const {
    return sink_ ? sink_->path() : std::filesystem::path{};
}

std::string Writer::summary_line() const {
    std::string line = fmt::format("  {:<14}", role());
    if (failed_) {
        line += fmt::format(" FAILED at {} — output discarded", failure_);
    } else {
        std::error_code ignored;
        const auto size_on_disk = std::filesystem::file_size(output_path(), ignored);
        line += fmt::format(" {:>12} rows {:>9.1f} MB csv {:>9.1f} MB on disk  {}",
                            rows_written(), sink_->bytes() / 1e6, ignored ? 0.0 : size_on_disk / 1e6, output_path().string());
    }
    if (!unknown_system_ids_.empty()) {
        line += fmt::format("  unknown system ids: {}", fmt::join(unknown_system_ids_, ", "));
    }
    for (const auto& [suffix, sink] : extra_sinks_) {
        if (!failed_) line += fmt::format("  + _{}: {} rows", suffix, sink->lines() > 0 ? sink->lines() - 1 : 0);
    }
    const std::string notes = summary_notes();
    if (!notes.empty()) line += "  " + notes;
    return line;
}
