# 5G-LENA (NR Module) — Reporting Parameters & 3GPP KPI Mapping

This document lists **reportable parameters** available in the **5G-LENA** module (`src/nr/`), maps them to **3GPP performance measurement / KPI names**, and documents **how to collect them** (trace source, API, output file, or E2).

**Module path:** `src/nr/`  
**Main helper:** `ns3::NrHelper` (`helper/nr-helper.h`, `helper/nr-helper.cc`)

---

## Implementation status legend

| Symbol | Meaning |
|--------|---------|
| ✅ | **Implemented** — trace sink and/or API exists; code is in `src/nr/` |
| ⚠️ | **Simulated / approximate** — modeled for ns-3; not a bit-exact 3GPP counter |
| 📡 | **E2 / O-RAN** — reported via `NrGnbNetDevice` KPM (this repository fork) |
| 🔧 | **Manual connect** — trace exists; not connected by default `EnableTraces()` |

---

## Quick start — enable all standard traces

```cpp
#include "ns3/nr-helper.h"

Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
// ... InstallGnbDevice / InstallUeDevice / Attach ...

nrHelper->EnableTraces();           // PHY + MAC + RLC + PDCP + pathloss
nrHelper->StartEnergyMonitoring();  // optional: EnergyConsumption_Cell_*.csv

// Runtime API examples:
Ptr<NrBearerStatsCalculator> pdcp = nrHelper->GetPdcpStatsCalculator();
uint64_t dlBytes = pdcp->GetDlTxData(imsi, lcid);   // bytes since last epoch reset
double delay = pdcp->GetDlDelay(imsi, lcid);        // seconds (avg)

Ptr<NrGnbNetDevice> gnb = ...;
double prbUtil = gnb->GetPhy(0)->GetPrbUtilization();
std::vector<double> ports = gnb->GetPortPower();
```

`EnableTraces()` calls (see `nr-helper.cc`):

| Function | Layer |
|----------|-------|
| `EnableDlDataPhyTraces()` | PHY |
| `EnableDlCtrlPhyTraces()` | PHY |
| `EnableUlPhyTraces()` | PHY |
| `EnableGnbPhyCtrlMsgsTraces()` / `EnableUePhyCtrlMsgsTraces()` | PHY |
| `EnableGnbMacCtrlMsgsTraces()` / `EnableUeMacCtrlMsgsTraces()` | MAC |
| `EnableDlMacSchedTraces()` / `EnableUlMacSchedTraces()` | MAC |
| `EnableRlcSimpleTraces()` / `EnableRlcE2eTraces()` | RLC |
| `EnablePdcpSimpleTraces()` / `EnablePdcpE2eTraces()` | PDCP |
| `EnablePathlossTraces()` | PHY (channel) |

---

## 3GPP references

| Document | Use |
|----------|-----|
| **TS 28.552** | 5G performance measurements (counter names, e.g. `DRB.*`, `RRU.*`) |
| **TS 38.331** | RRC procedures (measurement reports, connection, HO) |
| **TS 38.214** | PHY/MAC scheduling (MCS, PRB, HARQ) |
| **TS 38.213** | UE power control (PUSCH/PUCCH/SRS power) |
| **O-RAN WG3** | E2SM-KPM style service metrics (PDCP throughput, etc.) |

> **Note:** 3GPP counter names below follow **TS 28.552** naming style. LENA provides **simulation observables** that correspond to these KPIs; values are not produced by a real gNB PM subsystem unless you export them (files, E2, custom callbacks).

---

# PHY layer

| # | 3GPP KPI / measurement (TS 28.552) | LENA trace / API name | Class | Unit | How to get | Output / API | Status |
|---|-----------------------------------|----------------------|-------|------|------------|--------------|--------|
| 1 | **L1M.RS-SINR** (DL data SINR) | `DlDataSinr` | `NrUePhy` | dB | `nrHelper->EnableDlDataPhyTraces()` | `DlDataSinr.txt` | ✅ |
| 2 | **L1M.RS-SINR** (DL control SINR) | `DlCtrlSinr` | `NrUePhy` | dB | `EnableDlCtrlPhyTraces()` | `DlCtrlSinr.txt` | ✅ |
| 3 | **L1M.RS-SINR** (UL SINR) | `UlSinrTrace` | `NrGnbPhy` | dB (spectrum) | `EnableUlPhyTraces()` | via `NrPhyRxTrace::UlSinrTraceCallback` | ✅ |
| 4 | **RRQ.RSRP** | `ReportRsrp` | `NrUePhy` | dBm | `Config::Connect` to trace | callback | ✅ 🔧 |
| 5 | **RRQ.RSRQ** | `ReportUeMeasurements` | `NrUePhy` | dB | `Config::Connect` to trace | RSRP + RSRQ in callback | ✅ 🔧 |
| 6 | **L1M.PathLoss** (DL/UL) | `PathLoss` (channel) | `SpectrumChannel` | dB | `EnablePathlossTraces()` | `UlPathlossTrace.txt`, `DlPathlossTrace.txt` | ✅ |
| 7 | **L1M.PathLoss** (DL ctrl/data per UE) | `DlCtrlPathloss`, `DlDataPathloss` | `NrSpectrumPhy` | dB | `EnableDlCtrlPathlossTraces()` / `EnableDlDataPathlossTraces()` | dedicated pathloss files | ✅ 🔧 |
| 8 | **L1M.SNR** (per TB) | `DlDataSnrTrace` | `NrSpectrumPhy` | dB | manual `Config::Connect` | callback | ✅ 🔧 |
| 9 | **DRB.UEThpDl** (PHY TB contribution) | `ReportDownlinkTbSize`, `ReportUplinkTbSize` | `NrUePhy` | bytes | `EnableTransportBlockTrace()` | via `NrPhyRxTrace` | ✅ 🔧 |
| 10 | **L1M.RxBytes** / packet trace | `RxPacketTraceUe`, `RxPacketTraceGnb` | `NrSpectrumPhy` | — | `EnableDlDataPhyTraces()` / `EnableUlPhyTraces()` | `RxPacketTrace.txt` | ✅ |
| 11 | **L1M.TBler** (simulated) | fields in `RxPacketTraceParams` | `NrSpectrumPhy` | ratio | same as #10 | `m_tbler`, `m_corrupt` in trace | ✅ ⚠️ |
| 12 | **L1M.CQI** / **L1M.RI** / MCS | `CqiFeedbackTrace` | `NrUePhy` | index | manual connect | RNTI, WB CQI, MCS, RI | ✅ 🔧 |
| 13 | **L1M.TxPower** PUSCH | `ReportPuschTxPower` | `NrUePowerControl` | dBm | enable `EnableUplinkPowerControl` on UE PHY + connect | callback | ✅ 🔧 |
| 14 | **L1M.TxPower** PUCCH | `ReportPucchTxPower` | `NrUePowerControl` | dBm | same | callback | ✅ 🔧 |
| 15 | **L1M.TxPower** SRS | `ReportSrsTxPower` | `NrUePowerControl` | dBm | same | callback | ✅ 🔧 |
| 16 | **RRU.PrbUsedDl** (utilization) | `GetPrbUtilization()` | `NrGnbPhy` | 0–1 | API | `phy->GetPrbUtilization()` | ✅ ⚠️ |
| 17 | **RRU.PrbAvail** / slot resources | `SlotDataStats`, `SlotCtrlStats` | `NrGnbPhy` | RE/RB/symbols | manual connect | active UE, used/available RBs | ✅ 🔧 |
| 18 | **RRU.PrbUsedDl** (per-RB map) | `RBDataStats` | `NrGnbPhy` | RB map | manual connect | SFN, symbol, RB PHY map | ✅ 🔧 |
| 19 | **gNB activity factor** | `CalculateActivityFactor()` | `NrGnbPhy` | 0–1 | API | `phy->CalculateActivityFactor()` | ✅ ⚠️ |
| 20 | **Antenna port power** (CCC) | `GetPortPower()`, `GetPortPowerScaling()` | `NrGnbNetDevice`, `NrGnbPhy` | 0–1 per port | API | `gnb->GetPortPower()` | ✅ 📡 |
| 21 | **Average TX power** (antenna) | `GetAveragePower()` | `NrGnbNetDevice` | — | API | `gnb->GetAveragePower()` | ✅ 📡 |
| 22 | **Energy consumption** | `GetTotalEnergyConsumption()` | `NrGnbPhy`, `NrUePhy` | J | `StartEnergyMonitoring()` | `EnergyConsumption_Cell_<id>.csv` | ✅ ⚠️ |
| 23 | **Instantaneous power** | `GetCurrentPowerConsumption()` | `NrGnbPhy`, `NrUePhy` | W | `StartEnergyMonitoring()` | CSV + stdout | ✅ ⚠️ |
| 24 | **L1M.RSSI** / SNR per chunk | `RssiPerProcessedChunk`, `SnrPerProcessedChunk` | `NrInterference` | dB | manual connect | callback | ✅ 🔧 |
| 25 | **PSD** | `ReportPowerSpectralDensity` | `NrUePhy` | dBm/Hz | manual connect | callback | ✅ 🔧 |
| 26 | PHY ctrl messages | `GnbPhyRxed/TxedCtrlMsgsTrace`, `UePhyRxed/TxedCtrlMsgsTrace`, `UePhyRxedDlDciTrace`, `UePhyTxedHarqFeedbackTrace` | `NrGnbPhy`, `NrUePhy` | — | `EnableGnbPhyCtrlMsgsTraces()`, `EnableUePhyCtrlMsgsTraces()` | `RxedGnbPhyCtrlMsgsTrace.txt`, etc. | ✅ |

### `RxPacketTrace.txt` columns (PHY)

From `RxPacketTraceParams` (`model/nr-phy-mac-common.h`): time, cellId, RNTI, frame, subframe, slot, symStart, numSym, TB size, MCS, rank, RV, SINR avg/min, TBLER, corrupt flag, BWP ID, #RBs assigned, CQI.

### Energy CSV columns

`EnergyConsumption_Cell_<cellId>.csv`: `Time(s), IntervalEnergy(J), AveragePower(W), CurrentTotalPower(W), gNBPower(W), UesTotalPower(W), ActiveUEs, EnergyPerUE(J), PowerPerUE(W)` — written by `NrHelper::LogEnergyToFile()`.

---

# MAC layer

| # | 3GPP KPI / measurement | LENA trace / API name | Class | Unit | How to get | Output / API | Status |
|---|------------------------|----------------------|-------|------|------------|--------------|--------|
| 1 | **RRU.PrbUsedDl** (per allocation) | `DlScheduling` | `NrGnbMac` | RB/TB | `EnableDlMacSchedTraces()` | `NrDlMacStats.txt` | ✅ |
| 2 | **RRU.PrbUsedUl** | `UlScheduling` | `NrGnbMac` | RB/TB | `EnableUlMacSchedTraces()` | `NrUlMacStats.txt` | ✅ |
| 3 | **MAC.UEThpDl** (instantaneous) | derived from `m_tbSize` in scheduling | `NrMacSchedulingStats` | bytes/TTI | post-process MAC stats file | `NrDlMacStats.txt` | ✅ ⚠️ |
| 4 | **L1M.MCS** DL/UL | `m_mcs` in `NrSchedulingCallbackInfo` | `NrGnbMac` | index | MAC sched traces | MAC stats files | ✅ |
| 5 | **HARQ.DL.Fail** / feedback | `DlHarqFeedback` | `NrGnbMac` | ACK/NACK | manual connect | callback | ✅ 🔧 |
| 6 | **MAC.SR** (scheduling request) | `SrReq` | `NrGnbMac` | — | manual connect | callback | ✅ 🔧 |
| 7 | **RACH** timeout | `RaResponseTimeout` | `NrUeMac` | — | manual connect | callback | ✅ 🔧 |
| 8 | MAC ctrl messages | `GnbMacRxed/TxedCtrlMsgsTrace`, `UeMacRxed/TxedCtrlMsgsTrace` | `NrGnbMac`, `NrUeMac` | — | `EnableGnbMacCtrlMsgsTraces()`, `EnableUeMacCtrlMsgsTraces()` | `RxedGnbMacCtrlMsgsTrace.txt`, etc. | ✅ |

### `NrDlMacStats.txt` / `NrUlMacStats.txt` columns

`Time, CellId, BWPId, IMSI, RNTI, Frame, Subframe, Slot, SymStart, NumSym, HarqId, NDI, RV, MCS, TbSize`

Struct: `NrSchedulingCallbackInfo` in `model/nr-phy-mac-common.h`.

---

# RLC layer

| # | 3GPP KPI / measurement | LENA trace / API name | Class | Unit | How to get | Output / API | Status |
|---|------------------------|----------------------|-------|------|------------|--------------|--------|
| 1 | **DRB.RlcSduVolumeDl** | `TxPDU` (DL TX) | `NrRlc` | bytes | `EnableRlcSimpleTraces()` | `NrDlTxRlcStats.txt` | ✅ |
| 2 | **DRB.RlcSduVolumeDl** (RX) | `RxPDU` | `NrRlc` | bytes | same | `NrDlRxRlcStats.txt` | ✅ |
| 3 | **DRB.RlcSduVolumeUl** | `TxPDU` / `RxPDU` UL | `NrRlc` | bytes | same | `NrUlTxRlcStats.txt`, `NrUlRxRlcStats.txt` | ✅ |
| 4 | **DRB.RlcSduDelayDl** | E2E delay | `NrBearerStatsCalculator` | s | `EnableRlcE2eTraces()` | `NrDlRlcStatsE2E.txt` | ✅ |
| 5 | **DRB.RlcSduDelayUl** | E2E delay | `NrBearerStatsCalculator` | s | same | `NrUlRlcStatsE2E.txt` | ✅ |
| 6 | **DRB.RlcSduBitrateDl** | derived | `GetDlTxData(imsi,lcid)/epoch` | bit/s | API on calculator | `GetRlcStatsCalculator()` | ✅ ⚠️ |
| 7 | TX PDU count | `GetDlTxPackets()`, `GetUlTxPackets()` | `NrBearerStatsCalculator` | count | API | per IMSI + LCID | ✅ |
| 8 | RX PDU count | `GetDlRxPackets()`, `GetUlRxPackets()` | `NrBearerStatsCalculator` | count | API | per IMSI + LCID | ✅ |
| 9 | PDU size distribution | `GetDlPduSizeStats()`, `GetUlPduSizeStats()` | `NrBearerStatsCalculator` | bytes | API | avg, min, max, std | ✅ |
| 10 | RLC drops | `TxDrop` | `NrRlc` | count | manual connect | callback | ✅ 🔧 |
| 11 | RLC TX in E2 period | `GetTxPacketsInReportingPeriod()`, `GetTxBytesInReportingPeriod()` | `NrRlc` | count/bytes | API on DRB RLC | used in `BuildRicIndicationMessageCuUp` | ✅ 📡 |

### `NrDlRlcStatsE2E.txt` epoch columns

`start, end, CellId, IMSI, RNTI, LCID, nTxPDUs, TxBytes, nRxPDUs, RxBytes, delay(avg,std,min,max), PduSize(avg,std,min,max)`

API getters: `helper/nr-bearer-stats-calculator.h` — `GetDlTxData()`, `GetDlRxData()`, `GetDlDelay()`, `GetDlDelayStats()`, etc.

---

# PDCP layer

| # | 3GPP KPI / measurement | LENA trace / API name | Class | Unit | How to get | Output / API | Status |
|---|------------------------|----------------------|-------|------|------------|--------------|--------|
| 1 | **DRB.PdcpSduVolumeDl** | `TxPDU` | `NrPdcp` | bytes | `EnablePdcpSimpleTraces()` | `NrDlPdcpTxStats.txt` | ✅ |
| 2 | **DRB.PdcpSduVolumeDl** (RX) | `RxPDU` | `NrPdcp` | bytes | same | `NrDlPdcpRxStats.txt` | ✅ |
| 3 | **DRB.PdcpSduVolumeUl** | `TxPDU` / `RxPDU` | `NrPdcp` | bytes | same | `NrUlPdcpTxStats.txt`, `NrUlPdcpRxStats.txt` | ✅ |
| 4 | **DRB.PdcpSduDelayDl** | E2E delay | `NrBearerStatsCalculator` | s | `EnablePdcpE2eTraces()` | `NrDlPdcpStatsE2E.txt` | ✅ |
| 5 | **DRB.PdcpSduDelayUl** | E2E delay | `NrBearerStatsCalculator` | s | same | `NrUlPdcpStatsE2E.txt` | ✅ |
| 6 | **DRB.UEThpDl** (PDCP throughput) | derived | `NrGnbNetDevice::BuildGUICuUp()` | Mbps | E2 / GUI CSV | `(ΔTxBytes×8)/period` | ✅ 📡 |
| 7 | **DRB.PdcpSduBitrateDl** | `GetDlTxData()` / epoch | `NrBearerStatsCalculator` | bit/s | `GetPdcpStatsCalculator()` | API | ✅ |
| 8 | PDCP packet counts | `GetDlTxPackets()`, `GetDlRxPackets()` | `NrBearerStatsCalculator` | count | API | per IMSI + LCID | ✅ |
| 9 | Serving cell per bearer | `GetDlCellId()`, `GetUlCellId()` | `NrBearerStatsCalculator` | cell ID | API | | ✅ |

**Throughput formula (implemented in `nr-gnb-net-device.cc`):**

```cpp
double bytesInPeriod = currentTxKbit - previousTxKbit;
double throughputMbps = bytesInPeriod / (periodSeconds * 1000.0);  // GUI: 100 ms period
```

Set calculator epoch: `pdcpStats->SetEpoch(Seconds(0.1));` and `SetStartTime(...)`.

---

# RRC layer

| # | 3GPP KPI / event (TS 38.331 / 28.552) | LENA trace name | Class | How to get | Status |
|---|---------------------------------------|-----------------|-------|------------|--------|
| 1 | **RRC.ConnEstabSucc** | `ConnectionEstablished` | `NrUeRrc`, `NrGnbRrc` | `Config::Connect` | ✅ 🔧 |
| 2 | **RRC.ConnRel** | `NotifyConnectionRelease` | `NrGnbRrc` | connect trace | ✅ 🔧 |
| 3 | **RRC.ConnEstabFail** | `ConnectionTimeout` | `NrUeRrc` | connect trace | ✅ 🔧 |
| 4 | **MM.MeasRep** / **RRQ.RSRP** in report | `RecvMeasurementReport` | `NrGnbRrc` | connect trace; payload `NrRrcSap::MeasurementReport` | ✅ 🔧 |
| 5 | **HO.Att**, **HO.Succ** | `HandoverStart`, `HandoverEndOk` | `NrUeRrc`, `NrGnbRrc` | connect trace | ✅ 🔧 |
| 6 | **HO.Fail** | `HandoverEndError`, `HandoverFailure*` | `NrGnbRrc` | connect trace | ✅ 🔧 |
| 7 | **RRC.State** | `StateTransition` | `NrUeRrc`, `NrUeManager` | connect trace | ✅ 🔧 |
| 8 | **RRC.RLF** | `RadioLinkFailure`, `PhySyncDetection` | `NrUeRrc` | connect trace | ✅ 🔧 |
| 9 | **RRC.DRB.Estab** | `DrbCreated` | `NrUeRrc`, `NrUeManager` | connect trace | ✅ 🔧 |
| 10 | **RRC.SRB.Estab** | `Srb1Created` | `NrUeRrc` | connect trace | ✅ 🔧 |
| 11 | **RACH** success/fail | `RandomAccessSuccessful`, `RandomAccessError` | `NrUeRrc` | connect trace | ✅ 🔧 |
| 12 | **SIB/MIB** received | `MibReceived`, `Sib1Received`, `Sib2Received` | `NrUeRrc` | connect trace | ✅ 🔧 |
| 13 | **RRC.Reconfig** | `ConnectionReconfiguration` | `NrUeRrc`, `NrGnbRrc` | connect trace | ✅ 🔧 |
| 14 | **UE count** (active RRC connections) | `GetUeMap().size()` | `NrGnbRrc` | `gnb->GetRrc()->GetUeMap()` | ✅ 📡 |

### Measurement report content

`NrRrcSap::MeasurementReport` → `measResults` with **RSRP/RSRQ** per measured cell (`model/nr-rrc-sap.h`, fields `rsrpResult`, `rsrqResult`).

Example connect:

```cpp
Config::Connect("/NodeList/*/DeviceList/*/NrGnbRrc/RecvMeasurementReport",
  MakeCallback(&MyRecvMeasurementReport));
```

---

# E2 / O-RAN KPM (this fork)

Implemented in `model/nr-gnb-net-device.cc` when `E2Termination` is configured.

| Metric | 3GPP / O-RAN style name | Source | Function | Status |
|--------|-------------------------|--------|----------|--------|
| PDCP DL throughput | **DRB.UEThpDl** / KPM UE throughput | `NrBearerStatsCalculator` | `BuildGUICuUp()`, `BuildRicIndicationMessageCuUp()` | ✅ 📡 |
| PDCP TX/RX volume | **DRB.PdcpSduVolumeDl** | `GetDlTxData()`, `GetDlRxData()` | `BuildRicIndicationMessageCuUp()` | ✅ 📡 |
| PDCP latency | **DRB.PdcpSduDelayDl** | `GetDlDelay()` | same | ✅ 📡 |
| RLC PDU count/bytes | **DRB.RlcSduVolumeDl** | `NrRlc::GetTxPacketsInReportingPeriod()` | same | ✅ 📡 |
| Active UE count | **RRC.ConnMean** (approx.) | `m_rrc->GetUeMap().size()` | `AddPHYGnbConfiguration()` | ✅ 📡 |
| Antenna ports on/off | (vendor / CCC) | `GetPortPower()` | `AddPHYGnbConfiguration()` | ✅ 📡 |
| E2 periodicity | granularity period | attribute `E2Periodicity` | `NrGnbNetDevice` TypeId | ✅ 📡 |
| CSV logging | — | `EnableE2FileLogging` | `m_cuUpFileName` | ✅ 📡 |

**Enable E2 reporting:**

```cpp
gnbDev->SetAttribute("E2PdcpCalculator", PointerValue(nrHelper->GetPdcpStatsCalculator()));
gnbDev->SetAttribute("E2Periodicity", DoubleValue(0.1));
gnbDev->SetAttribute("EnableE2FileLogging", BooleanValue(true));
gnbDev->SetE2Termination(e2term);
```

---

# Complete output file list

| File | Layer | Enabled by |
|------|-------|------------|
| `DlDataSinr.txt` | PHY | `EnableDlDataPhyTraces()` |
| `DlCtrlSinr.txt` | PHY | `EnableDlCtrlPhyTraces()` |
| `UlPathlossTrace.txt`, `DlPathlossTrace.txt` | PHY | `EnablePathlossTraces()` |
| `RxPacketTrace.txt` | PHY | DL data + UL PHY traces |
| `Rxed/TxedGnbPhyCtrlMsgsTrace.txt` | PHY | `EnableGnbPhyCtrlMsgsTraces()` |
| `Rxed/TxedUePhyCtrlMsgsTrace.txt` | PHY | `EnableUePhyCtrlMsgsTraces()` |
| `NrDlMacStats.txt`, `NrUlMacStats.txt` | MAC | `EnableDl/UlMacSchedTraces()` |
| `Rxed/TxedGnbMacCtrlMsgsTrace.txt` | MAC | `EnableGnbMacCtrlMsgsTraces()` |
| `NrDlTx/RxRlcStats.txt`, `NrUlTx/RxRlcStats.txt` | RLC | `EnableRlcSimpleTraces()` |
| `NrDl/UlRlcStatsE2E.txt` | RLC | `EnableRlcE2eTraces()` |
| `NrDlPdcpTx/RxStats.txt`, `NrUlPdcpTx/RxStats.txt` | PDCP | `EnablePdcpSimpleTraces()` |
| `NrDl/UlPdcpStatsE2E.txt` | PDCP | `EnablePdcpE2eTraces()` |
| `EnergyConsumption_Cell_<id>.csv` | PHY (energy) | `StartEnergyMonitoring()` |

---

# Source code index

| Component | Path |
|-----------|------|
| Trace enabler | `helper/nr-helper.cc` — `EnableTraces()` |
| PHY trace sink | `helper/nr-phy-rx-trace.h`, `nr-phy-rx-trace.cc` |
| MAC trace sink | `helper/nr-mac-rx-trace.h`, `nr-mac-scheduling-stats.h` |
| RLC/PDCP stats | `helper/nr-bearer-stats-calculator.h`, `nr-bearer-stats-simple.h` |
| PHY models | `model/nr-ue-phy.cc`, `model/nr-gnb-phy.cc`, `model/nr-spectrum-phy.cc` |
| MAC models | `model/nr-gnb-mac.cc`, `model/nr-ue-mac.cc` |
| RLC/PDCP | `model/nr-rlc.cc`, `model/nr-pdcp.cc` |
| RRC | `model/nr-gnb-rrc.cc`, `model/nr-ue-rrc.cc` |
| E2 KPM | `model/nr-gnb-net-device.cc` |
| Power control | `model/nr-ue-power-control.cc` |

---

# Limitations

1. **Not all 3GPP counters** from TS 28.552 are implemented — only those listed above have LENA traces or APIs.
2. **Throughput** at PDCP is **derived** in software, not a native 3GPP counter register.
3. **Energy / antenna power** models are **simulation extensions** (especially in the CCC/O-RAN fork); map carefully to real PM if exporting to an OS.
4. **RRC traces** are event-based; aggregate KPIs (e.g. HO success rate) require post-processing.
5. Parameters marked 🔧 need explicit `Config::Connect` — they are **not** wired by `EnableTraces()`.

---

*Generated for 5G-LENA module in `ns-O-RAN-flexric/mmwave-LENA-oran`. For questions, see also upstream 5G-LENA documentation at `src/nr/doc/source/nr-module.rst`.*
