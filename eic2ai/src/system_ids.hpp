// system_ids.hpp — detector system id and name from a cellID: one table for every writer.
//
// The system id is the least significant byte of the cellID (the "system" field of the ePIC
// readout encoding); the names come from the detector's definitions.xml. Tracker systems live
// below 100, calorimeters in the 100-band, far-forward and far-backward from 150 up.
//
// An id missing from the table yields the name "Unknown" and the run continues (user decision
// 2026-09-29, QUESTIONS.md Q5); Writer::system_of records such ids for the run summary.
// Trap: extending the table changes the *_system_name column of every CSV that sees the new id.
#pragma once

#include <cstdint>
#include <map>
#include <string>

struct SystemInfo {
    uint64_t id = 0;      // cellID & 0xFF
    std::string name;     // detector name, or "Unknown"
    bool known = false;   // false when the id is not in the table
};

inline const std::map<uint64_t, std::string>& system_names_by_id() {
    static const std::map<uint64_t, std::string> table = {
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
        {199, "TaggerCalorimeter"},
    };
    return table;
}

inline SystemInfo system_of_cell(uint64_t cell_id) {
    SystemInfo info;
    info.id = cell_id & 0xFF;
    const auto& table = system_names_by_id();
    const auto found = table.find(info.id);
    if (found != table.end()) {
        info.name = found->second;
        info.known = true;
    } else {
        info.name = "Unknown";
    }
    return info;
}
