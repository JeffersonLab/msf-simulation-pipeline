// PairReader.hpp — one or two podio readers that deliver the same event from both files.
//
// Opens the reco file (.edm4eic.root) and, when given, the sim file (.edm4hep.root); read(i)
// returns the frames of entry i as EventInputs. Two guards make a wrong pairing impossible to
// miss: equal entry counts at open, equal EventHeader.eventNumber per entry. A guard failure
// throws std::runtime_error naming both files and the entry; main.cpp turns it into exit 2.
//
// Trap: entries are read by explicit index (readEntry), never with readNextEntry, so a peek at
// any entry cannot move a cursor and desynchronize the two files.
#pragma once

#include "EventInputs.hpp"

#include <podio/ROOTReader.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class PairReader {
public:
    PairReader(const std::optional<std::string>& reco_file, const std::optional<std::string>& sim_file);

    /// Entries in the file(s); equal for both after the guard.
    uint64_t entries() const { return entries_; }

    /// Frames of one entry, guarded. Throws std::runtime_error when an entry cannot be read
    /// or the event numbers differ.
    EventInputs read(uint64_t entry_index);

    /// Selective reading: from now on read() loads only the named collections plus EventHeader,
    /// each intersected with what its file holds; names a file lacks are ignored. Trap: a relation
    /// target left out of the list makes isAvailable() false without any error; the parity gate
    /// runs in both modes for that reason.
    void restrict_to(const std::vector<std::string>& collection_names);

    /// "reco=<path> (N entries), sim=<path> (N entries)" for the log, with the selection sizes when set.
    std::string describe() const;

private:
    struct Input {
        std::string path;
        podio::ROOTReader reader;
        uint64_t entries = 0;
        std::vector<std::string> selection;   // collections to read; empty = every collection
        size_t collections_in_file = 0;       // known after restrict_to
    };
    static std::unique_ptr<Input> open(const std::string& path);
    static podio::Frame read_frame(Input& input, uint64_t entry_index);

    std::unique_ptr<Input> reco_;
    std::unique_ptr<Input> sim_;
    uint64_t entries_ = 0;
};
