// SPDX-License-Identifier: GPL-2.0-only

#include "nr-phy-kpi-collector.h"

#include "nr-bearer-stats-calculator.h"
#include "nr-helper.h"

#include "ns3/config.h"
#include "ns3/log.h"
#include "ns3/mobility-model.h"
#include "ns3/nr-gnb-mac.h"
#include "ns3/nr-gnb-net-device.h"
#include "ns3/nr-gnb-phy.h"
#include "ns3/nr-gnb-rrc.h"
#include "ns3/nr-spectrum-phy.h"
#include "ns3/nr-ue-net-device.h"
#include "ns3/nr-ue-phy.h"
#include "ns3/nr-ue-power-control.h"
#include "ns3/nr-ue-rrc.h"
#include "nr-epc-helper.h"
#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NrKpiCollector");
NS_OBJECT_ENSURE_REGISTERED(NrKpiCollector);

TypeId
NrKpiCollector::GetTypeId()
{
    static TypeId tid = TypeId("ns3::NrKpiCollector").SetParent<Object>();
    return tid;
}

NrKpiCollector::NrKpiCollector()
{
}

void
NrKpiCollector::Configure(const std::vector<const NrKpiRegistryEntry*>& entries)
{
    m_enabledKpis.clear();
    for (const auto* entry : entries)
    {
        m_enabledKpis.insert(entry->kpiName);
    }
}

bool
NrKpiCollector::IsEnabled(const std::string& kpiName) const
{
    return m_enabledKpis.count(kpiName) > 0;
}

void
NrKpiCollector::RecordUeDouble(const std::string& kpiName,
                                  uint64_t imsi,
                                  uint16_t rnti,
                                  double value)
{
    if (!IsEnabled(kpiName))
    {
        return;
    }
    m_ueDouble[imsi][kpiName] = value;
    m_ueRnti[imsi] = rnti;
}

void
NrKpiCollector::RecordUeUint64(const std::string& kpiName,
                                  uint64_t imsi,
                                  uint16_t rnti,
                                  uint64_t value)
{
    if (!IsEnabled(kpiName))
    {
        return;
    }
    m_ueUint64[imsi][kpiName] = value;
    m_ueRnti[imsi] = rnti;
}

void
NrKpiCollector::RecordUeBool(const std::string& kpiName,
                                uint64_t imsi,
                                uint16_t rnti,
                                bool value)
{
    if (!IsEnabled(kpiName))
    {
        return;
    }
    m_ueBool[imsi][kpiName] = value;
    m_ueRnti[imsi] = rnti;
}

void
NrKpiCollector::RecordCellDouble(const std::string& kpiName, uint16_t cellId, double value)
{
    if (!IsEnabled(kpiName))
    {
        return;
    }
    m_cellDouble[kpiName] = value;
}

void
NrKpiCollector::RecordCellUint64(const std::string& kpiName, uint16_t cellId, uint64_t value)
{
    if (!IsEnabled(kpiName))
    {
        return;
    }
    m_cellUint64[kpiName] = value;
}

void
NrKpiCollector::RecordGlobalDouble(const std::string& kpiName, double value)
{
    if (!IsEnabled(kpiName))
    {
        return;
    }
    m_globalDouble[kpiName] = value;
}

namespace
{

uint64_t
LookupImsi(Ptr<NrGnbNetDevice> gnb, uint16_t rnti)
{
    if (!gnb || !gnb->GetRrc())
    {
        return 0;
    }
    for (const auto& ue : gnb->GetRrc()->GetUeMap())
    {
        if (ue.second->GetRnti() == rnti)
        {
            return ue.second->GetImsi();
        }
    }
    return 0;
}

void
DlDataSinrCb(Ptr<NrKpiCollector> collector,
             Ptr<NrGnbNetDevice> gnb,
             uint16_t cellId,
             uint16_t rnti,
             double sinr,
             uint16_t bwpId)
{
    uint64_t imsi = LookupImsi(gnb, rnti);
    double sinrDb = 10.0 * std::log10(std::max(sinr, 1e-30));
    collector->RecordUeDouble("PHY.DlDataSinr", imsi, rnti, sinrDb);
}

void
DlCtrlSinrCb(Ptr<NrKpiCollector> collector,
             Ptr<NrGnbNetDevice> gnb,
             uint16_t cellId,
             uint16_t rnti,
             double sinr,
             uint16_t bwpId)
{
    uint64_t imsi = LookupImsi(gnb, rnti);
    double sinrDb = 10.0 * std::log10(std::max(sinr, 1e-30));
    collector->RecordUeDouble("PHY.DlCtrlSinr", imsi, rnti, sinrDb);
}

void
ReportRsrpCb(Ptr<NrKpiCollector> collector,
             uint16_t cellId,
             uint16_t imsi16,
             uint16_t rnti,
             double rsrp,
             uint8_t bwpId)
{
    collector->RecordUeDouble("PHY.Rsrp", static_cast<uint64_t>(imsi16), rnti, rsrp);
}

void
ReportUeMeasurementsCb(Ptr<NrKpiCollector> collector,
                       Ptr<NrGnbNetDevice> gnb,
                       uint16_t rnti,
                       uint16_t cellId,
                       double rsrp,
                       double rsrq,
                       bool servingCell,
                       uint8_t bwpId)
{
    uint64_t imsi = LookupImsi(gnb, rnti);
    collector->RecordUeDouble("PHY.Rsrq", imsi, rnti, rsrq);
}

void
CqiFeedbackCb(Ptr<NrKpiCollector> collector,
              Ptr<NrGnbNetDevice> gnb,
              uint16_t rnti,
              uint8_t cqi,
              uint8_t mcs,
              uint8_t ri)
{
    uint64_t imsi = LookupImsi(gnb, rnti);
    collector->RecordUeUint64("PHY.Cqi", imsi, rnti, cqi);
    collector->RecordUeUint64("PHY.Mcs", imsi, rnti, mcs);
    collector->RecordUeUint64("PHY.Ri", imsi, rnti, ri);
}

void
RxPacketTraceCb(Ptr<NrKpiCollector> collector,
                Ptr<NrGnbNetDevice> gnb,
                RxPacketTraceParams params)
{
    uint64_t imsi = LookupImsi(gnb, params.m_rnti);
    collector->RecordUeUint64("PHY.RxPacketTbSize", imsi, params.m_rnti, params.m_tbSize);
    collector->RecordUeDouble("PHY.Tbler", imsi, params.m_rnti, params.m_tbler);
    collector->RecordUeBool("PHY.CorruptTb", imsi, params.m_rnti, params.m_corrupt);
}


void DlDataSnrCb(Ptr<NrKpiCollector> collector,
    const SfnSf sfn,        // by value, NOT reference
    const uint16_t cellId,
    const uint8_t bwpId,
    const uint64_t imsi,
    const double snr)
{
    collector->RecordUeDouble("PHY.DlDataSnr", imsi, 0, snr);
}

void
PathLossCb(Ptr<NrKpiCollector> collector,
           Ptr<const MobilityModel> /* txMob */,
           Ptr<const MobilityModel> /* rxMob */,
           double lossDb)
{
    collector->RecordGlobalDouble("PHY.PathLoss", lossDb);
}

void
DlCtrlPathlossCb(Ptr<NrKpiCollector> collector,
                 Ptr<NrGnbNetDevice> gnb,
                 uint16_t cellId,
                 uint8_t bwpId,
                 uint32_t rnti,
                 double pathloss)
{
    uint64_t imsi = LookupImsi(gnb, static_cast<uint16_t>(rnti));
    collector->RecordUeDouble("PHY.DlCtrlPathloss", imsi, static_cast<uint16_t>(rnti), pathloss);
}

void
DlDataPathlossCb(Ptr<NrKpiCollector> collector,
                 Ptr<NrGnbNetDevice> gnb,
                 uint16_t cellId,
                 uint8_t bwpId,
                 uint32_t rnti,
                 double pathloss)
{
    uint64_t imsi = LookupImsi(gnb, static_cast<uint16_t>(rnti));
    collector->RecordUeDouble("PHY.DlDataPathloss", imsi, static_cast<uint16_t>(rnti), pathloss);
}

void
UlSinrCb(Ptr<NrKpiCollector> collector,
         Ptr<NrGnbNetDevice> gnb,
         uint64_t imsi,
         SpectrumValue& sinr,
         SpectrumValue& power)
{
    double sum = 0.0;
    uint32_t n = 0;
    for (auto it = sinr.ConstValuesBegin(); it != sinr.ConstValuesEnd(); ++it)
    {
        sum += *it;
        ++n;
    }
    if (n > 0)
    {
        double avg = sum / n;
        collector->RecordUeDouble("PHY.UlSinr", imsi, 0, 10.0 * std::log10(std::max(avg, 1e-30)));
    }
}

void
SlotDataStatsCb(Ptr<NrKpiCollector> collector,
                const SfnSf& sfn,
                uint32_t activeUe,
                uint32_t dataReg,
                uint32_t dataSym,
                uint32_t availRb,
                uint32_t unusedSym,
                uint16_t bwpId,
                uint16_t cellId)
{
    collector->RecordCellUint64("PHY.SlotDataUsedSym", cellId, dataSym);
    collector->RecordCellUint64("PHY.SlotAvailRb", cellId, availRb);
}

void
SlotCtrlStatsCb(Ptr<NrKpiCollector> collector,
                const SfnSf& sfn,
                uint32_t activeUe,
                uint32_t ctrlReg,
                uint32_t ctrlSym,
                uint32_t availRb,
                uint32_t unusedSym,
                uint16_t bwpId,
                uint16_t cellId)
{
    collector->RecordCellUint64("PHY.SlotCtrlUsedSym", cellId, ctrlSym);
}

void
PuschPowerCb(Ptr<NrKpiCollector> collector, Ptr<NrGnbNetDevice> gnb, double power)
{
    // Power control trace has no IMSI in context; attach to UE via Config path if needed later.
    collector->RecordGlobalDouble("PHY.PuschTxPower", power);
}

void
PucchPowerCb(Ptr<NrKpiCollector> collector, double power)
{
    collector->RecordGlobalDouble("PHY.PucchTxPower", power);
}

void
SrsPowerCb(Ptr<NrKpiCollector> collector, double power)
{
    collector->RecordGlobalDouble("PHY.SrsTxPower", power);
}

void
RssiChunkCb(Ptr<NrKpiCollector> collector, double rssi)
{
    collector->RecordGlobalDouble("PHY.RssiPerChunk", rssi);
}

void
SnrChunkCb(Ptr<NrKpiCollector> collector, double snr)
{
    collector->RecordGlobalDouble("PHY.SnrPerChunk", snr);
}

void
PsdCb(Ptr<NrKpiCollector> collector,
      const SfnSf& sfn,
      uint16_t nodeId,
      uint16_t rnti,
      uint16_t bwpId,
      double power,
      const SpectrumValue& psd)
{
    collector->RecordGlobalDouble("PHY.PowerSpectralDensity", power);
}

} // namespace

void
NrKpiCollector::ConnectTraces(Ptr<NrHelper> helper,
                                 Ptr<NrGnbNetDevice> gnb,
                                 const NetDeviceContainer& ueDevs)
{
    NS_LOG_FUNCTION(this);

    if (IsEnabled("PHY.DlDataSinr"))
    {
        Config::ConnectWithoutContext(
            "/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/NrUePhy/DlDataSinr",
            MakeBoundCallback(&DlDataSinrCb, this, gnb));
    }
    if (IsEnabled("PHY.DlCtrlSinr"))
    {
        Config::ConnectWithoutContext(
            "/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/NrUePhy/DlCtrlSinr",
            MakeBoundCallback(&DlCtrlSinrCb, this, gnb));
    }
    if (IsEnabled("PHY.Rsrp"))
    {
        Config::ConnectWithoutContext(
            "/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/NrUePhy/ReportRsrp",
            MakeBoundCallback(&ReportRsrpCb, this));
    }
    if (IsEnabled("PHY.Rsrq"))
    {
        Config::ConnectWithoutContext(
            "/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/NrUePhy/ReportUeMeasurements",
            MakeBoundCallback(&ReportUeMeasurementsCb, this, gnb));
    }
    if (IsEnabled("PHY.Cqi") || IsEnabled("PHY.Mcs") || IsEnabled("PHY.Ri"))
    {
        Config::ConnectWithoutContext(
            "/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/NrUePhy/CqiFeedbackTrace",
            MakeBoundCallback(&CqiFeedbackCb, this, gnb));
    }
    if (IsEnabled("PHY.RxPacketTbSize") || IsEnabled("PHY.Tbler") || IsEnabled("PHY.CorruptTb"))
    {
        Config::ConnectWithoutContext(
            "/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/NrUePhy/SpectrumPhy/RxPacketTraceUe",
            MakeBoundCallback(&RxPacketTraceCb, this, gnb));
    }
    if (IsEnabled("PHY.DlDataSnr"))
    {
        Config::ConnectWithoutContext(
            "/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/NrUePhy/SpectrumPhy/DlDataSnrTrace",
            MakeBoundCallback(&DlDataSnrCb, this));
    }
    if (IsEnabled("PHY.PathLoss"))
    {
        Config::ConnectWithoutContext("/ChannelList/*/$ns3::SpectrumChannel/PathLoss",
                                      MakeBoundCallback(&PathLossCb, this));
    }
    if (IsEnabled("PHY.DlCtrlPathloss") || IsEnabled("PHY.DlDataPathloss"))
    {
        helper->EnableDlCtrlPathlossTraces(const_cast<NetDeviceContainer&>(ueDevs));
        helper->EnableDlDataPathlossTraces(const_cast<NetDeviceContainer&>(ueDevs));
        if (IsEnabled("PHY.DlCtrlPathloss"))
        {
            Config::ConnectWithoutContext(
                "/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/NrUePhy/SpectrumPhy/"
                "DlCtrlPathloss",
                MakeBoundCallback(&DlCtrlPathlossCb, this, gnb));
        }
        if (IsEnabled("PHY.DlDataPathloss"))
        {
            Config::ConnectWithoutContext(
                "/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/NrUePhy/SpectrumPhy/"
                "DlDataPathloss",
                MakeBoundCallback(&DlDataPathlossCb, this, gnb));
        }
    }
    if (IsEnabled("PHY.UlSinr"))
    {
        Config::ConnectWithoutContext(
            "/NodeList/*/DeviceList/*/BandwidthPartMap/*/NrGnbPhy/UlSinrTrace",
            MakeBoundCallback(&UlSinrCb, this, gnb));
    }
    if (IsEnabled("PHY.SlotDataUsedSym") || IsEnabled("PHY.SlotAvailRb"))
    {
        for (uint32_t b = 0; b < gnb->GetCcMapSize(); ++b)
        {
            Ptr<NrGnbPhy> phy = gnb->GetPhy(b);
            phy->TraceConnectWithoutContext("SlotDataStats",
                                            MakeBoundCallback(&SlotDataStatsCb, this));
        }
    }
    if (IsEnabled("PHY.SlotCtrlUsedSym"))
    {
        for (uint32_t b = 0; b < gnb->GetCcMapSize(); ++b)
        {
            Ptr<NrGnbPhy> phy = gnb->GetPhy(b);
            phy->TraceConnectWithoutContext("SlotCtrlStats",
                                            MakeBoundCallback(&SlotCtrlStatsCb, this));
        }
    }
    if (IsEnabled("PHY.PuschTxPower") || IsEnabled("PHY.PucchTxPower") || IsEnabled("PHY.SrsTxPower"))
    {
        helper->SetUePhyAttribute("EnableUplinkPowerControl", BooleanValue(true));
        for (uint32_t u = 0; u < ueDevs.GetN(); ++u)
        {
            Ptr<NrUeNetDevice> ueDev = ueDevs.Get(u)->GetObject<NrUeNetDevice>();
            for (uint32_t b = 0; b < ueDev->GetCcMapSize(); ++b)
            {
                Ptr<NrUePowerControl> upc = ueDev->GetPhy(b)->GetUplinkPowerControl();
                if (!upc)
                {
                    continue;
                }
                if (IsEnabled("PHY.PuschTxPower"))
                {
                    upc->TraceConnectWithoutContext("ReportPuschTxPower",
                                                    MakeBoundCallback(&PuschPowerCb, this, gnb));
                }
                if (IsEnabled("PHY.PucchTxPower"))
                {
                    upc->TraceConnectWithoutContext("ReportPucchTxPower",
                                                    MakeBoundCallback(&PucchPowerCb, this));
                }
                if (IsEnabled("PHY.SrsTxPower"))
                {
                    upc->TraceConnectWithoutContext("ReportSrsTxPower",
                                                    MakeBoundCallback(&SrsPowerCb, this));
                }
            }
        }
    }
    if (IsEnabled("PHY.RssiPerChunk") || IsEnabled("PHY.SnrPerChunk"))
    {
        for (uint32_t u = 0; u < ueDevs.GetN(); ++u)
        {
            Ptr<NrUeNetDevice> ueDev = ueDevs.Get(u)->GetObject<NrUeNetDevice>();
            for (uint32_t b = 0; b < ueDev->GetCcMapSize(); ++b)
            {
                Ptr<NrSpectrumPhy> sp = ueDev->GetPhy(b)->GetSpectrumPhy();
                if (IsEnabled("PHY.RssiPerChunk"))
                {
                    sp->GetNrInterference()->TraceConnectWithoutContext(
                        "RssiPerProcessedChunk",
                        MakeBoundCallback(&RssiChunkCb, this));
                }
                if (IsEnabled("PHY.SnrPerChunk"))
                {
                    sp->GetNrInterference()->TraceConnectWithoutContext(
                        "SnrPerProcessedChunk",
                        MakeBoundCallback(&SnrChunkCb, this));
                }
            }
        }
    }
    if (IsEnabled("PHY.PowerSpectralDensity"))
    {
        Config::ConnectWithoutContext(
            "/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/NrUePhy/ReportPowerSpectralDensity",
            MakeBoundCallback(&PsdCb, this));
    }
}

NrKpiSnapshot
NrKpiCollector::CollectPhy(Ptr<NrGnbNetDevice> gnb) const
{
    NrKpiSnapshot snapshot;
    snapshot.collectionTime = Simulator::Now();
    const uint16_t cellId = gnb->GetCellId();

    auto addSample = [&](const NrKpiRegistryEntry* entry,
                         uint64_t imsi,
                         uint16_t rnti,
                         double dVal,
                         uint64_t uVal,
                         bool bVal) {
        if (!entry)
        {
            return;
        }
        NrKpiSample sample;
        sample.kpiName = entry->kpiName;
        sample.layer = NrKpiLayer::PHY;
        sample.scope = entry->scope;
        sample.timestamp = snapshot.collectionTime;
        sample.imsi = imsi;
        sample.rnti = rnti;
        sample.cellId = cellId;
        sample.type = entry->valueType;
        if (entry->valueType == NrKpiValueType::DOUBLE)
        {
            sample.doubleValue = dVal;
        }
        else if (entry->valueType == NrKpiValueType::UINT64)
        {
            sample.uint64Value = uVal;
        }
        else if (entry->valueType == NrKpiValueType::BOOL)
        {
            sample.boolValue = bVal;
        }
        snapshot.samples.push_back(sample);
    };

    for (const auto& kv : m_ueDouble)
    {
        for (const auto& metric : kv.second)
        {
            addSample(NrKpiRegistry::Find(metric.first),
                      kv.first,
                      m_ueRnti.count(kv.first) ? m_ueRnti.at(kv.first) : 0,
                      metric.second,
                      0,
                      false);
        }
    }
    for (const auto& kv : m_ueUint64)
    {
        for (const auto& metric : kv.second)
        {
            addSample(NrKpiRegistry::Find(metric.first),
                      kv.first,
                      m_ueRnti.count(kv.first) ? m_ueRnti.at(kv.first) : 0,
                      0.0,
                      metric.second,
                      false);
        }
    }
    for (const auto& kv : m_ueBool)
    {
        for (const auto& metric : kv.second)
        {
            addSample(NrKpiRegistry::Find(metric.first),
                      kv.first,
                      m_ueRnti.count(kv.first) ? m_ueRnti.at(kv.first) : 0,
                      0.0,
                      0,
                      metric.second);
        }
    }

    if (IsEnabled("PHY.PrbUtilizationDl"))
    {
        double util = 0.0;
        for (uint32_t i = 0; i < gnb->GetCcMapSize(); ++i)
        {
            util += gnb->GetPhy(i)->GetPrbUtilization();
        }
        if (gnb->GetCcMapSize() > 0)
        {
            util /= gnb->GetCcMapSize();
        }
        addSample(NrKpiRegistry::Find("PHY.PrbUtilizationDl"), 0, 0, util, 0, false);
    }
    if (IsEnabled("PHY.ActivityFactor"))
    {
        double af = 0.0;
        for (uint32_t i = 0; i < gnb->GetCcMapSize(); ++i)
        {
            af += gnb->GetPhy(i)->CalculateActivityFactor();
        }
        if (gnb->GetCcMapSize() > 0)
        {
            af /= gnb->GetCcMapSize();
        }
        addSample(NrKpiRegistry::Find("PHY.ActivityFactor"), 0, 0, af, 0, false);
    }
    if (IsEnabled("PHY.TxPowerWatts"))
    {
        addSample(NrKpiRegistry::Find("PHY.TxPowerWatts"), 0, 0, gnb->GetAveragePower(), 0, false);
    }
    if (IsEnabled("PHY.AntennaPortsOn"))
    {
        uint64_t portsOn = 0;
        for (double p : gnb->GetPortPower())
        {
            if (p >= 0.5)
            {
                ++portsOn;
            }
        }
        addSample(NrKpiRegistry::Find("PHY.AntennaPortsOn"), 0, 0, 0.0, portsOn, false);
    }
    if (IsEnabled("PHY.EnergyConsumptionJ"))
    {
        double energy = 0.0;
        for (uint32_t i = 0; i < gnb->GetCcMapSize(); ++i)
        {
            energy += gnb->GetPhy(i)->GetTotalEnergyConsumption();
        }
        addSample(NrKpiRegistry::Find("PHY.EnergyConsumptionJ"), 0, 0, energy, 0, false);
    }
    if (IsEnabled("PHY.InstantaneousPowerW"))
    {
        double power = 0.0;
        for (uint32_t i = 0; i < gnb->GetCcMapSize(); ++i)
        {
            power += gnb->GetPhy(i)->GetCurrentPowerConsumption();
        }
        addSample(NrKpiRegistry::Find("PHY.InstantaneousPowerW"), 0, 0, power, 0, false);
    }

    for (const auto& metric : m_cellUint64)
    {
        addSample(NrKpiRegistry::Find(metric.first), 0, 0, 0.0, metric.second, false);
    }
    for (const auto& metric : m_cellDouble)
    {
        addSample(NrKpiRegistry::Find(metric.first), 0, 0, metric.second, 0, false);
    }
    for (const auto& metric : m_globalDouble)
    {
        addSample(NrKpiRegistry::Find(metric.first), 0, 0, metric.second, 0, false);
    }

    return snapshot;
}

void
NrKpiCollector::PrintSnapshot(const NrKpiSnapshot& snapshot, std::ostream& os)
{
    for (const auto& s : snapshot.samples)
    {
        os << "[KPI] t=" << std::fixed << std::setprecision(3)
           << snapshot.collectionTime.GetSeconds() << "s"
           << " name=" << s.kpiName
           << " cell=" << s.cellId
           << " imsi=" << s.imsi
           << " rnti=" << s.rnti
           << " val=";
        if (s.type == NrKpiValueType::DOUBLE)
            os << s.doubleValue;
        else if (s.type == NrKpiValueType::UINT64)
            os << s.uint64Value;
        else
            os << (s.boolValue ? "true" : "false");
        os << "\n";
    }
}

// ===================================================================
// Increment helpers (used by MAC / RRC counter callbacks)
// ===================================================================

void
NrKpiCollector::IncrementUeCounter(const std::string& kpiName, uint64_t imsi, uint16_t rnti)
{
    if (!IsEnabled(kpiName))
        return;
    ++m_ueCounter[imsi][kpiName];
    m_ueRnti[imsi] = rnti;
}

void
NrKpiCollector::IncrementCellCounter(const std::string& kpiName, uint16_t /*cellId*/)
{
    if (!IsEnabled(kpiName))
        return;
    ++m_cellCounter[kpiName];
}

// ===================================================================
// MAC layer
// ===================================================================

namespace
{

void
MacDlSchedCb(Ptr<NrKpiCollector> col, Ptr<NrGnbNetDevice> gnb, NrSchedulingCallbackInfo info)
{
    uint64_t imsi = LookupImsi(gnb, info.m_rnti);
    col->RecordUeUint64("MAC.DlTbSize", imsi, info.m_rnti, info.m_tbSize);
    col->RecordUeUint64("MAC.DlMcs",   imsi, info.m_rnti, info.m_mcs);
}

void
MacUlSchedCb(Ptr<NrKpiCollector> col, Ptr<NrGnbNetDevice> gnb, NrSchedulingCallbackInfo info)
{
    uint64_t imsi = LookupImsi(gnb, info.m_rnti);
    col->RecordUeUint64("MAC.UlTbSize", imsi, info.m_rnti, info.m_tbSize);
    col->RecordUeUint64("MAC.UlMcs",   imsi, info.m_rnti, info.m_mcs);
}

void
MacDlHarqCb(Ptr<NrKpiCollector> col, Ptr<NrGnbNetDevice> gnb, const DlHarqInfo& info)
{
    if (!info.IsReceivedOk())
    {
        uint64_t imsi = LookupImsi(gnb, info.m_rnti);
        col->IncrementUeCounter("MAC.HarqFail", imsi, info.m_rnti);
    }
}

} // namespace

void
NrKpiCollector::ConnectMacTraces(Ptr<NrGnbNetDevice> gnb)
{
    NS_LOG_FUNCTION(this);
    for (uint32_t b = 0; b < gnb->GetCcMapSize(); ++b)
    {
        Ptr<NrGnbMac> mac = gnb->GetMac(b);
        if (!mac)
            continue;
        if (IsEnabled("MAC.DlTbSize") || IsEnabled("MAC.DlMcs"))
            mac->TraceConnectWithoutContext("DlScheduling",
                MakeBoundCallback(&MacDlSchedCb, this, gnb));
        if (IsEnabled("MAC.UlTbSize") || IsEnabled("MAC.UlMcs"))
            mac->TraceConnectWithoutContext("UlScheduling",
                MakeBoundCallback(&MacUlSchedCb, this, gnb));
        if (IsEnabled("MAC.HarqFail"))
            mac->TraceConnectWithoutContext("DlHarqFeedback",
                MakeBoundCallback(&MacDlHarqCb, this, gnb));
    }
}

NrKpiSnapshot
NrKpiCollector::CollectMac(Ptr<NrGnbNetDevice> gnb) const
{
    NrKpiSnapshot snap;
    snap.collectionTime = Simulator::Now();
    const uint16_t cellId = gnb->GetCellId();

    auto add = [&](const std::string& name, uint64_t imsi, uint16_t rnti, uint64_t val) {
        const NrKpiRegistryEntry* e = NrKpiRegistry::Find(name);
        if (!e) return;
        NrKpiSample s;
        s.kpiName = name; s.layer = NrKpiLayer::MAC; s.scope = NrKpiScope::UE;
        s.timestamp = snap.collectionTime; s.imsi = imsi; s.rnti = rnti; s.cellId = cellId;
        s.type = NrKpiValueType::UINT64; s.uint64Value = val;
        snap.samples.push_back(s);
    };

    // Latest scheduled TBsize/MCS
    for (const auto& kv : m_ueUint64)
    {
        for (const auto& metric : kv.second)
        {
            if (metric.first.substr(0, 3) == "MAC")
                add(metric.first, kv.first,
                    m_ueRnti.count(kv.first) ? m_ueRnti.at(kv.first) : 0,
                    metric.second);
        }
    }
    // Event counters (HARQ fail, SR)
    for (const auto& kv : m_ueCounter)
    {
        for (const auto& metric : kv.second)
        {
            if (metric.first.substr(0, 3) == "MAC")
                add(metric.first, kv.first,
                    m_ueRnti.count(kv.first) ? m_ueRnti.at(kv.first) : 0,
                    metric.second);
        }
    }
    return snap;
}

// ===================================================================
// RLC layer
// ===================================================================

void
NrKpiCollector::SetRlcCalculator(Ptr<NrBearerStatsCalculator> calc)
{
    m_rlcCalc = calc;
}

NrKpiSnapshot
NrKpiCollector::CollectRlc(Ptr<NrGnbNetDevice> gnb) const
{
    NrKpiSnapshot snap;
    snap.collectionTime = Simulator::Now();
    if (!m_rlcCalc || !gnb->GetRrc())
        return snap;
    const uint16_t cellId = gnb->GetCellId();
    // DRBs start at LCID 3 in NR (LCID 1/2 are SRBs, not data bearers)
    constexpr uint8_t kFirstDrb = 3;
    constexpr uint8_t kMaxDrb   = 15;

    for (const auto& ue : gnb->GetRrc()->GetUeMap())
    {
        uint64_t imsi = ue.second->GetImsi();
        uint16_t rnti = ue.second->GetRnti();

        auto addU = [&](const std::string& name, uint64_t val) {
            const NrKpiRegistryEntry* e = NrKpiRegistry::Find(name);
            if (!e || !IsEnabled(name)) return;
            NrKpiSample s;
            s.kpiName = name; s.layer = NrKpiLayer::RLC; s.scope = NrKpiScope::UE;
            s.timestamp = snap.collectionTime; s.imsi = imsi; s.rnti = rnti; s.cellId = cellId;
            s.type = NrKpiValueType::UINT64; s.uint64Value = val;
            snap.samples.push_back(s);
        };
        auto addD = [&](const std::string& name, double val) {
            const NrKpiRegistryEntry* e = NrKpiRegistry::Find(name);
            if (!e || !IsEnabled(name)) return;
            NrKpiSample s;
            s.kpiName = name; s.layer = NrKpiLayer::RLC; s.scope = NrKpiScope::UE;
            s.timestamp = snap.collectionTime; s.imsi = imsi; s.rnti = rnti; s.cellId = cellId;
            s.type = NrKpiValueType::DOUBLE; s.doubleValue = val;
            snap.samples.push_back(s);
        };

        // Sum over all active DRB LCIDs (3..15) — cumulative totals
        uint64_t dlTxCum = 0, dlRxCum = 0, ulTxCum = 0, dlPktsCum = 0, dlRxPktsCum = 0;
        double dlDelay = 0.0, ulDelay = 0.0;
        uint32_t delayCount = 0;
        for (uint8_t lcid = kFirstDrb; lcid <= kMaxDrb; ++lcid)
        {
            dlTxCum    += m_rlcCalc->GetDlTxData(imsi, lcid);
            dlRxCum    += m_rlcCalc->GetDlRxData(imsi, lcid);
            ulTxCum    += m_rlcCalc->GetUlTxData(imsi, lcid);
            dlPktsCum  += m_rlcCalc->GetDlTxPackets(imsi, lcid);
            dlRxPktsCum+= m_rlcCalc->GetDlRxPackets(imsi, lcid);
            double d = m_rlcCalc->GetDlDelay(imsi, lcid);
            double u = m_rlcCalc->GetUlDelay(imsi, lcid);
            if (d > 0.0) { dlDelay += d; ++delayCount; }
            if (u > 0.0)   ulDelay += u;
        }
        if (delayCount > 1) dlDelay /= delayCount;

        // Compute per-interval deltas
        uint64_t dlTx    = dlTxCum    - m_prevRlcDlTx[imsi];
        uint64_t dlRx    = dlRxCum    - m_prevRlcDlRx[imsi];
        uint64_t ulTx    = ulTxCum    - m_prevRlcUlTx[imsi];
        uint64_t dlPkts  = dlPktsCum  - m_prevRlcDlPkts[imsi];
        uint64_t dlRxPkts= dlRxPktsCum- m_prevRlcDlRxPkts[imsi];

        // Update previous values
        m_prevRlcDlTx[imsi]     = dlTxCum;
        m_prevRlcDlRx[imsi]     = dlRxCum;
        m_prevRlcUlTx[imsi]     = ulTxCum;
        m_prevRlcDlPkts[imsi]   = dlPktsCum;
        m_prevRlcDlRxPkts[imsi] = dlRxPktsCum;

        addU("RLC.DlTxBytes",   dlTx);
        addU("RLC.DlRxBytes",   dlRx);
        addU("RLC.UlTxBytes",   ulTx);
        addU("RLC.DlTxPackets", dlPkts);
        addU("RLC.DlRxPackets", dlRxPkts);
        addD("RLC.DlDelay",     dlDelay);
        addD("RLC.UlDelay",     ulDelay);

        // MAC.UEThpDl: RLC DL Tx bytes delivered in interval → MAC-layer DL throughput (Mbps)
        if (m_reportingPeriodSeconds > 0.0)
        {
            double thpDl = (static_cast<double>(dlTx) * 8.0) /
                           (m_reportingPeriodSeconds * 1.0e6); // Mbps
            addD("MAC.UEThpDl", thpDl);
        }
    }
    return snap;
}

// ===================================================================
// PDCP layer
// ===================================================================

void
NrKpiCollector::SetPdcpCalculator(Ptr<NrBearerStatsCalculator> calc)
{
    m_pdcpCalc = calc;
}

NrKpiSnapshot
NrKpiCollector::CollectPdcp(Ptr<NrGnbNetDevice> gnb) const
{
    NrKpiSnapshot snap;
    snap.collectionTime = Simulator::Now();
    if (!m_pdcpCalc || !gnb->GetRrc())
        return snap;
    const uint16_t cellId = gnb->GetCellId();
    constexpr uint8_t kFirstDrb = 3;
    constexpr uint8_t kMaxDrb   = 15;

    for (const auto& ue : gnb->GetRrc()->GetUeMap())
    {
        uint64_t imsi = ue.second->GetImsi();
        uint16_t rnti = ue.second->GetRnti();

        auto addU = [&](const std::string& name, uint64_t val) {
            const NrKpiRegistryEntry* e = NrKpiRegistry::Find(name);
            if (!e || !IsEnabled(name)) return;
            NrKpiSample s;
            s.kpiName = name; s.layer = NrKpiLayer::PDCP; s.scope = NrKpiScope::UE;
            s.timestamp = snap.collectionTime; s.imsi = imsi; s.rnti = rnti; s.cellId = cellId;
            s.type = NrKpiValueType::UINT64; s.uint64Value = val;
            snap.samples.push_back(s);
        };
        auto addD = [&](const std::string& name, double val) {
            const NrKpiRegistryEntry* e = NrKpiRegistry::Find(name);
            if (!e || !IsEnabled(name)) return;
            NrKpiSample s;
            s.kpiName = name; s.layer = NrKpiLayer::PDCP; s.scope = NrKpiScope::UE;
            s.timestamp = snap.collectionTime; s.imsi = imsi; s.rnti = rnti; s.cellId = cellId;
            s.type = NrKpiValueType::DOUBLE; s.doubleValue = val;
            snap.samples.push_back(s);
        };

        // Sum over all active DRB LCIDs (3..15) — cumulative totals
        uint64_t dlTxCum = 0, dlRxCum = 0, dlPktsCum = 0, dlRxPktsCum = 0;
        double dlDelay = 0.0, ulDelay = 0.0;
        uint32_t delayCount = 0;
        for (uint8_t lcid = kFirstDrb; lcid <= kMaxDrb; ++lcid)
        {
            dlTxCum     += m_pdcpCalc->GetDlTxData(imsi, lcid);
            dlRxCum     += m_pdcpCalc->GetDlRxData(imsi, lcid);
            dlPktsCum   += m_pdcpCalc->GetDlTxPackets(imsi, lcid);
            dlRxPktsCum += m_pdcpCalc->GetDlRxPackets(imsi, lcid);
            double d  = m_pdcpCalc->GetDlDelay(imsi, lcid);
            double u  = m_pdcpCalc->GetUlDelay(imsi, lcid);
            if (d > 0.0) { dlDelay += d; ++delayCount; }
            if (u > 0.0)   ulDelay += u;
        }
        if (delayCount > 1) dlDelay /= delayCount;

        // Compute per-interval deltas
        uint64_t dlTx    = dlTxCum    - m_prevPdcpDlTx[imsi];
        uint64_t dlRx    = dlRxCum    - m_prevPdcpDlRx[imsi];
        uint64_t dlPkts  = dlPktsCum  - m_prevPdcpDlPkts[imsi];
        uint64_t dlRxPkts= dlRxPktsCum- m_prevPdcpDlRxPkts[imsi];

        // Update previous values
        m_prevPdcpDlTx[imsi]     = dlTxCum;
        m_prevPdcpDlRx[imsi]     = dlRxCum;
        m_prevPdcpDlPkts[imsi]   = dlPktsCum;
        m_prevPdcpDlRxPkts[imsi] = dlRxPktsCum;

        addU("PDCP.DlTxBytes",   dlTx);
        addU("PDCP.DlRxBytes",   dlRx);
        addU("PDCP.DlTxPackets", dlPkts);
        addU("PDCP.DlRxPackets", dlRxPkts);
        addD("PDCP.DlDelay",     dlDelay);
        addD("PDCP.UlDelay",     ulDelay);

        // DRB.UEThpDl: bytes received at UE side in interval → DRB-layer DL throughput (Mbps)
        if (m_reportingPeriodSeconds > 0.0)
        {
            double thpDl = (static_cast<double>(dlRx) * 8.0) /
                           (m_reportingPeriodSeconds * 1.0e6); // Mbps
            addD("DRB.UEThpDl", thpDl);
        }
    }
    return snap;
}

// ===================================================================
// RRC layer
// ===================================================================

namespace
{

void
RrcConnEstabCb(Ptr<NrKpiCollector> col, uint64_t imsi, uint16_t /*cid*/, uint16_t rnti)
{
    col->IncrementUeCounter("RRC.ConnEstab", imsi, rnti);
}

void
RrcHoSuccCb(Ptr<NrKpiCollector> col, uint64_t imsi, uint16_t /*cid*/, uint16_t rnti)
{
    col->IncrementUeCounter("RRC.HoSucc", imsi, rnti);
}

void
RrcRlfCb(Ptr<NrKpiCollector> col, uint64_t imsi, uint16_t /*cid*/, uint16_t rnti)
{
    col->IncrementUeCounter("RRC.RLF", imsi, rnti);
}

void
RrcStateCb(Ptr<NrKpiCollector> col,
           uint64_t imsi,
           uint16_t /*cid*/,
           uint16_t rnti,
           NrUeRrc::State /*from*/,
           NrUeRrc::State /*to*/)
{
    col->IncrementUeCounter("RRC.StateChange", imsi, rnti);
}

} // namespace

void
NrKpiCollector::ConnectRrcTraces(Ptr<NrGnbNetDevice> /*gnb*/, const NetDeviceContainer& ueDevs)
{
    NS_LOG_FUNCTION(this);
    for (uint32_t u = 0; u < ueDevs.GetN(); ++u)
    {
        Ptr<NrUeNetDevice> ud = ueDevs.Get(u)->GetObject<NrUeNetDevice>();
        if (!ud)
            continue;
        Ptr<NrUeRrc> rrc = ud->GetRrc();
        if (!rrc)
            continue;
        if (IsEnabled("RRC.ConnEstab"))
            rrc->TraceConnectWithoutContext("ConnectionEstablished",
                MakeBoundCallback(&RrcConnEstabCb, this));
        if (IsEnabled("RRC.HoSucc"))
            rrc->TraceConnectWithoutContext("HandoverEndOk",
                MakeBoundCallback(&RrcHoSuccCb, this));
        if (IsEnabled("RRC.RLF"))
            rrc->TraceConnectWithoutContext("RadioLinkFailure",
                MakeBoundCallback(&RrcRlfCb, this));
        if (IsEnabled("RRC.StateChange"))
            rrc->TraceConnectWithoutContext("StateTransition",
                MakeBoundCallback(&RrcStateCb, this));
    }
}

NrKpiSnapshot
NrKpiCollector::CollectRrc(Ptr<NrGnbNetDevice> gnb) const
{
    NrKpiSnapshot snap;
    snap.collectionTime = Simulator::Now();
    const uint16_t cellId = gnb->GetCellId();

    // Active UE count polled from RRC
    if (IsEnabled("RRC.ActiveUes") && gnb->GetRrc())
    {
        NrKpiSample s;
        s.kpiName = "RRC.ActiveUes"; s.layer = NrKpiLayer::RRC; s.scope = NrKpiScope::CELL;
        s.timestamp = snap.collectionTime; s.cellId = cellId;
        s.type = NrKpiValueType::UINT64;
        s.uint64Value = static_cast<uint64_t>(gnb->GetRrc()->GetUeMap().size());
        snap.samples.push_back(s);
    }

    // Event counters (ConnEstab, HO, RLF, StateChange)
    for (const auto& kv : m_ueCounter)
    {
        for (const auto& metric : kv.second)
        {
            if (metric.first.substr(0, 3) != "RRC")
                continue;
            const NrKpiRegistryEntry* e = NrKpiRegistry::Find(metric.first);
            if (!e || !IsEnabled(metric.first)) continue;
            NrKpiSample s;
            s.kpiName = metric.first; s.layer = NrKpiLayer::RRC; s.scope = NrKpiScope::UE;
            s.timestamp = snap.collectionTime;
            s.imsi = kv.first;
            s.rnti = m_ueRnti.count(kv.first) ? m_ueRnti.at(kv.first) : 0;
            s.cellId = cellId;
            s.type = NrKpiValueType::UINT64; s.uint64Value = metric.second;
            snap.samples.push_back(s);
        }
    }
    return snap;
}

// ===================================================================
// CollectAll – merge all enabled layers
// ===================================================================

NrKpiSnapshot
NrKpiCollector::CollectAll(Ptr<NrGnbNetDevice> gnb) const
{
    NrKpiSnapshot all;
    all.collectionTime = Simulator::Now();

    auto merge = [&](NrKpiSnapshot sub) {
        all.samples.insert(all.samples.end(), sub.samples.begin(), sub.samples.end());
    };

    merge(CollectPhy(gnb));
    merge(CollectMac(gnb));
    merge(CollectRlc(gnb));
    merge(CollectPdcp(gnb));
    merge(CollectRrc(gnb));
    return all;
}

} // namespace ns3
