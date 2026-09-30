// writer_CalHitsWriter.hpp — CSV role "cal_hits": one row per digitized calorimeter rec hit, truth-labeled from the sim cell.
//
// Needs: reco + sim. Collections read: the *RecHits (edm4eic::CalorimeterHit) collections of
// the reco frame and the paired *Hits (edm4hep::SimCalorimeterHit) collections of the sim
// frame listed below; through the relations, the sim frame's CaloHitContributions and
// MCParticles.
// Columns: evt, rec_collection, the cal_hit_* block (digitized quantities, the inference-legal
// features), the sim_* truth aggregates of the cell, label_purity, the prt_* block of the
// representative particle.
//
// The reco output carries no calorimeter hit ↔ MCParticle association, so the join runs on
// (event, cellID): every rec hit's cell is looked up among the event's sim hits. A cell with
// several contributing particles gets the prt_origin of the energy-majority origin class,
// label_purity = that class's energy fraction, and the prt_* block of the largest contributor
// of that class. A rec hit whose cell has no sim hit keeps its features and prt_origin 0.
//
// Port of csv_convert/edm4eic_cal_hits.cxx (process_detector); rows are byte-identical to the
// macro's, the header follows column_renames.py (prt_status → prt_gen_status, rec_collection →
// cal_hit_collection). Trap: ties in the majority vote resolve to the lower origin value and, inside a
// class, to the lower particle key (std::map order); change the containers and the labels of
// tied cells change.
#pragma once

#include "Writer.hpp"
#include "origin.hpp"

#include <edm4eic/CalorimeterHitCollection.h>
#include <edm4hep/CaloHitContributionCollection.h>
#include <edm4hep/MCParticleCollection.h>
#include <edm4hep/SimCalorimeterHitCollection.h>
#include <fmt/core.h>

#include <algorithm>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>

class CalHitsWriter : public Writer {
public:
    std::string role() const override { return "cal_hits"; }
    Needs needs() const override { return Needs{.reco = true, .sim = true}; }

    std::vector<std::string> collections() const override {
        std::vector<std::string> names = {"MCParticles"};
        for (const auto& [rec_name, sim_name] : calorimeter_pairs()) {
            names.push_back(rec_name);
            names.push_back(sim_name);
            names.push_back(sim_name + "Contributions");
        }
        return names;
    }

    std::string csv_header() const override {
        return "evt,cal_hit_collection,cal_hit_cell_id,cal_hit_system_id,cal_hit_system_name,"
               "cal_hit_pos_x,cal_hit_pos_y,cal_hit_pos_z,"
               "cal_hit_energy,cal_hit_energy_err,cal_hit_time,cal_hit_time_err,"
               "sim_energy,sim_time,n_contrib,label_purity,"
               "prt_index,prt_pdg,prt_gen_status,prt_origin,prt_energy,prt_charge,"
               "prt_mom_x,prt_mom_y,prt_mom_z,"
               "prt_vtx_time,prt_vtx_pos_x,prt_vtx_pos_y,prt_vtx_pos_z,"
               "prt_end_pos_x,prt_end_pos_y,prt_end_pos_z";
    }

private:
    /// One row: a digitized calorimeter rec hit with the truth of its cell.
    struct Record {
        uint64_t evt;                    // entry counter
        std::string cal_hit_collection;  // rec-hit collection name (the detector)
        uint64_t cal_hit_cell_id;        // full cellID (the same cell in sim and reco)
        uint64_t cal_hit_system_id;      // detector system id (bits 0-7 of the cellID)
        std::string cal_hit_system_name; // detector name from the system table

        float cal_hit_pos_x;             // hit position x [mm]
        float cal_hit_pos_y;             // hit position y [mm]
        float cal_hit_pos_z;             // hit position z [mm]
        float cal_hit_energy;            // calibrated energy [GeV]
        float cal_hit_energy_err;        // energy uncertainty [GeV]
        float cal_hit_time;              // digitized time [ns]
        float cal_hit_time_err;          // time uncertainty [ns]

        double sim_energy;               // sum of the cell's contribution energies [GeV] (raw deposit)
        float sim_time;                  // earliest contribution time [ns]
        int32_t n_contrib;               // number of contributions in the cell
        float label_purity;              // energy fraction of the majority origin class

        uint64_t prt_index;              // index of the representative MCParticle (zeros below when the cell has no sim match)
        int32_t prt_pdg;                 // PDG code
        int32_t prt_gen_status;          // generator status (0 = created by Geant4)
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
        float prt_end_pos_x;             // endpoint x [mm]
        float prt_end_pos_y;             // endpoint y [mm]
        float prt_end_pos_z;             // endpoint z [mm]

        std::string csv_line() const {
            return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                evt, cal_hit_collection, cal_hit_cell_id, cal_hit_system_id, cal_hit_system_name,
                cal_hit_pos_x, cal_hit_pos_y, cal_hit_pos_z,
                cal_hit_energy, cal_hit_energy_err, cal_hit_time, cal_hit_time_err,
                sim_energy, sim_time, n_contrib, label_purity,
                prt_index, prt_pdg, prt_gen_status, prt_origin, prt_energy, prt_charge,
                prt_mom_x, prt_mom_y, prt_mom_z,
                prt_vtx_time, prt_vtx_pos_x, prt_vtx_pos_y, prt_vtx_pos_z,
                prt_end_pos_x, prt_end_pos_y, prt_end_pos_z);
        }
    };

    /// Rec-hit collection (reco frame) → sim-hit collection (sim frame), per calorimeter.
    /// Scope: central and far-forward (B0, ZDC, insert); no far-backward (Lumi, Tagger).
    static const std::vector<std::pair<std::string, std::string>>& calorimeter_pairs() {
        static const std::vector<std::pair<std::string, std::string>> pairs = {
            {"EcalBarrelScFiRecHits",       "EcalBarrelScFiHits"},
            {"EcalBarrelImagingRecHits",    "EcalBarrelImagingHits"},
            {"EcalEndcapNRecHits",          "EcalEndcapNHits"},
            {"EcalEndcapPRecHits",          "EcalEndcapPHits"},
            {"HcalBarrelRecHits",           "HcalBarrelHits"},
            {"HcalEndcapNRecHits",          "HcalEndcapNHits"},
            {"HcalEndcapPInsertRecHits",    "HcalEndcapPInsertHits"},
            {"LFHCALRecHits",               "LFHCALHits"},
            {"B0ECalRecHits",               "B0ECalHits"},
            {"EcalFarForwardZDCRecHits",    "EcalFarForwardZDCHits"},
            {"HcalFarForwardZDCRecHits",    "HcalFarForwardZDCHits"},
        };
        return pairs;
    }

    uint64_t unmatched_rec_hits_ = 0;   // rec hits whose cellID has no sim hit (written with prt_origin 0)

    std::string summary_notes() const override {
        return unmatched_rec_hits_ > 0 ? fmt::format("rec hits without a sim cell: {} (written with prt_origin=0)", unmatched_rec_hits_) : "";
    }

    void write_event(const EventInputs& inputs) override {
        for (const auto& [rec_name, sim_name] : calorimeter_pairs()) {
            write_calorimeter(*inputs.reco, *inputs.sim, rec_name, sim_name, inputs.entry_index);
        }
    }

    /// Packed (collectionID, index) of a particle: an ordered, hashable key that is stable across podio versions.
    static uint64_t particle_key(const edm4hep::MCParticle& particle) {
        return (uint64_t(particle.getObjectID().collectionID) << 32) | uint32_t(particle.getObjectID().index);
    }

    void write_calorimeter(const podio::Frame& reco, const podio::Frame& sim,
                           const std::string& rec_name, const std::string& sim_name, uint64_t evt) {
        const auto* rec_hits = get_optional_collection<edm4eic::CalorimeterHitCollection>(reco, rec_name);
        if (!rec_hits) {
            skip_notes.note(rec_name, "rec-hit collection not in reco file");
            return;
        }
        if (rec_hits->empty()) return;

        const auto* sim_hits = get_optional_collection<edm4hep::SimCalorimeterHitCollection>(sim, sim_name);
        if (!sim_hits) {
            skip_notes.note(sim_name, "sim-hit collection not in sim file");
            return;
        }

        // The join index: this event's sim cells by cellID. dd4hep aggregates the contributions
        // of a cell into one sim hit per event.
        std::unordered_map<uint64_t, edm4hep::SimCalorimeterHit> sim_by_cell;
        sim_by_cell.reserve(sim_hits->size());
        for (const auto& sim_hit : *sim_hits) {
            sim_by_cell.emplace(sim_hit.getCellID(), sim_hit);
        }

        for (const auto& rec_hit : *rec_hits) {
            Record record{};
            record.evt = evt;
            record.cal_hit_collection = rec_name;
            record.cal_hit_cell_id = rec_hit.getCellID();
            const SystemInfo system = system_of(rec_hit.getCellID());
            record.cal_hit_system_id = system.id;
            record.cal_hit_system_name = system.name;
            record.cal_hit_pos_x = rec_hit.getPosition().x;
            record.cal_hit_pos_y = rec_hit.getPosition().y;
            record.cal_hit_pos_z = rec_hit.getPosition().z;
            record.cal_hit_energy = rec_hit.getEnergy();
            record.cal_hit_energy_err = rec_hit.getEnergyError();
            record.cal_hit_time = rec_hit.getTime();
            record.cal_hit_time_err = rec_hit.getTimeError();

            const auto sim_match = sim_by_cell.find(rec_hit.getCellID());
            if (sim_match == sim_by_cell.end()) {
                ++unmatched_rec_hits_;
                write_row(record.csv_line());
                continue;
            }
            label_from_sim_cell(sim_match->second, record);
            write_row(record.csv_line());
        }
    }

    /// Fills the sim_* aggregates, label_purity and the prt_* block from the cell's contributions.
    void label_from_sim_cell(const edm4hep::SimCalorimeterHit& sim_hit, Record& record) {
        double energy_total = 0.0;
        float time_earliest = std::numeric_limits<float>::max();
        std::map<int32_t, double> energy_by_origin;
        std::map<int32_t, std::map<uint64_t, double>> energy_by_origin_particle;
        std::map<uint64_t, edm4hep::MCParticle> particle_by_key;
        int32_t contribution_count = 0;

        for (const auto& contribution : sim_hit.getContributions()) {
            if (!contribution.isAvailable()) continue;
            ++contribution_count;
            const double energy = contribution.getEnergy();
            energy_total += energy;
            if (contribution.getTime() < time_earliest) time_earliest = contribution.getTime();
            const auto particle = contribution.getParticle();
            if (!particle.isAvailable()) continue;
            const int32_t origin = get_origin_status(particle);
            energy_by_origin[origin] += energy;
            energy_by_origin_particle[origin][particle_key(particle)] += energy;
            particle_by_key.emplace(particle_key(particle), particle);
        }

        record.sim_energy = energy_total;
        record.sim_time = contribution_count ? time_earliest : 0.f;
        record.n_contrib = contribution_count;

        if (energy_by_origin.empty() || energy_total <= 0) return;

        const auto by_energy = [](const auto& a, const auto& b) { return a.second < b.second; };
        const auto majority = std::max_element(energy_by_origin.begin(), energy_by_origin.end(), by_energy);
        record.prt_origin = majority->first;
        record.label_purity = majority->second / energy_total;

        // Representative particle: the largest contributor of the majority class.
        const auto& per_particle = energy_by_origin_particle[majority->first];
        const auto lead = std::max_element(per_particle.begin(), per_particle.end(), by_energy);
        const auto& particle = particle_by_key.at(lead->first);

        record.prt_index = particle.getObjectID().index;
        record.prt_pdg = particle.getPDG();
        record.prt_gen_status = particle.getGeneratorStatus();
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
    }
};
