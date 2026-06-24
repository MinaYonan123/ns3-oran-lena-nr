// SPDX-License-Identifier: GPL-2.0-only

#include "nr-kpi-registry.h"

#include "ns3/log.h"
#include <algorithm>
#include <set>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NrKpiRegistry");

static const std::vector<NrKpiRegistryEntry> g_registry = {
    {"PHY.DlDataSinr", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrUePhy", "DlDataSinr",
     NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.DlCtrlSinr", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrUePhy", "DlCtrlSinr",
     NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.UlSinr", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrGnbPhy", "UlSinrTrace",
     NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.Rsrp", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrUePhy", "ReportRsrp",
     NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.Rsrq", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrUePhy", "ReportUeMeasurements",
     NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.PathLoss", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "SpectrumChannel", "PathLoss",
     NrKpiValueType::DOUBLE, NrKpiScope::GLOBAL, true},
    {"PHY.DlCtrlPathloss", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrSpectrumPhy",
     "DlCtrlPathloss", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.DlDataPathloss", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrSpectrumPhy",
     "DlDataPathloss", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.DlDataSnr", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrSpectrumPhy",
     "DlDataSnrTrace", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.RxPacketTbSize", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrSpectrumPhy",
     "RxPacketTraceUe", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"PHY.Tbler", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrSpectrumPhy", "RxPacketTraceUe",
     NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.CorruptTb", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrSpectrumPhy",
     "RxPacketTraceUe", NrKpiValueType::BOOL, NrKpiScope::UE, true},
    {"PHY.Cqi", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrUePhy", "CqiFeedbackTrace",
     NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"PHY.Ri", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrUePhy", "CqiFeedbackTrace",
     NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"PHY.Mcs", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrUePhy", "CqiFeedbackTrace",
     NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"PHY.PuschTxPower", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrUePowerControl",
     "ReportPuschTxPower", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.PucchTxPower", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrUePowerControl",
     "ReportPucchTxPower", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.SrsTxPower", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrUePowerControl",
     "ReportSrsTxPower", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.PrbUtilizationDl", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrGnbPhy",
     "GetPrbUtilization", NrKpiValueType::DOUBLE, NrKpiScope::CELL, true},
    {"PHY.SlotDataUsedSym", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrGnbPhy",
     "SlotDataStats", NrKpiValueType::UINT64, NrKpiScope::CELL, true},
    {"PHY.SlotCtrlUsedSym", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrGnbPhy",
     "SlotCtrlStats", NrKpiValueType::UINT64, NrKpiScope::CELL, true},
    {"PHY.SlotAvailRb", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrGnbPhy", "SlotDataStats",
     NrKpiValueType::UINT64, NrKpiScope::CELL, true},
    {"PHY.ActivityFactor", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrGnbPhy",
     "CalculateActivityFactor", NrKpiValueType::DOUBLE, NrKpiScope::CELL, true},
    {"PHY.AntennaPortsOn", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrGnbNetDevice",
     "GetPortPower", NrKpiValueType::UINT64, NrKpiScope::CELL, true},
    {"PHY.TxPowerWatts", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrGnbNetDevice",
     "GetAveragePower", NrKpiValueType::DOUBLE, NrKpiScope::CELL, true},
    {"PHY.EnergyConsumptionJ", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrGnbPhy",
     "GetTotalEnergyConsumption", NrKpiValueType::DOUBLE, NrKpiScope::CELL, true},
    {"PHY.InstantaneousPowerW", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrGnbPhy",
     "GetCurrentPowerConsumption", NrKpiValueType::DOUBLE, NrKpiScope::CELL, true},
    {"PHY.RssiPerChunk", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrInterference",
     "RssiPerProcessedChunk", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.SnrPerChunk", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrInterference",
     "SnrPerProcessedChunk", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PHY.PowerSpectralDensity", NrKpiLayer::PHY, NrKpiStatus::IMPLEMENTABLE, "NrUePhy",
     "ReportPowerSpectralDensity", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},

    // ---- MAC ----
    {"MAC.DlTbSize",    NrKpiLayer::MAC, NrKpiStatus::IMPLEMENTABLE, "NrGnbMac",
     "DlScheduling", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"MAC.DlMcs",       NrKpiLayer::MAC, NrKpiStatus::IMPLEMENTABLE, "NrGnbMac",
     "DlScheduling", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"MAC.UlTbSize",    NrKpiLayer::MAC, NrKpiStatus::IMPLEMENTABLE, "NrGnbMac",
     "UlScheduling", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"MAC.UlMcs",       NrKpiLayer::MAC, NrKpiStatus::IMPLEMENTABLE, "NrGnbMac",
     "UlScheduling", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"MAC.HarqFail",    NrKpiLayer::MAC, NrKpiStatus::IMPLEMENTABLE, "NrGnbMac",
     "DlHarqFeedback", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"MAC.SR",          NrKpiLayer::MAC, NrKpiStatus::IMPLEMENTABLE, "NrGnbMac",
     "SrReq", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    // MAC.UEThpDl: UE DL throughput from RLC DL Tx bytes per reporting interval (Mbps)
    {"MAC.UEThpDl",     NrKpiLayer::MAC, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetDlTxData/interval", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},

    // ---- RLC ----
    {"RLC.DlTxBytes",   NrKpiLayer::RLC, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetDlTxData", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"RLC.DlRxBytes",   NrKpiLayer::RLC, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetDlRxData", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"RLC.UlTxBytes",   NrKpiLayer::RLC, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetUlTxData", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"RLC.DlDelay",     NrKpiLayer::RLC, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetDlDelay", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"RLC.UlDelay",     NrKpiLayer::RLC, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetUlDelay", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"RLC.DlTxPackets", NrKpiLayer::RLC, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetDlTxPackets", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"RLC.DlRxPackets", NrKpiLayer::RLC, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetDlRxPackets", NrKpiValueType::UINT64, NrKpiScope::UE, true},

    // ---- PDCP ----
    {"PDCP.DlTxBytes",   NrKpiLayer::PDCP, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetDlTxData", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"PDCP.DlRxBytes",   NrKpiLayer::PDCP, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetDlRxData", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"PDCP.DlDelay",     NrKpiLayer::PDCP, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetDlDelay", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PDCP.UlDelay",     NrKpiLayer::PDCP, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetUlDelay", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},
    {"PDCP.DlTxPackets", NrKpiLayer::PDCP, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetDlTxPackets", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"PDCP.DlRxPackets", NrKpiLayer::PDCP, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetDlRxPackets", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    // DRB.UEThpDl: UE DL throughput computed from PDCP DL Rx bytes per reporting interval (Mbps)
    {"DRB.UEThpDl", NrKpiLayer::PDCP, NrKpiStatus::IMPLEMENTABLE, "NrBearerStatsCalculator",
     "GetDlRxData/interval", NrKpiValueType::DOUBLE, NrKpiScope::UE, true},

    // ---- RRC ----
    {"RRC.ConnEstab",   NrKpiLayer::RRC, NrKpiStatus::IMPLEMENTABLE, "NrUeRrc",
     "ConnectionEstablished", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"RRC.HoSucc",      NrKpiLayer::RRC, NrKpiStatus::IMPLEMENTABLE, "NrUeRrc",
     "HandoverEndOk", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"RRC.RLF",         NrKpiLayer::RRC, NrKpiStatus::IMPLEMENTABLE, "NrUeRrc",
     "RadioLinkFailure", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"RRC.StateChange", NrKpiLayer::RRC, NrKpiStatus::IMPLEMENTABLE, "NrUeRrc",
     "StateTransition", NrKpiValueType::UINT64, NrKpiScope::UE, true},
    {"RRC.ActiveUes",   NrKpiLayer::RRC, NrKpiStatus::IMPLEMENTABLE, "NrGnbRrc",
     "GetUeMap", NrKpiValueType::UINT64, NrKpiScope::CELL, true},
};

const NrKpiRegistryEntry*
NrKpiRegistry::Find(const std::string& kpiName)
{
    for (const auto& entry : g_registry)
    {
        if (entry.kpiName == kpiName)
        {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<const NrKpiRegistryEntry*>
NrKpiRegistry::GetByLayer(NrKpiLayer layer)
{
    std::vector<const NrKpiRegistryEntry*> result;
    for (const auto& entry : g_registry)
    {
        if (entry.layer == layer)
        {
            result.push_back(&entry);
        }
    }
    return result;
}

std::vector<std::string>
NrKpiRegistry::GetAllImplementablePhyKpiNames()
{
    std::vector<std::string> names;
    for (const auto& entry : g_registry)
    {
        if (entry.layer == NrKpiLayer::PHY && entry.validationStatus == NrKpiStatus::IMPLEMENTABLE &&
            entry.phase1Enabled)
        {
            names.push_back(entry.kpiName);
        }
    }
    return names;
}

std::vector<const NrKpiRegistryEntry*>
NrKpiRegistry::ValidatePhySelection(const std::vector<std::string>& requested, bool layerEnabled)
{
    std::vector<const NrKpiRegistryEntry*> resolved;
    if (!layerEnabled || requested.empty())
    {
        return resolved;
    }

    std::set<std::string> seen;
    for (const auto& name : requested)
    {
        if (!seen.insert(name).second)
        {
            continue;
        }
        const auto* entry = Find(name);
        if (!entry)
        {
            NS_FATAL_ERROR("Unknown PHY KPI name: " << name);
        }
        if (entry->layer != NrKpiLayer::PHY)
        {
            NS_FATAL_ERROR("KPI " << name << " is not a PHY-layer metric");
        }
        if (entry->validationStatus != NrKpiStatus::IMPLEMENTABLE)
        {
            NS_FATAL_ERROR("KPI " << name << " is not IMPLEMENTABLE in Phase 1");
        }
        if (!entry->phase1Enabled)
        {
            NS_FATAL_ERROR("KPI " << name << " is not enabled for Phase 1");
        }
        resolved.push_back(entry);
    }
    return resolved;
}

std::vector<const NrKpiRegistryEntry*>
NrKpiRegistry::ValidateSelection(const std::vector<std::string>& requested, bool layerEnabled)
{
    std::vector<const NrKpiRegistryEntry*> resolved;
    if (!layerEnabled || requested.empty())
        return resolved;
    std::set<std::string> seen;
    for (const auto& name : requested)
    {
        if (!seen.insert(name).second)
            continue;
        const auto* entry = Find(name);
        if (!entry)
        {
            NS_LOG_WARN("NrKpiRegistry: unknown KPI '" << name << "' – skipping.");
            continue;
        }
        if (entry->validationStatus != NrKpiStatus::IMPLEMENTABLE)
        {
            NS_LOG_WARN("NrKpiRegistry: KPI '" << name << "' not IMPLEMENTABLE – skipping.");
            continue;
        }
        resolved.push_back(entry);
    }
    return resolved;
}

std::vector<std::string>
NrKpiRegistry::GetAllImplementableKpiNames(NrKpiLayer layer)
{
    std::vector<std::string> names;
    for (const auto& entry : g_registry)
    {
        if (entry.layer == layer && entry.validationStatus == NrKpiStatus::IMPLEMENTABLE &&
            entry.phase1Enabled)
            names.push_back(entry.kpiName);
    }
    return names;
}

} // namespace ns3
