#ifdef __CLING__
R__LOAD_LIBRARY(podioDict)
R__LOAD_LIBRARY(podioRootIO)
R__LOAD_LIBRARY(libedm4hepDict)
R__LOAD_LIBRARY(libedm4eicDict)
#endif

// edm4eic_calo_clusters.cxx
//
// Dumps reconstructed calorimeter CLUSTERS to CSV, one row per
// cluster<->MCParticle association. For each cluster we resolve the associated
// MCParticle and tag it with a `prt_origin` flag using EXACTLY the same
// classification logic as edm4eic_trk_hits.cxx (generator-status band +
// parent-chain walk for Geant4 secondaries), so cluster rows and tracker-hit
// rows share one origin convention (0 unknown, 1 signal, 2 g4-from-signal,
// 3 background, 4 g4-from-background).
//
// Association layout (verified against a real eicrecon file):
//   <Det>ClusterAssociations : vector<edm4eic::MCRecoClusterParticleAssociation>
//        getRec() -> edm4eic::Cluster        (the reconstructed cluster)
//        getSim() -> edm4hep::MCParticle     (the MC particle it maps to)
//        getWeight() -> float                (association weight)
// The cluster carries no cellID of its own; the detector system is taken from
// its first constituent edm4eic::CalorimeterHit (cluster.getHits()).

#include "podio/Frame.h"
#include "podio/ROOTReader.h"
#include <edm4hep/MCParticleCollection.h>
#include <edm4eic/ClusterCollection.h>
#include <edm4eic/CalorimeterHitCollection.h>
#include <edm4eic/MCRecoClusterParticleAssociationCollection.h>

#include <fmt/core.h>
#include <fmt/ostream.h>

#include <TFile.h>

#include <fstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <tuple>

using namespace edm4hep;

//------------------------------------------------------------------------------
// globals & helpers
//------------------------------------------------------------------------------
int events_limit = -1; // -n  <N>
long total_evt_processed = 0;
std::ofstream csv;
bool header_written = false;

// Cluster<->MCParticle association collections. Same set the tracker-hit
// converter carries; collections absent from a given file are skipped
// gracefully (the set differs between eicrecon versions).
const std::vector<std::string> cal_cluster_associations = {
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

// Dictionary from definitions.xml (system_id -> human name). Same table as the
// tracker-hit converter; calorimeter systems live in the 100-band.
std::map<uint64_t, std::string> system_names_by_ids = {
    {10, "BeamPipe"},
    {11, "BeamPipeB0"},
    {25, "VertexSubAssembly_0"},
    {26, "VertexSubAssembly_1"},
    {27, "VertexSubAssembly_2"},
    {31, "VertexBarrel_0"},
    {32, "VertexBarrel_1"},
    {33, "VertexBarrel_2"},
    {34, "VertexEndcapN_0"},
    {35, "VertexEndcapN_1"},
    {36, "VertexEndcapN_2"},
    {37, "VertexEndcapP_0"},
    {38, "VertexEndcapP_1"},
    {39, "VertexEndcapP_2"},
    {40, "TrackerSubAssembly_0"},
    {41, "TrackerSubAssembly_1"},
    {42, "TrackerSubAssembly_2"},
    {43, "TrackerSubAssembly_3"},
    {44, "TrackerSubAssembly_4"},
    {45, "TrackerSubAssembly_5"},
    {46, "TrackerSubAssembly_6"},
    {47, "TrackerSubAssembly_7"},
    {48, "TrackerSubAssembly_8"},
    {49, "TrackerSubAssembly_9"},
    {50, "SVT_IB_Support_0"},
    {51, "SVT_IB_Support_1"},
    {52, "SVT_IB_Support_2"},
    {53, "SVT_IB_Support_3"},
    {59, "TrackerBarrel_0"},
    {60, "TrackerBarrel_1"},
    {61, "TrackerBarrel_2"},
    {62, "TrackerBarrel_3"},
    {63, "TrackerBarrel_4"},
    {64, "TrackerBarrel_5"},
    {65, "TrackerBarrel_6"},
    {66, "TrackerBarrel_7"},
    {67, "TrackerBarrel_8"},
    {68, "TrackerEndcapN_0"},
    {69, "TrackerEndcapN_1"},
    {70, "TrackerEndcapN_2"},
    {71, "TrackerEndcapN_3"},
    {72, "TrackerEndcapN_4"},
    {73, "TrackerEndcapN_5"},
    {74, "TrackerEndcapN_6"},
    {75, "TrackerEndcapN_7"},
    {76, "TrackerEndcapN_8"},
    {77, "TrackerEndcapP_0"},
    {78, "TrackerEndcapP_1"},
    {79, "TrackerEndcapP_2"},
    {80, "TrackerEndcapP_3"},
    {81, "TrackerEndcapP_4"},
    {82, "TrackerEndcapP_5"},
    {83, "TrackerEndcapP_6"},
    {84, "TrackerSupport_0"},
    {85, "TrackerSupport_1"},
    {90, "BarrelDIRC"},
    {91, "BarrelTRD"},
    {92, "BarrelTOF"},
    {93, "TOFSubAssembly"},
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
    {120, "ForwardRICH"},
    {121, "ForwardTRD"},
    {122, "ForwardTOF"},
    {131, "BackwardRICH"},
    {132, "BackwardTOF"},
    {140, "Solenoid"},
    {141, "SolenoidSupport"},
    {142, "SolenoidYoke"},
    {150, "B0Tracker_Station_1"},
    {151, "B0Tracker_Station_2"},
    {152, "B0Tracker_Station_3"},
    {153, "B0Tracker_Station_4"},
    {154, "B0Preshower_Station_1"},
    {155, "ForwardRomanPot_Station_1"},
    {156, "ForwardRomanPot_Station_2"},
    {157, "B0TrackerCompanion"},
    {158, "B0TrackerSubAssembly"},
    {159, "ForwardOffMTracker_station_1"},
    {160, "ForwardOffMTracker_station_2"},
    {161, "ForwardOffMTracker_station_3"},
    {162, "ForwardOffMTracker_station_4"},
    {163, "ZDC_1stSilicon"},
    {164, "ZDC_Crystal"},
    {165, "ZDC_WSi"},
    {166, "ZDC_PbSi"},
    {167, "ZDC_PbSci"},
    {168, "VacuumMagnetElement_1"},
    {169, "B0ECal"},
    {170, "B0PF"},
    {171, "B0APF"},
    {172, "Q1APF"},
    {173, "Q1BPF"},
    {174, "Q2PF"},
    {175, "B1PF"},
    {176, "B1APF"},
    {177, "B2PF"},
    {180, "Q0EF"},
    {181, "Q1EF"},
    {182, "B0Window"},
    {190, "LumiCollimator"},
    {191, "LumiDipole"},
    {192, "LumiWindow"},
    {193, "LumiSpecTracker"},
    {194, "LumiSpecCAL"},
    {195, "LumiDirectPCAL"},
    {197, "BackwardsBeamline"},
    {198, "TaggerTracker"},
    {199, "TaggerCalorimeter"}
};

/// Gets detector system ID and name from a full cellID. Unlike the tracker
/// converter this variant never throws: cluster constituents can carry exotic
/// cellIDs, and one unrecognised system must not abort the whole file. Unknown
/// systems fall back to name "Unknown".
std::tuple<uint64_t, std::string> get_detector_info(uint64_t cell_id) {
    uint64_t system_id = cell_id & 0xFF;  // system_id is the least significant 8 bits
    auto it = system_names_by_ids.find(system_id);
    if (it != system_names_by_ids.end()) {
        return {system_id, it->second};
    }
    return {system_id, "Unknown"};
}

/// Returns the collection cast to T, or nullptr if it is absent in this frame
/// or stored with a different type. Collection sets differ between eic_xl /
/// eicrecon versions, so absence is expected and must not be fatal.
template <typename T>
const T* get_optional_collection(const podio::Frame& event, const std::string& name) {
    const podio::CollectionBase* coll = event.get(name);
    return coll ? dynamic_cast<const T*>(coll) : nullptr;
}

/// Prints a skip notice, once per collection name, so per-event skips don't flood the log
void note_skipped(const std::string& name, const std::string& why) {
    static std::set<std::string> already_noted;
    if (already_noted.insert(name).second) {
        fmt::print("[skip] {}: {} (expected for some eic_xl versions)\n", name, why);
    }
}

/// Classifies a *generator-level* (non-zero) generatorStatus value into the
/// signal / background bands defined by the merger's status-offset convention
/// (see docs/background.md). Uses a generic threshold so new background sources
/// don't require editing a table:
///    status == 1 or 2          -> signal       (offset band 0)
///    status >= 1000            -> background   (offset bands 2000, 3000, ...)
///    anything else (e.g. 0)    -> unknown
/// @return 1 for signal, 3 for background, 0 for unknown.
///
/// NOTE: kept byte-for-byte identical to edm4eic_trk_hits.cxx so cluster and
/// tracker-hit rows share one origin convention.
inline int32_t classify_gen_status(int32_t gen_status) {
    if (gen_status == 1 || gen_status == 2) return 1; // signal
    if (gen_status >= 1000)                 return 3; // background offset band
    return 0;                                         // unknown
}

/// Gets particle origin status.
///
/// The merger adds its status offset at the HepMC/generator level, *before*
/// Geant4 runs. So in the simulated MCParticle collection only generator
/// particles carry the offset in their generatorStatus; Geant4-created
/// secondaries always have generatorStatus == 0 regardless of whether their
/// ancestor was signal or background. For those we walk up the parent chain to
/// the first generator ancestor (non-zero generatorStatus) and read its band.
///
/// @return
///    0 - unknown (no generator ancestor found / unrecognised band),
///    1 - signal particle,
///    2 - g4 gen from signal,
///    3 - background,
///    4 - g4 gen from background
///
/// NOTE: kept byte-for-byte identical to edm4eic_trk_hits.cxx.
int32_t get_origin_status(const MCParticle& particle) {
    const int32_t gen_status = particle.getGeneratorStatus();

    // Generator-level particle: read its own offset band directly.
    if (gen_status != 0) {
        return classify_gen_status(gen_status); // 1 signal, 3 background, 0 unknown
    }

    // Geant4-created secondary: trace up the parent chain to the first
    // generator ancestor. A depth guard protects against cyclic/broken links.
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
            return 0;                // unrecognised band
        }
        current = parent;
    }

    return 0; // fallback: no generator ancestor found
}

//------------------------------------------------------------------------------
// CSV record
//------------------------------------------------------------------------------
/// Struct representing one line in the CSV file (one cluster<->MCParticle association).
struct ClusterRecord {
    // Event and indexing
    uint64_t evt;                    // Event number
    uint64_t clu_index;              // Index of the association in its collection
    std::string clu_collection;      // Association collection name (detector + algorithm)
    uint64_t prt_index;              // Index of MCParticle associated with this cluster

    // Particle identification / origin (SAME convention as tracker-hit dump)
    int32_t prt_pdg;                 // PDG code of the particle
    int32_t prt_status;              // Generator status: 1/2 stable-from-generator, 0 Geant4-created
    int32_t prt_origin;              // 0 unknown, 1 signal, 2 g4-from-signal, 3 background, 4 g4-from-background

    // Particle kinematics
    double prt_energy;               // Total energy [GeV]
    float prt_charge;                // Electric charge [e]
    double prt_mom_x;                // Momentum x [GeV/c]
    double prt_mom_y;                // Momentum y [GeV/c]
    double prt_mom_z;                // Momentum z [GeV/c]

    // Particle vertex (production point)
    float prt_vtx_time;              // Time at production vertex [ns]
    float prt_vtx_pos_x;             // Production vertex x [mm]
    float prt_vtx_pos_y;             // Production vertex y [mm]
    float prt_vtx_pos_z;             // Production vertex z [mm]

    // Particle endpoint
    float prt_end_time;              // Time at endpoint [ns] (EDM4hep stores only one particle time)
    float prt_end_pos_x;             // Endpoint x [mm]
    float prt_end_pos_y;             // Endpoint y [mm]
    float prt_end_pos_z;             // Endpoint z [mm]

    // Cluster detector info (taken from the first constituent calorimeter hit)
    uint64_t clu_first_cell_id;      // cellID of the cluster's first constituent hit (0 if none)
    uint64_t clu_system_id;          // Detector system ID (bits 0-7 of that cellID)
    std::string clu_system_name;     // Human-readable detector name

    // Cluster quantities
    int32_t clu_type;                // Cluster type flag
    float clu_energy;                // Reconstructed cluster energy [GeV]
    float clu_energy_err;            // Cluster energy uncertainty [GeV]
    float clu_time;                  // Cluster time [ns]
    float clu_time_err;              // Cluster time uncertainty [ns]
    uint32_t clu_nhits;              // Number of hits in the cluster
    float clu_pos_x;                 // Cluster position x [mm]
    float clu_pos_y;                 // Cluster position y [mm]
    float clu_pos_z;                 // Cluster position z [mm]
    float clu_pos_err_xx;            // Position error variance xx [mm^2]
    float clu_pos_err_yy;            // Position error variance yy [mm^2]
    float clu_pos_err_zz;            // Position error variance zz [mm^2]
    float clu_theta;                 // Intrinsic polar angle theta [rad]
    float clu_phi;                   // Intrinsic azimuthal angle phi [rad]
    float clu_assoc_weight;          // Association weight (cluster<->MCParticle)

    static std::string make_csv_header() {
        return "evt,clu_index,clu_collection,prt_index,"
               "prt_pdg,prt_status,prt_origin,prt_energy,prt_charge,"
               "prt_mom_x,prt_mom_y,prt_mom_z,"
               "prt_vtx_time,prt_vtx_pos_x,prt_vtx_pos_y,prt_vtx_pos_z,"
               "prt_end_time,prt_end_pos_x,prt_end_pos_y,prt_end_pos_z,"
               "clu_first_cell_id,clu_system_id,clu_system_name,"
               "clu_type,clu_energy,clu_energy_err,clu_time,clu_time_err,clu_nhits,"
               "clu_pos_x,clu_pos_y,clu_pos_z,"
               "clu_pos_err_xx,clu_pos_err_yy,clu_pos_err_zz,"
               "clu_theta,clu_phi,clu_assoc_weight";
    }

    std::string get_csv_line() const {
        return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
            evt, clu_index, clu_collection, prt_index,
            prt_pdg, prt_status, prt_origin, prt_energy, prt_charge,
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

//------------------------------------------------------------------------------
// cluster processing
//------------------------------------------------------------------------------
void process_cluster_associations(const podio::Frame& event, const std::string& assoc_col_name, int evt_id) {

    const auto* assocs_ptr =
        get_optional_collection<edm4eic::MCRecoClusterParticleAssociationCollection>(event, assoc_col_name);
    if (!assocs_ptr) {
        note_skipped(assoc_col_name, "cluster association collection not in file");
        return;
    }
    const auto& assocs = *assocs_ptr;

    // Write header lazily, on the first association we actually reach.
    if (!header_written) {
        csv << ClusterRecord::make_csv_header() << "\n";
        header_written = true;
    }

    for (const auto& assoc : assocs) {

        // All warnings share this format.
        auto warn = [&](std::string_view msg) {
            fmt::print("WARNING! process_cluster_associations event={} col={} assoc.index:{}. {}\n",
                       evt_id, assoc_col_name, assoc.id().index, msg);
        };

        // Check we have a reconstructed cluster.
        const auto cluster = assoc.getRec();
        if (!cluster.isAvailable()) {
            warn("!assoc.getRec().isAvailable()");
            continue;
        }

        // Check we have an MCParticle.
        const auto particle = assoc.getSim();
        if (!particle.isAvailable()) {
            warn("!assoc.getSim().isAvailable()");
            continue;
        }

        // Origin flag — SAME logic as the tracker-hit converter.
        const int origin = get_origin_status(particle);

        // Detector system: a cluster carries no cellID of its own, so we take
        // it from a constituent calorimeter hit. Scan for the first hit that
        // resolves to a non-zero cellID -- some combined/truth cluster
        // collections (e.g. EcalBarrel) do not persist a resolvable hit
        // relation, in which case the cellID stays 0 and the system falls back
        // to "Unknown" (the clu_collection column still identifies the detector).
        uint64_t first_cell_id = 0;
        for (const auto& hit : cluster.getHits()) {
            if (hit.isAvailable() && hit.getCellID() != 0) {
                first_cell_id = hit.getCellID();
                break;
            }
        }
        auto [system_id, system_name] = get_detector_info(first_cell_id);

        ClusterRecord rec;
        rec.evt = evt_id;
        rec.clu_index = assoc.id().index;
        rec.clu_collection = assoc_col_name;
        rec.prt_index = particle.id().index;

        rec.prt_pdg = particle.getPDG();
        rec.prt_status = particle.getGeneratorStatus();
        rec.prt_origin = origin;
        rec.prt_energy = particle.getEnergy();
        rec.prt_charge = particle.getCharge();
        rec.prt_mom_x = particle.getMomentum().x;
        rec.prt_mom_y = particle.getMomentum().y;
        rec.prt_mom_z = particle.getMomentum().z;
        rec.prt_vtx_time = particle.getTime();
        rec.prt_vtx_pos_x = particle.getVertex().x;
        rec.prt_vtx_pos_y = particle.getVertex().y;
        rec.prt_vtx_pos_z = particle.getVertex().z;
        rec.prt_end_time = particle.getTime();  // EDM4hep stores a single particle time
        rec.prt_end_pos_x = particle.getEndpoint().x;
        rec.prt_end_pos_y = particle.getEndpoint().y;
        rec.prt_end_pos_z = particle.getEndpoint().z;

        rec.clu_first_cell_id = first_cell_id;
        rec.clu_system_id = system_id;
        rec.clu_system_name = system_name;

        rec.clu_type = cluster.getType();
        rec.clu_energy = cluster.getEnergy();
        rec.clu_energy_err = cluster.getEnergyError();
        rec.clu_time = cluster.getTime();
        rec.clu_time_err = cluster.getTimeError();
        rec.clu_nhits = cluster.getNhits();
        rec.clu_pos_x = cluster.getPosition().x;
        rec.clu_pos_y = cluster.getPosition().y;
        rec.clu_pos_z = cluster.getPosition().z;
        rec.clu_pos_err_xx = cluster.getPositionError().xx;
        rec.clu_pos_err_yy = cluster.getPositionError().yy;
        rec.clu_pos_err_zz = cluster.getPositionError().zz;
        rec.clu_theta = cluster.getIntrinsicTheta();
        rec.clu_phi = cluster.getIntrinsicPhi();
        rec.clu_assoc_weight = assoc.getWeight();

        csv << rec.get_csv_line() << "\n";
    }
}

//------------------------------------------------------------------------------
// event processing
//------------------------------------------------------------------------------
void process_event(const podio::Frame& event, int evt_id) {
    for (const auto& assoc_name : cal_cluster_associations) {
        process_cluster_associations(event, assoc_name, evt_id);
    }
}

//------------------------------------------------------------------------------
// file loop
//------------------------------------------------------------------------------
bool process_file(const std::string& file_name) {
    podio::ROOTReader reader;
    try {
        reader.openFile(file_name);
    }
    catch (const std::exception& e) {
        fmt::print(stderr, "Error opening file {}: {}\n", file_name, e.what());
        fmt::print(stderr, "(a file written by a newer eic_xl cannot be read by older software)\n");
        return false;
    }

    try {
        const auto event_count = reader.getEntries(podio::Category::Event);

        for (unsigned ie = 0; ie < event_count; ++ie) {
            if (events_limit > 0 && total_evt_processed >= events_limit) return true;

            podio::Frame evt(reader.readNextEntry(podio::Category::Event));
            process_event(evt, total_evt_processed);
            ++total_evt_processed;
        }
    }
    catch (const std::exception& e) {
        fmt::print(stderr, "Error reading file {}: {}\n", file_name, e.what());
        fmt::print(stderr, "(possible causes: file from a newer eic_xl than this software, or unexpected data)\n");
        return false;
    }
    return true;
}

void execute(const std::string& infile, const std::string& outfile, int events) {
    csv.open(outfile);

    if (!csv) {
        fmt::print(stderr, "error: cannot open output file\n");
        exit(1);
    }

    events_limit = events;
    const bool ok = process_file(infile);

    csv.close();

    // Remove the incomplete output so job reruns don't see it and skip the file
    if (!ok) {
        std::remove(outfile.c_str());
        fmt::print(stderr, "Failed processing {}. Removed incomplete output {}\n", infile, outfile);
        exit(2);
    }

    fmt::print("\nWrote cluster data for {} events to {}\n", total_evt_processed, outfile);
}

// ---------------------------------------------------------------------------
// ROOT-macro entry point.
// Call it from the prompt:
//   root -x -l -b -q 'edm4eic_calo_clusters.cxx("file.root", "output.csv", 100)'
// ---------------------------------------------------------------------------
void edm4eic_calo_clusters(const char* infile, const char* outfile, int events = -1)
{
    fmt::print("'edm4eic_calo_clusters' entry point is used.\n");
    execute(infile, outfile, events);
}

//------------------------------------------------------------------------------
// main function entry point (standalone application)
//------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::vector<std::string> infiles;
    std::string out_name = "clusters.csv";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-n" && i + 1 < argc) events_limit = std::atoi(argv[++i]);
        else if (a == "-o" && i + 1 < argc) out_name = argv[++i];
        else if (a == "-h" || a == "--help") {
            fmt::print("usage: {} [-n N] [-o file] input1.root [...]\n", argv[0]);
            return 0;
        }
        else if (!a.empty() && a[0] != '-') infiles.emplace_back(a);
        else {
            fmt::print(stderr, "unknown option {}\n", a);
            return 1;
        }
    }
    if (infiles.empty()) {
        fmt::print(stderr, "error: no input files\n");
        return 1;
    }

    execute(infiles[0], out_name, events_limit);

    return 0;
}
