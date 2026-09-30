// mc_particle_block.hpp — the 16-column MC particle block of the Λ analysis roles, defined once.
//
// Used by mcpart_lambda, acceptance_ppim, acceptance_npi0 and combinatorics_ppim, which write
// one such block per particle role (lam, prot, pimin, ...). Columns: <prefix>_index, _pdg,
// _gen_status, _sim_status, _mom_x, _mom_y, _mom_z, _vtx_pos_x, _vtx_pos_y, _vtx_pos_z,
// _end_pos_x, _end_pos_y, _end_pos_z, _vtx_time, _n_daughters, _n_parents: the prt_* vocabulary
// of the hit roles. A particle that does not exist yields 16 empty fields.
// Trap: the macros wrote short names (_id, _gen, _px, _vx, _epx, _time, _nd, _np); the mapping
// lives in column_renames.py and the rows are unchanged.
#pragma once

#include <edm4hep/MCParticleCollection.h>
#include <fmt/core.h>

#include <optional>
#include <string>

inline std::string mc_particle_block_header(const std::string& prefix) {
    return fmt::format("{0}_index,{0}_pdg,{0}_gen_status,{0}_sim_status,{0}_mom_x,{0}_mom_y,{0}_mom_z,"
                       "{0}_vtx_pos_x,{0}_vtx_pos_y,{0}_vtx_pos_z,{0}_end_pos_x,{0}_end_pos_y,{0}_end_pos_z,"
                       "{0}_vtx_time,{0}_n_daughters,{0}_n_parents", prefix);
}

inline std::string mc_particle_block(const std::optional<edm4hep::MCParticle>& particle) {
    if (!particle) return ",,,,,,,,,,,,,,,";
    const auto momentum = particle->getMomentum();
    const auto vertex = particle->getVertex();
    const auto endpoint = particle->getEndpoint();
    return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
        particle->getObjectID().index, particle->getPDG(), particle->getGeneratorStatus(), particle->getSimulatorStatus(),
        momentum.x, momentum.y, momentum.z, vertex.x, vertex.y, vertex.z, endpoint.x, endpoint.y, endpoint.z,
        particle->getTime(), particle->getDaughters().size(), particle->getParents().size());
}
