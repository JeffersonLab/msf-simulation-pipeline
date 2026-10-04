// writer_RecoDisWriter.hpp — CSV role "reco_dis": one row per event with reconstructed and true DIS kinematics.
//
// Needs: reco. Collections read: the seven InclusiveKinematics* collections (one method each),
// MCParticles (beam proton, beam and scattered electron, the Λ), ReconstructedLambdas (the
// far-forward Λ) and, through the electron method's scat relation, the reconstructed electron.
// Columns: event; x, q2, y, nu, w per method (da, esigma, electron, jb, ml, sigma, truth); the true
// mc_* values from the generator's frame parameters; Mandelstam t of the Λ against the true
// and the assumed beam proton; the reconstructed electron; MC momenta of the scattered
// electron, the Λ, the far-forward Λ, the beam proton and the beam electron.
//
// An event without a beam proton among MCParticles yields no row (the macro's behavior;
// background-merged frames have none). Port of csv_convert/edm4eic_reco_dis.cxx; rows are
// byte-identical to the macro's except the added truth_* block, the header follows
// column_renames.py (evt, elec_index,
// *_mom_x, *_ref_pos_x; the parallel kinematics blocks keep their names). Trap: the 35
// kinematics columns use ostream formatting
// (stream_text, 6 significant digits); every other number uses fmt. The four-vector arithmetic
// repeats TLorentzVector's inline formulas term by term, which the parity gate checks.
#pragma once

#include "Writer.hpp"
#include "legacy_format.hpp"

#include <edm4eic/InclusiveKinematicsCollection.h>
#include <edm4eic/ReconstructedParticleCollection.h>
#include <edm4hep/MCParticleCollection.h>
#include <fmt/core.h>

#include <cmath>
#include <stdexcept>
#include <utility>

class RecoDisWriter : public Writer {
public:
    std::string role() const override { return "reco_dis"; }
    Needs needs() const override { return Needs{.reco = true, .sim = false}; }

    std::vector<std::string> collections() const override {
        std::vector<std::string> names = {"MCParticles", "ReconstructedLambdas",
                                          "ReconstructedElectrons", "ReconstructedChargedParticles", "ReconstructedParticles"};
        for (const auto& [method, collection] : kinematics_methods()) names.push_back(collection);
        return names;
    }

    std::string csv_header() const override {
        std::string header = "evt";
        for (const auto& [method, collection] : kinematics_methods()) {
            header += fmt::format(",{0}_x,{0}_q2,{0}_y,{0}_nu,{0}_w", method);
        }
        header += ",mc_x,mc_q2,mc_y,mc_nu,mc_w";
        header += ",mc_true_t,mc_lam_tb_t,mc_lam_exp_t,ff_lam_tb_t,ff_lam_exp_t";
        header += ",elec_index,elec_energy,elec_mom_x,elec_mom_y,elec_mom_z,elec_ref_pos_x,elec_ref_pos_y,elec_ref_pos_z,"
                  "elec_pid_goodness,elec_type,elec_n_clusters,elec_n_tracks,elec_n_particles,elec_n_particle_ids";
        header += ",mc_elec_mom_x,mc_elec_mom_y,mc_elec_mom_z,mc_lam_mom_x,mc_lam_mom_y,mc_lam_mom_z,ff_lam_mom_x,ff_lam_mom_y,ff_lam_mom_z,"
                  "mc_beam_prot_mom_x,mc_beam_prot_mom_y,mc_beam_prot_mom_z,mc_beam_elec_mom_x,mc_beam_elec_mom_y,mc_beam_elec_mom_z";
        return header;
    }

private:
    static constexpr double proton_mass = 0.938272;     // GeV
    static constexpr double lambda_mass = 1.115683;     // GeV
    static constexpr double electron_mass = 0.000511;   // GeV

    /// The subset of TLorentzVector the macro used, with the same inline arithmetic.
    struct FourVector {
        double px = 0, py = 0, pz = 0, energy = 0;
        bool set() const { return energy > 0; }   // a default vector has energy 0, the macro's "not found" test
        double momentum() const { return std::sqrt(px * px + py * py + pz * pz); }
    };

    static FourVector four_vector(double px, double py, double pz, double mass) {
        FourVector vector;
        vector.px = px;
        vector.py = py;
        vector.pz = pz;
        vector.energy = std::sqrt(px * px + py * py + pz * pz + mass * mass);
        return vector;
    }

    /// t = (a - b)^2, as TLorentzVector::M2 of the difference.
    static double mandelstam_t(const FourVector& a, const FourVector& b) {
        const double dx = a.px - b.px;
        const double dy = a.py - b.py;
        const double dz = a.pz - b.pz;
        const double de = a.energy - b.energy;
        return de * de - (dx * dx + dy * dy + dz * dz);
    }

    /// The beam proton as an experiment would assume it: the nominal momentum of the running
    /// mode closest to the true beam (41, 100, 130 or 275 GeV/c) with the crossing angles
    /// applied. Unset when no mode is within 10 GeV (a wrongly-picked proton in a merged
    /// frame): the t-against-assumed-beam columns stay empty and the row survives — a throw
    /// here would discard the whole role for the file.
    static FourVector approximate_beam_proton(const FourVector& true_beam_proton) {
        const double true_momentum = true_beam_proton.momentum();
        double nominal_momentum = 0.0;
        for (double mode : {41.0, 100.0, 130.0, 275.0}) {
            if (std::abs(true_momentum - mode) < 10) nominal_momentum = mode;
        }
        if (nominal_momentum == 0.0) {
            return FourVector{};
        }
        constexpr double crossing_angle_horizontal = 25e-3;   // rad
        constexpr double crossing_angle_vertical = 100e-6;    // rad
        const double px = nominal_momentum * std::sin(crossing_angle_horizontal);
        const double py = nominal_momentum * std::sin(crossing_angle_vertical) * std::cos(crossing_angle_horizontal);
        const double pz = nominal_momentum * std::cos(crossing_angle_horizontal) * std::cos(crossing_angle_vertical);
        return four_vector(px, py, pz, proton_mass);
    }

    struct McKinematics {
        FourVector beam_proton;         // the first generatorStatus-4 proton, else the first proton
        FourVector beam_electron;       // the first generatorStatus-4 electron, else the first electron
        FourVector scattered_electron;  // the first electron after the beam electron
        FourVector lambda;              // the first Λ; the scan stops there
    };

    /// Beam particles carry generatorStatus 4 (HepMC convention). Preferring it over "first of
    /// its PDG" matters in background-merged pythia8 frames, where final-state protons and
    /// electrons can precede the beams in the collection; in the meson-structure files the
    /// beams come first anyway, so the selection is unchanged there.
    static McKinematics find_mc_particles(const edm4hep::MCParticleCollection& particles) {
        McKinematics found;
        bool beam_proton_is_beam_status = false, beam_electron_is_beam_status = false;
        bool found_beam_proton = false, found_beam_electron = false, found_scattered_electron = false;
        for (const auto& particle : particles) {
            const auto momentum = particle.getMomentum();
            const bool beam_status = particle.getGeneratorStatus() == 4;
            if (particle.getPDG() == 2212
                && (!found_beam_proton || (beam_status && !beam_proton_is_beam_status))) {
                found.beam_proton = four_vector(momentum.x, momentum.y, momentum.z, proton_mass);
                found_beam_proton = true;
                beam_proton_is_beam_status = beam_status;
            }
            if (particle.getPDG() == 11) {
                if (!found_beam_electron || (beam_status && !beam_electron_is_beam_status)) {
                    found.beam_electron = four_vector(momentum.x, momentum.y, momentum.z, electron_mass);
                    found_beam_electron = true;
                    beam_electron_is_beam_status = beam_status;
                    found_scattered_electron = false;   // scattered = first electron after the beam
                } else if (!found_scattered_electron) {
                    found.scattered_electron = four_vector(momentum.x, momentum.y, momentum.z, electron_mass);
                    found_scattered_electron = true;
                }
            }
            if (particle.getPDG() == 3122 && !found.lambda.set()) {
                found.lambda = four_vector(momentum.x, momentum.y, momentum.z, lambda_mass);
            }
            // The scan stops at the first Λ once the other particles are in hand. In merged
            // pythia8 frames a Λ can precede the beams; stopping there would lose the row.
            if (found.lambda.set() && found_beam_proton && found_beam_electron && found_scattered_electron) {
                break;
            }
        }
        return found;
    }

    /// Method name (column prefix) → InclusiveKinematics collection, in column order.
    static const std::vector<std::pair<std::string, std::string>>& kinematics_methods() {
        static const std::vector<std::pair<std::string, std::string>> methods = {
            {"da", "InclusiveKinematicsDA"},
            {"esigma", "InclusiveKinematicsESigma"},
            {"electron", "InclusiveKinematicsElectron"},
            {"jb", "InclusiveKinematicsJB"},
            {"ml", "InclusiveKinematicsML"},
            {"sigma", "InclusiveKinematicsSigma"},
            {"truth", "InclusiveKinematicsTruth"},   // EICrecon's true-MC kinematics; the only
                                                     // true Q2/x source on files without dis_* params
        };
        return methods;
    }

    static std::string electron_columns(const edm4eic::ReconstructedParticle& electron) {
        const auto momentum = electron.getMomentum();
        const auto reference = electron.getReferencePoint();
        return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
            electron.getObjectID().index, electron.getEnergy(), momentum.x, momentum.y, momentum.z,
            reference.x, reference.y, reference.z, electron.getGoodnessOfPID(), electron.getType(),
            electron.getClusters().size(), electron.getTracks().size(), electron.getParticles().size(), electron.getParticleIDs().size());
    }

    /// Three momentum columns, empty when the vector was not found.
    static std::string momentum_columns(const FourVector& vector) {
        if (!vector.set()) return ",,,";
        return fmt::format(",{},{},{}", vector.px, vector.py, vector.pz);
    }

    static std::string optional_number(bool present, double value) {
        return present ? fmt::format("{}", value) : "";
    }

    void write_event(const EventInputs& inputs) override {
        const podio::Frame& reco = *inputs.reco;
        const auto* particles = get_optional_collection<edm4hep::MCParticleCollection>(reco, "MCParticles");
        if (!particles) {
            skip_notes.note("MCParticles", "collection not in file");
            return;
        }
        const McKinematics mc = find_mc_particles(*particles);
        if (!mc.beam_proton.set()) {
            fmt::print("Warning: No beam proton found in event {}, skipping...\n", inputs.entry_index);
            return;
        }
        const FourVector assumed_beam_proton = approximate_beam_proton(mc.beam_proton);
        const double mc_lambda_t_true_beam = mc.lambda.set() ? mandelstam_t(mc.beam_proton, mc.lambda) : 0.0;
        const double mc_lambda_t_assumed_beam = mc.lambda.set() && assumed_beam_proton.set()
            ? mandelstam_t(assumed_beam_proton, mc.lambda) : 0.0;

        FourVector ff_lambda;
        const auto* ff_lambdas = get_optional_collection<edm4eic::ReconstructedParticleCollection>(reco, "ReconstructedLambdas");
        if (!ff_lambdas) {
            skip_notes.note("ReconstructedLambdas", "collection not in file");
        } else if (!ff_lambdas->empty()) {
            const auto momentum = (*ff_lambdas)[0].getMomentum();
            ff_lambda = four_vector(momentum.x, momentum.y, momentum.z, lambda_mass);
        }
        const double ff_lambda_t_true_beam = ff_lambda.set() ? mandelstam_t(mc.beam_proton, ff_lambda) : 0.0;
        const double ff_lambda_t_assumed_beam = ff_lambda.set() && assumed_beam_proton.set()
            ? mandelstam_t(assumed_beam_proton, ff_lambda) : 0.0;

        std::string line = fmt::format("{}", inputs.entry_index);
        const edm4eic::InclusiveKinematicsCollection* electron_method = nullptr;
        for (const auto& [method, collection_name] : kinematics_methods()) {
            const auto* kinematics = get_optional_collection<edm4eic::InclusiveKinematicsCollection>(reco, collection_name);
            if (!kinematics) skip_notes.note(collection_name, "collection not in file");
            if (method == "electron") electron_method = kinematics;
            if (!kinematics || kinematics->size() != 1) {
                line += ",,,,,";
                continue;
            }
            const auto& value = (*kinematics)[0];
            line += "," + stream_text(value.getX()) + "," + stream_text(value.getQ2()) + "," + stream_text(value.getY())
                  + "," + stream_text(value.getNu()) + "," + stream_text(value.getW());
        }
        for (const char* key : {"dis_xbj", "dis_q2", "dis_y_d", "dis_nu", "dis_w", "dis_tspectator"}) {
            line += "," + reco.getParameter<std::string>(key).value_or("");
        }
        line += "," + optional_number(mc.lambda.set(), mc_lambda_t_true_beam);
        line += "," + optional_number(mc.lambda.set() && assumed_beam_proton.set(), mc_lambda_t_assumed_beam);
        line += "," + optional_number(ff_lambda.set(), ff_lambda_t_true_beam);
        line += "," + optional_number(ff_lambda.set() && assumed_beam_proton.set(), ff_lambda_t_assumed_beam);

        if (electron_method && electron_method->size() == 1 && (*electron_method)[0].getScat().isAvailable()) {
            line += "," + electron_columns((*electron_method)[0].getScat());
        } else {
            line += "," + std::string(13, ',');   // 14 empty fields
        }
        line += momentum_columns(mc.scattered_electron);
        line += momentum_columns(mc.lambda);
        line += momentum_columns(ff_lambda);
        line += momentum_columns(mc.beam_proton);
        line += momentum_columns(mc.beam_electron);
        write_row(line);
    }
};
