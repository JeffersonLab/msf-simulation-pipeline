#ifdef __CLING__
R__LOAD_LIBRARY(podioDict)
R__LOAD_LIBRARY(podioRootIO)
R__LOAD_LIBRARY(libedm4hepDict)
R__LOAD_LIBRARY(libedm4eicDict)
#endif

// mc_particles.cxx
//
// Dumps every MCParticle of every event to a CSV, one row per particle.
// Companion of trk_hits.cxx (which dumps reconstructed tracker hits with
// their originating particle). Here we keep the full MC truth particle list so
// downstream analyses can study kinematics, vertices and the generator/simulator
// status codes (e.g. to verify the merger status-offset convention, see
// docs/background.md).
#include "podio/Frame.h"
#include "podio/ROOTReader.h"
#include <edm4hep/MCParticleCollection.h>

#include <fmt/core.h>
#include <fmt/ostream.h>

#include <fstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <set>

using namespace edm4hep;

//------------------------------------------------------------------------------
// globals & helpers
//------------------------------------------------------------------------------
int events_limit = -1; // -n  <N>
long total_evt_processed = 0;
long total_prt_written = 0;
std::ofstream csv;
bool header_written = false;

// Name of the MC truth particle collection in the (reco/sim) podio frame.
const std::string mc_particles_col_name = "MCParticles";

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


/// Struct representing a line in the particles csv file
struct ParticleRecord {
    // Event and indexing
    uint64_t evt;                    // Event number
    uint64_t prt_index;              // Index of this MCParticle in the collection

    // Particle identification & status
    int32_t prt_pdg;                 // PDG code (e.g. 11=e-, 211=pi+, 2212=proton)
    int32_t prt_gen_status;          // generatorStatus (carries merger offset: 1/2 signal, 2001.. bg, 0 g4)
    int32_t prt_sim_status;          // simulatorStatus (EDM4hep bit field: createdInSimulation, decayedInTracker, ...)
    int32_t prt_origin;              // Origin flag: 0 unknown, 1 signal, 2 g4-from-signal, 3 background, 4 g4-from-background

    // Particle kinematics
    double prt_mass;                 // Mass [GeV/c^2]
    double prt_energy;               // Total energy [GeV] (sqrt(p^2 + m^2))
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
    float prt_end_pos_x;             // Endpoint x [mm]
    float prt_end_pos_y;             // Endpoint y [mm]
    float prt_end_pos_z;             // Endpoint z [mm]

    // Family linkage
    uint32_t prt_n_parents;          // Number of parents
    uint32_t prt_n_daughters;        // Number of daughters
    int64_t prt_parent_index;        // Collection index of first parent, or -1 if none

    static std::string make_csv_header() {
        return "evt,prt_index,"
               "prt_pdg,prt_gen_status,prt_sim_status,prt_origin,"
               "prt_mass,prt_energy,prt_charge,"
               "prt_mom_x,prt_mom_y,prt_mom_z,"
               "prt_vtx_time,prt_vtx_pos_x,prt_vtx_pos_y,prt_vtx_pos_z,"
               "prt_end_pos_x,prt_end_pos_y,prt_end_pos_z,"
               "prt_n_parents,prt_n_daughters,prt_parent_index";
    }

    std::string get_csv_line() const {
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


//------------------------------------------------------------------------------
// event processing
//------------------------------------------------------------------------------
void process_event(const podio::Frame& event, int evt_id) {

    const auto* particles_ptr = get_optional_collection<MCParticleCollection>(event, mc_particles_col_name);
    if (!particles_ptr) {
        note_skipped(mc_particles_col_name, "collection not in file");
        return;
    }
    const auto& particles = *particles_ptr;

    // Write header if needed
    if (!header_written) {
        csv << ParticleRecord::make_csv_header() << "\n";
        header_written = true;
    }

    for (const auto& particle : particles) {

        ParticleRecord record;
        record.evt = evt_id;
        record.prt_index = particle.id().index;

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
            ? static_cast<int64_t>(particle.getParents(0).id().index)
            : -1;

        csv << record.get_csv_line() << "\n";
        ++total_prt_written;
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

    fmt::print("\nWrote {} particles from {} events to {}\n",
               total_prt_written, total_evt_processed, outfile);
}


// ---------------------------------------------------------------------------
// ROOT-macro entry point.
// Call it from the prompt:  root -x -l -b -q 'mc_particles.cxx("file.root", "particles.csv", 100)'
// ---------------------------------------------------------------------------
void edm4eic_mc_particles(const char* infile, const char* outfile, int events = -1)
{
    fmt::print("'mc_particles' entry point is used.\n");
    execute(infile, outfile, events);
}


//------------------------------------------------------------------------------
// main function entry point (standalone application)
//------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::vector<std::string> infiles;
    std::string out_name = "particles.csv";

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
