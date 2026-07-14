// SPDX-License-Identifier: GPL-2.0-only

#ifndef NR_KPI_SAMPLE_H
#define NR_KPI_SAMPLE_H

#include "ns3/nstime.h"
#include <string>
#include <vector>

namespace ns3
{

enum class NrKpiLayer
{
    PHY,
    MAC,
    RLC,
    PDCP,
    RRC
};

enum class NrKpiScope
{
    UE,
    CELL,
    GLOBAL
};

enum class NrKpiValueType
{
    DOUBLE,
    UINT64,
    BOOL
};

struct NrKpiSample
{
    std::string kpiName;
    NrKpiLayer layer{NrKpiLayer::PHY};
    NrKpiScope scope{NrKpiScope::UE};
    Time timestamp;

    uint64_t imsi{0};
    uint16_t rnti{0};
    uint16_t cellId{0};
    uint16_t gnbId{0};

    NrKpiValueType type{NrKpiValueType::DOUBLE};
    double doubleValue{0.0};
    uint64_t uint64Value{0};
    bool boolValue{false};
};

struct NrKpiSnapshot
{
    Time collectionTime;
    std::vector<NrKpiSample> samples;
};

} // namespace ns3

#endif /* NR_KPI_SAMPLE_H */
