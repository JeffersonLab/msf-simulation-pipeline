#ifdef __CLING__
R__LOAD_LIBRARY(podioDict)
R__LOAD_LIBRARY(podioRootIO)
R__LOAD_LIBRARY(libedm4hepDict)
R__LOAD_LIBRARY(libedm4eicDict)
#endif

// edm4eic_cal_hits.cxx
//
// Dumps digitized calorimeter REC HITS to CSV, truth-labeled from the paired
// simulation file. Takes TWO inputs: the eicrecon file (rec hits: realistic
// energies, thresholds, timing) and the same-stem dd4hep file (SimCalorimeterHit
// + CaloHitContribution -> MCParticle). The reco output carries no calorimeter
// hit<->MCParticle associations, so the join runs on (event, cellID): every
// rec hit's cell is looked up among the event's sim hits and labeled from that
// cell's contributions.
//
// Label of a cell with several contributing particles: prt_origin of the
// ENERGY-MAJORITY origin class, with `label_purity` = its energy fraction, so
// mixed cells stay visible. The prt_* block describes the largest-energy
// contributor of the majority class. `prt_origin` uses EXACTLY the same
// classification as edm4eic_trk_hits.cxx / edm4eic_calo_clusters.cxx
// (generator-status band + parent-chain walk; 0 unknown, 1 signal,
// 2 g4-from-signal, 3 background, 4 g4-from-background).
//
// Events are matched by position AND verified via EventHeader.eventNumber —
// a mismatch aborts the file (wrong pairing must not produce silent garbage).
//
// Usage (ROOT macro — the paired csv stage calls it exactly like this):
//   root -x -l -b -q 'edm4eic_cal_hits.cxx("reco.edm4eic.root","sim.edm4hep.root","out.csv")'

#include "podio/Frame.h"
#include "podio/ROOTReader.h"
#include <edm4hep/EventHeaderCollection.h>
#include <edm4hep/MCParticleCollection.h>
#include <edm4hep/SimCalorimeterHitCollection.h>
#include <edm4hep/CaloHitContributionCollection.h>
#include <edm4eic/CalorimeterHitCollection.h>

#include <fmt/core.h>
#include <fmt/ostream.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace edm4hep;

//------------------------------------------------------------------------------
// globals & helpers
//------------------------------------------------------------------------------
int events_limit = -1; // -n  <N>
long total_evt_processed = 0;
long total_unmatched = 0;  // rec hits whose cellID has no sim hit (label 0)
std::ofstream csv;
bool header_written = false;

// Rec-hit collection -> sim-hit collection, per calorimeter. Scope: central +
// far-forward (B0, ZDC, insert); no far-backward (Lumi, Tagger). Collections
// absent from a given file version are skipped gracefully.
const std::vector<std::pair<std::string, std::string>> cal_hit_pairs = {
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

// Dictionary from definitions.xml (system_id -> human name). Same table as the
// tracker-hit and cluster converters; calorimeters live in the 100-band.
std::map<uint64_t, std::string> system_names_by_ids = {
    {100, "EcalSubAssembly"},
    {101, "EcalBarrel"},
    {102, "EcalEndcapP"},
    {103, "EcalEndcapN"},
    {104, "CrystalEndcap"},
    {105, "EcalBarrel2"},
    {106, "EcalEndcapPInsert"},
    {110, "HcalSubAssembly"},
    {111, "HcalBarrel"},
    {113, "HcalEndcapN"},
    {114, "PassiveSteelRingEndcapP"},
    {115, "HcalEndcapPInsert"},
    {116, "LFHCAL"},
    {163, "ZDC_1stSilicon"},
    {164, "ZDC_Crystal"},
    {165, "ZDC_WSi"},
    {166, "ZDC_PbSi"},
    {167, "ZDC_PbSci"},
    {169, "B0ECal"},
};

/// Detector system from a cellID: least significant 8 bits. Never throws —
/// an unknown system must not abort the file ("Unknown" + the collection
/// column still identify the detector).
std::tuple<uint64_t, std::string> get_detector_info(uint64_t cell_id) {
    uint64_t system_id = cell_id & 0xFF;
    auto it = system_names_by_ids.find(system_id);
    if (it != system_names_by_ids.end()) {
        return {system_id, it->second};
    }
    return {system_id, "Unknown"};
}

/// Returns the collection cast to T, or nullptr if absent in this frame or of
/// a different type. Collection sets differ between eic_xl versions.
template <typename T>
const T* get_optional_collection(const podio::Frame& event, const std::string& name) {
    const podio::CollectionBase* coll = event.get(name);
    return coll ? dynamic_cast<const T*>(coll) : nullptr;
}

/// Prints a skip notice once per collection name.
void note_skipped(const std::string& name, const std::string& why) {
    static std::set<std::string> already_noted;
    if (already_noted.insert(name).second) {
        fmt::print("[skip] {}: {} (expected for some eic_xl versions)\n", name, why);
    }
}

/// Signal / background band of a generator-level status value:
///    status == 1 or 2 -> signal, status >= 1000 -> background, else unknown.
/// @return 1 for signal, 3 for background, 0 for unknown.
/// NOTE: kept byte-for-byte identical to edm4eic_trk_hits.cxx.
inline int32_t classify_gen_status(int32_t gen_status) {
    if (gen_status == 1 || gen_status == 2) return 1; // signal
    if (gen_status >= 1000)                 return 3; // background offset band
    return 0;                                         // unknown
}

/// Particle origin: generator particles from their own status band; Geant4
/// secondaries (status 0) from the first generator ancestor up the parent
/// chain. NOTE: kept byte-for-byte identical to edm4eic_trk_hits.cxx.
/// @return 0 unknown, 1 signal, 2 g4-from-signal, 3 background, 4 g4-from-background
int32_t get_origin_status(const MCParticle& particle) {
    const int32_t gen_status = particle.getGeneratorStatus();

    if (gen_status != 0) {
        return classify_gen_status(gen_status);
    }

    MCParticle current = particle;
    const int max_depth = 200;
    for (int depth = 0; depth < max_depth; ++depth) {
        if (current.parents_size() == 0) break;
        MCParticle parent = current.getParents(0);
        if (!parent.isAvailable()) break;

        const int32_t parent_status = parent.getGeneratorStatus();
        if (parent_status != 0) {
            const int32_t band = classify_gen_status(parent_status);
            if (band == 1) return 2; // g4 gen from signal
            if (band == 3) return 4; // g4 gen from background
            return 0;
        }
        current = parent;
    }

    return 0;
}

//------------------------------------------------------------------------------
// CSV record
//------------------------------------------------------------------------------
/// One line per digitized calorimeter rec hit, truth-labeled from its sim cell.
struct CalHitRecord {
    // Event and indexing
    uint64_t evt;                    // Event number
    std::string rec_collection;      // Rec-hit collection name (detector)
    uint64_t cal_hit_cell_id;        // Full cellID (same cell in sim and reco)
    uint64_t cal_hit_system_id;      // Detector system ID (bits 0-7)
    std::string cal_hit_system_name; // Human-readable detector name

    // Digitized (reconstructed) quantities — the inference-legal features
    float cal_hit_pos_x;             // Hit position x [mm]
    float cal_hit_pos_y;             // Hit position y [mm]
    float cal_hit_pos_z;             // Hit position z [mm]
    float cal_hit_energy;            // Calibrated energy [GeV]
    float cal_hit_energy_err;        // Energy uncertainty [GeV]
    float cal_hit_time;              // Digitized time [ns]
    float cal_hit_time_err;          // Time uncertainty [ns]

    // Truth aggregates of the sim cell (analysis columns)
    double sim_energy;               // Sum of contribution energies [GeV] (raw deposit)
    float sim_time;                  // Earliest contribution time [ns]
    int32_t n_contrib;               // Number of contributions in the cell
    float label_purity;              // Energy fraction of the majority origin class

    // Majority-class representative particle (SAME prt_* convention as
    // trk_hits / calo_clusters; zeros when the cell has no sim match)
    uint64_t prt_index;              // Index of the MCParticle in its collection
    int32_t prt_pdg;                 // PDG code
    int32_t prt_status;              // Generator status (0 = Geant4-created)
    int32_t prt_origin;              // 0 unknown, 1 signal, 2 g4-from-signal, 3 background, 4 g4-from-background
    double prt_energy;               // Total energy [GeV]
    float prt_charge;                // Electric charge [e]
    double prt_mom_x;                // Momentum x [GeV/c]
    double prt_mom_y;                // Momentum y [GeV/c]
    double prt_mom_z;                // Momentum z [GeV/c]
    float prt_vtx_time;              // Time at production vertex [ns]
    float prt_vtx_pos_x;             // Production vertex x [mm]
    float prt_vtx_pos_y;             // Production vertex y [mm]
    float prt_vtx_pos_z;             // Production vertex z [mm]
    float prt_end_pos_x;             // Endpoint x [mm]
    float prt_end_pos_y;             // Endpoint y [mm]
    float prt_end_pos_z;             // Endpoint z [mm]

    static std::string make_csv_header() {
        return "evt,rec_collection,cal_hit_cell_id,cal_hit_system_id,cal_hit_system_name,"
               "cal_hit_pos_x,cal_hit_pos_y,cal_hit_pos_z,"
               "cal_hit_energy,cal_hit_energy_err,cal_hit_time,cal_hit_time_err,"
               "sim_energy,sim_time,n_contrib,label_purity,"
               "prt_index,prt_pdg,prt_status,prt_origin,prt_energy,prt_charge,"
               "prt_mom_x,prt_mom_y,prt_mom_z,"
               "prt_vtx_time,prt_vtx_pos_x,prt_vtx_pos_y,prt_vtx_pos_z,"
               "prt_end_pos_x,prt_end_pos_y,prt_end_pos_z";
    }

    std::string get_csv_line() const {
        return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
            evt, rec_collection, cal_hit_cell_id, cal_hit_system_id, cal_hit_system_name,
            cal_hit_pos_x, cal_hit_pos_y, cal_hit_pos_z,
            cal_hit_energy, cal_hit_energy_err, cal_hit_time, cal_hit_time_err,
            sim_energy, sim_time, n_contrib, label_purity,
            prt_index, prt_pdg, prt_status, prt_origin, prt_energy, prt_charge,
            prt_mom_x, prt_mom_y, prt_mom_z,
            prt_vtx_time, prt_vtx_pos_x, prt_vtx_pos_y, prt_vtx_pos_z,
            prt_end_pos_x, prt_end_pos_y, prt_end_pos_z);
    }
};

//------------------------------------------------------------------------------
// per-detector processing
//------------------------------------------------------------------------------
void process_detector(const podio::Frame& reco_evt, const podio::Frame& sim_evt,
                      const std::string& rec_name, const std::string& sim_name,
                      int evt_id) {

    const auto* rec_hits =
        get_optional_collection<edm4eic::CalorimeterHitCollection>(reco_evt, rec_name);
    if (!rec_hits) {
        note_skipped(rec_name, "rec-hit collection not in reco file");
        return;
    }
    if (rec_hits->empty()) return;

    const auto* sim_hits =
        get_optional_collection<SimCalorimeterHitCollection>(sim_evt, sim_name);
    if (!sim_hits) {
        note_skipped(sim_name, "sim-hit collection not in sim file");
        return;
    }

    // The join index: this event's sim cells by cellID. One sim hit per cell
    // per event (dd4hep aggregates contributions inside the cell).
    std::unordered_map<uint64_t, SimCalorimeterHit> sim_by_cell;
    sim_by_cell.reserve(sim_hits->size());
    for (const auto& sh : *sim_hits) {
        sim_by_cell.emplace(sh.getCellID(), sh);
    }

    if (!header_written) {
        csv << CalHitRecord::make_csv_header() << "\n";
        header_written = true;
    }

    for (const auto& rh : *rec_hits) {
        CalHitRecord rec{};
        rec.evt = evt_id;
        rec.rec_collection = rec_name;
        rec.cal_hit_cell_id = rh.getCellID();
        auto [system_id, system_name] = get_detector_info(rh.getCellID());
        rec.cal_hit_system_id = system_id;
        rec.cal_hit_system_name = system_name;
        rec.cal_hit_pos_x = rh.getPosition().x;
        rec.cal_hit_pos_y = rh.getPosition().y;
        rec.cal_hit_pos_z = rh.getPosition().z;
        rec.cal_hit_energy = rh.getEnergy();
        rec.cal_hit_energy_err = rh.getEnergyError();
        rec.cal_hit_time = rh.getTime();
        rec.cal_hit_time_err = rh.getTimeError();

        auto it = sim_by_cell.find(rh.getCellID());
        if (it == sim_by_cell.end()) {
            // No truth cell (verified 0 occurrences on the 2026-07 campaign;
            // possible with noise hits once digi adds them). Features stay
            // valid, label stays 0/unknown; counted and reported at the end.
            ++total_unmatched;
            csv << rec.get_csv_line() << "\n";
            continue;
        }

        // Aggregate the cell's contributions: energy per origin class and,
        // inside each class, energy per contributing particle.
        // Particles are keyed by a packed (collectionID, index) uint64 —
        // podio::ObjectID is not guaranteed ordered/hashable across versions.
        auto particle_key = [](const MCParticle& p) {
            return (uint64_t(p.id().collectionID) << 32) | uint32_t(p.id().index);
        };
        const auto& sim_hit = it->second;
        double e_total = 0.0;
        float t_earliest = std::numeric_limits<float>::max();
        std::map<int32_t, double> e_by_origin;
        std::map<int32_t, std::map<uint64_t, double>> e_by_origin_particle;
        std::map<uint64_t, MCParticle> particle_by_key;
        int32_t n_contrib = 0;

        for (const auto& contrib : sim_hit.getContributions()) {
            if (!contrib.isAvailable()) continue;
            ++n_contrib;
            const double e = contrib.getEnergy();
            e_total += e;
            if (contrib.getTime() < t_earliest) t_earliest = contrib.getTime();
            const auto particle = contrib.getParticle();
            if (!particle.isAvailable()) continue;
            const int32_t origin = get_origin_status(particle);
            e_by_origin[origin] += e;
            e_by_origin_particle[origin][particle_key(particle)] += e;
            particle_by_key.emplace(particle_key(particle), particle);
        }

        rec.sim_energy = e_total;
        rec.sim_time = n_contrib ? t_earliest : 0.f;
        rec.n_contrib = n_contrib;

        if (!e_by_origin.empty() && e_total > 0) {
            // Majority class by deposited energy; ties resolve to the lower
            // origin value (map order) — deterministic and irrelevant in practice.
            auto majority = std::max_element(
                e_by_origin.begin(), e_by_origin.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            rec.prt_origin = majority->first;
            rec.label_purity = majority->second / e_total;

            // Representative particle: largest-energy contributor of the class.
            const auto& per_particle = e_by_origin_particle[majority->first];
            auto lead = std::max_element(
                per_particle.begin(), per_particle.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            const auto& particle = particle_by_key.at(lead->first);

            rec.prt_index = particle.id().index;
            rec.prt_pdg = particle.getPDG();
            rec.prt_status = particle.getGeneratorStatus();
            rec.prt_energy = particle.getEnergy();
            rec.prt_charge = particle.getCharge();
            rec.prt_mom_x = particle.getMomentum().x;
            rec.prt_mom_y = particle.getMomentum().y;
            rec.prt_mom_z = particle.getMomentum().z;
            rec.prt_vtx_time = particle.getTime();
            rec.prt_vtx_pos_x = particle.getVertex().x;
            rec.prt_vtx_pos_y = particle.getVertex().y;
            rec.prt_vtx_pos_z = particle.getVertex().z;
            rec.prt_end_pos_x = particle.getEndpoint().x;
            rec.prt_end_pos_y = particle.getEndpoint().y;
            rec.prt_end_pos_z = particle.getEndpoint().z;
        }

        csv << rec.get_csv_line() << "\n";
    }
}

//------------------------------------------------------------------------------
// event & file loop
//------------------------------------------------------------------------------
uint64_t event_number_of(const podio::Frame& frame, const char* which) {
    const auto* headers = get_optional_collection<EventHeaderCollection>(frame, "EventHeader");
    if (!headers || headers->empty()) {
        fmt::print(stderr, "FATAL: no EventHeader in {} file — cannot verify pairing\n", which);
        exit(3);
    }
    return (*headers)[0].getEventNumber();
}

bool process_files(const std::string& reco_file, const std::string& sim_file) {
    podio::ROOTReader reco_reader, sim_reader;
    try {
        reco_reader.openFile(reco_file);
        sim_reader.openFile(sim_file);
    }
    catch (const std::exception& e) {
        fmt::print(stderr, "Error opening input: {}\n", e.what());
        return false;
    }

    try {
        const auto n_reco = reco_reader.getEntries(podio::Category::Event);
        const auto n_sim = sim_reader.getEntries(podio::Category::Event);
        if (n_reco != n_sim) {
            fmt::print(stderr, "FATAL: event count mismatch reco={} sim={} — wrong file pair?\n",
                       n_reco, n_sim);
            return false;
        }

        for (unsigned ie = 0; ie < n_reco; ++ie) {
            if (events_limit > 0 && total_evt_processed >= events_limit) return true;

            podio::Frame reco_evt(reco_reader.readNextEntry(podio::Category::Event));
            podio::Frame sim_evt(sim_reader.readNextEntry(podio::Category::Event));

            const auto en_reco = event_number_of(reco_evt, "reco");
            const auto en_sim = event_number_of(sim_evt, "sim");
            if (en_reco != en_sim) {
                fmt::print(stderr, "FATAL: eventNumber mismatch at entry {}: reco={} sim={} — "
                           "files are not the same-event pair\n", ie, en_reco, en_sim);
                return false;
            }

            for (const auto& [rec_name, sim_name] : cal_hit_pairs) {
                process_detector(reco_evt, sim_evt, rec_name, sim_name, total_evt_processed);
            }
            ++total_evt_processed;
        }
    }
    catch (const std::exception& e) {
        fmt::print(stderr, "Error reading input: {}\n", e.what());
        return false;
    }
    return true;
}

void execute(const std::string& reco_file, const std::string& sim_file,
             const std::string& outfile, int events) {
    csv.open(outfile);

    if (!csv) {
        fmt::print(stderr, "error: cannot open output file\n");
        exit(1);
    }

    events_limit = events;
    const bool ok = process_files(reco_file, sim_file);

    csv.close();

    // Remove the incomplete output so job reruns don't see it and skip the file
    if (!ok) {
        std::remove(outfile.c_str());
        fmt::print(stderr, "Failed processing {} + {}. Removed incomplete output {}\n",
                   reco_file, sim_file, outfile);
        exit(2);
    }

    fmt::print("\nWrote labeled calorimeter rec hits for {} events to {}\n",
               total_evt_processed, outfile);
    if (total_unmatched > 0) {
        fmt::print("NOTE: {} rec hits had no sim cell (written with prt_origin=0)\n",
                   total_unmatched);
    }
}

// ---------------------------------------------------------------------------
// ROOT-macro entry point.
//   root -x -l -b -q 'edm4eic_cal_hits.cxx("reco.root","sim.root","out.csv",100)'
// ---------------------------------------------------------------------------
void edm4eic_cal_hits(const char* reco_file, const char* sim_file,
                      const char* outfile, int events = -1)
{
    fmt::print("'edm4eic_cal_hits' entry point is used.\n");
    execute(reco_file, sim_file, outfile, events);
}

//------------------------------------------------------------------------------
// main function entry point (standalone application)
//------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::vector<std::string> infiles;
    std::string out_name = "cal_hits.csv";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-n" && i + 1 < argc) events_limit = std::atoi(argv[++i]);
        else if (a == "-o" && i + 1 < argc) out_name = argv[++i];
        else if (a == "-h" || a == "--help") {
            fmt::print("usage: {} [-n N] [-o file] reco.edm4eic.root sim.edm4hep.root\n", argv[0]);
            return 0;
        }
        else if (!a.empty() && a[0] != '-') infiles.emplace_back(a);
        else {
            fmt::print(stderr, "unknown option {}\n", a);
            return 1;
        }
    }
    if (infiles.size() != 2) {
        fmt::print(stderr, "error: need exactly two inputs: reco.edm4eic.root sim.edm4hep.root\n");
        return 1;
    }

    execute(infiles[0], infiles[1], out_name, events_limit);

    return 0;
}
