// writer_CombCandidatesWriter.hpp — CSV role "comb_candidates": the p / π⁻ candidate table for Λ → p π⁻ combinatorics.
//
// Needs: reco. Collections read: MCParticles (the Sullivan Λ and its daughters);
// ReconstructedChargedParticles with their associations (central tracking, source 0);
// B0TrackerCKFTruthSeededTracks with their associations (source 1; empty in stock campaign
// reco); ForwardRomanPotRecParticles (source 2) and ForwardOffMRecParticles (source 4), which
// carry no associations and get kinematic distances to the true daughters instead.
// Two outputs: <stem>.comb_candidates.csv, one row per reco candidate, and
// <stem>.comb_candidates_events.csv, one row per event with the Λ truth summary.
//
// Truth columns (mc_*, is_sul_*, dp_*, dth_*) serve labels and monitoring; the training feature
// list lives in ai-comb-lam. Port of csv_convert/edm4eic_comb_candidates.cxx; the headers
// follow column_renames.py (cand_*, mc_index, pimin). Rows are byte-identical to the macro's
// except the decay column of the events file, which uses the
// shared lambda_decay.hpp codes (the macro classified any single daughter as 4 and any
// unmatched pair as 8; user decision 2026-09-30, QUESTIONS.md Q16; the gate waives it).
// Trap: candidate momenta and reference points are floats in EDM4EIC and the macro formatted
// them as doubles; the record keeps double fields for that reason.
#pragma once

#include "Writer.hpp"
#include "lambda_decay.hpp"

#include <edm4eic/MCRecoParticleAssociationCollection.h>
#include <edm4eic/MCRecoTrackParticleAssociationCollection.h>
#include <edm4eic/ReconstructedParticleCollection.h>
#include <edm4eic/TrackCollection.h>
#include <edm4hep/MCParticleCollection.h>
#include <fmt/core.h>

#include <algorithm>
#include <cmath>
#include <optional>

class CombCandidatesWriter : public Writer {
public:
    std::string role() const override { return "comb_candidates"; }
    Needs needs() const override { return Needs{.reco = true, .sim = false}; }
    std::vector<std::string> collections() const override {
        return {"MCParticles", "ReconstructedChargedParticles", "ReconstructedChargedParticleAssociations",
                "B0TrackerCKFTruthSeededTracks", "B0TrackerCKFTruthSeededTrackAssociations",
                "ForwardRomanPotRecParticles", "ForwardOffMRecParticles"};
    }

    std::string csv_header() const override {
        return "evt,cand_source,cand_index,cand_charge,cand_mom_x,cand_mom_y,cand_mom_z,cand_mom,cand_theta,cand_eta,cand_phi,"
               "cand_ref_pos_x,cand_ref_pos_y,cand_ref_pos_z,cand_quality,cand_ndf,cand_n_measurements,"
               "mc_index,mc_weight,mc_pdg,mc_gen_status,is_sul_prot,is_sul_pimin,"
               "dp_prot,dth_prot,dp_pimin,dth_pimin";
    }

    std::vector<ExtraOutput> extra_outputs() const override {
        return {{"events", "evt,n_lambdas,lam_decay,prot_index,pimin_index,"
                           "lam_mom,lam_eta,lam_phi,lam_decay_pos_x,lam_decay_pos_y,lam_decay_pos_z,"
                           "prot_mom,prot_theta,prot_phi,pimin_mom,pimin_theta,pimin_phi"}};
    }

private:
    struct Kinematics {
        double p = 0, theta = 0, eta = 0, phi = 0;
    };

    static Kinematics kinematics_of(double px, double py, double pz) {
        const double p = std::sqrt(px * px + py * py + pz * pz);
        const double theta = std::acos(pz / std::max(p, 1e-12));
        return {p, theta, -std::log(std::tan(std::max(theta, 1e-9) / 2.0)), std::atan2(py, px)};
    }

    /// The MC label of a candidate: the associated MCParticle of largest weight, or none.
    struct McLabel {
        int index = -1;
        double weight = -1;
        int pdg = 0;
        int generator_status = -1;
    };

    template <typename AssociationCollectionT>
    static McLabel label_of(const AssociationCollectionT& associations, int candidate_index) {
        McLabel label;
        for (const auto& association : associations) {
            if (!association.getRec().isAvailable() || association.getRec().getObjectID().index != candidate_index) continue;
            if (association.getWeight() > label.weight && association.getSim().isAvailable()) {
                label.weight = association.getWeight();
                label.index = association.getSim().getObjectID().index;
                label.pdg = association.getSim().getPDG();
                label.generator_status = association.getSim().getGeneratorStatus();
            }
        }
        return label;
    }

    /// One candidate row. Momenta and reference points are doubles although EDM4EIC stores floats (see the file comment).
    struct CandidateRecord {
        uint64_t event;          // entry counter
        int coll;                // source: 0 central, 1 B0 truth-seeded tracks, 2 Roman Pots, 4 OffM
        int idx;                 // index in the source collection
        double charge;           // electric charge [e]
        double px, py, pz;       // momentum [GeV/c]
        double p, theta, eta, phi;   // derived from the momentum
        double ref_x, ref_y, ref_z;  // reference point or track position [mm]
        double quality;          // goodness of PID (0), track chi2 (1), -1 (2, 4)
        int ndf;                 // track ndf (1), -1 otherwise
        int nmeas;               // tracks of the particle (0), measurements of the track (1), -1 otherwise
        int mc_idx;              // associated MCParticle index, -1 without association
        double mc_weight;        // association weight, -1 without association
        int mc_pdg;              // associated MCParticle PDG, 0 without association
        int mc_gen;              // associated MCParticle generator status, -1 without association
        int is_sul_prot;         // 1 when the label is the Sullivan Λ's proton
        int is_sul_pim;          // 1 when the label is the Sullivan Λ's π⁻
        double dp_prot, dth_prot;    // relative momentum and angle distance to the true proton (2, 4), -1 otherwise
        double dp_pim, dth_pim;      // the same for the true π⁻

        std::string csv_line() const {
            return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                event, coll, idx, charge, px, py, pz, p, theta, eta, phi, ref_x, ref_y, ref_z, quality, ndf, nmeas,
                mc_idx, mc_weight, mc_pdg, mc_gen, is_sul_prot, is_sul_pim, dp_prot, dth_prot, dp_pim, dth_pim);
        }
    };

    void write_candidate(uint64_t event, int coll, int idx, double charge, double px, double py, double pz,
                         double ref_x, double ref_y, double ref_z, double quality, int ndf, int nmeas,
                         const McLabel& label, int true_proton_index, int true_pi_minus_index,
                         double dp_prot, double dth_prot, double dp_pim, double dth_pim) {
        const Kinematics kinematics = kinematics_of(px, py, pz);
        CandidateRecord record{};
        record.event = event;
        record.coll = coll;
        record.idx = idx;
        record.charge = charge;
        record.px = px;
        record.py = py;
        record.pz = pz;
        record.p = kinematics.p;
        record.theta = kinematics.theta;
        record.eta = kinematics.eta;
        record.phi = kinematics.phi;
        record.ref_x = ref_x;
        record.ref_y = ref_y;
        record.ref_z = ref_z;
        record.quality = quality;
        record.ndf = ndf;
        record.nmeas = nmeas;
        record.mc_idx = label.index;
        record.mc_weight = label.weight;
        record.mc_pdg = label.pdg;
        record.mc_gen = label.generator_status;
        record.is_sul_prot = (label.index >= 0 && label.index == true_proton_index) ? 1 : 0;
        record.is_sul_pim = (label.index >= 0 && label.index == true_pi_minus_index) ? 1 : 0;
        record.dp_prot = dp_prot;
        record.dth_prot = dth_prot;
        record.dp_pim = dp_pim;
        record.dth_pim = dth_pim;
        write_row(record.csv_line());
    }

    template <typename CollectionT>
    const CollectionT* collection_or_note(const podio::Frame& reco, const std::string& name) {
        const auto* collection = get_optional_collection<CollectionT>(reco, name);
        if (!collection) skip_notes.note(name, "collection not in file");
        return collection;
    }

    void write_event(const EventInputs& inputs) override {
        const podio::Frame& reco = *inputs.reco;
        const auto* particles = collection_or_note<edm4hep::MCParticleCollection>(reco, "MCParticles");
        if (!particles) return;

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
        const int true_proton_index = proton ? proton->getObjectID().index : -1;
        const int true_pi_minus_index = pi_minus ? pi_minus->getObjectID().index : -1;
        const Kinematics lambda_kinematics = lambda ? kinematics_of(lambda->getMomentum().x, lambda->getMomentum().y, lambda->getMomentum().z) : Kinematics{};
        double decay_vertex_x = 0, decay_vertex_y = 0, decay_vertex_z = 0;
        Kinematics proton_kinematics, pi_minus_kinematics;
        if (proton) {
            const auto vertex = proton->getVertex();
            decay_vertex_x = vertex.x;
            decay_vertex_y = vertex.y;
            decay_vertex_z = vertex.z;
            proton_kinematics = kinematics_of(proton->getMomentum().x, proton->getMomentum().y, proton->getMomentum().z);
        }
        if (pi_minus) pi_minus_kinematics = kinematics_of(pi_minus->getMomentum().x, pi_minus->getMomentum().y, pi_minus->getMomentum().z);

        write_extra_row("events", fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
            inputs.entry_index, lambda_count, decay, true_proton_index, true_pi_minus_index,
            lambda_kinematics.p, lambda_kinematics.eta, lambda_kinematics.phi, decay_vertex_x, decay_vertex_y, decay_vertex_z,
            proton_kinematics.p, proton_kinematics.theta, proton_kinematics.phi, pi_minus_kinematics.p, pi_minus_kinematics.theta, pi_minus_kinematics.phi));

        // Source 0: central tracking, labeled through the particle associations.
        const auto* charged = collection_or_note<edm4eic::ReconstructedParticleCollection>(reco, "ReconstructedChargedParticles");
        const auto* charged_associations = collection_or_note<edm4eic::MCRecoParticleAssociationCollection>(reco, "ReconstructedChargedParticleAssociations");
        if (charged && charged_associations) {
            int index = 0;
            for (const auto& candidate : *charged) {
                const auto momentum = candidate.getMomentum();
                const auto reference = candidate.getReferencePoint();
                write_candidate(inputs.entry_index, 0, index, candidate.getCharge(), momentum.x, momentum.y, momentum.z,
                                reference.x, reference.y, reference.z, candidate.getGoodnessOfPID(), -1, static_cast<int>(candidate.getTracks().size()),
                                label_of(*charged_associations, index), true_proton_index, true_pi_minus_index, -1, -1, -1, -1);
                ++index;
            }
        }

        // Source 1: B0 truth-seeded tracks, labeled through the track associations.
        const auto* b0_tracks = collection_or_note<edm4eic::TrackCollection>(reco, "B0TrackerCKFTruthSeededTracks");
        const auto* b0_associations = collection_or_note<edm4eic::MCRecoTrackParticleAssociationCollection>(reco, "B0TrackerCKFTruthSeededTrackAssociations");
        if (b0_tracks && b0_associations) {
            int index = 0;
            for (const auto& track : *b0_tracks) {
                const auto momentum = track.getMomentum();
                const auto position = track.getPosition();
                write_candidate(inputs.entry_index, 1, index, track.getCharge(), momentum.x, momentum.y, momentum.z,
                                position.x, position.y, position.z, track.getChi2(), static_cast<int>(track.getNdf()), static_cast<int>(track.getMeasurements().size()),
                                label_of(*b0_associations, index), true_proton_index, true_pi_minus_index, -1, -1, -1, -1);
                ++index;
            }
        }

        // Sources 2 and 4: matrix reconstruction without associations; kinematic distances to the true daughters.
        for (const auto& [source, name] : {std::pair<int, const char*>{2, "ForwardRomanPotRecParticles"}, {4, "ForwardOffMRecParticles"}}) {
            const auto* candidates = collection_or_note<edm4eic::ReconstructedParticleCollection>(reco, name);
            if (!candidates) continue;
            int index = 0;
            for (const auto& candidate : *candidates) {
                const auto momentum = candidate.getMomentum();
                const auto reference = candidate.getReferencePoint();
                const Kinematics kinematics = kinematics_of(momentum.x, momentum.y, momentum.z);
                double dp_prot = -1, dth_prot = -1, dp_pim = -1, dth_pim = -1;
                if (proton) {
                    dp_prot = std::abs(kinematics.p - proton_kinematics.p) / proton_kinematics.p;
                    dth_prot = std::abs(kinematics.theta - proton_kinematics.theta);
                }
                if (pi_minus) {
                    dp_pim = std::abs(kinematics.p - pi_minus_kinematics.p) / pi_minus_kinematics.p;
                    dth_pim = std::abs(kinematics.theta - pi_minus_kinematics.theta);
                }
                write_candidate(inputs.entry_index, source, index, candidate.getCharge(), momentum.x, momentum.y, momentum.z,
                                reference.x, reference.y, reference.z, -1, -1, -1,
                                McLabel{}, true_proton_index, true_pi_minus_index, dp_prot, dth_prot, dp_pim, dth_pim);
                ++index;
            }
        }
    }
};
