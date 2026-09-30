// main.cpp — eic2ai: read each event once, hand it to every enabled writer, write one CSV per role.
//
// The story, top to bottom: parse the command line, build the writers, check that every writer
// has the input file it needs, open the inputs, open the outputs, loop over entries, close,
// print the summary. Everything else lives in the file named after it: Options, PairReader,
// EventInputs, Writer, WritersRegistry, CsvSink.
//
// Failure policy: a reader failure (unreadable file, pairing guard) throws out of the loop; the
// writers are destroyed with their sinks, which remove their .tmp files, and the exit code is 2.
// A failure inside one writer marks that writer alone (see Writer.hpp); exit code 1.
#include "Options.hpp"
#include "PairReader.hpp"
#include "Writer.hpp"
#include "WritersRegistry.hpp"

#include <fmt/core.h>
#include <sys/resource.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using Writers = std::vector<std::unique_ptr<Writer>>;

/// Stops before any output is opened when a writer's input file is missing.
void check_needs(const Writers& writers, const Options& options) {
    std::string problems;
    for (const auto& writer : writers) {
        const std::string missing = writer->missing_inputs(options);
        if (!missing.empty()) problems += fmt::format("writer {} needs {}, which was not given\n", writer->role(), missing);
    }
    if (!problems.empty()) throw std::runtime_error(problems + "no output was opened");
}

/// Every collection any enabled writer reads, for --selective.
std::vector<std::string> collections_of(const Writers& writers) {
    std::vector<std::string> names;
    for (const auto& writer : writers) {
        for (const std::string& name : writer->collections()) names.push_back(name);
    }
    return names;
}

double peak_memory_mb() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss / 1024.0;   // ru_maxrss is in kilobytes on Linux
}

void print_summary(const PairReader& reader, const Writers& writers, uint64_t events_processed, double wall_seconds) {
    fmt::print("\neic2ai summary\n");
    fmt::print("  events processed: {} of {} ({})\n", events_processed, reader.entries(), reader.describe());
    for (const auto& writer : writers) fmt::print("{}\n", writer->summary_line());
    fmt::print("  wall time {:.1f} s, peak memory {:.0f} MB\n", wall_seconds, peak_memory_mb());
}

bool any_failed(const Writers& writers) {
    for (const auto& writer : writers) {
        if (writer->failed()) return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const auto start_time = std::chrono::steady_clock::now();
    try {
        const Options options = parse_options(argc, argv);
        if (options.show_help) {
            fmt::print("{}", usage_text());
            return 0;
        }
        if (options.list_writers) {
            fmt::print("{}", list_writers_text());
            return 0;
        }
        if (options.list_columns) {
            fmt::print("{}", list_columns_text());
            return 0;
        }

        Writers writers = options.writers_auto ? make_writers_for_inputs(options) : make_writers(options.writer_roles);
        check_needs(writers, options);

        PairReader reader(options.reco_file, options.sim_file);
        if (options.selective) reader.restrict_to(collections_of(writers));
        fmt::print("eic2ai: {}\n", reader.describe());
        for (auto& writer : writers) writer->begin(options);

        const uint64_t events_to_process = options.events_to_process(reader.entries());
        uint64_t events_processed = 0;
        for (uint64_t entry_index = 0; entry_index < events_to_process; ++entry_index) {
            const EventInputs inputs = reader.read(entry_index);
            for (auto& writer : writers) writer->event(inputs);
            ++events_processed;
            if (options.progress_every > 0 && events_processed % options.progress_every == 0) {
                fmt::print("  {} / {} events\n", events_processed, events_to_process);
                std::fflush(stdout);
            }
        }
        for (auto& writer : writers) writer->end();

        const double wall_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
        print_summary(reader, writers, events_processed, wall_seconds);
        return any_failed(writers) ? 1 : 0;
    } catch (const std::invalid_argument& error) {
        fmt::print(stderr, "eic2ai: {}\nRun eic2ai --help for the options.\n", error.what());
        return 2;
    } catch (const std::exception& error) {
        fmt::print(stderr, "eic2ai: {}\n", error.what());
        return 2;
    }
}
