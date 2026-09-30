// origin.hpp — the signal / background origin of an MCParticle: one definition for every writer.
//
// The background merger adds a status offset at the generator level, before Geant4 runs, so
// only generator particles carry the band in generatorStatus (1 or 2 signal, >= 1000
// background). Geant4 secondaries have generatorStatus 0 whatever their ancestry; for them
// the first generator ancestor up the parent chain decides.
//
// prt_origin values written to every CSV:
//   0 unknown, 1 signal, 2 Geant4 secondary of a signal particle,
//   3 background, 4 Geant4 secondary of a background particle.
//
// Trap: the walk follows getParents(0) alone and stops after 200 levels or at a parent that is
// not available (its collection was not read). Changing either changes prt_origin in every CSV;
// the parity gate against the macros catches it.
#pragma once

#include <edm4hep/MCParticleCollection.h>

#include <cstdint>

/// Band of a generator-level status: 1 signal (status 1 or 2), 3 background (status >= 1000), 0 unknown.
inline int32_t classify_gen_status(int32_t gen_status) {
    if (gen_status == 1 || gen_status == 2) return 1;
    if (gen_status >= 1000) return 3;
    return 0;
}

/// 0 unknown, 1 signal, 2 g4-from-signal, 3 background, 4 g4-from-background.
inline int32_t get_origin_status(const edm4hep::MCParticle& particle) {
    const int32_t gen_status = particle.getGeneratorStatus();
    if (gen_status != 0) {
        return classify_gen_status(gen_status);
    }

    // Geant4 secondary: the first generator ancestor names the band. The depth guard protects
    // against cyclic or broken parent links.
    edm4hep::MCParticle current = particle;
    const int max_depth = 200;
    for (int depth = 0; depth < max_depth; ++depth) {
        if (current.parents_size() == 0) break;
        edm4hep::MCParticle parent = current.getParents(0);
        if (!parent.isAvailable()) break;

        const int32_t parent_status = parent.getGeneratorStatus();
        if (parent_status != 0) {
            const int32_t band = classify_gen_status(parent_status);
            if (band == 1) return 2;
            if (band == 3) return 4;
            return 0;
        }
        current = parent;
    }
    return 0;
}
