// writer_AcceptanceNpi0Writer.hpp — CSV role "acceptance_npi0": the first Λ of every event with n π⁰ → n γ γ detection flags.
//
// Needs: sim. Collections read: MCParticles and six SimCalorimeterHit collections (with their
// contributions): the ZDC, B0 and forward endcap ECALs for the photons, the ZDC, insert and
// LFHCAL HCALs for the neutron.
// Columns: evt, lam_is_first, lam_decay, the 16-column blocks of lam, prot, pimin, neut,
// pizero, gamone, gamtwo, then nine 0/1 flags: the neutron per HCAL, each photon per ECAL.
// The flags are computed for n π⁰ decays whose π⁰ photons Geant4 recorded; other rows carry 0.
//
// One row per event (the first Λ). The run summary repeats the macro's detection statistics.
// Port of csv_convert/edm4hep_acceptance_npi0.cxx; rows are byte-identical to the macro's, the
// header follows column_renames.py.
#pragma once

#include "Writer.hpp"
#include "lambda_decay.hpp"
#include "mc_particle_block.hpp"

#include <edm4hep/CaloHitContributionCollection.h>
#include <edm4hep/MCParticleCollection.h>
#include <edm4hep/SimCalorimeterHitCollection.h>
#include <fmt/core.h>

class AcceptanceNpi0Writer : public Writer {
public:
    std::string role() const override { return "acceptance_npi0"; }
    Needs needs() const override { return Needs{.reco = false, .sim = true}; }
    std::vector<std::string> collections() const override {
        std::vector<std::string> names = {"MCParticles"};
        for (const char* calorimeter : {"EcalFarForwardZDCHits", "B0ECalHits", "EcalEndcapPHits", "HcalFarForwardZDCHits", "HcalEndcapPInsertHits", "LFHCALHits"}) {
            names.push_back(calorimeter);
            names.push_back(std::string(calorimeter) + "Contributions");
        }
        return names;
    }

    std::string csv_header() const override {
        std::string header = "evt,lam_is_first,lam_decay";
        for (const char* prefix : {"lam", "prot", "pimin", "neut", "pizero", "gamone", "gamtwo"}) header += "," + mc_particle_block_header(prefix);
        return header + ",neut_HcalFarForwardZDCHits,neut_HcalEndcapPInsertHits,neut_LFHCALHits,"
                        "gamone_EcalFarForwardZDCHits,gamtwo_EcalFarForwardZDCHits,"
                        "gamone_B0ECalHits,gamtwo_B0ECalHits,"
                        "gamone_EcalEndcapPHits,gamtwo_EcalEndcapPHits";
    }

private:
    struct DetectionFlags {
        bool neutron_zdc_hcal = false;
        bool neutron_insert_hcal = false;
        bool neutron_lfhcal = false;
        bool gamma_one_zdc_ecal = false;
        bool gamma_one_b0_ecal = false;
        bool gamma_one_endcap_p_ecal = false;
        bool gamma_two_zdc_ecal = false;
        bool gamma_two_b0_ecal = false;
        bool gamma_two_endcap_p_ecal = false;
    };

    /// The macro's end-of-run statistics; every counter is per first Λ.
    struct Statistics {
        long lambdas = 0;
        long npi0_decays = 0;
        long npi0_with_observable_gammas = 0;
        long neutron_in_any_hcal = 0;
        long neutron_and_both_gammas = 0;
        long all_three_detected = 0;
        long neutron_zdc_hcal = 0, neutron_insert_hcal = 0, neutron_lfhcal = 0;
        long gamma_one_zdc_ecal = 0, gamma_two_zdc_ecal = 0, gamma_one_b0_ecal = 0, gamma_two_b0_ecal = 0, gamma_one_endcap_p = 0, gamma_two_endcap_p = 0;
        long gammas_and_neutron_in_zdc = 0;
        long all3_neutron_zdc_hcal = 0, all3_neutron_insert_hcal = 0, all3_neutron_lfhcal = 0;
        long all3_gamma_one_zdc_ecal = 0, all3_gamma_two_zdc_ecal = 0, all3_gamma_one_b0_ecal = 0, all3_gamma_two_b0_ecal = 0, all3_gamma_one_endcap_p = 0, all3_gamma_two_endcap_p = 0;
        long decay_not_decayed = 0, decay_p_pi_minus = 0, decay_shower = 0, decay_only_p = 0, decay_only_pi_plus = 0, decay_only_n = 0, decay_only_pi0 = 0, decay_other = 0;
    };
    Statistics statistics_;

    /// Whether the particle contributed to any hit of the collection (absent collection: no).
    static bool contributed_to(const edm4hep::SimCalorimeterHitCollection* hits, const edm4hep::MCParticle& particle) {
        if (!hits) return false;
        for (const auto& hit : *hits) {
            for (const auto& contribution : hit.getContributions()) {
                if (contribution.getParticle().getObjectID() == particle.getObjectID()) return true;
            }
        }
        return false;
    }

    DetectionFlags detect(const podio::Frame& sim, const edm4hep::MCParticle& neutron, const edm4hep::MCParticle& gamma_one, const edm4hep::MCParticle& gamma_two) {
        DetectionFlags flags;
        const auto* zdc_ecal = get_optional_collection<edm4hep::SimCalorimeterHitCollection>(sim, "EcalFarForwardZDCHits");
        flags.gamma_one_zdc_ecal = contributed_to(zdc_ecal, gamma_one);
        flags.gamma_two_zdc_ecal = contributed_to(zdc_ecal, gamma_two);
        const auto* b0_ecal = get_optional_collection<edm4hep::SimCalorimeterHitCollection>(sim, "B0ECalHits");
        flags.gamma_one_b0_ecal = contributed_to(b0_ecal, gamma_one);
        flags.gamma_two_b0_ecal = contributed_to(b0_ecal, gamma_two);
        const auto* endcap_p_ecal = get_optional_collection<edm4hep::SimCalorimeterHitCollection>(sim, "EcalEndcapPHits");
        flags.gamma_one_endcap_p_ecal = contributed_to(endcap_p_ecal, gamma_one);
        flags.gamma_two_endcap_p_ecal = contributed_to(endcap_p_ecal, gamma_two);
        flags.neutron_zdc_hcal = contributed_to(get_optional_collection<edm4hep::SimCalorimeterHitCollection>(sim, "HcalFarForwardZDCHits"), neutron);
        flags.neutron_insert_hcal = contributed_to(get_optional_collection<edm4hep::SimCalorimeterHitCollection>(sim, "HcalEndcapPInsertHits"), neutron);
        flags.neutron_lfhcal = contributed_to(get_optional_collection<edm4hep::SimCalorimeterHitCollection>(sim, "LFHCALHits"), neutron);

        const bool neutron_anywhere = flags.neutron_zdc_hcal || flags.neutron_insert_hcal || flags.neutron_lfhcal;
        const bool gamma_one_anywhere = flags.gamma_one_zdc_ecal || flags.gamma_one_b0_ecal || flags.gamma_one_endcap_p_ecal;
        const bool gamma_two_anywhere = flags.gamma_two_zdc_ecal || flags.gamma_two_b0_ecal || flags.gamma_two_endcap_p_ecal;
        Statistics& s = statistics_;
        if (neutron_anywhere) ++s.neutron_in_any_hcal;
        if (neutron_anywhere && gamma_one_anywhere && gamma_two_anywhere) ++s.neutron_and_both_gammas;
        if (flags.neutron_zdc_hcal) ++s.neutron_zdc_hcal;
        if (flags.neutron_insert_hcal) ++s.neutron_insert_hcal;
        if (flags.neutron_lfhcal) ++s.neutron_lfhcal;
        if (flags.gamma_one_zdc_ecal) ++s.gamma_one_zdc_ecal;
        if (flags.gamma_two_zdc_ecal) ++s.gamma_two_zdc_ecal;
        if (flags.gamma_one_b0_ecal) ++s.gamma_one_b0_ecal;
        if (flags.gamma_two_b0_ecal) ++s.gamma_two_b0_ecal;
        if (flags.gamma_one_endcap_p_ecal) ++s.gamma_one_endcap_p;
        if (flags.gamma_two_endcap_p_ecal) ++s.gamma_two_endcap_p;
        if (neutron_anywhere && gamma_one_anywhere && gamma_two_anywhere) {
            ++s.all_three_detected;
            if (flags.neutron_zdc_hcal && flags.gamma_one_zdc_ecal && flags.gamma_two_zdc_ecal) ++s.gammas_and_neutron_in_zdc;
            if (flags.neutron_zdc_hcal) ++s.all3_neutron_zdc_hcal;
            if (flags.neutron_insert_hcal) ++s.all3_neutron_insert_hcal;
            if (flags.neutron_lfhcal) ++s.all3_neutron_lfhcal;
            if (flags.gamma_one_zdc_ecal) ++s.all3_gamma_one_zdc_ecal;
            if (flags.gamma_two_zdc_ecal) ++s.all3_gamma_two_zdc_ecal;
            if (flags.gamma_one_b0_ecal) ++s.all3_gamma_one_b0_ecal;
            if (flags.gamma_two_b0_ecal) ++s.all3_gamma_two_b0_ecal;
            if (flags.gamma_one_endcap_p_ecal) ++s.all3_gamma_one_endcap_p;
            if (flags.gamma_two_endcap_p_ecal) ++s.all3_gamma_two_endcap_p;
        }
        return flags;
    }

    void count_decay(const LambdaDecay& decay) {
        Statistics& s = statistics_;
        switch (decay.type) {
            case 0: ++s.decay_not_decayed; break;
            case 3: ++s.decay_shower; break;
            case 4: ++s.decay_only_p; break;
            case 5: ++s.decay_only_pi_plus; break;
            case 6: ++s.decay_only_n; break;
            case 7: ++s.decay_only_pi0; break;
            case 8: ++s.decay_other; break;
            default: break;
        }
        if (decay.neutron && decay.pi_zero) ++s.npi0_decays;
        if (decay.proton && decay.pi_minus) ++s.decay_p_pi_minus;
    }

    void write_event(const EventInputs& inputs) override {
        const podio::Frame& sim = *inputs.sim;
        const auto* particles = get_optional_collection<edm4hep::MCParticleCollection>(sim, "MCParticles");
        if (!particles) {
            skip_notes.note("MCParticles", "collection not in file");
            return;
        }
        for (const auto& lambda : *particles) {
            if (lambda.getPDG() != 3122) continue;
            ++statistics_.lambdas;
            const LambdaDecay decay = classify_lambda_decay(lambda);
            count_decay(decay);
            if (decay.neutron && decay.proton) {
                fmt::print("(!!!) WARNING: I see neut && prot at evt_id={}\n", inputs.entry_index);
            }
            DetectionFlags flags;
            if (decay.neutron && decay.gamma_one && decay.gamma_two) {
                ++statistics_.npi0_with_observable_gammas;
                flags = detect(sim, *decay.neutron, *decay.gamma_one, *decay.gamma_two);
            }
            write_row(fmt::format("{},1,{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                inputs.entry_index, decay.type,
                mc_particle_block(lambda), mc_particle_block(decay.proton), mc_particle_block(decay.pi_minus), mc_particle_block(decay.neutron),
                mc_particle_block(decay.pi_zero), mc_particle_block(decay.gamma_one), mc_particle_block(decay.gamma_two),
                static_cast<int>(flags.neutron_zdc_hcal), static_cast<int>(flags.neutron_insert_hcal), static_cast<int>(flags.neutron_lfhcal),
                static_cast<int>(flags.gamma_one_zdc_ecal), static_cast<int>(flags.gamma_two_zdc_ecal),
                static_cast<int>(flags.gamma_one_b0_ecal), static_cast<int>(flags.gamma_two_b0_ecal),
                static_cast<int>(flags.gamma_one_endcap_p_ecal), static_cast<int>(flags.gamma_two_endcap_p_ecal)));
            break;   // one Λ per event: the first one
        }
    }

    std::string summary_notes() const override {
        const Statistics& s = statistics_;
        auto percent = [&](long count) { return s.lambdas > 0 ? 100.0 * count / s.lambdas : 0.0; };
        std::string text = fmt::format("\n=== DETECTION STATISTICS ===\nTotal first lambdas: {}\nLambda decay channels:\n", s.lambdas);
        text += fmt::format("  0 Not decayed:     {} ({:.2f}%)\n  1 p + π⁻:          {} ({:.2f}%)\n  2 n + π⁰:          {} ({:.2f}%)\n"
                            "  3 Shower (>2):     {} ({:.2f}%)\n  4 Only p:          {} ({:.2f}%)\n  5 Only π⁺:         {} ({:.2f}%)\n"
                            "  6 Only n:          {} ({:.2f}%)\n  7 Only π⁰:         {} ({:.2f}%)\n  8 Other:           {} ({:.2f}%)\n",
                            s.decay_not_decayed, percent(s.decay_not_decayed), s.decay_p_pi_minus, percent(s.decay_p_pi_minus),
                            s.npi0_decays, percent(s.npi0_decays), s.decay_shower, percent(s.decay_shower), s.decay_only_p, percent(s.decay_only_p),
                            s.decay_only_pi_plus, percent(s.decay_only_pi_plus), s.decay_only_n, percent(s.decay_only_n),
                            s.decay_only_pi0, percent(s.decay_only_pi0), s.decay_other, percent(s.decay_other));
        text += fmt::format("n+π⁰ decays: {}, with observable γγ: {}", s.npi0_decays, s.npi0_with_observable_gammas);
        if (s.npi0_with_observable_gammas > 0) {
            const double observable = s.npi0_with_observable_gammas;
            text += fmt::format("\n  neutron in any HCAL: {} ({:.2f}%), neutron + both gammas: {} ({:.2f}%), all three in the ZDC: {} ({:.2f}%)\n",
                                s.neutron_in_any_hcal, 100.0 * s.neutron_in_any_hcal / observable,
                                s.neutron_and_both_gammas, 100.0 * s.neutron_and_both_gammas / observable,
                                s.gammas_and_neutron_in_zdc, 100.0 * s.gammas_and_neutron_in_zdc / observable);
            text += fmt::format("  neutron: ZDC {} insert {} LFHCAL {}; gamone: ZDC {} B0 {} EndcapP {}; gamtwo: ZDC {} B0 {} EndcapP {}\n",
                                s.neutron_zdc_hcal, s.neutron_insert_hcal, s.neutron_lfhcal,
                                s.gamma_one_zdc_ecal, s.gamma_one_b0_ecal, s.gamma_one_endcap_p,
                                s.gamma_two_zdc_ecal, s.gamma_two_b0_ecal, s.gamma_two_endcap_p);
            text += fmt::format("  all three detected: {} — neutron: ZDC {} insert {} LFHCAL {}; gamone: ZDC {} B0 {} EndcapP {}; gamtwo: ZDC {} B0 {} EndcapP {}",
                                s.all_three_detected, s.all3_neutron_zdc_hcal, s.all3_neutron_insert_hcal, s.all3_neutron_lfhcal,
                                s.all3_gamma_one_zdc_ecal, s.all3_gamma_one_b0_ecal, s.all3_gamma_one_endcap_p,
                                s.all3_gamma_two_zdc_ecal, s.all3_gamma_two_b0_ecal, s.all3_gamma_two_endcap_p);
        }
        return text;
    }
};
