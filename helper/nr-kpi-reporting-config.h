// SPDX-License-Identifier: GPL-2.0-only

#ifndef NR_KPI_REPORTING_CONFIG_H
#define NR_KPI_REPORTING_CONFIG_H

#include <string>
#include <vector>

namespace ns3
{

/**
 * Per-scenario modular KPI reporting configuration.
 */
struct NrKpiReportingConfig
{
    bool enablePhyReporting{false};
    bool enableMacReporting{false};
    bool enableRlcReporting{false};
    bool enablePdcpReporting{false};
    bool enableRrcReporting{false};

    std::vector<std::string> selectedPhyKpis;
    std::vector<std::string> selectedMacKpis;
    std::vector<std::string> selectedRlcKpis;
    std::vector<std::string> selectedPdcpKpis;
    std::vector<std::string> selectedRrcKpis;

    double reportingPeriodSeconds{0.1};
    bool enableFileLogging{false};
    bool enableE2Reporting{false};
};

} // namespace ns3

#endif /* NR_KPI_REPORTING_CONFIG_H */
