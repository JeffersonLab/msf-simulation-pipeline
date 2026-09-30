// writer_McDisWriter.hpp — CSV role "mc_dis": one row per event with the generator's DIS kinematics.
//
// Needs: reco. Collections read: none; the values are frame parameters named "dis_<name>"
// (strings the generator wrote into the event header, carried through npsim and eicrecon).
// Columns: event, then one column per parameter, in the order of the table below.
//
// Port of csv_convert/edm4eic_mc_dis.cxx; rows are byte-identical to the macro's: a missing
// parameter yields an empty field, and every event yields a row. The header follows
// column_renames.py (event → evt); the parameter columns keep the generator's names.
#pragma once

#include "Writer.hpp"

#include <fmt/core.h>

class McDisWriter : public Writer {
public:
    std::string role() const override { return "mc_dis"; }
    Needs needs() const override { return Needs{.reco = true, .sim = false}; }
    std::vector<std::string> collections() const override { return {}; }

    std::string csv_header() const override {
        std::string header = "evt";
        for (const char* name : parameter_names()) header += fmt::format(",{}", name);
        return header;
    }

private:
    /// Parameter names without the "dis_" prefix; the CSV columns carry these names in this order.
    static const std::vector<const char*>& parameter_names() {
        static const std::vector<const char*> names = {
            "alphas", "mx2", "nu", "p_rt", "pdrest", "pperps", "pperpz", "q2", "s_e", "s_q",
            "tempvar", "tprime", "tspectator", "twopdotk", "twopdotq", "w", "x_d", "xbj", "y_d", "yplus",
        };
        return names;
    }

    void write_event(const EventInputs& inputs) override {
        std::string line = fmt::format("{}", inputs.entry_index);
        for (const char* name : parameter_names()) {
            const std::string key = fmt::format("dis_{}", name);
            line += fmt::format(",{}", inputs.reco->getParameter<std::string>(key).value_or(""));
        }
        write_row(line);
    }
};
