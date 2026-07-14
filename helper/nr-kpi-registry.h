// SPDX-License-Identifier: GPL-2.0-only

#ifndef NR_KPI_REGISTRY_H
#define NR_KPI_REGISTRY_H

#include "nr-kpi-sample.h"
#include <string>
#include <vector>

namespace ns3
{

enum class NrKpiStatus
{
    IMPLEMENTABLE,
    DERIVABLE,
    NOT_SUPPORTED
};

struct NrKpiRegistryEntry
{
    std::string kpiName;
    NrKpiLayer layer;
    NrKpiStatus validationStatus;
    std::string sourceClass;
    std::string sourceSymbol;
    NrKpiValueType valueType;
    NrKpiScope scope;
    bool phase1Enabled;
};

class NrKpiRegistry
{
  public:
    static const NrKpiRegistryEntry* Find(const std::string& kpiName);
    static std::vector<const NrKpiRegistryEntry*> ValidateSelection(
        const std::vector<std::string>& requested,
        bool layerEnabled);
    static std::vector<std::string> GetAllImplementableKpiNames(NrKpiLayer layer);
};

} // namespace ns3

#endif /* NR_KPI_REGISTRY_H */
