// writer_LambdaAcceptanceWriter.hpp — CSV role "lambda_acceptance": one row per event, the Λ → p π⁻ acceptance ledger.
//
// Needs: reco. Collections read: MCParticles; the B0, Roman Pot and OffM RawHitAssociations
// (with their sim hits) for the daughters' hit counts; ReconstructedChargedParticleAssociations
// and B0TrackerCKFTruthSeededTrackAssociations for the reconstruction flags; the Roman Pot and
// OffM RecParticles for the closest-candidate momentum distances.
// Columns: evt, lam_decay, n_lambdas, the first Λ's true kinematics and decay vertex z, the
// daughters' kinematics, hit counts per far-forward tracker, central and B0 reconstruction
// flags, relative momentum distances, the number of Roman Pot candidates.
//
// Every event yields a row (also events without a Λ), so branching fractions come out of the
// same file. Port of csv_convert/edm4eic_lambda_acceptance.cxx; the header follows
// column_renames.py. Rows are byte-identical to the macro's except the decay column: the macro classified any single daughter as 4 and any
// unmatched pair as 8; this writer uses the shared lambda_decay.hpp codes (user decision
// 2026-09-30, QUESTIONS.md Q16), and the parity gate waives that column.
#pragma once

#include "Writer.hpp"
#include "lambda_decay.hpp"

#include <edm4eic/MCRecoParticleAssociationCollection.h>
#include <edm4eic/MCRecoTrackParticleAssociationCollection.h>
#include <edm4eic/MCRecoTrackerHitAssociationCollection.h>
#include <edm4eic/ReconstructedParticleCollection.h>
#include <edm4hep/MCParticleCollection.h>
#include <fmt/core.h>

#include <algorithm>
#include <cmath>
#include <optional>

class LambdaAcceptanceWriter : public Writer {
public:
    std::string role() const override { return "lambda_acceptance"; }
    Needs needs() const override { return Needs{.reco = true, .sim = false}; }
    std::vector<std::string> collections() const override {
        return {"MCParticles",
                "B0TrackerRawHitAssociations", "B0TrackerHits", "B0TrackerRawHits",
                "ForwardRomanPotRawHitAssociations", "ForwardRomanPotHits", "ForwardRomanPotRawHits",
                "ForwardOffMTrackerRawHitAssociations", "ForwardOffMTrackerHits", "ForwardOffMTrackerRawHits",
                "ReconstructedChargedParticleAssociations", "ReconstructedChargedParticles",
                "B0TrackerCKFTruthSeededTrackAssociations", "B0TrackerCKFTruthSeededTracks",
                "ForwardRomanPotRecParticles", "ForwardOffMRecParticles"};
    }

    std::string csv_header() const override {
        return "evt,lam_decay,n_lambdas,lam_mom,lam_eta,lam_decay_pos_z,"
               "prot_mom,prot_theta,prot_eta,pimin_mom,pimin_theta,pimin_eta,"
               "prot_n_hits_b0,prot_n_hits_rp,prot_n_hits_offm,"
               "pimin_n_hits_b0,pimin_n_hits_rp,pimin_n_hits_offm,"
               "prot_cent,pimin_cent,prot_b0trk,pimin_b0trk,prot_rp_dp,prot_offm_dp,n_rp";
    }

private:
    struct Kinematics {
        double p = 0, theta = 0, eta = 0;
    };

    static Kinematics kinematics_of(const edm4hep::MCParticle& particle) {
        const auto momentum = particle.getMomentum();
        const double p = std::sqrt(momentum.x * momentum.x + momentum.y * momentum.y + momentum.z * momentum.z);
        const double theta = std::acos(momentum.z / std::max(p, 1e-12));
        return {p, theta, -std::log(std::tan(std::max(theta, 1e-9) / 2.0))};
    }

    /// Raw hits of `particle` in one tracker: associations whose sim hit belongs to it.
    int hits_from(const podio::Frame& reco, const std::string& associations_name, const std::optional<edm4hep::MCParticle>& particle) {
        if (!particle) return 0;
        const auto* associations = get_optional_collection<edm4eic::MCRecoTrackerHitAssociationCollection>(reco, associations_name);
        if (!associations) {
            skip_notes.note(associations_name, "association collection not in file");
            return 0;
        }
        int count = 0;
        for (const auto& association : *associations) {
            const auto sim_hit = association.getSimHit();
            if (sim_hit.isAvailable() && sim_hit.getParticle().isAvailable() &&
                sim_hit.getParticle().getObjectID() == particle->getObjectID()) ++count;
        }
        return count;
    }

    /// Smallest relative momentum distance of any candidate in the collection to `particle`; -1 without candidates.
    double best_momentum_distance(const podio::Frame& reco, const std::string& candidates_name, const std::optional<edm4hep::MCParticle>& particle) {
        if (!particle) return -1;
        const auto* candidates = get_optional_collection<edm4eic::ReconstructedParticleCollection>(reco, candidates_name);
        if (!candidates) {
            skip_notes.note(candidates_name, "collection not in file");
            return -1;
        }
        const Kinematics truth = kinematics_of(*particle);
        double best = -1;
        for (const auto& candidate : *candidates) {
            const auto momentum = candidate.getMomentum();
            const double p = std::sqrt(momentum.x * momentum.x + momentum.y * momentum.y + momentum.z * momentum.z);
            const double distance = std::abs(p - truth.p) / std::max(truth.p, 1e-12);
            if (best < 0 || distance < best) best = distance;
        }
        return best;
    }

    void write_event(const EventInputs& inputs) override {
        const podio::Frame& reco = *inputs.reco;
        const auto* particles = get_optional_collection<edm4hep::MCParticleCollection>(reco, "MCParticles");
        if (!particles) {
            skip_notes.note("MCParticles", "collection not in file");
            return;
        }
        std::optional<edm4hep::MCParticle> lambda, proton, pi_minus;
        int lambda_count = 0, decay = -1;
        for (const auto& particle : *particles) {
            if (particle.getPDG() != 3122) continue;
            ++lambda_count;
            if (!lambda) lambda = particle;
        }
        if (lambda) {
            const LambdaDecay classified = classify_lambda_decay(*lambda);
            decay = classified.type;
            proton = classified.proton;
            pi_minus = classified.pi_minus;
        }
        const Kinematics lambda_kinematics = lambda ? kinematics_of(*lambda) : Kinematics{};
        const Kinematics proton_kinematics = proton ? kinematics_of(*proton) : Kinematics{};
        const Kinematics pi_minus_kinematics = pi_minus ? kinematics_of(*pi_minus) : Kinematics{};
        const double decay_vertex_z = proton ? proton->getVertex().z : 0;

        // Central tracking: an association to the daughter is enough.
        int proton_central = 0, pi_minus_central = 0;
        if (const auto* associations = get_optional_collection<edm4eic::MCRecoParticleAssociationCollection>(reco, "ReconstructedChargedParticleAssociations")) {
            for (const auto& association : *associations) {
                if (!association.getSim().isAvailable()) continue;
                if (proton && association.getSim().getObjectID() == proton->getObjectID()) proton_central = 1;
                if (pi_minus && association.getSim().getObjectID() == pi_minus->getObjectID()) pi_minus_central = 1;
            }
        } else {
            skip_notes.note("ReconstructedChargedParticleAssociations", "collection not in file");
        }

        // B0 truth-seeded tracks: filled when reco ran with the B0 recovery parameters; empty in stock campaign files.
        int proton_b0 = 0, pi_minus_b0 = 0;
        if (const auto* associations = get_optional_collection<edm4eic::MCRecoTrackParticleAssociationCollection>(reco, "B0TrackerCKFTruthSeededTrackAssociations")) {
            for (const auto& association : *associations) {
                if (!association.getSim().isAvailable()) continue;
                if (proton && association.getSim().getObjectID() == proton->getObjectID()) proton_b0 = 1;
                if (pi_minus && association.getSim().getObjectID() == pi_minus->getObjectID()) pi_minus_b0 = 1;
            }
        } else {
            skip_notes.note("B0TrackerCKFTruthSeededTrackAssociations", "collection not in file");
        }

        int roman_pot_candidates = 0;
        if (const auto* candidates = get_optional_collection<edm4eic::ReconstructedParticleCollection>(reco, "ForwardRomanPotRecParticles")) {
            roman_pot_candidates = static_cast<int>(candidates->size());
        } else {
            skip_notes.note("ForwardRomanPotRecParticles", "collection not in file");
        }

        write_row(fmt::format(
            "{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
            inputs.entry_index, decay, lambda_count, lambda_kinematics.p, lambda_kinematics.eta, decay_vertex_z,
            proton_kinematics.p, proton_kinematics.theta, proton_kinematics.eta,
            pi_minus_kinematics.p, pi_minus_kinematics.theta, pi_minus_kinematics.eta,
            hits_from(reco, "B0TrackerRawHitAssociations", proton),
            hits_from(reco, "ForwardRomanPotRawHitAssociations", proton),
            hits_from(reco, "ForwardOffMTrackerRawHitAssociations", proton),
            hits_from(reco, "B0TrackerRawHitAssociations", pi_minus),
            hits_from(reco, "ForwardRomanPotRawHitAssociations", pi_minus),
            hits_from(reco, "ForwardOffMTrackerRawHitAssociations", pi_minus),
            proton_central, pi_minus_central, proton_b0, pi_minus_b0,
            best_momentum_distance(reco, "ForwardRomanPotRecParticles", proton),
            best_momentum_distance(reco, "ForwardOffMRecParticles", proton),
            roman_pot_candidates));
    }
};
