// Options.cpp — the command-line parser: one pass over argv, then the checks that need every option.
#include "Options.hpp"

#include <fmt/core.h>

#include <filesystem>
#include <stdexcept>

namespace {

std::string take_value(int argc, char** argv, int& index, const std::string& option) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(fmt::format("{} needs a value", option));
    }
    return argv[++index];
}

Compression parse_compression(const std::string& text) {
    if (text == "zip") return Compression::zip;
    if (text == "none") return Compression::none;
    if (text == "gz") return Compression::gz;
    throw std::invalid_argument(fmt::format("--compress {}: expected zip, none or gz", text));
}

uint64_t parse_count(const std::string& text, const std::string& option) {
    try {
        size_t consumed = 0;
        const unsigned long long value = std::stoull(text, &consumed);
        if (consumed != text.size()) throw std::invalid_argument("trailing characters");
        return value;
    } catch (const std::exception&) {
        throw std::invalid_argument(fmt::format("{} {}: expected a non-negative integer", option, text));
    }
}

std::vector<std::string> split_commas(const std::string& text) {
    std::vector<std::string> items;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!item.empty()) items.push_back(item);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return items;
}

/// A positional file is a reco or a sim input by its suffix; anything else is an error.
void assign_input_by_suffix(Options& options, const std::string& path) {
    if (path.ends_with(".edm4eic.root")) {
        if (options.reco_file) throw std::invalid_argument(fmt::format("two reco files given: {} and {}", *options.reco_file, path));
        options.reco_file = path;
    } else if (path.ends_with(".edm4hep.root")) {
        if (options.sim_file) throw std::invalid_argument(fmt::format("two sim files given: {} and {}", *options.sim_file, path));
        options.sim_file = path;
    } else {
        throw std::invalid_argument(fmt::format(
            "{}: an input file must end with .edm4eic.root (reco) or .edm4hep.root (sim); use --reco or --sim for other names", path));
    }
}

}  // namespace

uint64_t Options::events_to_process(uint64_t entries_in_file) const {
    if (max_events && *max_events < entries_in_file) return *max_events;
    return entries_in_file;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
        } else if (argument == "--list-writers") {
            options.list_writers = true;
        } else if (argument == "--list-columns") {
            options.list_columns = true;
        } else if (argument == "--selective") {
            options.selective = true;
        } else if (argument == "--reco") {
            options.reco_file = take_value(argc, argv, index, argument);
        } else if (argument == "--sim") {
            options.sim_file = take_value(argc, argv, index, argument);
        } else if (argument == "--out-dir") {
            options.out_dir = take_value(argc, argv, index, argument);
        } else if (argument == "--writers") {
            const std::string list = take_value(argc, argv, index, argument);
            if (list == "auto") {
                options.writers_auto = true;
            } else {
                options.writer_roles = split_commas(list);
            }
        } else if (argument == "--compress") {
            options.compression = parse_compression(take_value(argc, argv, index, argument));
        } else if (argument == "--zip-level") {
            const uint64_t level = parse_count(take_value(argc, argv, index, argument), argument);
            if (level < 1 || level > 9) throw std::invalid_argument(fmt::format("--zip-level {}: expected 1 to 9", level));
            options.zip_level = static_cast<int>(level);
        } else if (argument == "-n" || argument == "--events") {
            options.max_events = parse_count(take_value(argc, argv, index, argument), argument);
        } else if (argument == "--progress") {
            options.progress_every = parse_count(take_value(argc, argv, index, argument), argument);
        } else if (!argument.empty() && argument[0] == '-') {
            throw std::invalid_argument(fmt::format("unknown option {}", argument));
        } else {
            assign_input_by_suffix(options, argument);
        }
    }

    if (options.show_help || options.list_writers || options.list_columns) return options;   // nothing else is needed

    if (!options.reco_file && !options.sim_file) {
        throw std::invalid_argument("no input file: give --reco FILE, --sim FILE or both");
    }
    if (options.out_dir.empty()) {
        throw std::invalid_argument("--out-dir DIR is required");
    }
    if (!options.writers_auto && options.writer_roles.empty()) {
        throw std::invalid_argument("--writers ROLE[,ROLE...] or --writers auto is required (roles: --list-writers)");
    }
    options.stem = input_stem(options.reco_file ? *options.reco_file : *options.sim_file);
    return options;
}

std::string input_stem(const std::string& path) {
    const std::string name = std::filesystem::path(path).filename().string();
    for (const char* suffix : {".eicrecon.edm4eic.root", ".edm4eic.root", ".edm4hep.root", ".root"}) {
        if (name.ends_with(suffix)) {
            return name.substr(0, name.size() - std::string(suffix).size());
        }
    }
    return name;
}

std::string compression_name(Compression compression) {
    switch (compression) {
        case Compression::none: return "none";
        case Compression::zip: return "zip";
        case Compression::gz: return "gz";
    }
    return "?";
}

std::string usage_text() {
    return
        "Usage: eic2ai [--reco FILE.edm4eic.root] [--sim FILE.edm4hep.root] --out-dir DIR --writers ROLES|auto [options] [FILE...]\n"
        "\n"
        "Reads the input file(s) once and writes one CSV per role to DIR/<stem>.<role>.csv[.zip|.gz].\n"
        "\n"
        "Inputs (at least one; positional files are sorted into reco/sim by their suffix):\n"
        "  --reco FILE       eicrecon output (.edm4eic.root); <stem> is this file name without its suffix\n"
        "  --sim FILE        npsim output (.edm4hep.root) holding the same events as the reco file\n"
        "\n"
        "Outputs:\n"
        "  --out-dir DIR     output directory (created when missing)\n"
        "  --writers LIST    comma-separated roles, for example trk_hits,cal_hits,calo_clusters\n"
        "  --writers auto    every writer whose needs the given inputs satisfy; skipped writers are listed\n"
        "  --compress MODE   zip (default): one deflate ZIP64 entry <stem>.<role>.csv | none: plain CSV | gz: gzip\n"
        "  --zip-level N     deflate level for zip and gz, 1 (fastest) to 9 (smallest); default 3\n"
        "\n"
        "Run control:\n"
        "  -n N              process the first N entries only (default: all)\n"
        "  --progress N      print a progress line every N events (default 100; 0 = never)\n"
        "  --selective       read only the collections the enabled writers list (see --list-writers) instead of whole entries\n"
        "  --list-writers    print every writer's role, needs and collections, then exit\n"
        "  --list-columns    print the header line of every output (role: columns), then exit\n"
        "  --help            this text\n"
        "\n"
        "Exit codes: 0 every writer succeeded; 1 at least one writer failed (its output is discarded, the\n"
        "others are complete); 2 the run did not start or stopped (bad options, unreadable input, pairing guard).\n";
}
