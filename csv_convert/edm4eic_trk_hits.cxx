#ifdef __CLING__
R__LOAD_LIBRARY(podioDict)
R__LOAD_LIBRARY(podioRootIO)
R__LOAD_LIBRARY(libedm4hepDict)
R__LOAD_LIBRARY(libedm4eicDict)
#endif

// csv_edm4hep_acceptance_ppim.cxx
#include "podio/Frame.h"
#include "podio/ROOTReader.h"
#include <edm4hep/MCParticleCollection.h>
#include <edm4hep/SimCalorimeterHitCollection.h>
#include <edm4hep/SimTrackerHitCollection.h>
#include <edm4hep/CaloHitContributionCollection.h>
#include <edm4eic/MCRecoTrackerHitAssociationCollection.h>
#include <edm4eic/MCRecoTrackerHitAssociationCollection.h>
#include <edm4eic/MCRecoCalorimeterHitAssociationCollection.h>
#include <edm4eic/TrackerHitCollection.h>
#include <edm4eic/TrackerHit.h>

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
#include <optional>

using namespace edm4hep;

//------------------------------------------------------------------------------
// globals & helpers
//------------------------------------------------------------------------------
int events_limit = -1; // -n  <N>
long total_evt_processed = 0;
std::ofstream csv;
bool header_written = false;
bool trk_hits_header_written = false;



/// Struct representing a line in csv file
struct HitRecord {
    // Event and indexing
    uint64_t evt;                    // Event number
    uint64_t hit_index;              // Index of MCRecoTrackerHitAssociation in collection
    uint64_t prt_index;              // Index of MCParticle that created this hit

    // Particle identification
    int32_t prt_pdg;                 // PDG code of the particle (e.g., 11=e-, 211=pi+, 2212=proton)
    int32_t prt_status;              // Generator status: 1=stable from generator, 0=created by Geant4
    int32_t prt_origin;              // Origin flag, 0 - uknown, 1 - signal particle, 2 - g4 gen from signal, 3 - background, 4 genat4 gen from background

    // Particle kinematics
    double prt_energy;               // Total energy of particle [GeV]
    float prt_charge;                // Electric charge [e]
    double prt_mom_x;                // Momentum x-component [GeV/c]
    double prt_mom_y;                // Momentum y-component [GeV/c]
    double prt_mom_z;                // Momentum z-component [GeV/c]

    // Particle vertex (production point)
    float prt_vtx_time;              // Time at production vertex [ns]
    float prt_vtx_pos_x;             // Production vertex x [mm]
    float prt_vtx_pos_y;             // Production vertex y [mm]
    float prt_vtx_pos_z;             // Production vertex z [mm]

    // Particle endpoint (decay/absorption point)
    float prt_end_time;              // Time at endpoint [ns] (Note: same as vtx_time in EDM4hep)
    float prt_end_pos_x;             // Endpoint x [mm]
    float prt_end_pos_y;             // Endpoint y [mm]
    float prt_end_pos_z;             // Endpoint z [mm]

    // Tracker hit detector info
    uint64_t trk_hit_cell_id;        // Full cell ID encoding detector hierarchy
    uint64_t trk_hit_system_id;      // Detector system ID (bits 0-7 of cell_id)
    std::string trk_hit_system_name; // Human-readable detector name (e.g., "SiBarrelVertex")

    // Tracker hit position and time
    float trk_hit_pos_x;             // Reconstructed hit position x [mm]
    float trk_hit_pos_y;             // Reconstructed hit position y [mm]
    float trk_hit_pos_z;             // Reconstructed hit position z [mm]
    float trk_hit_time;              // Reconstructed hit time [ns]

    // Tracker hit uncertainties (covariance matrix diagonal)
    float trk_hit_pos_err_xx;        // Position error variance in x [mm^2]
    float trk_hit_pos_err_yy;        // Position error variance in y [mm^2]
    float trk_hit_pos_err_zz;        // Position error variance in z [mm^2]
    float trk_hit_time_err;          // Time uncertainty [ns]

    // Tracker hit energy deposition
    float trk_hit_edep;              // Energy deposited in sensor [GeV]
    float trk_hit_edep_err;          // Energy deposition uncertainty [GeV]


    static std::string make_csv_header() {
        return "evt,hit_index,prt_index,"
               "prt_pdg,prt_status,prt_origin,prt_energy,prt_charge,"
               "prt_mom_x,prt_mom_y,prt_mom_z,"
               "prt_vtx_time,prt_vtx_pos_x,prt_vtx_pos_y,prt_vtx_pos_z,"
               "prt_end_time,prt_end_pos_x,prt_end_pos_y,prt_end_pos_z,"
               "trk_hit_cell_id,trk_hit_system_id,trk_hit_system_name,"
               "trk_hit_pos_x,trk_hit_pos_y,trk_hit_pos_z,trk_hit_time,"
               "trk_hit_pos_err_xx,trk_hit_pos_err_yy,trk_hit_pos_err_zz,trk_hit_time_err,"
               "trk_hit_edep,trk_hit_edep_err";
    }

    std::string get_csv_line() const {
        return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
            evt, hit_index, prt_index,
            prt_pdg, prt_status, prt_origin, prt_energy, prt_charge,
            prt_mom_x, prt_mom_y, prt_mom_z,
            prt_vtx_time, prt_vtx_pos_x, prt_vtx_pos_y, prt_vtx_pos_z,
            prt_end_time, prt_end_pos_x, prt_end_pos_y, prt_end_pos_z,
            trk_hit_cell_id, trk_hit_system_id, trk_hit_system_name,
            trk_hit_pos_x, trk_hit_pos_y, trk_hit_pos_z, trk_hit_time,
            trk_hit_pos_err_xx, trk_hit_pos_err_yy, trk_hit_pos_err_zz, trk_hit_time_err,
            trk_hit_edep, trk_hit_edep_err);
    }
};

// Name of collections for track associations, which associate:
// - edm4eic::RawTrackerHit rawHit       // reference to the digitized hit
// - edm4hep::SimTrackerHit simHit       // reference to the simulated hit
const std::vector<std::string> track_associations = {
    "B0TrackerRawHitAssociations",
    "BackwardMPGDEndcapRawHitAssociations",
    "ForwardMPGDEndcapRawHitAssociations",
    "ForwardOffMTrackerRawHitAssociations",
    "ForwardRomanPotRawHitAssociations",
    "MPGDBarrelRawHitAssociations",
    "OuterMPGDBarrelRawHitAssociations",
    "RICHEndcapNRawHitsAssociations",
    "SiBarrelRawHitAssociations",
    "SiBarrelVertexRawHitAssociations",
    "SiEndcapTrackerRawHitAssociations",
    "TOFBarrelRawHitAssociations",
    "TOFEndcapRawHitAssociations"//,
    //"TaggerTrackerRawHitAssociations" //, "DRICHRawHitsAssociations"
};

const std::map<std::string, std::string> tracker_names_by_assoc = {
    {"B0TrackerRawHitAssociations", "B0TrackerRecHits"},
    {"BackwardMPGDEndcapRawHitAssociations", "BackwardMPGDEndcapRecHits"},
    {"ForwardMPGDEndcapRawHitAssociations", "ForwardMPGDEndcapRecHits"},
    {"ForwardOffMTrackerRawHitAssociations", "ForwardOffMTrackerRecHits"},
    {"ForwardRomanPotRawHitAssociations", "ForwardRomanPotRecHits"},
    {"MPGDBarrelRawHitAssociations", "MPGDBarrelRecHits"},
    {"OuterMPGDBarrelRawHitAssociations", "OuterMPGDBarrelRecHits"},
    {"RICHEndcapNRawHitsAssociations", "RICHEndcapNRecHits"},
    {"SiBarrelRawHitAssociations", "SiBarrelTrackerRecHits"},
    {"SiBarrelVertexRawHitAssociations", "SiBarrelVertexRecHits"},
    {"SiEndcapTrackerRawHitAssociations", "SiEndcapTrackerRecHits"},
    {"TOFBarrelRawHitAssociations", "TOFBarrelRecHits"},
    {"TOFEndcapRawHitAssociations", "TOFEndcapRecHits"},
    // {"TaggerTrackerRawHitAssociations", "TaggerTrackerHit"} //,    {"DRICHRawHitsAssociations", ""}
};

const std::vector<std::string> cal_associations = {
    "B0ECalRawHitAssociations",
    "EcalBarrelImagingRawHitAssociations",
    "EcalBarrelScFiRawHitAssociations",
    "EcalEndcapNRawHitAssociations",
    "EcalEndcapPRawHitAssociations",
    "EcalFarForwardZDCRawHitAssociations",
    "EcalLumiSpecRawHitAssociations",
    "HcalBarrelRawHitAssociations",
    "HcalEndcapNRawHitAssociations",
    "HcalEndcapPInsertRawHitAssociations",
    "HcalFarForwardZDCRawHitAssociations",
    "LFHCALRawHitAssociations",

};

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


// Dictionary from your definitions.xml
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

/// Gets detector system ID and name from full cellID

std::tuple<uint64_t, std::string> get_detector_info(uint64_t cell_id) {
    uint64_t system_id = cell_id & 0xFF;  // system_id is saved in the least significant 8 bits (ha!)

    auto it = system_names_by_ids.find(system_id);
    std::string system_name;
    if (it != system_names_by_ids.end()) {
        system_name = it->second;
    } else {
        auto err_msg = fmt::format("system_names_by_id not found for system: {} . Full cell ID: {}", system_id, cell_id);
        throw std::out_of_range(err_msg);
    }
    return {system_id, system_name};
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

/// Utility function, finds tracker_hit by raw_hit
/// Returns optional with TrackerHit on success, or nullopt with error message via out parameter
std::optional<edm4eic::TrackerHit> get_tracker_hit(
    const edm4eic::RawTrackerHit & raw_hit,
    const edm4eic::TrackerHitCollection & tracker_hits,
    std::string* error_msg = nullptr)
{
    for (const auto & tracker_hit: tracker_hits){
        if (tracker_hit.getRawHit().id() == raw_hit.id())
            return tracker_hit;
    }
    if (error_msg) {
        *error_msg = fmt::format("edm4eic::TrackerHit was not found for raw hit with index: {}", raw_hit.id().index);
    }
    return std::nullopt;
}

/// Classifies a *generator-level* (non-zero) generatorStatus value into the
/// signal / background bands defined by the merger's status-offset convention
/// (see docs/background.md). Uses a generic threshold so new background sources
/// don't require editing a table:
///    status == 1 or 2          -> signal       (offset band 0)
///    status >= 1000            -> background   (offset bands 2000, 3000, ...)
///    anything else (e.g. 0)    -> unknown
/// @return 1 for signal, 3 for background, 0 for unknown.
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

// Specialized function for Tracker Hits
void process_tracker_hits(const podio::Frame& event, const std::string& assoc_col_name, int evt_id) {

    const auto* hit_assocs_ptr = get_optional_collection<edm4eic::MCRecoTrackerHitAssociationCollection>(event, assoc_col_name);
    if (!hit_assocs_ptr) {
        note_skipped(assoc_col_name, "association collection not in file");
        return;
    }
    const auto& hit_assocs = *hit_assocs_ptr;

    // We get corresponding tracker hits collection before hits iteration. We will need it there.
    const auto& traker_col_name = tracker_names_by_assoc.at(assoc_col_name);
    const auto* tracker_hits_ptr = get_optional_collection<edm4eic::TrackerHitCollection>(event, traker_col_name);
    if (!tracker_hits_ptr) {
        note_skipped(traker_col_name, "rec-hits collection not in file");
        return;
    }
    const auto& tracker_hits = *tracker_hits_ptr;

    // Write header if needed
    if (!trk_hits_header_written) {
        csv << HitRecord::make_csv_header() << "\n";
        trk_hits_header_written = true;
    }

    for (const auto& hit_assoc : hit_assocs) {

        // Since all the warning are ~the same, we create this printing functions.
        auto warn = [&](std::string_view msg) {
            fmt::print("WARNING! process_tracker_hits event={} col={} hit_assoc.index:{}. {}\n",
                       evt_id, assoc_col_name, hit_assoc.id().index, msg);
        };

        // Check we have a RawHit
        if (!hit_assoc.getRawHit().isAvailable()) {
            warn("!hit_assoc.getRawHit().isAvailable()");
            continue;
        }

        // Check we have a SimHit
        if (!hit_assoc.getSimHit().isAvailable()) {
            warn("!hit_assoc.getSimHit().isAvailable()");
            continue;
        }

        // Check we have a MCParticle
        if (!hit_assoc.getSimHit().getParticle().isAvailable()) {
            warn("!hit_assoc.getSimHit().getParticle().isAvailable()");
            continue;
        }

        // Pull raw hit
        auto & raw_hit = hit_assoc.getRawHit();

        // Find Tracker hit for this raw hit
        std::string error_msg;
        auto find_result = get_tracker_hit(raw_hit, tracker_hits, &error_msg);
        if (!find_result.has_value()) {
            warn(error_msg);
            continue;
        }
        auto & trk_hit = find_result.value();

        // Pull sim hit and particle
        auto & sim_hit = hit_assoc.getSimHit();
        auto & particle = sim_hit.getParticle();

        // We now have all info to fill our CSV line!

        // >oO Debug
        // if (sim_hit.getParticle().getGeneratorStatus() < 10) {
        //     fmt::print("evt_id:{:<5} col:{:<35} hit_idx:{:<7} prt_id:{:<7}, prt_pid:{:<5}, prt_gstat:{:<8}, prt_sstat:{:<6}, prt_e:{:.3}\n",
        //         evt_id,
        //         assoc_col_name,
        //         hit_assoc.id().index,
        //
        //         sim_hit.getParticle().id().index,
        //         sim_hit.getParticle().getPDG(),
        //         sim_hit.getParticle().getGeneratorStatus(),
        //         sim_hit.getParticle().getSimulatorStatus(),
        //         sim_hit.getParticle().getEnergy()
        //         );
        // }

        int origin = get_origin_status(particle);

        HitRecord record;
        record.evt = evt_id;
        record.hit_index = hit_assoc.id().index;
        record.prt_index = particle.id().index;
        record.prt_pdg = particle.getPDG();
        record.prt_status = particle.getGeneratorStatus();
        record.prt_origin = origin;
        record.prt_energy = particle.getEnergy();
        record.prt_charge = particle.getCharge();
        record.prt_mom_x = particle.getMomentum().x;
        record.prt_mom_y = particle.getMomentum().y;
        record.prt_mom_z = particle.getMomentum().z;
        record.prt_vtx_time = particle.getTime();
        record.prt_vtx_pos_x = particle.getVertex().x;
        record.prt_vtx_pos_y = particle.getVertex().y;
        record.prt_vtx_pos_z = particle.getVertex().z;
        record.prt_end_time = particle.getTime();  // Note: EDM4hep doesn't have separate end time
        record.prt_end_pos_x = particle.getEndpoint().x;
        record.prt_end_pos_y = particle.getEndpoint().y;
        record.prt_end_pos_z = particle.getEndpoint().z;

        record.trk_hit_cell_id = trk_hit.getCellID();
        std::tie(record.trk_hit_system_id, record.trk_hit_system_name) = get_detector_info(record.trk_hit_cell_id);
        record.trk_hit_pos_x = trk_hit.getPosition().x;
        record.trk_hit_pos_y = trk_hit.getPosition().y;
        record.trk_hit_pos_z = trk_hit.getPosition().z;
        record.trk_hit_time = trk_hit.getTime();
        record.trk_hit_pos_err_xx = trk_hit.getPositionError().xx;
        record.trk_hit_pos_err_yy = trk_hit.getPositionError().yy;
        record.trk_hit_pos_err_zz = trk_hit.getPositionError().zz;
        record.trk_hit_time_err = trk_hit.getTimeError();
        record.trk_hit_edep = trk_hit.getEdep();
        record.trk_hit_edep_err = trk_hit.getEdepError();

        // Write record to CSV
        csv << record.get_csv_line() << "\n";
    }
}



// Specialized function for Calorimeter Hits
void process_calo_hits(const podio::Frame& event, const std::string& collection_name, int evt_id) {

    const auto* hit_assocs_ptr = get_optional_collection<edm4eic::MCRecoCalorimeterHitAssociationCollection>(event, collection_name);
    if (!hit_assocs_ptr) {
        note_skipped(collection_name, "association collection not in file");
        return;
    }
    const auto& hit_assocs = *hit_assocs_ptr;

    for (const auto& hit_assoc : hit_assocs) {
        //hit_assoc.getSimHit().getContributions()
        hit_assoc.getSimHit();

        // Since all the warning are ~the same, we create this printing functions.
        auto warn = [&](std::string_view msg) {
            fmt::print("WARNING! process_calo_hits event={} col={} hit_assoc.index:{}. {}\n",
                       evt_id, collection_name, hit_assoc.id().index, msg);
        };

        // Check we have a RawHit
        if (!hit_assoc.getRawHit().isAvailable()) {
            warn("!hit_assoc.getRawHit().isAvailable()");
            continue;
        }

        // Check we have a SimHit
        if (!hit_assoc.getSimHit().isAvailable()) {
            warn("!hit_assoc.getSimHit().isAvailable()");
            continue;
        }

        // Check we have a Contributions
        if (hit_assoc.getSimHit().getContributions().empty()) {
            warn("hit_assoc.getSimHit().getContributions().empty()");
            continue;
        }

        // Check we have at least one MCParticle
        if (!hit_assoc.getSimHit().getContributions().at(0).getParticle().isAvailable()) {
            warn("hit_assoc.getSimHit().getContributions().at(0).getParticle().isAvailable()");
            continue;
        }

        // Pull raw hit
        auto & raw_hit = hit_assoc.getRawHit();

        // Pull sim hit and particle
        auto & sim_hit = hit_assoc.getSimHit();

        auto [system_id, system_name] = get_detector_info(raw_hit.getCellID());

        if (evt_id < 3 && hit_assoc.id().index < 10) {
            fmt::print("evt_id:{:<5} col:{:<35} hit_idx:{:<7} sys_id:{:<7}, sys:{:<20}, amp:{:<8}, ts:{:<6}, nc:{:<6} c0_time:{:.5f} \n",
                evt_id,
                collection_name,
                hit_assoc.id().index,
                system_id,
                system_name,
                raw_hit.getAmplitude(),
                raw_hit.getTimeStamp(),
                sim_hit.getContributions().size(),
                sim_hit.getContributions().at(0).getTime()
                );
        }
    }
}


//------------------------------------------------------------------------------
// event processing
//------------------------------------------------------------------------------
void process_event(const podio::Frame& event, int evt_id) {

    for (const auto& trk_assoc_name: track_associations) {
        process_tracker_hits(event, trk_assoc_name, evt_id);
    }

    for (const auto& cal_assoc_name: cal_associations) {
        //process_calo_hits(event, cal_assoc_name, evt_id);
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
        fmt::print(stderr, "error: cannot open output files\n");
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

    fmt::print("\nWrote data for {} tracks to {}\n", total_evt_processed, outfile);

}


// ---------------------------------------------------------------------------
// ROOT-macro entry point.
// Call it from the prompt:  root -x -l -b -q 'trk_hits.cxx("file.root", "output.csv", 100)'
// ---------------------------------------------------------------------------
void edm4eic_trk_hits(const char* infile, const char* outfile, int events = -1)
{
    fmt::print("'trk_hits' entry point is used.\n");
    execute(infile, outfile, events);
}


//------------------------------------------------------------------------------
// main function entry point (standalone application)
//------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::vector<std::string> infiles;
    std::string out_name = "hits.csv";

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