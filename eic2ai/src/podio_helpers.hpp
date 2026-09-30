// podio_helpers.hpp — small functions the reader and every writer use on a podio::Frame.
//
// get_optional_collection: a collection by name, or nullptr when the frame lacks it or stores
//   a different type. Collection sets differ between eic_xl versions, so absence is expected.
// event_number_of: EventHeader.eventNumber of a frame; throws when the frame has no EventHeader.
// SkipNotes: prints "[skip] <collection>: <why>" once per collection name, so a per-event skip
//   does not flood the log. Each writer owns one (the macros used a function-local static set).
#pragma once

#include <edm4hep/EventHeaderCollection.h>
#include <fmt/core.h>
#include <podio/Frame.h>

#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>

template <typename CollectionT>
const CollectionT* get_optional_collection(const podio::Frame& frame, const std::string& name) {
    const podio::CollectionBase* collection = frame.get(name);
    return collection ? dynamic_cast<const CollectionT*>(collection) : nullptr;
}

inline uint64_t event_number_of(const podio::Frame& frame, const std::string& file_path, uint64_t entry_index) {
    const auto* headers = get_optional_collection<edm4hep::EventHeaderCollection>(frame, "EventHeader");
    if (!headers || headers->empty()) {
        throw std::runtime_error(fmt::format(
            "{}: entry {} has no EventHeader; the event number cannot be read and pairing cannot be verified",
            file_path, entry_index));
    }
    return static_cast<uint64_t>((*headers)[0].getEventNumber());
}

class SkipNotes {
public:
    void note(const std::string& collection_name, const std::string& why) {
        if (already_noted_.insert(collection_name).second) {
            fmt::print("[skip] {}: {} (expected for some eic_xl versions)\n", collection_name, why);
        }
    }

private:
    std::set<std::string> already_noted_;
};
