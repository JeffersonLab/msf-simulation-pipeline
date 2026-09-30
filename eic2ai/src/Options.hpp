// Options.hpp — everything the command line configures, in one struct filled by one function.
//
// Used by main.cpp (parse_options, usage_text), Writer::begin (out_dir, stem, compression)
// and PairReader (input files).
//
// Trap: `stem` must equal input_stem() in simulation_pipeline/40_csv_convert.py, which derives
// the same "<stem>.<role>.csv.zip" names to decide what a job still has to produce.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class Compression { none, zip, gz };

struct Options {
    std::optional<std::string> reco_file;   // .edm4eic.root (eicrecon output); empty in sim-only runs
    std::optional<std::string> sim_file;    // .edm4hep.root (npsim output); empty in reco-only runs
    std::string out_dir;                    // every output lands here as <stem>.<role>.csv[.zip|.gz]
    std::string stem;                       // from the reco file name, or the sim file name without a reco file
    std::vector<std::string> writer_roles;  // roles listed in --writers; empty when writers_auto
    bool writers_auto = false;              // --writers auto: every writer whose inputs are given
    Compression compression = Compression::zip;
    int zip_level = 3;                      // --zip-level N: deflate level for zip and gz, 1 fastest to 9 smallest (user decision: 3)
    std::optional<uint64_t> max_events;     // -n N: process the first N entries only
    uint64_t progress_every = 100;          // print a progress line every N events; 0 = never
    bool selective = false;                 // --selective: read only the collections the enabled writers list
    bool list_writers = false;              // --list-writers: print the writer table and exit
    bool list_columns = false;              // --list-columns: print every output's header and exit
    bool show_help = false;                 // --help

    /// Entries the event loop processes: the file's entry count, capped by -n.
    uint64_t events_to_process(uint64_t entries_in_file) const;
};

/// Fills Options from the command line. Throws std::invalid_argument with a message that names
/// the offending argument and is complete enough to print as the error.
Options parse_options(int argc, char** argv);

/// The --help text.
std::string usage_text();

/// "dir/x.edm4eic.root" → "x". Mirrors input_stem() of 40_csv_convert.py: strips the first
/// matching suffix of .eicrecon.edm4eic.root, .edm4eic.root, .edm4hep.root, .root.
std::string input_stem(const std::string& path);

/// "zip", "none" or "gz": the spelling the command line uses.
std::string compression_name(Compression compression);
