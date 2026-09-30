// writer_RecoParticlesWriter.hpp — CSV role "reco_particles": one row per reconstructed particle.
//
// Needs: reco. Collections read: ReconstructedParticles; through the relations, its Clusters
// (and their CalorimeterHits), Tracks (their Measurement2D and TrackerHits), daughter
// particles and ParticleIDs, which are only counted.
// Columns: evt, then the rec_* block: identity, kinematics, reference point, PID goodness and
// type, and the sizes of its relations.
//
// Port of csv_convert/edm4eic_reco_particles.cxx; rows are byte-identical to the macro's, the
// header follows column_renames.py (the macro wrote unprefixed names and "event").
#pragma once

#include "Writer.hpp"

#include <edm4eic/ReconstructedParticleCollection.h>
#include <fmt/core.h>

class RecoParticlesWriter : public Writer {
public:
    std::string role() const override { return "reco_particles"; }
    Needs needs() const override { return Needs{.reco = true, .sim = false}; }
    std::vector<std::string> collections() const override {
        // The counts follow relations into clusters (their hit ranges), tracks and their
        // measurements, so those collections must be read; eicrecon 26.06 names.
        return {"ReconstructedParticles",
                "B0ECalClusters", "EcalBarrelClusters", "EcalBarrelImagingClusters", "EcalBarrelScFiClusters", "EcalBarrelTruthClusters",
                "EcalEndcapNClusters", "EcalEndcapNSplitMergeClusters", "EcalEndcapNTruthClusters",
                "EcalEndcapPClusters", "EcalEndcapPSplitMergeClusters", "EcalEndcapPTruthClusters",
                "EcalFarForwardZDCClusters", "EcalFarForwardZDCTruthClusters", "EcalLumiSpecClusters", "EcalLumiSpecTruthClusters",
                "HcalBarrelClusters", "HcalBarrelSplitMergeClusters", "HcalBarrelTruthClusters",
                "HcalEndcapNClusters", "HcalEndcapNSplitMergeClusters", "HcalEndcapNTruthClusters", "HcalEndcapPInsertClusters",
                "HcalFarForwardZDCClusters", "HcalFarForwardZDCClustersBaseline", "HcalFarForwardZDCTruthClusters",
                "LFHCALClusters", "LFHCALSplitMergeClusters",
                "CentralCKFTracks", "CentralCKFTracksUnfiltered", "CentralCKFTruthSeededTracks", "CentralCKFTruthSeededTracksUnfiltered",
                "B0TrackerCKFTracks", "B0TrackerCKFTracksUnfiltered", "B0TrackerCKFTruthSeededTracks", "B0TrackerCKFTruthSeededTracksUnfiltered",
                "CentralTrackerMeasurements", "B0TrackerMeasurements"};
    }

    std::string csv_header() const override {
        return "evt,rec_index,rec_pdg,rec_charge,rec_energy,rec_mass,rec_mom_x,rec_mom_y,rec_mom_z,"
               "rec_ref_pos_x,rec_ref_pos_y,rec_ref_pos_z,rec_pid_goodness,rec_type,"
               "rec_n_clusters,rec_n_tracks,rec_n_particles,rec_n_particle_ids,rec_n_cluster_hits,rec_n_track_measurements,rec_n_tracker_hits";
    }

private:
    /// One row: a reconstructed particle.
    struct Record {
        uint64_t event;                  // entry counter
        int32_t id;                      // index of the particle in ReconstructedParticles
        int32_t pdg;                     // PDG code assigned by reconstruction
        float charge;                    // electric charge [e]
        float energy;                    // energy [GeV]
        float mass;                      // mass [GeV/c^2]
        float px;                        // momentum x [GeV/c]
        float py;                        // momentum y [GeV/c]
        float pz;                        // momentum z [GeV/c]
        float ref_x;                     // reference point x [mm]
        float ref_y;                     // reference point y [mm]
        float ref_z;                     // reference point z [mm]
        float pid_goodness;              // goodness of the PID hypothesis
        int32_t type;                    // particle type flag
        size_t n_clusters;               // clusters attached to the particle
        size_t n_tracks;                 // tracks attached to the particle
        size_t n_particles;              // daughter particles
        size_t n_particle_ids;           // ParticleID hypotheses
        size_t n_cluster_hits;           // calorimeter hits behind the clusters
        size_t n_track_measurements;     // measurements behind the tracks
        size_t n_tracker_hits;           // tracker hits behind those measurements

        std::string csv_line() const {
            return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                event, id, pdg, charge, energy, mass, px, py, pz, ref_x, ref_y, ref_z, pid_goodness, type,
                n_clusters, n_tracks, n_particles, n_particle_ids, n_cluster_hits, n_track_measurements, n_tracker_hits);
        }
    };

    void write_event(const EventInputs& inputs) override {
        const auto* particles = get_optional_collection<edm4eic::ReconstructedParticleCollection>(*inputs.reco, "ReconstructedParticles");
        if (!particles) {
            skip_notes.note("ReconstructedParticles", "collection not in file");
            return;
        }
        for (const auto& particle : *particles) {
            Record record{};
            record.event = inputs.entry_index;
            record.id = particle.getObjectID().index;
            record.pdg = particle.getPDG();
            record.charge = particle.getCharge();
            record.energy = particle.getEnergy();
            record.mass = particle.getMass();
            record.px = particle.getMomentum().x;
            record.py = particle.getMomentum().y;
            record.pz = particle.getMomentum().z;
            record.ref_x = particle.getReferencePoint().x;
            record.ref_y = particle.getReferencePoint().y;
            record.ref_z = particle.getReferencePoint().z;
            record.pid_goodness = particle.getGoodnessOfPID();
            record.type = particle.getType();
            record.n_clusters = particle.getClusters().size();
            record.n_tracks = particle.getTracks().size();
            record.n_particles = particle.getParticles().size();
            record.n_particle_ids = particle.getParticleIDs().size();
            for (const auto& cluster : particle.getClusters()) {
                record.n_cluster_hits += cluster.getHits().size();
            }
            for (const auto& track : particle.getTracks()) {
                for (const auto& measurement : track.getMeasurements()) {
                    ++record.n_track_measurements;
                    record.n_tracker_hits += measurement.getHits().size();
                }
            }
            write_row(record.csv_line());
        }
    }
};
