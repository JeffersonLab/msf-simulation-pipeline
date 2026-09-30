// writer_McpartLambdaWriter.hpp — CSV role "mcpart_lambda": one row per generated Λ with its decay products.
//
// Needs: reco (the MCParticles collection eicrecon copies from the simulation). Collections
// read: MCParticles. Columns: event, lam_is_first, lam_decay, then a 16-column particle block
// (mc_particle_block.hpp) for the Λ and for each possible product: prot, pimin (Λ → p π⁻),
// neut, pizero and the two photons gamone, gamtwo (Λ → n π⁰ → n γ γ). A product that does not
// exist leaves its block empty. lam_decay codes: lambda_decay.hpp. lam_is_first marks the first
// Λ of the event, which the generator makes the spectator Λ.
//
// Port of csv_convert/edm4eic_mcpart_lambda.cxx; rows are byte-identical to the macro's, the
// header follows column_renames.py (event → evt, the block names of mc_particle_block.hpp).
#pragma once

#include "Writer.hpp"
#include "lambda_decay.hpp"
#include "mc_particle_block.hpp"

#include <edm4hep/MCParticleCollection.h>
#include <fmt/core.h>

class McpartLambdaWriter : public Writer {
public:
    std::string role() const override { return "mcpart_lambda"; }
    Needs needs() const override { return Needs{.reco = true, .sim = false}; }
    std::vector<std::string> collections() const override { return {"MCParticles"}; }

    std::string csv_header() const override {
        std::string header = "evt,lam_is_first,lam_decay";
        for (const char* prefix : {"lam", "prot", "pimin", "neut", "pizero", "gamone", "gamtwo"}) {
            header += "," + mc_particle_block_header(prefix);
        }
        return header;
    }

private:
    void write_event(const EventInputs& inputs) override {
        const auto* particles = get_optional_collection<edm4hep::MCParticleCollection>(*inputs.reco, "MCParticles");
        if (!particles) {
            skip_notes.note("MCParticles", "collection not in file");
            return;
        }
        bool is_first_lambda = true;
        for (const auto& lambda : *particles) {
            if (lambda.getPDG() != 3122) continue;
            const LambdaDecay decay = classify_lambda_decay(lambda);
            if (decay.neutron && decay.proton) {
                fmt::print("(!!!) WARNING: I see neut && prot at evt_id={}\n", inputs.entry_index);
            }
            write_row(fmt::format("{},{},{},{},{},{},{},{},{},{}",
                inputs.entry_index, static_cast<int>(is_first_lambda), decay.type,
                mc_particle_block(lambda), mc_particle_block(decay.proton), mc_particle_block(decay.pi_minus),
                mc_particle_block(decay.neutron), mc_particle_block(decay.pi_zero),
                mc_particle_block(decay.gamma_one), mc_particle_block(decay.gamma_two)));
            is_first_lambda = false;
        }
    }
};
