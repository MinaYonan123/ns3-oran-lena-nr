# Modular KPI Reporting Architecture — NR Module Design

Design-only document. No implementation in this phase.

**Scope:** `src/nr/` only  
**Reference:** [`VALIDATED_REPORTING_PARAMETERS.md`](VALIDATED_REPORTING_PARAMETERS.md)  
**Compatibility:** existing E2/KPM flow in `NrGnbNetDevice::BuildAndSendReportMessage()` → `BuildRicIndicationMessageCuUp()`

---

## 1. Goals

| Goal | How |
|------|-----|
| Per-layer on/off | `enablePhyReporting`, `enableMacReporting`, … |
| Per-KPI selection | `selectedPhyKpis`, `selectedMacKpis`, … |
| No default flood | Empty selection list ⇒ **zero** KPIs collected for that layer |
| Registry-driven | One table drives validation, collection, printing, E2 encoding |
| PHY first | Phase 1 implements PHY `IMPLEMENTABLE` KPIs only |
| E2-safe | Legacy KPM items remain unless explicitly migrated to selected-KPI path |

---

## 2. Architecture overview

```
┌─────────────────────────────────────────────────────────────────┐
│  Scenario (scratch/*.cc)                                        │
│  NrKpiReportingConfig { layer flags + selected KPI name lists } │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│  NrHelper                                                       │
│  EnableSelectedKpiReporting(config)                             │
│    → validate names (NrKpiRegistry)                             │
│    → connect only required traces / APIs per layer              │
│    → attach NrKpiCollector to gNB devices                       │
└────────────────────────────┬────────────────────────────────────┘
                             │
          ┌──────────────────┼──────────────────┐
          ▼                  ▼                  ▼
   NrPhyKpiCollector   NrMacKpiCollector   … (later phases)
          │                  │
          └────────┬─────────┘
                   ▼
┌─────────────────────────────────────────────────────────────────┐
│  NrGnbNetDevice (periodic / E2 callback)                        │
│  CollectSelectedKpis() → NrKpiSnapshot                            │
│  PrintSelectedKpis()  → stdout / CSV                              │
│  AddSelectedKpisToIndicationMessage() → LenaIndicationMessageHelper│
└─────────────────────────────────────────────────────────────────┘
```

**Key principle:** scenarios configure *what* to report; the registry defines *how*; collectors hold runtime samples; `NrGnbNetDevice` orchestrates periodic export and E2.

---

## 3. Scenario configuration (layer flags)

### 3.1 Configuration object

```cpp
// helper/nr-kpi-reporting-config.h  (new)
struct NrKpiReportingConfig
{
    bool enablePhyReporting   = false;
    bool enableMacReporting   = false;
    bool enableRlcReporting   = false;
    bool enablePdcpReporting  = false;
    bool enableRrcReporting   = false;

    std::vector<std::string> selectedPhyKpis;
    std::vector<std::string> selectedMacKpis;
    std::vector<std::string> selectedRlcKpis;
    std::vector<std::string> selectedPdcpKpis;
    std::vector<std::string> selectedRrcKpis;

    double reportingPeriodSeconds = 0.1;  // align with E2Periodicity when E2 enabled
    bool enableFileLogging = false;
    bool enableE2Reporting = false;
};
```

### 3.2 Scenario usage pattern

```cpp
NrKpiReportingConfig kpiCfg;
kpiCfg.enablePhyReporting = true;
kpiCfg.enableMacReporting = false;
kpiCfg.selectedPhyKpis = {
    "PHY.DlDataSinr",
    "PHY.Rsrp",
    "PHY.Rsrq",
    "PHY.PrbUtilizationDl"
};

Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
// ... InstallGnbDevice / InstallUeDevice / Attach ...

nrHelper->EnableSelectedKpiReporting(kpiCfg);   // NOT EnableTraces()

// Optional: per-gNB E2 (unchanged entry point)
gnbDev->SetAttribute("E2Periodicity", DoubleValue(kpiCfg.reportingPeriodSeconds));
gnbDev->SetE2Termination(e2term);
```

### 3.3 Flag semantics

| Condition | Behaviour |
|-----------|-----------|
| `enablePhyReporting == false` | PHY collector not created; PHY traces not connected; no PHY KPIs in output |
| `enablePhyReporting == true` && `selectedPhyKpis.empty()` | **No PHY KPIs** (explicit opt-in per name; avoids accidental full dump) |
| `enablePhyReporting == true` && names listed | Only listed IMPLEMENTABLE PHY KPIs are wired and reported |
| Layer flag false but names listed | Names ignored; `NS_LOG_WARN` once at setup |

`EnableTraces()` remains available for backward compatibility but is **orthogonal** to this system. New scenarios should prefer `EnableSelectedKpiReporting()` only.

---

## 4. Canonical KPI naming

Stable string IDs used in scenarios, registry, CSV, and E2 labels:

```
<LAYER>.<MetricName>
```

| Layer prefix | Example IDs (Phase 1 PHY) |
|--------------|---------------------------|
| `PHY` | `PHY.DlDataSinr`, `PHY.DlCtrlSinr`, `PHY.UlSinr`, `PHY.Rsrp`, `PHY.Rsrq`, … |
| `MAC` | `MAC.DlPrbUsed`, `MAC.UlPrbUsed`, `MAC.McsDl`, … (Phase 2+) |
| `RLC` | `RLC.DlTxVolume`, … |
| `PDCP` | `PDCP.DlTxVolume`, … |
| `RRC` | `RRC.ConnEstabSucc`, … |

Aliases (optional, resolved at validation): `PHY.Sinr` → `PHY.DlDataSinr` if unambiguous; reject ambiguous aliases.

---

## 5. KPI registry

### 5.1 Entry structure

```cpp
enum class NrKpiLayer   { PHY, MAC, RLC, PDCP, RRC };
enum class NrKpiStatus  { IMPLEMENTABLE, DERIVABLE, NOT_SUPPORTED };
enum class NrKpiScope   { UE, CELL, GLOBAL };
enum class NrKpiValueType { DOUBLE, UINT64, INT64, BOOL, STRING, DOUBLE_VECTOR };

struct NrKpiRegistryEntry
{
    std::string kpiName;           // e.g. "PHY.DlDataSinr"
    NrKpiLayer layer;
    NrKpiStatus validationStatus;  // from VALIDATED_REPORTING_PARAMETERS.md
    std::string layerFlagName;     // e.g. "enablePhyReporting"
    std::string collectorId;       // e.g. "phy-dl-data-sinr"
    std::string sourceClass;       // e.g. "NrUePhy"
    std::string sourceFile;        // e.g. "model/nr-ue-phy.cc"
    std::string sourceSymbol;      // TraceSource or API, e.g. "DlDataSinr"
    NrKpiValueType valueType;
    NrKpiScope scope;
    bool phase1Enabled;            // true only for Phase-1 PHY IMPLEMENTABLE set
};
```

### 5.2 Registry API (new)

```cpp
class NrKpiRegistry
{
public:
    static const NrKpiRegistryEntry* Find(const std::string& kpiName);
    static std::vector<const NrKpiRegistryEntry*> GetByLayer(NrKpiLayer layer);
    static bool IsValidForReporting(const std::string& kpiName); // IMPLEMENTABLE + phase enabled
    static std::vector<std::string> ValidateSelection(
        NrKpiLayer layer,
        const std::vector<std::string>& requested,
        bool layerEnabled);
};
```

Registry data lives in `helper/nr-kpi-registry.cc` as a `static const std::vector<NrKpiRegistryEntry>` generated to mirror `VALIDATED_REPORTING_PARAMETERS.md`.

---

## 6. Validation against VALIDATED_REPORTING_PARAMETERS.md

Validation runs once at `EnableSelectedKpiReporting()`:

```
For each name in selectedPhyKpis:
  1. Lookup in NrKpiRegistry
  2. If not found → NS_FATAL_ERROR with list of valid PHY names
  3. If entry.validationStatus != IMPLEMENTABLE → reject (Phase 1)
     (DERIVABLE entries rejected until Phase N+)
  4. If entry.layer != PHY → reject ("MAC KPI in PHY list")
  5. If !enablePhyReporting → ignore with warning
  6. If duplicate name → deduplicate
```

| Validation rule | Rationale |
|-----------------|-----------|
| Reject `NOT_SUPPORTED` | Never in current validated set |
| Reject `DERIVABLE` in Phase 1 | Requires extra state (Δt windows); PHY phase uses direct sources only |
| Reject unlisted strings | Prevents typos silently reporting nothing |
| `phase1Enabled == false` | Reserved for later layers / deferred PHY KPIs |

**Phase 1 PHY allow-list** (24 entries, all `IMPLEMENTABLE` from validation doc):

| KPI name | Scope | Source |
|----------|-------|--------|
| `PHY.DlDataSinr` | UE | `NrUePhy` / `DlDataSinr` |
| `PHY.DlCtrlSinr` | UE | `NrUePhy` / `DlCtrlSinr` |
| `PHY.UlSinr` | UE | `NrGnbPhy` / `UlSinrTrace` |
| `PHY.Rsrp` | UE | `NrUePhy` / `ReportRsrp` |
| `PHY.Rsrq` | UE | `NrUePhy` / `ReportUeMeasurements` |
| `PHY.PathLoss` | GLOBAL | `SpectrumChannel` / `PathLoss` |
| `PHY.DlCtrlPathloss` | UE | `NrSpectrumPhy` / `DlCtrlPathloss` |
| `PHY.DlDataPathloss` | UE | `NrSpectrumPhy` / `DlDataPathloss` |
| `PHY.DlDataSnr` | UE | `NrSpectrumPhy` / `DlDataSnrTrace` |
| `PHY.RxPacketTrace` | UE | `NrSpectrumPhy` / `RxPacketTraceUe` |
| `PHY.Tbler` | UE | `RxPacketTraceParams.m_tbler` |
| `PHY.CorruptTb` | UE | `RxPacketTraceParams.m_corrupt` |
| `PHY.Cqi` | UE | `NrUePhy` / `CqiFeedbackTrace` (CQI component) |
| `PHY.Ri` | UE | `CqiFeedbackTrace` (RI component) |
| `PHY.Mcs` | UE | `CqiFeedbackTrace` (MCS component) |
| `PHY.PuschTxPower` | UE | `NrUePowerControl` / `ReportPuschTxPower` |
| `PHY.PucchTxPower` | UE | `NrUePowerControl` / `ReportPucchTxPower` |
| `PHY.SrsTxPower` | UE | `NrUePowerControl` / `ReportSrsTxPower` |
| `PHY.PrbUtilizationDl` | CELL | `NrGnbPhy` / `GetPrbUtilization()` |
| `PHY.SlotDataStats` | CELL | `NrGnbPhy` / `SlotDataStats` |
| `PHY.SlotCtrlStats` | CELL | `NrGnbPhy` / `SlotCtrlStats` |
| `PHY.RbDataStats` | CELL | `NrGnbPhy` / `RBDataStats` |
| `PHY.ActivityFactor` | CELL | `NrGnbPhy` / `CalculateActivityFactor()` |
| `PHY.AntennaPortPower` | CELL | `NrGnbNetDevice` / `GetPortPower()` |
| `PHY.TxPowerWatts` | CELL | `NrGnbNetDevice` / `GetAveragePower()` |
| `PHY.EnergyConsumptionJ` | CELL | `NrGnbPhy` / `GetTotalEnergyConsumption()` |
| `PHY.InstantaneousPowerW` | CELL | `NrGnbPhy` / `GetCurrentPowerConsumption()` |
| `PHY.RssiPerChunk` | UE | `NrInterference` / `RssiPerProcessedChunk` |
| `PHY.SnrPerChunk` | UE | `NrInterference` / `SnrPerProcessedChunk` |
| `PHY.PowerSpectralDensity` | UE | `NrUePhy` / `ReportPowerSpectralDensity` |
| `PHY.CtrlMsgTrace` | CELL | `GnbPhyRxedCtrlMsgsTrace` (+ optional UE side) |

**Excluded from Phase 1 (DERIVABLE):**

- `PHY.TbThroughputDl` (`DRB.UEThpDl` PHY TB) — needs ΔTB aggregation

**Deferred (high volume / debug-only):**

- `PHY.CtrlMsgTrace` — opt-in only; not in default examples

---

## 7. Runtime value model

```cpp
struct NrKpiSample
{
    std::string kpiName;
    NrKpiLayer layer;
    NrKpiScope scope;
    Time timestamp;

    uint64_t imsi = 0;      // UE scope
    uint16_t rnti = 0;      // UE scope
    uint16_t cellId = 0;    // CELL / UE
    uint16_t gnbId = 0;     // CELL

    NrKpiValueType type;
    double doubleValue = 0.0;
    uint64_t uint64Value = 0;
    bool boolValue = false;
    std::vector<double> vectorValue;  // e.g. port power mask, SINR vector summary
};

struct NrKpiSnapshot
{
    Time collectionTime;
    std::vector<NrKpiSample> samples;
};
```

For spectrum-valued KPIs (`UlSinr`), Phase 1 stores **scalar summary** (e.g. average SINR across RBs) in `doubleValue`; full vector export is Phase 2.

---

## 8. Per-layer collector pattern (PHY first)

### 8.1 Base interface (reusable)

```cpp
class NrKpiCollector : public Object
{
public:
    virtual void Configure(const std::vector<const NrKpiRegistryEntry*>& entries) = 0;
    virtual void ConnectTraces(Ptr<NrHelper> helper,
                               Ptr<NrGnbNetDevice> gnb,
                               NetDeviceContainer ueDevs) = 0;
    virtual NrKpiSnapshot Collect(Ptr<NrGnbNetDevice> gnb) = 0;
    virtual void ResetPeriod() = 0;  // optional epoch reset
};
```

### 8.2 PHY functions

```cpp
// Enable: validate, create NrPhyKpiCollector, connect minimal traces
void NrHelper::EnablePhyKpiReporting(const NrKpiReportingConfig& cfg);

// Called by EnableSelectedKpiReporting when enablePhyReporting
void EnablePhyKpiReporting(Ptr<NrPhyKpiCollector> collector,
                             const std::vector<std::string>& selectedPhyKpis,
                             Ptr<NrGnbNetDevice> gnb,
                             NetDeviceContainer ueDevs);

// Snapshot at reporting instant (poll APIs + read collector buffers)
NrKpiSnapshot CollectSelectedPhyKpis(Ptr<NrPhyKpiCollector> collector,
                                     Ptr<NrGnbNetDevice> gnb);

// Human-readable / CSV
void PrintSelectedPhyKpis(const NrKpiSnapshot& snapshot,
                          std::ostream& os,
                          bool csvFormat = false);

// E2 bridge
void AddSelectedPhyKpisToIndicationMessage(const NrKpiSnapshot& snapshot,
                                           Ptr<LenaIndicationMessageHelper> helper);
```

MAC/RLC/PDCP/RRC follow the same four-function pattern in later phases:

```
EnableMacKpiReporting / CollectSelectedMacKpis / …
EnableRlcKpiReporting / …
```

### 8.3 Collection strategies by source type

| Source type | Phase-1 approach |
|-------------|------------------|
| **API poll** (`GetPrbUtilization`, `GetPortPower`, …) | Read in `CollectSelectedPhyKpis()` at period boundary |
| **Trace push** (`DlDataSinr`, `ReportRsrp`, …) | `NrPhyKpiCollector` trace callbacks update `std::map<imsi, LastValue>` |
| **File sink avoided** | Do not parse `DlDataSinr.txt`; keep in-memory only for selected KPIs |

### 8.4 Trace connection minimisation

`EnablePhyKpiReporting` connects **only** traces required by selected entries:

| If selected | Connect |
|-------------|---------|
| `PHY.DlDataSinr` | `NrUePhy/DlDataSinr` → collector callback |
| `PHY.UlSinr` | `NrGnbPhy/UlSinrTrace` → collector (not bundled in `EnableUlPhyTraces`) |
| `PHY.DlCtrlPathloss` | `EnableDlCtrlPathlossTraces(ueDevs)` + collector callback |
| `PHY.PrbUtilizationDl` | No trace; poll `gnb->GetPhy(i)->GetPrbUtilization()` |

---

## 9. Orchestration in NrGnbNetDevice

### 9.1 New members (design)

```cpp
Ptr<NrKpiCollector> m_phyKpiCollector;  // null if !enablePhyReporting
NrKpiReportingConfig m_kpiReportingConfig;
```

### 9.2 Reporting cycle

Integrate with existing timers (do not add a second period unless `reportingPeriodSeconds` differs from `m_e2Periodicity`):

```
BuildAndSendReportMessage() / BuildGUICuUp() path:
  if (m_kpiReportingConfig.enablePhyReporting)
      phySnapshot = CollectSelectedPhyKpis(m_phyKpiCollector, this);
  snapshot = merge(phySnapshot, macSnapshot, …);

  if (enableFileLogging)
      PrintSelectedPhyKpis(snapshot, csvStream, true);

  if (E2 active)
      AddSelectedPhyKpisToIndicationMessage(snapshot, indicationMessageHelper);
```

Legacy `AddPdcpUePmItem` / `AddPHYGnbConfiguration` remain **unchanged** unless scenario sets `kpiCfg.legacyE2PdcpMetrics = false` (future option). Phase 1 **adds** selected PHY KPIs alongside legacy CU-UP items.

---

## 10. Indication message format (selected KPIs only)

### 10.1 Logical record

Each reported sample becomes one **generic KPI item**:

| Field | Type | Required |
|-------|------|----------|
| `kpiName` | string | yes |
| `layer` | string (`"PHY"`) | yes |
| `value` | double/uint64/bool/string | yes |
| `timestampMs` | uint64 | yes |
| `imsi` | uint64 | if scope == UE |
| `rnti` | uint16 | if scope == UE |
| `cellId` | uint16 | if scope == UE or CELL |
| `gnbId` | uint16 | if scope == CELL |

### 10.2 E2 encoding strategy (compatible with existing flow)

**Option A (recommended Phase 1):** extend `LenaIndicationMessageHelper` with:

```cpp
void AddGenericKpiItem(const std::string& kpiName,
                       const std::string& layer,
                       double value,
                       uint64_t timestampMs,
                       uint64_t imsi,
                       uint16_t cellId);
```

Maps to O-RAN E2SM-KPM **custom** or vendor label style names matching `kpiName` (e.g. `PHY.DlDataSinr`). Existing `AddPdcpUePmItem` path untouched.

**Option B (later):** map registry entries to standard TS 28.552 names for official KPM labels.

### 10.3 CSV row format (file logging)

```csv
timestampMs,layer,kpiName,scope,imsi,rnti,cellId,gnbId,value
```

Only rows for **selected** KPIs appear each period.

---

## 11. File change plan

### 11.1 New files (`src/nr/helper/` unless noted)

| File | Purpose |
|------|---------|
| `nr-kpi-reporting-config.h` | `NrKpiReportingConfig` struct |
| `nr-kpi-registry.h` / `.cc` | Registry tables + validation |
| `nr-kpi-sample.h` | `NrKpiSample`, `NrKpiSnapshot` |
| `nr-kpi-collector.h` | Abstract `NrKpiCollector` |
| `nr-phy-kpi-collector.h` / `.cc` | PHY trace buffers + poll helpers |
| `nr-kpi-printer.h` / `.cc` | `PrintSelectedPhyKpis`, CSV formatting |
| `nr-kpi-e2-adapter.h` / `.cc` | `AddSelectedPhyKpisToIndicationMessage` |
| `doc/MODULAR_KPI_REPORTING_ARCHITECTURE.md` | This document |

Later phases add `nr-mac-kpi-collector.*`, `nr-rlc-kpi-collector.*`, etc.

### 11.2 Modified files

| File | Change |
|------|--------|
| `helper/nr-helper.h` / `.cc` | `EnableSelectedKpiReporting()`, `EnablePhyKpiReporting()`; selective trace connect |
| `model/nr-gnb-net-device.h` / `.cc` | Hold config + collector; call collect/print/E2 in existing report callbacks |
| `helper/CMakeLists.txt` or `wscript` | Register new sources |
| `doc/VALIDATED_REPORTING_PARAMETERS.md` | Cross-link canonical KPI names (no status change) |

### 11.3 External (minimal, Phase 1)

| File | Change |
|------|--------|
| O-RAN `Lena-indication-message-helper.*` | Add `AddGenericKpiItem()` for selected KPI envelope |

If O-RAN change is deferred, Phase 1 can log CSV + `NS_LOG` only and stub E2 adapter.

### 11.4 Not modified in Phase 1

- `NrUePhy`, `NrGnbPhy`, trace sources (read-only use)
- `EnableTraces()` behaviour
- CCC `SampleTransmitPower` / `SetPortPower` logic
- Existing `BuildGUICuUp` column layout (additive columns optional later)

---

## 12. Phase plan

| Phase | Layer | KPIs included |
|-------|-------|---------------|
| **1** | PHY | 24 `IMPLEMENTABLE` (exclude `PHY.TbThroughputDl` DERIVABLE) |
| 2 | MAC | 6 `IMPLEMENTABLE` |
| 3 | RLC + PDCP | `IMPLEMENTABLE` bearer stats + calculators |
| 4 | RRC | Event KPIs via trace counters + `GetUeMap().size()` |
| 5 | DERIVABLE | Throughput, bitrate, ConnMean, power savings |

---

## 13. Example end-to-end (Phase 1 PHY)

```cpp
NrKpiReportingConfig cfg;
cfg.enablePhyReporting = true;
cfg.selectedPhyKpis = { "PHY.DlDataSinr", "PHY.Rsrp", "PHY.Rsrq", "PHY.PrbUtilizationDl" };
cfg.reportingPeriodSeconds = 0.1;
cfg.enableE2Reporting = true;
cfg.enableFileLogging = true;

nrHelper->EnableSelectedKpiReporting(cfg);

// Runtime (inside simulator, each 100 ms at gNB):
// 1. CollectSelectedPhyKpis → 4 KPI types × N UEs + 1 cell metric
// 2. PrintSelectedPhyKpis → nr-kpi-cell-1.csv
// 3. AddSelectedPhyKpisToIndicationMessage → E2 CU-UP indication
//    (legacy PDCP KPM items still sent if E2 CU-UP subscription active)
```

---

## 14. Design constraints checklist

| # | Requirement | Design response |
|---|-------------|-----------------|
| 1 | No implementation yet | This document only |
| 2 | PHY first, reusable | `NrKpiCollector` + per-layer `Enable/Collect/Print/AddToIndication` |
| 3 | Scenario flags | `NrKpiReportingConfig` |
| 4 | Name validation | `NrKpiRegistry::ValidateSelection` vs validated doc |
| 5 | Registry fields | §5.1 |
| 6 | Per-layer functions | §8.2 |
| 7 | Indication content | §10 |
| 8 | Modified files | §11.2 |
| 9 | New files | §11.1 |
| 10 | E2 compatible | Additive `AddGenericKpiItem`; legacy path preserved |
| 11 | No unsupported KPIs | Registry rejects unknown / NOT_SUPPORTED |
| 12 | Not all by default | Empty `selected*Kpis` ⇒ none; layer flag alone insufficient |
| 13 | Phase 1 IMPLEMENTABLE PHY only | §6 allow-list |

---

*Design for 5G-LENA NR module (`src/nr/`). Implementation to follow in a separate change set.*
