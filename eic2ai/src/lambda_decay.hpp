// lambda_decay.hpp — the Λ decay classification the Λ analysis roles share, defined once.
//
// Used by every role that classifies a Λ decay. Decay types:
//   0 no daughters, 1 p π⁻, 2 n π⁰, 3 shower (more than two daughters),
//   4 only p, 5 only π⁺, 6 only n, 7 only π⁰, 8 other.
// For type 2 the π⁰ photons are taken when Geant4 recorded the π⁰ decay.
// Trap: the lambda_acceptance and comb_candidates macros used a coarser scheme (every single
// daughter 4, every unmatched pair 8); their CSVs produced before 2026-09-30 carry those codes.
#pragma once

#include <edm4hep/MCParticleCollection.h>

#include <optional>

struct LambdaDecay {
    int type = 8;
    std::optional<edm4hep::MCParticle> proton;
    std::optional<edm4hep::MCParticle> pi_minus;
    std::optional<edm4hep::MCParticle> neutron;
    std::optional<edm4hep::MCParticle> pi_zero;
    std::optional<edm4hep::MCParticle> gamma_one;
    std::optional<edm4hep::MCParticle> gamma_two;
};

inline LambdaDecay classify_lambda_decay(const edm4hep::MCParticle& lambda) {
    LambdaDecay decay;
    const auto daughters = lambda.getDaughters();
    if (daughters.size() == 0) {
        decay.type = 0;
    } else if (daughters.size() == 1) {
        switch (daughters.at(0).getPDG()) {
            case 2212: decay.type = 4; break;
            case 211:  decay.type = 5; break;
            case 2112: decay.type = 6; break;
            case 111:  decay.type = 7; break;
            default:   decay.type = 8; break;
        }
    } else if (daughters.size() == 2) {
        const int pdg0 = daughters.at(0).getPDG();
        const int pdg1 = daughters.at(1).getPDG();
        if (pdg0 == 2212 && pdg1 == -211) {
            decay.type = 1;
            decay.proton = daughters.at(0);
            decay.pi_minus = daughters.at(1);
        } else if (pdg1 == 2212 && pdg0 == -211) {
            decay.type = 1;
            decay.proton = daughters.at(1);
            decay.pi_minus = daughters.at(0);
        } else if (pdg0 == 2112 && pdg1 == 111) {
            decay.type = 2;
            decay.neutron = daughters.at(0);
            decay.pi_zero = daughters.at(1);
        } else if (pdg1 == 2112 && pdg0 == 111) {
            decay.type = 2;
            decay.neutron = daughters.at(1);
            decay.pi_zero = daughters.at(0);
        }
    } else {
        decay.type = 3;
    }
    if (decay.neutron && decay.pi_zero) {
        if (decay.pi_zero->getDaughters().size() > 0) decay.gamma_one = decay.pi_zero->getDaughters().at(0);
        if (decay.pi_zero->getDaughters().size() > 1) decay.gamma_two = decay.pi_zero->getDaughters().at(1);
    }
    return decay;
}
