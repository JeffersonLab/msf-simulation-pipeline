// writer_CombinatoricsPpimWriter.hpp — CSV role "combinatorics_ppim": every (proton candidate, pion candidate) pair per event.
//
// Needs: sim. Collections read: MCParticles, ForwardRomanPotHits (proton candidates: particles
// with at least two hits), B0TrackerHits (pion candidates: at least three hits) and B0ECalHits
// with its contributions (whether a pion candidate reached the B0 ECAL).
// Columns: evt, is_true_lam (both candidates are the first Λ's p and π⁻), the true daughters'
// indices, the pion candidate's 16-column block (prefix pimin) with its B0 hit count, first B0
// hit and first B0 ECAL hit, the proton candidate's block with its Roman Pot hit count and
// first hit.
//
// Port of csv_convert/edm4hep_combinatorics_ppim.cxx; rows are byte-identical to the macro's,
// the header follows column_renames.py (the macro wrote the pion block with prefix pi).
// Trap: the hit positions use ostream formatting (stream_text); candidates are ordered by
// MCParticle index (std::map order) as in the macro.
#pragma once

#include "Writer.hpp"
#include "legacy_format.hpp"
#include "mc_particle_block.hpp"

#include <edm4hep/CaloHitContributionCollection.h>
#include <edm4hep/MCParticleCollection.h>
#include <edm4hep/SimCalorimeterHitCollection.h>
#include <edm4hep/SimTrackerHitCollection.h>
#include <fmt/core.h>

#include <map>

class CombinatoricsPpimWriter : public Writer {
public:
    std::string role() const override { return "combinatorics_ppim"; }
    Needs needs() const override { return Needs{.reco = false, .sim = true}; }
    std::vector<std::string> collections() const override {
        return {"MCParticles", "ForwardRomanPotHits", "B0TrackerHits", "B0ECalHits", "B0ECalHitsContributions"};
    }

    std::string csv_header() const override {
        return "evt,is_true_lam,true_prot_index,true_pimin_index," + mc_particle_block_header("pimin") +
               ",pimin_n_hits_b0,pimin_first_b0_pos_x,pimin_first_b0_pos_y,pimin_first_b0_pos_z,"
               "pimin_ecal_contrib,pimin_first_ecal_pos_x,pimin_first_ecal_pos_y,pimin_first_ecal_pos_z," +
               mc_particle_block_header("prot") + ",prot_n_hits_rp,prot_first_rp_pos_x,prot_first_rp_pos_y,prot_first_rp_pos_z";
    }

private:
    /// A particle that left enough hits in one tracker, with its first hit in collection order.
    struct Candidate {
        edm4hep::MCParticle particle;
        int hit_count = 0;
        double first_hit_x = 0, first_hit_y = 0, first_hit_z = 0;
    };

    /// Candidates of one tracker: particles with at least `minimum_hits` hits, ordered by particle index.
    static std::vector<Candidate> candidates_in(const edm4hep::SimTrackerHitCollection* hits, int minimum_hits,
                                                const std::map<int, edm4hep::MCParticle>& particle_by_index) {
        std::map<int, std::vector<edm4hep::SimTrackerHit>> hits_by_particle;
        if (hits) {
            for (const auto& hit : *hits) {
                if (hit.getParticle().isAvailable()) hits_by_particle[hit.getParticle().getObjectID().index].push_back(hit);
            }
        }
        std::vector<Candidate> candidates;
        for (const auto& [particle_index, particle_hits] : hits_by_particle) {
            if (static_cast<int>(particle_hits.size()) < minimum_hits) continue;
            const auto particle = particle_by_index.find(particle_index);
            if (particle == particle_by_index.end()) continue;
            Candidate candidate{particle->second};
            candidate.hit_count = static_cast<int>(particle_hits.size());
            candidate.first_hit_x = particle_hits[0].getPosition().x;
            candidate.first_hit_y = particle_hits[0].getPosition().y;
            candidate.first_hit_z = particle_hits[0].getPosition().z;
            candidates.push_back(candidate);
        }
        return candidates;
    }

    void write_event(const EventInputs& inputs) override {
        const podio::Frame& sim = *inputs.sim;
        const auto* particles = get_optional_collection<edm4hep::MCParticleCollection>(sim, "MCParticles");
        if (!particles) {
            skip_notes.note("MCParticles", "collection not in file");
            return;
        }
        std::map<int, edm4hep::MCParticle> particle_by_index;
        for (const auto& particle : *particles) particle_by_index[particle.getObjectID().index] = particle;

        // The first Λ → p π⁻: its daughters' indices label the true pair.
        int true_proton_index = -1, true_pi_minus_index = -1;
        for (const auto& lambda : *particles) {
            if (lambda.getPDG() != 3122) continue;
            const auto daughters = lambda.getDaughters();
            if (daughters.size() != 2) continue;
            if (daughters.at(0).getPDG() == 2212 && daughters.at(1).getPDG() == -211) {
                true_proton_index = daughters.at(0).getObjectID().index;
                true_pi_minus_index = daughters.at(1).getObjectID().index;
            } else if (daughters.at(1).getPDG() == 2212 && daughters.at(0).getPDG() == -211) {
                true_proton_index = daughters.at(1).getObjectID().index;
                true_pi_minus_index = daughters.at(0).getObjectID().index;
            }
            if (true_proton_index >= 0) break;
        }

        const std::vector<Candidate> proton_candidates = candidates_in(get_optional_collection<edm4hep::SimTrackerHitCollection>(sim, "ForwardRomanPotHits"), 2, particle_by_index);
        const std::vector<Candidate> pion_candidates = candidates_in(get_optional_collection<edm4hep::SimTrackerHitCollection>(sim, "B0TrackerHits"), 3, particle_by_index);

        // Pion candidates that contributed to the B0 ECAL, with the position of the first such hit.
        std::map<int, bool> pion_has_ecal;
        std::map<int, double> ecal_first_x, ecal_first_y, ecal_first_z;
        for (const Candidate& pion : pion_candidates) pion_has_ecal[pion.particle.getObjectID().index] = false;
        if (const auto* ecal_hits = get_optional_collection<edm4hep::SimCalorimeterHitCollection>(sim, "B0ECalHits")) {
            for (const auto& hit : *ecal_hits) {
                for (const auto& contribution : hit.getContributions()) {
                    if (!contribution.getParticle().isAvailable()) continue;
                    const int contributor = contribution.getParticle().getObjectID().index;
                    const auto flag = pion_has_ecal.find(contributor);
                    if (flag != pion_has_ecal.end() && !flag->second) {
                        flag->second = true;
                        ecal_first_x[contributor] = hit.getPosition().x;
                        ecal_first_y[contributor] = hit.getPosition().y;
                        ecal_first_z[contributor] = hit.getPosition().z;
                    }
                }
            }
        }

        if (proton_candidates.empty() || pion_candidates.empty()) return;
        for (const Candidate& proton : proton_candidates) {
            const int proton_index = proton.particle.getObjectID().index;
            for (const Candidate& pion : pion_candidates) {
                const int pion_index = pion.particle.getObjectID().index;
                const bool is_true_pair = proton_index == true_proton_index && pion_index == true_pi_minus_index;
                const bool has_ecal = pion_has_ecal.count(pion_index) && pion_has_ecal[pion_index];
                const double ecal_x = has_ecal ? ecal_first_x[pion_index] : 0.0;
                const double ecal_y = has_ecal ? ecal_first_y[pion_index] : 0.0;
                const double ecal_z = has_ecal ? ecal_first_z[pion_index] : 0.0;
                write_row(fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                    inputs.entry_index, is_true_pair ? 1 : 0, true_proton_index, true_pi_minus_index,
                    mc_particle_block(pion.particle), pion.hit_count,
                    stream_text(pion.first_hit_x), stream_text(pion.first_hit_y), stream_text(pion.first_hit_z),
                    has_ecal ? 1 : 0, stream_text(ecal_x), stream_text(ecal_y), stream_text(ecal_z),
                    mc_particle_block(proton.particle), proton.hit_count,
                    stream_text(proton.first_hit_x), stream_text(proton.first_hit_y), stream_text(proton.first_hit_z)));
            }
        }
    }
};
