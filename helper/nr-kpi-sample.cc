// SPDX-License-Identifier: GPL-2.0-only

#include "nr-kpi-sample.h"

namespace ns3
{

std::string
NrKpiLayerToString(NrKpiLayer layer)
{
    switch (layer)
    {
    case NrKpiLayer::PHY:
        return "PHY";
    case NrKpiLayer::MAC:
        return "MAC";
    case NrKpiLayer::RLC:
        return "RLC";
    case NrKpiLayer::PDCP:
        return "PDCP";
    case NrKpiLayer::RRC:
        return "RRC";
    default:
        return "UNKNOWN";
    }
}

} // namespace ns3
