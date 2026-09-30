"""column_renames.py — the unified CSV column vocabulary of eic2ai: what every macro-era column is called now.

Source of truth for acceptance rule 3 (one name per quantity across roles). The writers carry the
new names; this table maps the csv_convert macro headers to them, so:
  - tests/parity.py translates a macro reference header before comparing with an eic2ai output;
  - a consumer loads an old CSV (or a parquet/feather made from one) into the new names with
        df = df.rename(columns=OLD_TO_NEW["trk_hits"])
    (import this file by path; it has no dependencies).
Rows never changed: every rename is a header change alone.

Vocabulary (prefix + quantity): a prefix names the object (prt, trk_hit, cal_hit, clu, rec, cand,
hit, lam, prot, pimin, neut, pizero, gamone, gamtwo, elec, mc_*, ff_lam, ...), the quantity is one of
the names below. tests/check_columns.py enforces the set on every header eic2ai prints.

  evt                                   0-based entry counter, the join key of every role
  index                                 index of the object in its collection
  pdg, gen_status, sim_status, origin   MC particle identity and status; origin per origin.hpp
  mass, energy, charge
  mom_x/y/z, mom, theta, eta, phi       momentum components and derived kinematics
  vtx_time, vtx_pos_x/y/z               production vertex; end_time, end_pos_x/y/z the endpoint
  n_parents, n_daughters, parent_index
  ref_pos_x/y/z, pid_goodness, type     reconstructed particle
  n_clusters, n_tracks, n_particles, n_particle_ids, n_cluster_hits, n_track_measurements, n_tracker_hits
  cov_xx .. cov_tt                      covariance of a reconstructed particle
  collection, cell_id, system_id, system_name, pos_x/y/z, time, pos_err_xx/yy/zz, time_err, edep, edep_err,
  energy_err, path_length               hits and clusters
  decay, decay_pos_x/y/z, is_first      Λ decay code (lambda_decay.hpp), decay vertex, first-Λ flag
"""

MC_BLOCK = {  # the 16-column MC particle block of the Λ analysis roles: old suffix → new suffix
    "id": "index", "pdg": "pdg", "gen": "gen_status", "sim": "sim_status",
    "px": "mom_x", "py": "mom_y", "pz": "mom_z",
    "vx": "vtx_pos_x", "vy": "vtx_pos_y", "vz": "vtx_pos_z",
    "epx": "end_pos_x", "epy": "end_pos_y", "epz": "end_pos_z",
    "time": "vtx_time", "nd": "n_daughters", "np": "n_parents",
}

RECO_BLOCK = {  # reconstructed particle quantities: old suffix → new suffix
    "id": "index", "pdg": "pdg", "charge": "charge", "energy": "energy", "mass": "mass",
    "px": "mom_x", "py": "mom_y", "pz": "mom_z",
    "ref_x": "ref_pos_x", "ref_y": "ref_pos_y", "ref_z": "ref_pos_z",
    "pid_goodness": "pid_goodness", "type": "type",
    "n_clusters": "n_clusters", "n_tracks": "n_tracks", "n_particles": "n_particles", "n_particle_ids": "n_particle_ids",
    "n_cluster_hits": "n_cluster_hits", "n_track_measurements": "n_track_measurements", "n_tracker_hits": "n_tracker_hits",
    "cov_xx": "cov_xx", "cov_xy": "cov_xy", "cov_xz": "cov_xz", "cov_yy": "cov_yy", "cov_yz": "cov_yz", "cov_zz": "cov_zz",
    "cov_xt": "cov_xt", "cov_yt": "cov_yt", "cov_zt": "cov_zt", "cov_tt": "cov_tt",
}


def block(old_prefix, suffixes, new_prefix=None):
    """{old_prefix_oldsuffix: new_prefix_newsuffix} for one particle block."""
    new_prefix = new_prefix or old_prefix
    return {f"{old_prefix}_{old}": f"{new_prefix}_{new}" for old, new in suffixes.items()}


def blocks(prefixes, suffixes=MC_BLOCK):
    table = {}
    for old_prefix in prefixes:
        table.update(block(old_prefix, suffixes))
    return table


LAMBDA_PREFIXES = ("lam", "prot", "pimin", "neut", "pizero", "gamone", "gamtwo")

OLD_TO_NEW = {
    "event_index": {},
    "trk_hits": {"prt_status": "prt_gen_status"},
    "cal_hits": {"prt_status": "prt_gen_status", "rec_collection": "cal_hit_collection"},
    "calo_clusters": {"prt_status": "prt_gen_status"},
    "mc_particles": {},
    "mc_dis": {"event": "evt"},
    "reco_particles": {"event": "evt", **{old: f"rec_{new}" for old, new in RECO_BLOCK.items() if not old.startswith("cov_")}},
    "mcpart_lambda": {"event": "evt", **blocks(LAMBDA_PREFIXES)},
    "reco_dis": {
        "event": "evt",
        **block("elec", {k: v for k, v in RECO_BLOCK.items() if k in ("id", "energy", "px", "py", "pz", "ref_x", "ref_y", "ref_z")}),
        "mc_elec_px": "mc_elec_mom_x", "mc_elec_py": "mc_elec_mom_y", "mc_elec_pz": "mc_elec_mom_z",
        "mc_lam_px": "mc_lam_mom_x", "mc_lam_py": "mc_lam_mom_y", "mc_lam_pz": "mc_lam_mom_z",
        "ff_lam_px": "ff_lam_mom_x", "ff_lam_py": "ff_lam_mom_y", "ff_lam_pz": "ff_lam_mom_z",
        "mc_beam_prot_px": "mc_beam_prot_mom_x", "mc_beam_prot_py": "mc_beam_prot_mom_y", "mc_beam_prot_pz": "mc_beam_prot_mom_z",
        "mc_beam_elec_px": "mc_beam_elec_mom_x", "mc_beam_elec_py": "mc_beam_elec_mom_y", "mc_beam_elec_pz": "mc_beam_elec_mom_z",
    },
    "reco_ff_lambda": {
        "event": "evt",
        **block("lam", RECO_BLOCK), **block("neut", RECO_BLOCK),
        **block("gam1", RECO_BLOCK, "gamone"), **block("gam2", RECO_BLOCK, "gamtwo"),
    },
    "lambda_acceptance": {
        "event": "evt", "decay": "lam_decay",
        "lam_p": "lam_mom", "lam_dvz": "lam_decay_pos_z",
        "prot_p": "prot_mom", "pim_p": "pimin_mom", "pim_theta": "pimin_theta", "pim_eta": "pimin_eta",
        "prot_hits_b0": "prot_n_hits_b0", "prot_hits_rp": "prot_n_hits_rp", "prot_hits_offm": "prot_n_hits_offm",
        "pim_hits_b0": "pimin_n_hits_b0", "pim_hits_rp": "pimin_n_hits_rp", "pim_hits_offm": "pimin_n_hits_offm",
        "pim_cent": "pimin_cent", "pim_b0trk": "pimin_b0trk",
    },
    "comb_candidates": {
        "event": "evt", "coll": "cand_source", "idx": "cand_index", "charge": "cand_charge",
        "px": "cand_mom_x", "py": "cand_mom_y", "pz": "cand_mom_z", "p": "cand_mom",
        "theta": "cand_theta", "eta": "cand_eta", "phi": "cand_phi",
        "ref_x": "cand_ref_pos_x", "ref_y": "cand_ref_pos_y", "ref_z": "cand_ref_pos_z",
        "quality": "cand_quality", "ndf": "cand_ndf", "nmeas": "cand_n_measurements",
        "mc_idx": "mc_index", "mc_gen": "mc_gen_status",
        "is_sul_pim": "is_sul_pimin", "dp_pim": "dp_pimin", "dth_pim": "dth_pimin",
    },
    "comb_candidates_events": {
        "event": "evt", "decay": "lam_decay", "prot_mc_idx": "prot_index", "pim_mc_idx": "pimin_index",
        "lam_p": "lam_mom", "lam_dvx": "lam_decay_pos_x", "lam_dvy": "lam_decay_pos_y", "lam_dvz": "lam_decay_pos_z",
        "prot_p": "prot_mom", "pim_p": "pimin_mom", "pim_theta": "pimin_theta", "pim_phi": "pimin_phi",
    },
    "acceptance_ppim": {"event": "evt", **blocks(("lam", "prot", "pimin"))},
    "acceptance_ppim_prot_hits": {
        "event": "evt", "lam_id": "lam_index", "detector": "hit_collection", "hit_id": "hit_index",
        "x": "hit_pos_x", "y": "hit_pos_y", "z": "hit_pos_z", "eDep": "hit_edep", "time": "hit_time", "pathLength": "hit_path_length",
    },
    "acceptance_npi0": {"event": "evt", **blocks(LAMBDA_PREFIXES)},
    "combinatorics_ppim": {
        "true_prot_id": "true_prot_index", "true_pi_id": "true_pimin_index",
        **block("pi", MC_BLOCK, "pimin"), **block("prot", MC_BLOCK),
        "pi_nhits_b0": "pimin_n_hits_b0", "pi_first_b0_x": "pimin_first_b0_pos_x", "pi_first_b0_y": "pimin_first_b0_pos_y", "pi_first_b0_z": "pimin_first_b0_pos_z",
        "pi_ecal_contrib": "pimin_ecal_contrib", "pi_first_ecal_x": "pimin_first_ecal_pos_x", "pi_first_ecal_y": "pimin_first_ecal_pos_y", "pi_first_ecal_z": "pimin_first_ecal_pos_z",
        "prot_nhits_rp": "prot_n_hits_rp", "prot_first_rp_x": "prot_first_rp_pos_x", "prot_first_rp_y": "prot_first_rp_pos_y", "prot_first_rp_z": "prot_first_rp_pos_z",
    },
}
OLD_TO_NEW["acceptance_ppim_pimin_hits"] = OLD_TO_NEW["acceptance_ppim_prot_hits"]

# The roles whose macro-era CSVs a consumer may still hold, with the intermediate files made from them.
OUTPUTS = tuple(OLD_TO_NEW)


def translate(output, header_fields):
    """The macro-era header of `output` (a list of names) in the new vocabulary."""
    table = OLD_TO_NEW[output]
    return [table.get(name, name) for name in header_fields]


def rename_dataframe(df, output):
    """A pandas frame loaded from a macro-era CSV of `output`, with the new column names."""
    return df.rename(columns=OLD_TO_NEW[output])
