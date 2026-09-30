// writer_AcceptancePpimWriter.hpp — CSV role "acceptance_ppim": the first Λ → p π⁻ of every event with per-detector hit flags.
//
// Needs: sim. Collections read: MCParticles, the 17 SimTrackerHit and 6 SimCalorimeterHit
// collections listed below (with the calorimeters' contributions).
// Three outputs: <stem>.acceptance_ppim.csv, one row per event that holds a p π⁻ Λ (evt,
// lam_is_first, lam_decay, the 16-column blocks of lam, prot and pimin, then one 0/1 flag per
// particle and detector); <stem>.acceptance_ppim_prot_hits.csv and _pimin_hits.csv, one row
// per hit the daughter left (evt, lam_index, hit_collection, hit_index, hit_pos_x/y/z, hit_edep,
// hit_time, hit_path_length; calorimeter rows carry the hit energy, the first contribution's
// time and path length 0).
//
// Port of csv_convert/edm4hep_acceptance_ppim.cxx; rows are byte-identical to the macro's, the
// headers follow column_renames.py.
// Trap: the hit rows use ostream formatting (stream_text); the main row uses the shared block.
#pragma once

#include "Writer.hpp"
#include "lambda_decay.hpp"
#include "legacy_format.hpp"
#include "mc_particle_block.hpp"

#include <edm4hep/CaloHitContributionCollection.h>
#include <edm4hep/MCParticleCollection.h>
#include <edm4hep/SimCalorimeterHitCollection.h>
#include <edm4hep/SimTrackerHitCollection.h>
#include <fmt/core.h>

#include <map>

class AcceptancePpimWriter : public Writer {
public:
    std::string role() const override { return "acceptance_ppim"; }
    Needs needs() const override { return Needs{.reco = false, .sim = true}; }

    std::vector<std::string> collections() const override {
        std::vector<std::string> names = {"MCParticles"};
        names.insert(names.end(), tracker_collections().begin(), tracker_collections().end());
        for (const std::string& calorimeter : calorimeter_collections()) {
            names.push_back(calorimeter);
            names.push_back(calorimeter + "Contributions");
        }
        return names;
    }

    std::string csv_header() const override {
        std::string header = "evt,lam_is_first,lam_decay," + mc_particle_block_header("lam") + "," +
                             mc_particle_block_header("prot") + "," + mc_particle_block_header("pimin");
        for (const char* particle : {"prot", "pimin"}) {
            for (const std::string& name : tracker_collections()) header += fmt::format(",{}_{}", particle, name);
            for (const std::string& name : calorimeter_collections()) header += fmt::format(",{}_{}", particle, name);
        }
        return header;
    }

    std::vector<ExtraOutput> extra_outputs() const override {
        const std::string hits_header = "evt,lam_index,hit_collection,hit_index,hit_pos_x,hit_pos_y,hit_pos_z,hit_edep,hit_time,hit_path_length";
        return {{"prot_hits", hits_header}, {"pimin_hits", hits_header}};
    }

private:
    static const std::vector<std::string>& tracker_collections() {
        static const std::vector<std::string> names = {
            "B0TrackerHits", "BackwardMPGDEndcapHits", "DIRCBarHits", "DRICHHits", "ForwardMPGDEndcapHits",
            "ForwardOffMTrackerHits", "ForwardRomanPotHits", "LumiSpecTrackerHits", "MPGDBarrelHits", "OuterMPGDBarrelHits",
            "RICHEndcapNHits", "SiBarrelHits", "TOFBarrelHits", "TOFEndcapHits", "TaggerTrackerHits", "TrackerEndcapHits", "VertexBarrelHits",
        };
        return names;
    }

    /// Central and far-forward calorimeters; the ECAL forward insert left the geometry and is not listed.
    static const std::vector<std::string>& calorimeter_collections() {
        static const std::vector<std::string> names = {
            "EcalFarForwardZDCHits", "B0ECalHits", "EcalEndcapPHits", "HcalFarForwardZDCHits", "HcalEndcapPInsertHits", "LFHCALHits",
        };
        return names;
    }

    /// Writes the daughter's hits of one tracker to its hits output; returns whether it left any.
    bool write_tracker_hits(const podio::Frame& sim, const std::string& collection_name, const edm4hep::MCParticle& particle,
                            const std::string& hits_output, uint64_t evt, int lambda_index) {
        const auto* hits = get_optional_collection<edm4hep::SimTrackerHitCollection>(sim, collection_name);
        if (!hits) return false;
        bool detected = false;
        for (const auto& hit : *hits) {
            if (!hit.getParticle().isAvailable() || hit.getParticle().getObjectID() != particle.getObjectID()) continue;
            detected = true;
            write_extra_row(hits_output, fmt::format("{},{},{},{},{},{},{},{},{},{}",
                evt, lambda_index, collection_name, hit.getObjectID().index,
                stream_text(hit.getPosition().x), stream_text(hit.getPosition().y), stream_text(hit.getPosition().z),
                stream_text(hit.getEDep()), stream_text(hit.getTime()), stream_text(hit.getPathLength())));
        }
        return detected;
    }

    /// Writes the calorimeter hits the daughter contributed to; returns whether it contributed to any.
    bool write_calorimeter_hits(const podio::Frame& sim, const std::string& collection_name, const edm4hep::MCParticle& particle,
                                const std::string& hits_output, uint64_t evt, int lambda_index) {
        const auto* hits = get_optional_collection<edm4hep::SimCalorimeterHitCollection>(sim, collection_name);
        if (!hits) return false;
        bool detected = false;
        for (const auto& hit : *hits) {
            bool contributed = false;
            float time = -1;
            for (const auto& contribution : hit.getContributions()) {
                if (contribution.getParticle().getObjectID() == particle.getObjectID()) {
                    contributed = true;
                    time = contribution.getTime();
                    break;
                }
            }
            if (!contributed) continue;
            detected = true;
            write_extra_row(hits_output, fmt::format("{},{},{},{},{},{},{},{},{},0",
                evt, lambda_index, collection_name, hit.getObjectID().index,
                stream_text(hit.getPosition().x), stream_text(hit.getPosition().y), stream_text(hit.getPosition().z),
                stream_text(hit.getEnergy()), stream_text(time)));
        }
        return detected;
    }

    /// One 0/1 flag per detector for one daughter, in header order.
    std::string detection_flags(const podio::Frame& sim, const edm4hep::MCParticle& particle,
                                const std::string& hits_output, uint64_t evt, int lambda_index) {
        std::string flags;
        for (const std::string& name : tracker_collections()) {
            flags += fmt::format(",{}", static_cast<int>(write_tracker_hits(sim, name, particle, hits_output, evt, lambda_index)));
        }
        for (const std::string& name : calorimeter_collections()) {
            flags += fmt::format(",{}", static_cast<int>(write_calorimeter_hits(sim, name, particle, hits_output, evt, lambda_index)));
        }
        return flags;
    }

    void write_event(const EventInputs& inputs) override {
        const podio::Frame& sim = *inputs.sim;
        const auto* particles = get_optional_collection<edm4hep::MCParticleCollection>(sim, "MCParticles");
        if (!particles) {
            skip_notes.note("MCParticles", "collection not in file");
            return;
        }
        bool is_first_lambda = true;
        for (const auto& lambda : *particles) {
            if (lambda.getPDG() != 3122) continue;
            const LambdaDecay decay = classify_lambda_decay(lambda);
            if (decay.type != 1) {
                is_first_lambda = false;
                continue;
            }
            const int lambda_index = lambda.getObjectID().index;
            // The hit rows are written while the flags are collected: proton first, then the π⁻, each
            // over the trackers and then the calorimeters, which is the macro's row order.
            const std::string proton_flags = detection_flags(sim, *decay.proton, "prot_hits", inputs.entry_index, lambda_index);
            const std::string pi_minus_flags = detection_flags(sim, *decay.pi_minus, "pimin_hits", inputs.entry_index, lambda_index);
            write_row(fmt::format("{},{},{},{},{},{}{}{}",
                inputs.entry_index, static_cast<int>(is_first_lambda), decay.type,
                mc_particle_block(lambda), mc_particle_block(decay.proton), mc_particle_block(decay.pi_minus),
                proton_flags, pi_minus_flags));
            break;   // one Λ per event, as mcpart_lambda and acceptance_npi0 do
        }
    }
};
