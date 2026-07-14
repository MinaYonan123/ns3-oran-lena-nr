// SPDX-License-Identifier: GPL-2.0-only

#ifndef NR_PHY_KPI_COLLECTOR_H
#define NR_PHY_KPI_COLLECTOR_H

#include "nr-kpi-registry.h"
#include "nr-kpi-sample.h"

#include "ns3/net-device-container.h"
#include "ns3/nr-bearer-stats-calculator.h"
#include "ns3/nr-phy-mac-common.h"
#include "ns3/object.h"
#include <iomanip>
#include <map>
#include <ostream>
#include <set>
#include <string>

namespace ns3
{

class NrHelper;
class NrGnbNetDevice;

/**
 * \brief Multi-layer KPI collector.
 *
 * Handles PHY, MAC, RLC, PDCP, and RRC layers in a single class.
 * Previously named NrPhyKpiCollector; kept in the same file for compatibility.
 * Typedef NrPhyKpiCollector is provided so existing call sites still compile.
 */
class NrKpiCollector : public Object
{
  public:
    static TypeId GetTypeId();
    NrKpiCollector();

    // ---------------------------------------------------------------
    // PHY
    // ---------------------------------------------------------------
    void Configure(const std::vector<const NrKpiRegistryEntry*>& entries);
    void ConnectTraces(Ptr<NrHelper> helper,
                       Ptr<NrGnbNetDevice> gnb,
                       const NetDeviceContainer& ueDevs);
    NrKpiSnapshot CollectPhy(Ptr<NrGnbNetDevice> gnb) const;

    // ---------------------------------------------------------------
    // MAC
    // ---------------------------------------------------------------
    void ConnectMacTraces(Ptr<NrGnbNetDevice> gnb);
    NrKpiSnapshot CollectMac(Ptr<NrGnbNetDevice> gnb) const;

    // ---------------------------------------------------------------
    // RLC
    // ---------------------------------------------------------------
    void SetRlcCalculator(Ptr<NrBearerStatsCalculator> calc);
    NrKpiSnapshot CollectRlc(Ptr<NrGnbNetDevice> gnb) const;

    // ---------------------------------------------------------------
    // PDCP
    // ---------------------------------------------------------------
    void SetPdcpCalculator(Ptr<NrBearerStatsCalculator> calc);
    NrKpiSnapshot CollectPdcp(Ptr<NrGnbNetDevice> gnb) const;

    // ---------------------------------------------------------------
    // RRC
    // ---------------------------------------------------------------
    void ConnectRrcTraces(Ptr<NrGnbNetDevice> gnb, const NetDeviceContainer& ueDevs);
    NrKpiSnapshot CollectRrc(Ptr<NrGnbNetDevice> gnb) const;

    // ---------------------------------------------------------------
    // Collect ALL enabled layers in one snapshot
    // ---------------------------------------------------------------
    NrKpiSnapshot CollectAll(Ptr<NrGnbNetDevice> gnb) const;

    // ---------------------------------------------------------------
    // Shared utilities
    // ---------------------------------------------------------------
    static void PrintSnapshot(const NrKpiSnapshot& snapshot, std::ostream& os);
    bool IsEnabled(const std::string& kpiName) const;

    void RecordUeDouble(const std::string& kpiName, uint64_t imsi, uint16_t rnti, double value);
    void RecordUeUint64(const std::string& kpiName, uint64_t imsi, uint16_t rnti, uint64_t value);
    void RecordUeBool(const std::string& kpiName, uint64_t imsi, uint16_t rnti, bool value);
    void RecordCellDouble(const std::string& kpiName, uint16_t cellId, double value);
    void RecordCellUint64(const std::string& kpiName, uint16_t cellId, uint64_t value);
    void RecordGlobalDouble(const std::string& kpiName, double value);
    void IncrementUeCounter(const std::string& kpiName, uint64_t imsi, uint16_t rnti);
    void IncrementCellCounter(const std::string& kpiName, uint16_t cellId);
    void SetReportingPeriod(double periodSeconds) { m_reportingPeriodSeconds = periodSeconds; }

  private:
    std::set<std::string> m_enabledKpis;
    // PHY buffers (latest value, overwritten per trace event)
    std::map<uint64_t, std::map<std::string, double>> m_ueDouble;
    std::map<uint64_t, std::map<std::string, uint64_t>> m_ueUint64;
    std::map<uint64_t, std::map<std::string, bool>> m_ueBool;
    std::map<std::string, double> m_cellDouble;
    std::map<std::string, uint64_t> m_cellUint64;
    std::map<std::string, double> m_globalDouble;
    std::map<uint64_t, uint16_t> m_ueRnti;

    // MAC/RRC event counters (accumulate between Collect calls)
    std::map<uint64_t, std::map<std::string, uint64_t>> m_ueCounter;
    std::map<std::string, uint64_t>                     m_cellCounter;

    Ptr<NrBearerStatsCalculator> m_rlcCalc;
    Ptr<NrBearerStatsCalculator> m_pdcpCalc;

    // Reporting period in seconds (used for per-interval delta and throughput calculation)
    double m_reportingPeriodSeconds{0.1};

    // Previous cumulative totals per IMSI for delta computation (mutable: Collect methods are const)
    mutable std::map<uint64_t, uint64_t> m_prevRlcDlTx;
    mutable std::map<uint64_t, uint64_t> m_prevRlcDlRx;
    mutable std::map<uint64_t, uint64_t> m_prevRlcUlTx;
    mutable std::map<uint64_t, uint64_t> m_prevRlcDlPkts;
    mutable std::map<uint64_t, uint64_t> m_prevRlcDlRxPkts;

    mutable std::map<uint64_t, uint64_t> m_prevPdcpDlTx;
    mutable std::map<uint64_t, uint64_t> m_prevPdcpDlRx;
    mutable std::map<uint64_t, uint64_t> m_prevPdcpDlPkts;
    mutable std::map<uint64_t, uint64_t> m_prevPdcpDlRxPkts;
};

// Backward-compat alias so existing code compiles without changes
using NrPhyKpiCollector = NrKpiCollector;

} // namespace ns3

#endif /* NR_PHY_KPI_COLLECTOR_H */
