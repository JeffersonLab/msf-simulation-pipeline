// PairReader.cpp — opening, the entry-count guard, reading by index, the eventNumber guard.
#include "PairReader.hpp"

#include "podio_helpers.hpp"

#include <fmt/core.h>
#include <podio/FrameCategories.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

PairReader::PairReader(const std::optional<std::string>& reco_file, const std::optional<std::string>& sim_file) {
    if (!reco_file && !sim_file) {
        throw std::invalid_argument("PairReader needs at least one input file");
    }
    if (reco_file) reco_ = open(*reco_file);
    if (sim_file) sim_ = open(*sim_file);

    if (reco_ && sim_ && reco_->entries != sim_->entries) {
        throw std::runtime_error(fmt::format(
            "entry count mismatch: {} has {} entries, {} has {} — the files are not a same-event pair",
            reco_->path, reco_->entries, sim_->path, sim_->entries));
    }
    entries_ = reco_ ? reco_->entries : sim_->entries;
}

std::unique_ptr<PairReader::Input> PairReader::open(const std::string& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error(fmt::format("{}: no such file", path));
    }
    auto input = std::make_unique<Input>();
    input->path = path;
    try {
        input->reader.openFile(path);
    } catch (const std::exception& error) {
        throw std::runtime_error(fmt::format("{}: cannot open as a podio ROOT file: {}", path, error.what()));
    }
    input->entries = input->reader.getEntries(podio::Category::Event);
    return input;
}

void PairReader::restrict_to(const std::vector<std::string>& collection_names) {
    for (Input* input : {reco_.get(), sim_.get()}) {
        if (!input) continue;
        // The file's collection names come from a full read of entry 0 (an explicit index, so no cursor moves).
        const podio::Frame first_entry = read_frame(*input, 0);   // the selection is still empty: a full read, guarded
        const std::vector<std::string> available = first_entry.getAvailableCollections();
        input->collections_in_file = available.size();
        input->selection.clear();
        for (const std::string& name : available) {
            const bool wanted = name == "EventHeader" || std::find(collection_names.begin(), collection_names.end(), name) != collection_names.end();
            if (wanted) input->selection.push_back(name);
        }
    }
}

podio::Frame PairReader::read_frame(Input& input, uint64_t entry_index) {
    auto frame_data = input.reader.readEntry(podio::Category::Event, static_cast<unsigned>(entry_index), input.selection);
    if (!frame_data) {
        throw std::runtime_error(fmt::format(
            "{}: entry {} could not be read (the file has {} entries)", input.path, entry_index, input.entries));
    }
    return podio::Frame(std::move(frame_data));
}

EventInputs PairReader::read(uint64_t entry_index) {
    EventInputs inputs;
    inputs.entry_index = entry_index;
    if (reco_) inputs.reco = read_frame(*reco_, entry_index);
    if (sim_) inputs.sim = read_frame(*sim_, entry_index);

    std::optional<uint64_t> reco_number;
    std::optional<uint64_t> sim_number;
    if (inputs.reco) reco_number = event_number_of(*inputs.reco, reco_->path, entry_index);
    if (inputs.sim) sim_number = event_number_of(*inputs.sim, sim_->path, entry_index);
    if (reco_number && sim_number && *reco_number != *sim_number) {
        throw std::runtime_error(fmt::format(
            "eventNumber mismatch at entry {}: reco={} sim={} — the files are not a same-event pair ({} vs {})",
            entry_index, *reco_number, *sim_number, reco_->path, sim_->path));
    }
    inputs.event_number = reco_number ? *reco_number : *sim_number;
    return inputs;
}

std::string PairReader::describe() const {
    auto describe_input = [](const char* which, const Input& input) {
        std::string text = fmt::format("{}={} ({} entries", which, input.path, input.entries);
        if (!input.selection.empty()) text += fmt::format(", reading {} of {} collections", input.selection.size(), input.collections_in_file);
        return text + ")";
    };
    std::string text;
    if (reco_) text += describe_input("reco", *reco_);
    if (reco_ && sim_) text += ", ";
    if (sim_) text += describe_input("sim", *sim_);
    return text;
}
