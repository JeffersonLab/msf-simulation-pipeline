// writer_RecoFfLambdaWriter.hpp — CSV role "reco_ff_lambda": one row per far-forward Λ reconstructed as n γ γ.
//
// Needs: reco. Collections read: ReconstructedLambdas and, through its particles relation, the
// decay products (ReconstructedLambdaDecayProductsCM and the far-forward neutral collections).
// Columns: evt, then a 27-column reconstructed particle block for the Λ (lam), the neutron
// (neut) and the two photons (gamone, gamtwo): identity, kinematics, reference point, PID,
// relation counts and the 4×4 covariance.
//
// A Λ whose daughters are not exactly one neutron and two photons yields no row. Port of
// csv_convert/edm4eic_reco_ff_lambda.cxx; rows are byte-identical to the macro's, the header
// follows column_renames.py (evt, _index, _mom_x, _ref_pos_x, gamone/gamtwo).
#pragma once

#include "Writer.hpp"

#include <edm4eic/ReconstructedParticleCollection.h>
#include <fmt/core.h>

#include <optional>

class RecoFfLambdaWriter : public Writer {
public:
    std::string role() const override { return "reco_ff_lambda"; }
    Needs needs() const override { return Needs{.reco = true, .sim = false}; }
    std::vector<std::string> collections() const override {
        return {"ReconstructedLambdas", "ReconstructedLambdaDecayProductsCM", "ReconstructedHcalFarForwardZDCNeutrals",
                "ReconstructedFarForwardZDCNeutrals", "ReconstructedNeutralParticles"};
    }

    std::string csv_header() const override {
        std::string header = "evt";
        for (const char* prefix : {"lam", "neut", "gamone", "gamtwo"}) header += "," + particle_block_header(prefix);
        return header;
    }

private:
    static std::string particle_block_header(const std::string& prefix) {
        return fmt::format("{0}_index,{0}_pdg,{0}_charge,{0}_energy,{0}_mass,{0}_mom_x,{0}_mom_y,{0}_mom_z,{0}_ref_pos_x,{0}_ref_pos_y,{0}_ref_pos_z,"
                           "{0}_pid_goodness,{0}_type,{0}_n_clusters,{0}_n_tracks,{0}_n_particles,{0}_n_particle_ids,"
                           "{0}_cov_xx,{0}_cov_xy,{0}_cov_xz,{0}_cov_yy,{0}_cov_yz,{0}_cov_zz,{0}_cov_xt,{0}_cov_yt,{0}_cov_zt,{0}_cov_tt", prefix);
    }

    static std::string particle_block(const edm4eic::ReconstructedParticle& particle) {
        const auto momentum = particle.getMomentum();
        const auto reference = particle.getReferencePoint();
        const auto cov = particle.getCovMatrix();
        return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
            particle.getObjectID().index, particle.getPDG(), particle.getCharge(), particle.getEnergy(), particle.getMass(),
            momentum.x, momentum.y, momentum.z, reference.x, reference.y, reference.z,
            particle.getGoodnessOfPID(), particle.getType(),
            particle.getClusters().size(), particle.getTracks().size(), particle.getParticles().size(), particle.getParticleIDs().size(),
            cov.xx, cov.xy, cov.xz, cov.yy, cov.yz, cov.zz, cov.xt, cov.yt, cov.zt, cov.tt);
    }

    void write_event(const EventInputs& inputs) override {
        const auto* lambdas = get_optional_collection<edm4eic::ReconstructedParticleCollection>(*inputs.reco, "ReconstructedLambdas");
        if (!lambdas) {
            skip_notes.note("ReconstructedLambdas", "collection not in file");
            return;
        }
        for (const auto& lambda : *lambdas) {
            std::optional<edm4eic::ReconstructedParticle> neutron, gamma_one, gamma_two;
            int neutron_count = 0, gamma_count = 0;
            for (const auto& daughter : lambda.getParticles()) {
                if (daughter.getPDG() == 2112) {
                    neutron = daughter;
                    ++neutron_count;
                } else if (daughter.getPDG() == 22) {
                    if (!gamma_one) {
                        gamma_one = daughter;
                    } else if (!gamma_two) {
                        gamma_two = daughter;
                    }
                    ++gamma_count;
                }
            }
            if (neutron_count != 1 || gamma_count != 2) continue;
            write_row(fmt::format("{},{},{},{},{}", inputs.entry_index,
                                  particle_block(lambda), particle_block(*neutron), particle_block(*gamma_one), particle_block(*gamma_two)));
        }
    }
};
