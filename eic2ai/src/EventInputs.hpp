// EventInputs.hpp — what every writer receives per event: the frames of one entry and its indices.
//
// Filled by PairReader::read, consumed by every Writer::write_event. The frames live for one
// loop iteration in main.cpp; a writer must not keep references to them or to their collections.
//
// Trap: podio relations never cross frames. A writer walking sim CaloHitContributions reaches
// the sim frame's MCParticles; a writer walking reco associations reaches the reco frame's copy.
#pragma once

#include <podio/Frame.h>

#include <cstdint>
#include <optional>

struct EventInputs {
    std::optional<podio::Frame> reco;   // edm4eic frame of the entry; empty in sim-only runs
    std::optional<podio::Frame> sim;    // edm4hep frame of the entry; empty in reco-only runs
    uint64_t entry_index = 0;           // 0-based position in the file; the `evt` column of every CSV
    uint64_t event_number = 0;          // EventHeader.eventNumber; equal in both frames after the pairing guard
};
