// writer_McParticlesWriter.hpp — CSV role "mc_particles": one row per MCParticle of every event.
//
// Needs: reco (the MCParticles collection eicrecon copies from the simulation). Collections
// read: MCParticles. Columns: evt, prt_index, identity and both status words, prt_origin,
// kinematics, production vertex, endpoint, family links.
//
// Port of csv_convert/edm4eic_mc_particles.cxx; rows and header are byte-identical to the
// macro's: this role already used the unified vocabulary (prt_gen_status, prt_sim_status).
#pragma once

#include "Writer.hpp"
#include "origin.hpp"

#include <edm4hep/MCParticleCollection.h>
#include <fmt/core.h>

class McParticlesWriter : public Writer {
public:
    std::string role() const override { return "mc_particles"; }
    Needs needs() const override { return Needs{.reco = true, .sim = false}; }
    std::vector<std::string> collections() const override { return {"MCParticles"}; }

    std::string csv_header() const override {
        return "evt,prt_index,"
               "prt_pdg,prt_gen_status,prt_sim_status,prt_origin,"
               "prt_mass,prt_energy,prt_charge,"
               "prt_mom_x,prt_mom_y,prt_mom_z,"
               "prt_vtx_time,prt_vtx_pos_x,prt_vtx_pos_y,prt_vtx_pos_z,"
               "prt_end_pos_x,prt_end_pos_y,prt_end_pos_z,"
               "prt_n_parents,prt_n_daughters,prt_parent_index";
    }

private:
    /// One row: an MCParticle.
    struct Record {
        uint64_t evt;                    // entry counter
        uint64_t prt_index;              // index of the particle in MCParticles

        int32_t prt_pdg;                 // PDG code
        int32_t prt_gen_status;          // generatorStatus: 1 or 2 signal, >= 1000 background (merger offset), 0 created by Geant4
        int32_t prt_sim_status;          // simulatorStatus bit field (createdInSimulation, decayedInTracker, ...)
        int32_t prt_origin;              // 0 unknown, 1 signal, 2 g4-from-signal, 3 background, 4 g4-from-background

        double prt_mass;                 // mass [GeV/c^2]
        double prt_energy;               // total energy [GeV]
        float prt_charge;                // electric charge [e]
        double prt_mom_x;                // momentum x [GeV/c]
        double prt_mom_y;                // momentum y [GeV/c]
        double prt_mom_z;                // momentum z [GeV/c]

        float prt_vtx_time;              // time at the production vertex [ns]
        float prt_vtx_pos_x;             // production vertex x [mm]
        float prt_vtx_pos_y;             // production vertex y [mm]
        float prt_vtx_pos_z;             // production vertex z [mm]

        float prt_end_pos_x;             // endpoint x [mm]
        float prt_end_pos_y;             // endpoint y [mm]
        float prt_end_pos_z;             // endpoint z [mm]

        uint32_t prt_n_parents;          // number of parents
        uint32_t prt_n_daughters;        // number of daughters
        int64_t prt_parent_index;        // index of the first parent, or -1 without parents

        std::string csv_line() const {
            return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                evt, prt_index,
                prt_pdg, prt_gen_status, prt_sim_status, prt_origin,
                prt_mass, prt_energy, prt_charge,
                prt_mom_x, prt_mom_y, prt_mom_z,
                prt_vtx_time, prt_vtx_pos_x, prt_vtx_pos_y, prt_vtx_pos_z,
                prt_end_pos_x, prt_end_pos_y, prt_end_pos_z,
                prt_n_parents, prt_n_daughters, prt_parent_index);
        }
    };

    void write_event(const EventInputs& inputs) override {
        const auto* particles = get_optional_collection<edm4hep::MCParticleCollection>(*inputs.reco, "MCParticles");
        if (!particles) {
            skip_notes.note("MCParticles", "collection not in file");
            return;
        }
        for (const auto& particle : *particles) {
            Record record{};
            record.evt = inputs.entry_index;
            record.prt_index = particle.getObjectID().index;
            record.prt_pdg = particle.getPDG();
            record.prt_gen_status = particle.getGeneratorStatus();
            record.prt_sim_status = particle.getSimulatorStatus();
            record.prt_origin = get_origin_status(particle);
            record.prt_mass = particle.getMass();
            record.prt_energy = particle.getEnergy();
            record.prt_charge = particle.getCharge();
            record.prt_mom_x = particle.getMomentum().x;
            record.prt_mom_y = particle.getMomentum().y;
            record.prt_mom_z = particle.getMomentum().z;
            record.prt_vtx_time = particle.getTime();
            record.prt_vtx_pos_x = particle.getVertex().x;
            record.prt_vtx_pos_y = particle.getVertex().y;
            record.prt_vtx_pos_z = particle.getVertex().z;
            record.prt_end_pos_x = particle.getEndpoint().x;
            record.prt_end_pos_y = particle.getEndpoint().y;
            record.prt_end_pos_z = particle.getEndpoint().z;
            record.prt_n_parents = static_cast<uint32_t>(particle.parents_size());
            record.prt_n_daughters = static_cast<uint32_t>(particle.daughters_size());
            record.prt_parent_index = particle.parents_size() > 0
                ? static_cast<int64_t>(particle.getParents(0).getObjectID().index)
                : -1;
            write_row(record.csv_line());
        }
    }
};
