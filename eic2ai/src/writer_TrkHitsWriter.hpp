// writer_TrkHitsWriter.hpp — CSV role "trk_hits": one row per reconstructed tracker hit with its MC truth.
//
// Needs: reco. Collections read: the *RawHitAssociations collections listed below
// (MCRecoTrackerHitAssociation: rawHit → simHit → MCParticle) and the *RecHits (TrackerHit)
// collection paired with each; through the relations, MCParticles and the Sim*Hits copies the
// reco file carries.
// Columns: evt, hit_index, the prt_* block (particle identity, origin, kinematics, vertex,
// endpoint), the trk_hit_* block (cellID, system, position, time, errors, energy deposit).
//
// Port of csv_convert/edm4eic_trk_hits.cxx (process_tracker_hits); rows are byte-identical to
// the macro's, the header follows column_renames.py (prt_status → prt_gen_status). Trap: the TrackerHit of a raw hit is found by a linear scan of the RecHits
// collection (first match by rawHit id), as in the macro. Keep the scan until the parity gate
// has passed on a faster lookup.
#pragma once

#include "Writer.hpp"
#include "origin.hpp"

#include <edm4eic/MCRecoTrackerHitAssociationCollection.h>
#include <edm4eic/TrackerHitCollection.h>
#include <fmt/core.h>

#include <optional>
#include <string_view>

class TrkHitsWriter : public Writer {
public:
    std::string role() const override { return "trk_hits"; }
    Needs needs() const override { return Needs{.reco = true, .sim = false}; }

    std::vector<std::string> collections() const override {
        std::vector<std::string> names = {"MCParticles"};
        for (const Detector& detector : detectors()) {
            names.insert(names.end(), {detector.associations, detector.rec_hits, detector.raw_hits, detector.sim_hits});
        }
        return names;
    }

    std::string csv_header() const override {
        return "evt,hit_index,prt_index,"
               "prt_pdg,prt_gen_status,prt_origin,prt_energy,prt_charge,"
               "prt_mom_x,prt_mom_y,prt_mom_z,"
               "prt_vtx_time,prt_vtx_pos_x,prt_vtx_pos_y,prt_vtx_pos_z,"
               "prt_end_time,prt_end_pos_x,prt_end_pos_y,prt_end_pos_z,"
               "trk_hit_cell_id,trk_hit_system_id,trk_hit_system_name,"
               "trk_hit_pos_x,trk_hit_pos_y,trk_hit_pos_z,trk_hit_time,"
               "trk_hit_pos_err_xx,trk_hit_pos_err_yy,trk_hit_pos_err_zz,trk_hit_time_err,"
               "trk_hit_edep,trk_hit_edep_err";
    }

private:
    /// One row: a reconstructed tracker hit and the MC particle that made it.
    struct Record {
        uint64_t evt;                    // entry counter
        uint64_t hit_index;              // index of the MCRecoTrackerHitAssociation in its collection
        uint64_t prt_index;              // index of the MCParticle in MCParticles

        int32_t prt_pdg;                 // PDG code (11 e-, 211 pi+, 2212 proton, ...)
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

        uint64_t trk_hit_cell_id;        // full cellID of the reconstructed hit
        uint64_t trk_hit_system_id;      // detector system id (bits 0-7 of the cellID)
        std::string trk_hit_system_name; // detector name from the system table

        float trk_hit_pos_x;             // reconstructed hit position x [mm]
        float trk_hit_pos_y;             // reconstructed hit position y [mm]
        float trk_hit_pos_z;             // reconstructed hit position z [mm]
        float trk_hit_time;              // reconstructed hit time [ns]

        float trk_hit_pos_err_xx;        // position variance xx [mm^2]
        float trk_hit_pos_err_yy;        // position variance yy [mm^2]
        float trk_hit_pos_err_zz;        // position variance zz [mm^2]
        float trk_hit_time_err;          // time uncertainty [ns]

        float trk_hit_edep;              // energy deposited in the sensor [GeV]
        float trk_hit_edep_err;          // energy deposit uncertainty [GeV]

        std::string csv_line() const {
            return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                evt, hit_index, prt_index,
                prt_pdg, prt_gen_status, prt_origin, prt_energy, prt_charge,
                prt_mom_x, prt_mom_y, prt_mom_z,
                prt_vtx_time, prt_vtx_pos_x, prt_vtx_pos_y, prt_vtx_pos_z,
                prt_end_time, prt_end_pos_x, prt_end_pos_y, prt_end_pos_z,
                trk_hit_cell_id, trk_hit_system_id, trk_hit_system_name,
                trk_hit_pos_x, trk_hit_pos_y, trk_hit_pos_z, trk_hit_time,
                trk_hit_pos_err_xx, trk_hit_pos_err_yy, trk_hit_pos_err_zz, trk_hit_time_err,
                trk_hit_edep, trk_hit_edep_err);
        }
    };

    /// The collections of one tracker: the associations the rows come from, the TrackerHit
    /// collection the raw hits belong to, and the two relation targets (raw hits, sim hits) that
    /// must be read for the association to resolve. Names follow the eicrecon 26.06 output; the
    /// sim-hit names are irregular (VertexBarrelHits, TrackerEndcapHits). Table order is row order.
    struct Detector {
        std::string associations;
        std::string rec_hits;
        std::string raw_hits;
        std::string sim_hits;
    };
    static const std::vector<Detector>& detectors() {
        static const std::vector<Detector> table = {
            {"B0TrackerRawHitAssociations",          "B0TrackerRecHits",          "B0TrackerRawHits",          "B0TrackerHits"},
            {"BackwardMPGDEndcapRawHitAssociations", "BackwardMPGDEndcapRecHits", "BackwardMPGDEndcapRawHits", "BackwardMPGDEndcapHits"},
            {"ForwardMPGDEndcapRawHitAssociations",  "ForwardMPGDEndcapRecHits",  "ForwardMPGDEndcapRawHits",  "ForwardMPGDEndcapHits"},
            {"ForwardOffMTrackerRawHitAssociations", "ForwardOffMTrackerRecHits", "ForwardOffMTrackerRawHits", "ForwardOffMTrackerHits"},
            {"ForwardRomanPotRawHitAssociations",    "ForwardRomanPotRecHits",    "ForwardRomanPotRawHits",    "ForwardRomanPotHits"},
            {"MPGDBarrelRawHitAssociations",         "MPGDBarrelRecHits",         "MPGDBarrelRawHits",         "MPGDBarrelHits"},
            {"OuterMPGDBarrelRawHitAssociations",    "OuterMPGDBarrelRecHits",    "OuterMPGDBarrelRawHits",    "OuterMPGDBarrelHits"},
            {"RICHEndcapNRawHitsAssociations",       "RICHEndcapNRecHits",        "RICHEndcapNRawHits",        "RICHEndcapNHits"},
            {"SiBarrelRawHitAssociations",           "SiBarrelTrackerRecHits",    "SiBarrelRawHits",           "SiBarrelHits"},
            {"SiBarrelVertexRawHitAssociations",     "SiBarrelVertexRecHits",     "SiBarrelVertexRawHits",     "VertexBarrelHits"},
            {"SiEndcapTrackerRawHitAssociations",    "SiEndcapTrackerRecHits",    "SiEndcapTrackerRawHits",    "TrackerEndcapHits"},
            {"TOFBarrelRawHitAssociations",          "TOFBarrelRecHits",          "TOFBarrelRawHits",          "TOFBarrelHits"},
            {"TOFEndcapRawHitAssociations",          "TOFEndcapRecHits",          "TOFEndcapRawHits",          "TOFEndcapHits"},
        };
        return table;
    }

    void write_event(const EventInputs& inputs) override {
        for (const Detector& detector : detectors()) {
            write_association_collection(*inputs.reco, detector.associations, detector.rec_hits, inputs.entry_index);
        }
    }

    void write_association_collection(const podio::Frame& reco, const std::string& association_name,
                                      const std::string& rec_hits_name, uint64_t evt) {
        const auto* associations = get_optional_collection<edm4eic::MCRecoTrackerHitAssociationCollection>(reco, association_name);
        if (!associations) {
            skip_notes.note(association_name, "association collection not in file");
            return;
        }
        const auto* rec_hits = get_optional_collection<edm4eic::TrackerHitCollection>(reco, rec_hits_name);
        if (!rec_hits) {
            skip_notes.note(rec_hits_name, "rec-hits collection not in file");
            return;
        }

        for (const auto& association : *associations) {
            auto warn = [&](std::string_view message) {
                fmt::print("WARNING! trk_hits event={} col={} hit_assoc.index:{}. {}\n",
                           evt, association_name, association.getObjectID().index, message);
            };
            if (!association.getRawHit().isAvailable()) {
                warn("!hit_assoc.getRawHit().isAvailable()");
                continue;
            }
            if (!association.getSimHit().isAvailable()) {
                warn("!hit_assoc.getSimHit().isAvailable()");
                continue;
            }
            if (!association.getSimHit().getParticle().isAvailable()) {
                warn("!hit_assoc.getSimHit().getParticle().isAvailable()");
                continue;
            }

            const auto raw_hit = association.getRawHit();
            const std::optional<edm4eic::TrackerHit> rec_hit = find_rec_hit(raw_hit, *rec_hits);
            if (!rec_hit) {
                warn(fmt::format("edm4eic::TrackerHit was not found for raw hit with index: {}", raw_hit.getObjectID().index));
                continue;
            }
            const auto particle = association.getSimHit().getParticle();

            Record record{};
            record.evt = evt;
            record.hit_index = association.getObjectID().index;
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

            record.trk_hit_cell_id = rec_hit->getCellID();
            const SystemInfo system = system_of(record.trk_hit_cell_id);
            record.trk_hit_system_id = system.id;
            record.trk_hit_system_name = system.name;
            record.trk_hit_pos_x = rec_hit->getPosition().x;
            record.trk_hit_pos_y = rec_hit->getPosition().y;
            record.trk_hit_pos_z = rec_hit->getPosition().z;
            record.trk_hit_time = rec_hit->getTime();
            record.trk_hit_pos_err_xx = rec_hit->getPositionError().xx;
            record.trk_hit_pos_err_yy = rec_hit->getPositionError().yy;
            record.trk_hit_pos_err_zz = rec_hit->getPositionError().zz;
            record.trk_hit_time_err = rec_hit->getTimeError();
            record.trk_hit_edep = rec_hit->getEdep();
            record.trk_hit_edep_err = rec_hit->getEdepError();

            write_row(record.csv_line());
        }
    }

    /// The TrackerHit built from this raw hit: first match by rawHit id, as the macro does.
    static std::optional<edm4eic::TrackerHit> find_rec_hit(const edm4eic::RawTrackerHit& raw_hit,
                                                           const edm4eic::TrackerHitCollection& rec_hits) {
        for (const auto& rec_hit : rec_hits) {
            if (rec_hit.getRawHit().getObjectID() == raw_hit.getObjectID()) return rec_hit;
        }
        return std::nullopt;
    }
};
