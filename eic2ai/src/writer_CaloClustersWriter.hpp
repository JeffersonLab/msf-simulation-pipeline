// writer_CaloClustersWriter.hpp — CSV role "calo_clusters": one row per cluster ↔ MCParticle association.
//
// Needs: reco. Collections read: the *ClusterAssociations collections listed below
// (MCRecoClusterParticleAssociation: rec → Cluster, sim → MCParticle, weight); through the
// relations, the cluster collections, their CalorimeterHit constituents and MCParticles.
// Columns: evt, clu_index, clu_collection, the prt_* block (the associated particle: identity,
// origin, kinematics, vertex, endpoint), the clu_* block (detector from the first constituent
// hit, type, energy, time, size, position, errors, angles, association weight).
//
// Port of csv_convert/edm4eic_calo_clusters.cxx; rows are byte-identical to the macro's, the
// header follows column_renames.py (prt_status → prt_gen_status).
// Trap: a cluster carries no cellID of its own; the system comes from its first constituent
// hit with a non-zero cellID. Some combined or truth cluster collections (EcalBarrel) persist
// no resolvable hit relation, so clu_first_cell_id stays 0 and the system is "Unknown"; the
// clu_collection column still names the detector.
#pragma once

#include "Writer.hpp"
#include "origin.hpp"

#include <edm4eic/CalorimeterHitCollection.h>
#include <edm4eic/ClusterCollection.h>
#include <edm4eic/MCRecoClusterParticleAssociationCollection.h>
#include <edm4hep/MCParticleCollection.h>
#include <fmt/core.h>

#include <string_view>

class CaloClustersWriter : public Writer {
public:
    std::string role() const override { return "calo_clusters"; }
    Needs needs() const override { return Needs{.reco = true, .sim = false}; }

    std::vector<std::string> collections() const override {
        std::vector<std::string> names = {"MCParticles"};
        for (const std::string& association : association_names()) {
            // Relation targets: the cluster collection (ClusterAssociations → Clusters, keeping a
            // trailing tag such as Baseline) and the calorimeter hits the clusters were built from
            // (RecHits, or MergedHits / SubcellHits for the HCALs; names a file lacks are ignored).
            names.push_back(association);
            const size_t tag = association.find("ClusterAssociations");
            names.push_back(association.substr(0, tag) + "Clusters" + association.substr(tag + std::string("ClusterAssociations").size()));
            std::string detector = association.substr(0, tag);
            for (const char* algorithm : {"TruthCluster", "SplitMergeCluster", "Cluster"}) {
                const size_t cut = detector.find(algorithm);
                if (cut != std::string::npos) detector = detector.substr(0, cut);
            }
            names.insert(names.end(), {detector + "RecHits", detector + "MergedHits", detector + "SubcellHits"});
        }
        return names;
    }

    std::string csv_header() const override {
        return "evt,clu_index,clu_collection,prt_index,"
               "prt_pdg,prt_gen_status,prt_origin,prt_energy,prt_charge,"
               "prt_mom_x,prt_mom_y,prt_mom_z,"
               "prt_vtx_time,prt_vtx_pos_x,prt_vtx_pos_y,prt_vtx_pos_z,"
               "prt_end_time,prt_end_pos_x,prt_end_pos_y,prt_end_pos_z,"
               "clu_first_cell_id,clu_system_id,clu_system_name,"
               "clu_type,clu_energy,clu_energy_err,clu_time,clu_time_err,clu_nhits,"
               "clu_pos_x,clu_pos_y,clu_pos_z,"
               "clu_pos_err_xx,clu_pos_err_yy,clu_pos_err_zz,"
               "clu_theta,clu_phi,clu_assoc_weight";
    }

private:
    /// One row: a cluster ↔ MCParticle association.
    struct Record {
        uint64_t evt;                    // entry counter
        uint64_t clu_index;              // index of the association in its collection
        std::string clu_collection;      // association collection name (detector + algorithm)
        uint64_t prt_index;              // index of the associated MCParticle

        int32_t prt_pdg;                 // PDG code
        int32_t prt_gen_status;          // generator status: 1 or 2 from the generator, 0 created by Geant4
        int32_t prt_origin;              // 0 unknown, 1 signal, 2 g4-from-signal, 3 background, 4 g4-from-background

        double prt_energy;               // total energy [GeV]
        float prt_charge;                // electric charge [e]
        double prt_mom_x;                // momentum x [GeV/c]
        double prt_mom_y;                // momentum y [GeV/c]
        double prt_mom_z;                // momentum z [GeV/c]

        float prt_vtx_time;              // time at the production vertex [ns]
        float prt_vtx_pos_x;             // production vertex x [mm]
        float prt_vtx_pos_y;             // production vertex y [mm]
        float prt_vtx_pos_z;             // production vertex z [mm]

        float prt_end_time;              // time at the endpoint [ns]; EDM4hep stores one particle time, so equal to prt_vtx_time
        float prt_end_pos_x;             // endpoint x [mm]
        float prt_end_pos_y;             // endpoint y [mm]
        float prt_end_pos_z;             // endpoint z [mm]

        uint64_t clu_first_cell_id;      // cellID of the cluster's first constituent hit (0 when none resolves)
        uint64_t clu_system_id;          // detector system id (bits 0-7 of that cellID)
        std::string clu_system_name;     // detector name from the system table

        int32_t clu_type;                // cluster type flag
        float clu_energy;                // reconstructed cluster energy [GeV]
        float clu_energy_err;            // cluster energy uncertainty [GeV]
        float clu_time;                  // cluster time [ns]
        float clu_time_err;              // cluster time uncertainty [ns]
        uint32_t clu_nhits;              // number of hits in the cluster
        float clu_pos_x;                 // cluster position x [mm]
        float clu_pos_y;                 // cluster position y [mm]
        float clu_pos_z;                 // cluster position z [mm]
        float clu_pos_err_xx;            // position variance xx [mm^2]
        float clu_pos_err_yy;            // position variance yy [mm^2]
        float clu_pos_err_zz;            // position variance zz [mm^2]
        float clu_theta;                 // intrinsic polar angle [rad]
        float clu_phi;                   // intrinsic azimuthal angle [rad]
        float clu_assoc_weight;          // association weight (cluster ↔ MCParticle)

        std::string csv_line() const {
            return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                evt, clu_index, clu_collection, prt_index,
                prt_pdg, prt_gen_status, prt_origin, prt_energy, prt_charge,
                prt_mom_x, prt_mom_y, prt_mom_z,
                prt_vtx_time, prt_vtx_pos_x, prt_vtx_pos_y, prt_vtx_pos_z,
                prt_end_time, prt_end_pos_x, prt_end_pos_y, prt_end_pos_z,
                clu_first_cell_id, clu_system_id, clu_system_name,
                clu_type, clu_energy, clu_energy_err, clu_time, clu_time_err, clu_nhits,
                clu_pos_x, clu_pos_y, clu_pos_z,
                clu_pos_err_xx, clu_pos_err_yy, clu_pos_err_zz,
                clu_theta, clu_phi, clu_assoc_weight);
        }
    };

    /// Cluster ↔ MCParticle association collections. The order is the row order; collections
    /// absent from a file are skipped (the set differs between eicrecon versions).
    static const std::vector<std::string>& association_names() {
        static const std::vector<std::string> names = {
            "B0ECalClusterAssociations",

            "EcalBarrelClusterAssociations",
            "EcalBarrelImagingClusterAssociations",
            "EcalBarrelScFiClusterAssociations",
            "EcalBarrelTruthClusterAssociations",

            "EcalEndcapNClusterAssociations",
            "EcalEndcapNSplitMergeClusterAssociations",
            "EcalEndcapNTruthClusterAssociations",

            "EcalEndcapPClusterAssociations",
            "EcalEndcapPSplitMergeClusterAssociations",
            "EcalEndcapPTruthClusterAssociations",

            "EcalFarForwardZDCClusterAssociations",
            "EcalFarForwardZDCTruthClusterAssociations",

            "HcalFarForwardZDCClusterAssociations",
            "HcalFarForwardZDCClusterAssociationsBaseline",
            "HcalFarForwardZDCTruthClusterAssociations",

            "EcalLumiSpecClusterAssociations",
            "EcalLumiSpecTruthClusterAssociations",

            "HcalBarrelClusterAssociations",
            "HcalBarrelSplitMergeClusterAssociations",
            "HcalBarrelTruthClusterAssociations",

            "HcalEndcapNClusterAssociations",
            "HcalEndcapNSplitMergeClusterAssociations",
            "HcalEndcapNTruthClusterAssociations",

            "HcalEndcapPInsertClusterAssociations",

            "LFHCALClusterAssociations",
            "LFHCALSplitMergeClusterAssociations",
        };
        return names;
    }

    void write_event(const EventInputs& inputs) override {
        for (const std::string& association_name : association_names()) {
            write_association_collection(*inputs.reco, association_name, inputs.entry_index);
        }
    }

    void write_association_collection(const podio::Frame& reco, const std::string& association_name, uint64_t evt) {
        const auto* associations = get_optional_collection<edm4eic::MCRecoClusterParticleAssociationCollection>(reco, association_name);
        if (!associations) {
            skip_notes.note(association_name, "cluster association collection not in file");
            return;
        }

        for (const auto& association : *associations) {
            auto warn = [&](std::string_view message) {
                fmt::print("WARNING! calo_clusters event={} col={} assoc.index:{}. {}\n",
                           evt, association_name, association.getObjectID().index, message);
            };
            const auto cluster = association.getRec();
            if (!cluster.isAvailable()) {
                warn("!assoc.getRec().isAvailable()");
                continue;
            }
            const auto particle = association.getSim();
            if (!particle.isAvailable()) {
                warn("!assoc.getSim().isAvailable()");
                continue;
            }

            uint64_t first_cell_id = 0;
            for (const auto& hit : cluster.getHits()) {
                if (hit.isAvailable() && hit.getCellID() != 0) {
                    first_cell_id = hit.getCellID();
                    break;
                }
            }
            const SystemInfo system = system_of(first_cell_id);

            Record record{};
            record.evt = evt;
            record.clu_index = association.getObjectID().index;
            record.clu_collection = association_name;
            record.prt_index = particle.getObjectID().index;

            record.prt_pdg = particle.getPDG();
            record.prt_gen_status = particle.getGeneratorStatus();
            record.prt_origin = get_origin_status(particle);
            record.prt_energy = particle.getEnergy();
            record.prt_charge = particle.getCharge();
            record.prt_mom_x = particle.getMomentum().x;
            record.prt_mom_y = particle.getMomentum().y;
            record.prt_mom_z = particle.getMomentum().z;
            record.prt_vtx_time = particle.getTime();
            record.prt_vtx_pos_x = particle.getVertex().x;
            record.prt_vtx_pos_y = particle.getVertex().y;
            record.prt_vtx_pos_z = particle.getVertex().z;
            record.prt_end_time = particle.getTime();
            record.prt_end_pos_x = particle.getEndpoint().x;
            record.prt_end_pos_y = particle.getEndpoint().y;
            record.prt_end_pos_z = particle.getEndpoint().z;

            record.clu_first_cell_id = first_cell_id;
            record.clu_system_id = system.id;
            record.clu_system_name = system.name;

            record.clu_type = cluster.getType();
            record.clu_energy = cluster.getEnergy();
            record.clu_energy_err = cluster.getEnergyError();
            record.clu_time = cluster.getTime();
            record.clu_time_err = cluster.getTimeError();
            record.clu_nhits = cluster.getNhits();
            record.clu_pos_x = cluster.getPosition().x;
            record.clu_pos_y = cluster.getPosition().y;
            record.clu_pos_z = cluster.getPosition().z;
            record.clu_pos_err_xx = cluster.getPositionError().xx;
            record.clu_pos_err_yy = cluster.getPositionError().yy;
            record.clu_pos_err_zz = cluster.getPositionError().zz;
            record.clu_theta = cluster.getIntrinsicTheta();
            record.clu_phi = cluster.getIntrinsicPhi();
            record.clu_assoc_weight = association.getWeight();

            write_row(record.csv_line());
        }
    }
};
