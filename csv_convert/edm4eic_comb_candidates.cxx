// edm4eic_comb_candidates.cxx — p / pi- candidate table for Lambda -> p pi-
// combinatorics (the ai-comb-lam dataset builder, farm edition).
//
// Writes TWO CSVs (same pattern as edm4hep_acceptance_ppim: the job script zips
// the main one, the extra lives next to it):
//   <out>.csv                    one row per reco candidate
//   <out minus .csv>_events.csv  one row per event (Sullivan-Lambda truth summary)
//
// Candidate sources (column `coll`):
//   0  ReconstructedChargedParticles      central tracking, association labels
//   1  B0TrackerCKFTruthSeededTracks      B0 tracker. NOTE: this collection is
//      EMPTY in stock EICrecon output — reco must run with the B0 recovery
//      parameters (see config eicrecon.extra_flags and the AcceptSecondaries
//      patch); labels via B0TrackerCKFTruthSeededTrackAssociations
//   2  ForwardRomanPotRecParticles        matrix reco, NO associations: store
//   4  ForwardOffMRecParticles            kinematic distances to the true
//                                         daughters, label downstream
//
// Truth columns (mc_*, is_sul_*, dp_*/dth_*) are for LABELS AND MONITORING ONLY —
// the training feature whitelist lives in ai-comb-lam/comblam/features.py.
//
// Farm:  root -x -l -b -q 'edm4eic_comb_candidates.cxx("in.edm4eic.root","out.csv")'

#ifdef __CLING__
R__LOAD_LIBRARY(podioDict)
R__LOAD_LIBRARY(podioRootIO)
R__LOAD_LIBRARY(libedm4hepDict)
R__LOAD_LIBRARY(libedm4eicDict)
#endif

#include "podio/Frame.h"
#include "podio/ROOTReader.h"
#include <edm4hep/MCParticleCollection.h>
#include <edm4eic/TrackCollection.h>
#include <edm4eic/ReconstructedParticleCollection.h>
#include <edm4eic/MCRecoParticleAssociationCollection.h>
#include <edm4eic/MCRecoTrackParticleAssociationCollection.h>

#include <fmt/core.h>
#include <cmath>
#include <fstream>
#include <optional>
#include <string>

namespace comb_candidates {

std::ofstream ev_csv, cand_csv;
long n_evt = 0;

struct Kin { double p, theta, eta, phi; };
Kin kin(double px, double py, double pz) {
    const double p = std::sqrt(px * px + py * py + pz * pz);
    const double theta = std::acos(pz / std::max(p, 1e-12));
    return {p, theta, -std::log(std::tan(std::max(theta, 1e-9) / 2.0)), std::atan2(py, px)};
}

int classify(const edm4hep::MCParticle& lam,
             std::optional<edm4hep::MCParticle>& prot,
             std::optional<edm4hep::MCParticle>& pim) {
    auto ds = lam.getDaughters();
    if (ds.size() == 0) return 0;
    if (ds.size() == 2) {
        for (const auto& d : ds) {
            if (d.getPDG() == 2212) prot = d;
            else if (d.getPDG() == -211) pim = d;
        }
        if (prot && pim) return 1;
        if ((ds.at(0).getPDG() == 2112 && ds.at(1).getPDG() == 111) ||
            (ds.at(1).getPDG() == 2112 && ds.at(0).getPDG() == 111)) return 2;
        return 8;
    }
    return ds.size() == 1 ? 4 : 3;
}

void process_event(const podio::Frame& evt, long evt_id) {
    const auto& mcps = evt.get<edm4hep::MCParticleCollection>("MCParticles");

    std::optional<edm4hep::MCParticle> lam, prot, pim;
    int n_lambdas = 0, decay = -1;
    for (const auto& m : mcps) {
        if (m.getPDG() != 3122) continue;
        n_lambdas++;
        if (!lam) lam = m;
    }
    if (lam) decay = classify(*lam, prot, pim);

    const int prot_idx = prot ? prot->getObjectID().index : -1;
    const int pim_idx = pim ? pim->getObjectID().index : -1;
    const auto lk = lam ? kin(lam->getMomentum().x, lam->getMomentum().y, lam->getMomentum().z)
                        : Kin{0, 0, 0, 0};
    double dvx = 0, dvy = 0, dvz = 0;
    Kin pk{0, 0, 0, 0}, ik{0, 0, 0, 0};
    if (prot) {
        const auto v = prot->getVertex();
        dvx = v.x; dvy = v.y; dvz = v.z;
        pk = kin(prot->getMomentum().x, prot->getMomentum().y, prot->getMomentum().z);
    }
    if (pim) ik = kin(pim->getMomentum().x, pim->getMomentum().y, pim->getMomentum().z);

    ev_csv << fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
                          evt_id, n_lambdas, decay, prot_idx, pim_idx,
                          lk.p, lk.eta, lk.phi, dvx, dvy, dvz,
                          pk.p, pk.theta, pk.phi, ik.p, ik.theta, ik.phi);

    auto write_cand = [&](int coll, int idx, double charge, double px, double py, double pz,
                          double rx, double ry, double rz, double quality, int ndf, int nmeas,
                          int mc_idx, double mc_weight, int mc_pdg, int mc_gen,
                          double dp_prot, double dth_prot, double dp_pim, double dth_pim) {
        const auto k = kin(px, py, pz);
        cand_csv << fmt::format(
            "{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
            evt_id, coll, idx, charge, px, py, pz, k.p, k.theta, k.eta, k.phi,
            rx, ry, rz, quality, ndf, nmeas,
            mc_idx, mc_weight, mc_pdg, mc_gen,
            (mc_idx >= 0 && mc_idx == prot_idx) ? 1 : 0,
            (mc_idx >= 0 && mc_idx == pim_idx) ? 1 : 0,
            dp_prot, dth_prot, dp_pim, dth_pim);
    };

    // ---- 0: central tracking --------------------------------------------------
    const auto& rcp = evt.get<edm4eic::ReconstructedParticleCollection>("ReconstructedChargedParticles");
    const auto& assocs = evt.get<edm4eic::MCRecoParticleAssociationCollection>(
        "ReconstructedChargedParticleAssociations");
    {
        int i = 0;
        for (const auto& c : rcp) {
            int mc_idx = -1, mc_pdg = 0, mc_gen = -1;
            double w = -1;
            for (const auto& a : assocs) {
                if (!a.getRec().isAvailable() || a.getRec().getObjectID().index != i) continue;
                if (a.getWeight() > w && a.getSim().isAvailable()) {
                    w = a.getWeight();
                    mc_idx = a.getSim().getObjectID().index;
                    mc_pdg = a.getSim().getPDG();
                    mc_gen = a.getSim().getGeneratorStatus();
                }
            }
            const auto mom = c.getMomentum();
            const auto ref = c.getReferencePoint();
            write_cand(0, i, c.getCharge(), mom.x, mom.y, mom.z, ref.x, ref.y, ref.z,
                       c.getGoodnessOfPID(), -1, (int)c.getTracks().size(),
                       mc_idx, w, mc_pdg, mc_gen, -1, -1, -1, -1);
            ++i;
        }
    }

    // ---- 1: B0 truth-seeded tracks (empty unless reco ran with B0 recovery) ---
    {
        const auto& trks = evt.get<edm4eic::TrackCollection>("B0TrackerCKFTruthSeededTracks");
        const auto& ba = evt.get<edm4eic::MCRecoTrackParticleAssociationCollection>(
            "B0TrackerCKFTruthSeededTrackAssociations");
        int i = 0;
        for (const auto& t : trks) {
            int mc_idx = -1, mc_pdg = 0, mc_gen = -1;
            double w = -1;
            for (const auto& a : ba) {
                if (!a.getRec().isAvailable() || a.getRec().getObjectID().index != i) continue;
                if (a.getWeight() > w && a.getSim().isAvailable()) {
                    w = a.getWeight();
                    mc_idx = a.getSim().getObjectID().index;
                    mc_pdg = a.getSim().getPDG();
                    mc_gen = a.getSim().getGeneratorStatus();
                }
            }
            const auto mom = t.getMomentum();
            const auto pos = t.getPosition();
            write_cand(1, i, t.getCharge(), mom.x, mom.y, mom.z, pos.x, pos.y, pos.z,
                       t.getChi2(), (int)t.getNdf(), (int)t.getMeasurements().size(),
                       mc_idx, w, mc_pdg, mc_gen, -1, -1, -1, -1);
            ++i;
        }
    }

    // ---- 2/4: Roman Pot + OffM matrix reco (no associations -> kinematic
    //           distances to the Sullivan daughters, labeling decided downstream)
    for (const auto& [collcode, name] : {std::pair<int, const char*>{2, "ForwardRomanPotRecParticles"},
                                         {4, "ForwardOffMRecParticles"}}) {
        const auto& coll = evt.get<edm4eic::ReconstructedParticleCollection>(name);
        int i = 0;
        for (const auto& c : coll) {
            const auto mom = c.getMomentum();
            const auto ref = c.getReferencePoint();
            const auto k = kin(mom.x, mom.y, mom.z);
            double dpp = -1, dthp = -1, dpi = -1, dthi = -1;
            if (prot) { dpp = std::abs(k.p - pk.p) / pk.p; dthp = std::abs(k.theta - pk.theta); }
            if (pim) { dpi = std::abs(k.p - ik.p) / ik.p; dthi = std::abs(k.theta - ik.theta); }
            write_cand(collcode, i, c.getCharge(), mom.x, mom.y, mom.z, ref.x, ref.y, ref.z,
                       -1, -1, -1, -1, -1, 0, -1, dpp, dthp, dpi, dthi);
            ++i;
        }
    }
}

}  // namespace comb_candidates

void edm4eic_comb_candidates(const char* infile, const char* outfile, int events = -1) {
    using namespace comb_candidates;
    // derived second output: <out minus .csv>_events.csv
    std::string ev_name(outfile);
    const auto dot = ev_name.rfind(".csv");
    ev_name = (dot == std::string::npos ? ev_name + "_events" : ev_name.substr(0, dot) + "_events")
              + ".csv";

    cand_csv.open(outfile);
    ev_csv.open(ev_name);
    if (!cand_csv || !ev_csv) { fmt::print(stderr, "cannot open outputs\n"); return; }
    cand_csv << "event,coll,idx,charge,px,py,pz,p,theta,eta,phi,"
                "ref_x,ref_y,ref_z,quality,ndf,nmeas,"
                "mc_idx,mc_weight,mc_pdg,mc_gen,is_sul_prot,is_sul_pim,"
                "dp_prot,dth_prot,dp_pim,dth_pim\n";
    ev_csv << "event,n_lambdas,decay,prot_mc_idx,pim_mc_idx,"
              "lam_p,lam_eta,lam_phi,lam_dvx,lam_dvy,lam_dvz,"
              "prot_p,prot_theta,prot_phi,pim_p,pim_theta,pim_phi\n";

    podio::ROOTReader rdr;
    rdr.openFile(infile);
    const auto nEv = rdr.getEntries(podio::Category::Event);
    const unsigned nProc = (events > 0 && (unsigned)events < nEv) ? events : nEv;
    for (unsigned ie = 0; ie < nProc; ++ie) {
        podio::Frame evt(rdr.readNextEntry(podio::Category::Event));
        process_event(evt, n_evt++);
    }
    cand_csv.close();
    ev_csv.close();
    fmt::print("edm4eic_comb_candidates: {} events -> {} + {}\n", n_evt, outfile, ev_name);
}
