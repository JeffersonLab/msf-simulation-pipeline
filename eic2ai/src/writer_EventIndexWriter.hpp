// writer_EventIndexWriter.hpp — CSV role "event_index": one row per processed event.
//
// Needs: any input (reco, sim or both). Collections read: EventHeader (through PairReader).
// Columns: evt, event_number, reco_collections, sim_collections.
//
// Purpose: a manifest of what a run processed (the event count of a complete conversion), and
// the smallest complete writer to read first. Every other writer has this layout: header
// comment, record struct with one comment per column, write_event.
#pragma once

#include "Writer.hpp"

#include <fmt/core.h>

class EventIndexWriter : public Writer {
public:
    std::string role() const override { return "event_index"; }
    Needs needs() const override { return Needs{}; }
    std::vector<std::string> collections() const override { return {"EventHeader"}; }
    std::string csv_header() const override { return "evt,event_number,reco_collections,sim_collections"; }

private:
    /// One row per event.
    struct Record {
        uint64_t evt;               // entry counter; the join key of every role
        uint64_t event_number;      // EventHeader.eventNumber
        size_t reco_collections;    // collections in the reco frame (0 without a reco file)
        size_t sim_collections;     // collections in the sim frame (0 without a sim file)

        std::string csv_line() const {
            return fmt::format("{},{},{},{}", evt, event_number, reco_collections, sim_collections);
        }
    };

    void write_event(const EventInputs& inputs) override {
        Record record{};
        record.evt = inputs.entry_index;
        record.event_number = inputs.event_number;
        record.reco_collections = inputs.reco ? inputs.reco->getAvailableCollections().size() : 0;
        record.sim_collections = inputs.sim ? inputs.sim->getAvailableCollections().size() : 0;
        write_row(record.csv_line());
    }
};
