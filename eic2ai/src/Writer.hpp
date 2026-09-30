// Writer.hpp — what a writer is: one CSV role, the inputs it needs, and write_event.
//
// main.cpp drives every writer the same way: begin(options) opens the output and writes the
// header, event(inputs) once per entry, end() closes and renames (or discards after a failure).
// A concrete writer (writer_*.hpp) implements role(), needs(), collections(), csv_header() and
// write_event(); it formats rows with fmt::format and hands them to write_row().
//
// Failure policy: an exception out of write_event marks this writer failed; its output is
// discarded at end() and the run continues for the other writers, exit code 1.
// Trap: a writer must not keep anything from EventInputs beyond write_event; the frames die at
// the end of the loop iteration in main.cpp.
#pragma once

#include "CsvSink.hpp"
#include "EventInputs.hpp"
#include "Options.hpp"
#include "podio_helpers.hpp"
#include "system_ids.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <utility>
#include <string>
#include <vector>

/// Which input files a writer needs. main.cpp refuses to start when a need is unmet.
struct Needs {
    bool reco = false;   // --reco FILE.edm4eic.root
    bool sim = false;    // --sim FILE.edm4hep.root
};

/// An additional output of a multi-output writer: <stem>.<role>_<suffix>.csv[.zip] with its own header.
struct ExtraOutput {
    std::string suffix;
    std::string header;
};

class Writer {
public:
    virtual ~Writer() = default;

    virtual std::string role() const = 0;                       // "trk_hits" → <stem>.trk_hits.csv[.zip]
    virtual Needs needs() const = 0;
    virtual std::vector<std::string> collections() const = 0;   // collections read; --list-writers shows them
    virtual std::string csv_header() const = 0;                 // written before the first event
    virtual std::vector<ExtraOutput> extra_outputs() const { return {}; }   // multi-output writers declare their extra files

    void begin(const Options& options);
    void event(const EventInputs& inputs);
    void end();

    /// "" when the options give every input this writer needs; otherwise the missing option, "--reco" or "--sim".
    std::string missing_inputs(const Options& options) const;
    bool failed() const { return failed_; }
    uint64_t rows_written() const;
    std::filesystem::path output_path() const;
    /// One line for the end-of-run summary: rows, size and path, or the failure.
    std::string summary_line() const;

protected:
    virtual void write_event(const EventInputs& inputs) = 0;
    /// Extra text for the summary line, for counters a writer keeps (default: none).
    virtual std::string summary_notes() const { return {}; }

    void write_row(const std::string& line);
    /// A row of an extra output declared in extra_outputs().
    void write_extra_row(const std::string& suffix, const std::string& line);
    /// System id and name of a cellID. An id missing from the table is "Unknown" and recorded for the summary.
    SystemInfo system_of(uint64_t cell_id);

    SkipNotes skip_notes;   // skip_notes.note(collection, why): prints once per collection

private:
    std::unique_ptr<CsvSink> sink_;
    std::vector<std::pair<std::string, std::unique_ptr<CsvSink>>> extra_sinks_;   // (suffix, sink) in declaration order
    bool failed_ = false;
    std::string failure_;
    std::set<uint64_t> unknown_system_ids_;
};
