// WritersRegistry.cpp — the one table that maps a role name to its writer class.
#include "WritersRegistry.hpp"

#include "writer_AcceptanceNpi0Writer.hpp"
#include "writer_AcceptancePpimWriter.hpp"
#include "writer_CalHitsWriter.hpp"
#include "writer_CaloClustersWriter.hpp"
#include "writer_CombCandidatesWriter.hpp"
#include "writer_CombinatoricsPpimWriter.hpp"
#include "writer_EventIndexWriter.hpp"
#include "writer_LambdaAcceptanceWriter.hpp"
#include "writer_McDisWriter.hpp"
#include "writer_McParticlesWriter.hpp"
#include "writer_McpartLambdaWriter.hpp"
#include "writer_RecoDisWriter.hpp"
#include "writer_RecoFfLambdaWriter.hpp"
#include "writer_RecoParticlesWriter.hpp"
#include "writer_TrkHitsWriter.hpp"

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <stdexcept>

namespace {

struct Entry {
    const char* role;
    std::unique_ptr<Writer> (*make)();
};

/// The table. Roles appear in --list-writers and run under --writers auto in this order.
const std::vector<Entry>& writer_table() {
    static const std::vector<Entry> table = {
        {"event_index",   []() -> std::unique_ptr<Writer> { return std::make_unique<EventIndexWriter>(); }},
        {"trk_hits",      []() -> std::unique_ptr<Writer> { return std::make_unique<TrkHitsWriter>(); }},
        {"cal_hits",      []() -> std::unique_ptr<Writer> { return std::make_unique<CalHitsWriter>(); }},
        {"calo_clusters", []() -> std::unique_ptr<Writer> { return std::make_unique<CaloClustersWriter>(); }},
        {"mc_dis",        []() -> std::unique_ptr<Writer> { return std::make_unique<McDisWriter>(); }},
        {"mc_particles",  []() -> std::unique_ptr<Writer> { return std::make_unique<McParticlesWriter>(); }},
        {"mcpart_lambda", []() -> std::unique_ptr<Writer> { return std::make_unique<McpartLambdaWriter>(); }},
        {"reco_particles",    []() -> std::unique_ptr<Writer> { return std::make_unique<RecoParticlesWriter>(); }},
        {"reco_dis",          []() -> std::unique_ptr<Writer> { return std::make_unique<RecoDisWriter>(); }},
        {"reco_ff_lambda",    []() -> std::unique_ptr<Writer> { return std::make_unique<RecoFfLambdaWriter>(); }},
        {"lambda_acceptance", []() -> std::unique_ptr<Writer> { return std::make_unique<LambdaAcceptanceWriter>(); }},
        {"comb_candidates",   []() -> std::unique_ptr<Writer> { return std::make_unique<CombCandidatesWriter>(); }},
        {"acceptance_ppim",   []() -> std::unique_ptr<Writer> { return std::make_unique<AcceptancePpimWriter>(); }},
        {"acceptance_npi0",   []() -> std::unique_ptr<Writer> { return std::make_unique<AcceptanceNpi0Writer>(); }},
        {"combinatorics_ppim",[]() -> std::unique_ptr<Writer> { return std::make_unique<CombinatoricsPpimWriter>(); }},
    };
    return table;
}

std::vector<std::string> known_roles() {
    std::vector<std::string> roles;
    for (const Entry& entry : writer_table()) roles.push_back(entry.role);
    return roles;
}

std::string needs_text(const Needs& needs) {
    if (needs.reco && needs.sim) return "reco+sim";
    if (needs.reco) return "reco";
    if (needs.sim) return "sim";
    return "any";
}

}  // namespace

std::vector<std::unique_ptr<Writer>> make_writers(const std::vector<std::string>& roles) {
    std::vector<std::unique_ptr<Writer>> writers;
    for (const std::string& role : roles) {
        bool found = false;
        for (const Entry& entry : writer_table()) {
            if (role == entry.role) {
                writers.push_back(entry.make());
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::invalid_argument(fmt::format("unknown writer role '{}'; known roles: {}", role, fmt::join(known_roles(), ", ")));
        }
    }
    return writers;
}

std::vector<std::unique_ptr<Writer>> make_writers_for_inputs(const Options& options) {
    std::vector<std::unique_ptr<Writer>> writers;
    for (const Entry& entry : writer_table()) {
        std::unique_ptr<Writer> writer = entry.make();
        const std::string missing = writer->missing_inputs(options);
        if (missing.empty()) {
            writers.push_back(std::move(writer));
        } else {
            fmt::print("--writers auto: skipping {} (needs {})\n", writer->role(), missing);
        }
    }
    return writers;
}

std::string list_columns_text() {
    std::string text;
    for (const Entry& entry : writer_table()) {
        const std::unique_ptr<Writer> writer = entry.make();
        text += fmt::format("{}: {}\n", writer->role(), writer->csv_header());
        for (const ExtraOutput& extra : writer->extra_outputs()) {
            text += fmt::format("{}_{}: {}\n", writer->role(), extra.suffix, extra.header);
        }
    }
    return text;
}

std::string list_writers_text() {
    std::string text = fmt::format("{:<14} {:<9} collections read\n", "role", "needs");
    for (const Entry& entry : writer_table()) {
        const std::unique_ptr<Writer> writer = entry.make();
        text += fmt::format("{:<14} {:<9} {}\n", writer->role(), needs_text(writer->needs()), fmt::join(writer->collections(), ", "));
    }
    return text;
}
