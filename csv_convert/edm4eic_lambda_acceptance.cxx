// edm4eic_lambda_acceptance.cxx — per-event Lambda -> p pi- acceptance ledger.
//
// One row PER EVENT (all events, not only p pi- decays, so branching fractions
// come out of the same file), recording every stage where a Lambda can be lost:
//   * Sullivan (first) Lambda decay type and true kinematics, decay vertex z
//   * daughter true kinematics
//   * GEOMETRY: raw-hit counts the daughters leave in B0 / Roman Pots / OffM
//     (via MCRecoTrackerHitAssociations — tracking-independent acceptance)
//   * RECONSTRUCTION: association-based flags for central tracking and
//     truth-seeded B0 tracks (empty collections => 0, e.g. stock campaign reco),
//     and closest-candidate relative momentum for RP/OffM matrix reco
//     (those have no particle associations).
//
// Used for the acceptance table that answers "why are so few Lambdas
// reconstructed" (ai-comb-lam reports) and runs on the farm as a csv stage.
//
// Farm:  root -x -l -b -q 'edm4eic_lambda_acceptance.cxx("in.edm4eic.root","out.csv")'

#ifdef __CLING__
R__LOAD_LIBRARY(podioDict)
R__LOAD_LIBRARY(podioRootIO)
R__LOAD_LIBRARY(libedm4hepDict)
R__LOAD_LIBRARY(libedm4eicDict)
#endif

#include "podio/Frame.h"
#include "podio/ROOTReader.h"
#include <edm4hep/MCParticleCollection.h>
#include <edm4eic/ReconstructedParticleCollection.h>
#include <edm4eic/MCRecoParticleAssociationCollection.h>
#include <edm4eic/MCRecoTrackParticleAssociationCollection.h>
#include <edm4eic/MCRecoTrackerHitAssociationCollection.h>

#include <fmt/core.h>
#include <cmath>
#include <fstream>
#include <optional>
#include <string>

namespace lambda_acceptance {

std::ofstream csv;
long n_evt = 0;

struct Kin { double p = 0, theta = 0, eta = 0; };

Kin kin(const edm4hep::MCParticle& m) {
    const auto mom = m.getMomentum();
    const double p = std::sqrt(mom.x * mom.x + mom.y * mom.y + mom.z * mom.z);
    const double th = std::acos(mom.z / std::max(p, 1e-12));
    return {p, th, -std::log(std::tan(std::max(th, 1e-9) / 2.0))};
}

// decay type, same convention as edm4eic_mcpart_lambda.cxx (1 = p pi-)
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

int hits_from(const podio::Frame& evt, const char* coll,
              const std::optional<edm4hep::MCParticle>& mc) {
    if (!mc) return 0;
    int n = 0;
    for (const auto& a : evt.get<edm4eic::MCRecoTrackerHitAssociationCollection>(coll)) {
        const auto sh = a.getSimHit();
        if (sh.isAvailable() && sh.getParticle().isAvailable() &&
            sh.getParticle().getObjectID() == mc->getObjectID()) n++;
    }
    return n;
}

// best relative-momentum distance of any candidate in `coll` to `mc` (-1: none)
double best_dp(const podio::Frame& evt, const char* coll,
               const std::optional<edm4hep::MCParticle>& mc) {
    if (!mc) return -1;
    const auto k = kin(*mc);
    double best = -1;
    for (const auto& c : evt.get<edm4eic::ReconstructedParticleCollection>(coll)) {
        const auto mom = c.getMomentum();
        const double p = std::sqrt(mom.x * mom.x + mom.y * mom.y + mom.z * mom.z);
        const double dp = std::abs(p - k.p) / std::max(k.p, 1e-12);
        if (best < 0 || dp < best) best = dp;
    }
    return best;
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

    const auto lk = lam ? kin(*lam) : Kin{};
    const auto pk = prot ? kin(*prot) : Kin{};
    const auto ik = pim ? kin(*pim) : Kin{};
    const double dvz = prot ? prot->getVertex().z : 0;

    // central tracking: association-based (max-weight irrelevant, presence is enough)
    int prot_cent = 0, pim_cent = 0;
    for (const auto& a : evt.get<edm4eic::MCRecoParticleAssociationCollection>(
             "ReconstructedChargedParticleAssociations")) {
        if (!a.getSim().isAvailable()) continue;
        if (prot && a.getSim().getObjectID() == prot->getObjectID()) prot_cent = 1;
        if (pim && a.getSim().getObjectID() == pim->getObjectID()) pim_cent = 1;
    }
    // B0 truth-seeded tracks (filled only when reco ran with the B0 recovery
    // parameters — empty in stock campaign files, then these flags stay 0)
    int prot_b0 = 0, pim_b0 = 0;
    const int n_rp =
        evt.get<edm4eic::ReconstructedParticleCollection>("ForwardRomanPotRecParticles").size();
    {
        const auto& ba = evt.get<edm4eic::MCRecoTrackParticleAssociationCollection>(
            "B0TrackerCKFTruthSeededTrackAssociations");
        for (const auto& a : ba) {
            if (!a.getSim().isAvailable()) continue;
            if (prot && a.getSim().getObjectID() == prot->getObjectID()) prot_b0 = 1;
            if (pim && a.getSim().getObjectID() == pim->getObjectID()) pim_b0 = 1;
        }
    }

    csv << fmt::format(
        "{},{},{},{},{},{},{},{},{},{},{},{},"
        "{},{},{},{},{},{},"
        "{},{},{},{},{},{},{}\n",
        evt_id, decay, n_lambdas, lk.p, lk.eta, dvz,
        pk.p, pk.theta, pk.eta, ik.p, ik.theta, ik.eta,
        hits_from(evt, "B0TrackerRawHitAssociations", prot),
        hits_from(evt, "ForwardRomanPotRawHitAssociations", prot),
        hits_from(evt, "ForwardOffMTrackerRawHitAssociations", prot),
        hits_from(evt, "B0TrackerRawHitAssociations", pim),
        hits_from(evt, "ForwardRomanPotRawHitAssociations", pim),
        hits_from(evt, "ForwardOffMTrackerRawHitAssociations", pim),
        prot_cent, pim_cent, prot_b0, pim_b0,
        best_dp(evt, "ForwardRomanPotRecParticles", prot),
        best_dp(evt, "ForwardOffMRecParticles", prot), n_rp);
}

}  // namespace lambda_acceptance

void edm4eic_lambda_acceptance(const char* infile, const char* outfile, int events = -1) {
    using namespace lambda_acceptance;
    csv.open(outfile);
    if (!csv) { fmt::print(stderr, "cannot open {}\n", outfile); return; }
    csv << "event,decay,n_lambdas,lam_p,lam_eta,lam_dvz,"
           "prot_p,prot_theta,prot_eta,pim_p,pim_theta,pim_eta,"
           "prot_hits_b0,prot_hits_rp,prot_hits_offm,"
           "pim_hits_b0,pim_hits_rp,pim_hits_offm,"
           "prot_cent,pim_cent,prot_b0trk,pim_b0trk,prot_rp_dp,prot_offm_dp,n_rp\n";

    podio::ROOTReader rdr;
    rdr.openFile(infile);
    const auto nEv = rdr.getEntries(podio::Category::Event);
    const unsigned nProc = (events > 0 && (unsigned)events < nEv) ? events : nEv;
    for (unsigned ie = 0; ie < nProc; ++ie) {
        podio::Frame evt(rdr.readNextEntry(podio::Category::Event));
        process_event(evt, n_evt++);
    }
    csv.close();
    fmt::print("edm4eic_lambda_acceptance: {} events -> {}\n", n_evt, outfile);
}
