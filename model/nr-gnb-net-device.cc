// Copyright (c) 2019 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
//
// SPDX-License-Identifier: GPL-2.0-only

#include "nr-gnb-net-device.h"

#include "bandwidth-part-gnb.h"
#include "bwp-manager-gnb.h"
#include "nr-gnb-component-carrier-manager.h"
#include "nr-gnb-mac.h"
#include "nr-gnb-phy.h"
#include "nr-gnb-rrc.h"
#include "nr-ue-net-device.h"
#include "nr-ue-phy.h"

#include <ns3/abort.h>
#include <ns3/ipv4-l3-protocol.h>
#include <ns3/ipv6-l3-protocol.h>
#include <ns3/log.h>
#include <ns3/node-list.h>
#include <ns3/node.h>
#include <ns3/object-map.h>
#include <ns3/pointer.h>
#include "encode_e2apv1.hpp"
#include <ns3/double.h>

#include <cmath>
#include <curl/curl.h>
#include <filesystem> // For filesystem utilities, available since C++17
#include <fstream>
#include <iomanip> // Required for std::fixed and std::setprecision
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <vector>
#include <string>

std::vector<int> g_ueImsiList;
uint64_t start_sim_time = 0;
uint64_t current_sim_time = 0;
bool headerWritten_Cell = false;
bool headerWritten_UE = false;




// Example signature for Simulator::Now() mock
// Replace this with your actual ns-3 or simulation time function
// #include "ns3/simulator.h"  // if using ns-3

void SendToInfluxDB(const std::string &payload) {
  CURL *curl = curl_easy_init();
  std::string influx_host = "localhost";
  std::string influx_port = "8086";
  std::string influx_user = "root";
  std::string influx_password = "root";
  std::string db_name = "influx";

  if (curl) {
    const std::string url = "http://" + influx_host + ":" + influx_port +
                            "/api/v2/write?bucket=influx&precision=ns";
    struct curl_slist *headers = nullptr;
    const std::string auth =
        "Authorization: Token " + influx_user + ":" + influx_password;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: text/plain");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      std::cerr << "InfluxDB POST failed: " << curl_easy_strerror(res)
                << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
  }
}

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("NrGnbNetDevice");

NS_OBJECT_ENSURE_REGISTERED(NrGnbNetDevice);

TypeId NrGnbNetDevice::GetTypeId() {
  static TypeId tid =
      TypeId("ns3::NrGnbNetDevice")
          .SetParent<NrNetDevice>()
          .AddConstructor<NrGnbNetDevice>()
          .AddAttribute(
              "NrGnbComponentCarrierManager",
              "The component carrier manager associated to this GnbNetDevice",
              PointerValue(),
              MakePointerAccessor(&NrGnbNetDevice::m_componentCarrierManager),
              MakePointerChecker<NrGnbComponentCarrierManager>())
          .AddAttribute("BandwidthPartMap", "List of Bandwidth Part container.",
                        ObjectMapValue(),
                        MakeObjectMapAccessor(&NrGnbNetDevice::m_ccMap),
                        MakeObjectMapChecker<BandwidthPartGnb>())
          .AddAttribute("NrGnbRrc", "The RRC layer associated with the gNB",
                        PointerValue(),
                        MakePointerAccessor(&NrGnbNetDevice::m_rrc),
                        MakePointerChecker<NrGnbRrc>())
          .AddAttribute("sim_id", "ID of simulation", UintegerValue(0),
                        MakeUintegerAccessor(&NrGnbNetDevice::sim_id),
                        MakeUintegerChecker<uint64_t>())
            .AddAttribute("report_to_db", "Reporting to InfluxDB",
                          BooleanValue(false),
                          MakeBooleanAccessor(&NrGnbNetDevice::report_to_db),
                          MakeBooleanChecker())
            .AddAttribute ("E2Termination",
                          "The E2 termination object associated to this node",
                          PointerValue (),
                          MakePointerAccessor (&NrGnbNetDevice::SetE2Termination,
                                              &NrGnbNetDevice::GetE2Termination),
                          MakePointerChecker <E2Termination> ())
            .AddAttribute ("EnableE2FileLogging",
                          "If true, force E2 indication generation and write E2 fields in csv file",
                          BooleanValue (false),
                          MakeBooleanAccessor (&NrGnbNetDevice::m_forceE2FileLogging),
                          MakeBooleanChecker ())
            .AddAttribute ("KPM_E2functionID", "Function ID to subscribe", DoubleValue (2),
                          MakeDoubleAccessor (&NrGnbNetDevice::e2_func_id),
                          MakeDoubleChecker<double> ())
            .AddAttribute("RC_E2functionID", "Function ID to subscribe", DoubleValue(3),
                          MakeDoubleAccessor(&NrGnbNetDevice::rc_e2_func_id),
                          MakeDoubleChecker<double>());

    return tid;
}

NrGnbNetDevice::NrGnbNetDevice()
    : m_forceE2FileLogging(false),
      m_cellId(0),
      m_stopSendingMessages(false),
      m_isReportingEnabled(false)
{
    NS_LOG_FUNCTION(this);
}

NrGnbNetDevice::~NrGnbNetDevice()
{
    NS_LOG_FUNCTION(this);
}

Ptr<NrMacScheduler>
NrGnbNetDevice::GetScheduler(uint8_t index) const
{
    NS_LOG_FUNCTION(this);
    return m_ccMap.at(index)->GetScheduler();
}

void
NrGnbNetDevice::SetCcMap(const std::map<uint8_t, Ptr<BandwidthPartGnb>>& ccm)
{
    NS_ABORT_IF(!m_ccMap.empty());
    m_ccMap = ccm;
}

uint32_t
NrGnbNetDevice::GetCcMapSize() const
{
    return static_cast<uint32_t>(m_ccMap.size());
}

void
NrGnbNetDevice::stopSendingAndCancelSchedule()
{
    m_stopSendingMessages = true;
}

void
NrGnbNetDevice::KpmSubscriptionCallback(E2AP_PDU_t* sub_req_pdu)
{
    NS_LOG_DEBUG("\nReceived RIC Subscription Request, cellId= " << m_cellId << "\n");

    E2Termination::RicSubscriptionRequest_rval_s params =
        m_e2term->ProcessRicSubscriptionRequest(sub_req_pdu);
    NS_LOG_DEBUG("requestorId " << +params.requestorId << ", instanceId " << +params.instanceId
                                << ", ranFuncionId " << +params.ranFuncionId << ", actionId "
                                << +params.actionId);

    if (!m_stopSendingMessages && !m_isReportingEnabled && !m_forceE2FileLogging)
    {
        // BuildAndSendReportMessage (params);
        m_isReportingEnabled = true;
    }
}

void
NrGnbNetDevice::ControlMessageReceivedCallback(E2AP_PDU_t* sub_req_pdu)
{
    NS_LOG_DEBUG(
        "\n\nLteEnbNetDevice::ControlMessageReceivedCallback: Received RIC Control Message");

    // Create RIC Control ACK
    Ptr<RicControlMessage> controlMessage = Create<RicControlMessage>(sub_req_pdu);
    NS_LOG_INFO("After RicControlMessage::RicControlMessage constructor");
    NS_LOG_INFO("Request type " << controlMessage->m_requestType);
}

void
NrGnbNetDevice::SetE2Termination(Ptr<E2Termination> e2term)
{
    m_e2term = e2term;

    NS_LOG_DEBUG("Register E2SM NR");

    if (!m_forceE2FileLogging)
    {
        long m_e2_func_id = long(e2_func_id);
        long m_rc_e2_func_id = long(rc_e2_func_id);
        Ptr<KpmFunctionDescription> kpmFd = Create<KpmFunctionDescription>();
        e2term->RegisterKpmCallbackToE2Sm(
            m_e2_func_id,
            kpmFd,
            std::bind(&NrGnbNetDevice::KpmSubscriptionCallback, this, std::placeholders::_1));

        Ptr<RicControlFunctionDescription> ricCtrlFd = Create<RicControlFunctionDescription>();
        e2term->RegisterSmCallbackToE2Sm(m_rc_e2_func_id,
                                         ricCtrlFd,
                                         std::bind(&NrGnbNetDevice::ControlMessageReceivedCallback,
                                                   this,
                                                   std::placeholders::_1));

        e2term->RegisterCallbackFunctionToE2Sm(
            1,
            std::bind(&NrGnbNetDevice::stopSendingAndCancelSchedule, this));
    }
}

Ptr<E2Termination>
NrGnbNetDevice::GetE2Termination() const
{
    return m_e2term;
}

void
NrGnbNetDevice::SetNrFhControl(Ptr<NrFhControl> nrFh)
{
    NS_LOG_FUNCTION(this);
    m_nrFhControl = nrFh;
}

Ptr<NrFhControl>
NrGnbNetDevice::GetNrFhControl()
{
    NS_LOG_FUNCTION(this);
    return m_nrFhControl;
}

void
NrGnbNetDevice::RouteIngoingCtrlMsgs(const std::list<Ptr<NrControlMessage>>& msgList,
                                     uint8_t sourceBwpId)
{
    NS_LOG_FUNCTION(this);

    for (const auto& msg : msgList)
    {
        uint8_t bwpId = DynamicCast<BwpManagerGnb>(m_componentCarrierManager)
                            ->RouteIngoingCtrlMsgs(msg, sourceBwpId);
        m_ccMap.at(bwpId)->GetPhy()->PhyCtrlMessagesReceived(msg);
    }
}

void
NrGnbNetDevice::RouteOutgoingCtrlMsgs(const std::list<Ptr<NrControlMessage>>& msgList,
                                      uint8_t sourceBwpId)
{
    NS_LOG_FUNCTION(this);

    for (const auto& msg : msgList)
    {
        uint8_t bwpId = DynamicCast<BwpManagerGnb>(m_componentCarrierManager)
                            ->RouteOutgoingCtrlMsg(msg, sourceBwpId);
        NS_ASSERT_MSG(m_ccMap.size() > bwpId,
                      "Returned bwp " << +bwpId << " is not present. Check your configuration");
        NS_ASSERT_MSG(
            m_ccMap.at(bwpId)->GetPhy()->HasDlSlot(),
            "Returned bwp "
                << +bwpId
                << " has no DL slot, so the message can't go out. Check your configuration");
        m_ccMap.at(bwpId)->GetPhy()->EncodeCtrlMsg(msg);
    }
}

void
NrGnbNetDevice::DoInitialize()
{
    NS_LOG_FUNCTION(this);
    m_rrc->Initialize();
    Simulator::Schedule(MilliSeconds(100), &NrGnbNetDevice::Cell_KPI_tracker, this);
    Simulator::Schedule(MilliSeconds(100), &NrGnbNetDevice::UE_KPI_tracker, this);
    NrNetDevice::DoInitialize();
}

void
NrGnbNetDevice::DoDispose()
{
    NS_LOG_FUNCTION(this);

    m_rrc->Dispose();
    m_rrc = nullptr;
    for (const auto& it : m_ccMap)
    {
        it.second->Dispose();
    }
    m_ccMap.clear();
    m_componentCarrierManager->Dispose();
    m_componentCarrierManager = nullptr;
    NrNetDevice::DoDispose();
}

Ptr<NrGnbMac>
NrGnbNetDevice::GetMac(uint8_t index) const
{
    return m_ccMap.at(index)->GetMac();
}

Ptr<NrGnbPhy>
NrGnbNetDevice::GetPhy(uint8_t index) const
{
    NS_LOG_FUNCTION(this);
    return m_ccMap.at(index)->GetPhy();
}

Ptr<BwpManagerGnb>
NrGnbNetDevice::GetBwpManager() const
{
    return DynamicCast<BwpManagerGnb>(m_componentCarrierManager);
}

uint16_t
NrGnbNetDevice::GetCellId() const
{
    NS_LOG_FUNCTION(this);
    return m_cellId;
}

std::vector<uint16_t>
NrGnbNetDevice::GetCellIds() const
{
    std::vector<uint16_t> cellIds;

    cellIds.reserve(m_ccMap.size());
    for (auto& it : m_ccMap)
    {
        cellIds.push_back(it.second->GetCellId());
    }
    return cellIds;
}

void
NrGnbNetDevice::SetCellId(uint16_t cellId)
{
    NS_LOG_FUNCTION(this);
    m_cellId = cellId;
}

uint16_t
NrGnbNetDevice::GetBwpId(uint8_t index) const
{
    NS_LOG_FUNCTION(this);
    return m_ccMap.at(index)->GetCellId();
}

uint16_t
NrGnbNetDevice::GetEarfcn(uint8_t index) const
{
    NS_LOG_FUNCTION(this);
    return m_ccMap.at(index)->GetDlEarfcn(); // Ul or Dl doesn't matter, they are the same
}

void
NrGnbNetDevice::SetRrc(Ptr<NrGnbRrc> rrc)
{
    m_rrc = rrc;
}

Ptr<NrGnbRrc>
NrGnbNetDevice::GetRrc()
{
    return m_rrc;
}

bool
NrGnbNetDevice::DoSend(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
{
    NS_LOG_FUNCTION(this << packet << dest << protocolNumber);
    NS_ABORT_MSG_IF(protocolNumber != Ipv4L3Protocol::PROT_NUMBER &&
                        protocolNumber != Ipv6L3Protocol::PROT_NUMBER,
                    "unsupported protocol " << protocolNumber
                                            << ", only IPv4 and IPv6 are supported");

    NS_LOG_INFO("Forward received packet to RRC Layer");
    m_txTrace(packet, dest);

    return m_rrc->SendData(packet);
}

void
NrGnbNetDevice::UpdateConfig()
{
    NS_LOG_FUNCTION(this);

    NS_ASSERT(!m_ccMap.empty());

    std::map<uint8_t, Ptr<BandwidthPartGnb>> ccPhyConfMap;
    for (const auto& i : m_ccMap)
    {
        Ptr<BandwidthPartGnb> c = i.second;
        ccPhyConfMap.insert(std::pair<uint8_t, Ptr<BandwidthPartGnb>>(i.first, c));
    }

    m_rrc->ConfigureCell(ccPhyConfMap);
    if (m_e2term)
    {
        NS_LOG_DEBUG("E2sim start in cell " << m_cellId << " force CSV logging "
                                            << m_forceE2FileLogging);
        //
        if (!m_forceE2FileLogging)
        {
            Simulator::Schedule(MicroSeconds(0), &E2Termination::Start, m_e2term);
        }
    }
}

uint16_t
NrGnbNetDevice::GetCellIdDlBandwidth(uint16_t cellId) const
{
    NS_ASSERT_MSG(m_rrc->HasCellId(cellId), "Unknown cellId");
    if (m_rrc->HasCellId(cellId))
    {
        for (const auto& [key, cc] : m_ccMap)
        {
            if (cc->GetCellId() == cellId)
            {
                return cc->GetDlBandwidth();
            }
        }
    }
    return 0;
}

uint16_t
NrGnbNetDevice::GetCellIdUlBandwidth(uint16_t cellId) const
{
    NS_ASSERT_MSG(m_rrc->HasCellId(cellId), "Unknown cellId");
    if (m_rrc->HasCellId(cellId))
    {
        for (const auto& [key, cc] : m_ccMap)
        {
            if (cc->GetCellId() == cellId)
            {
                return cc->GetUlBandwidth();
            }
        }
    }
    return 0;
}

uint32_t
NrGnbNetDevice::GetCellIdDlEarfcn(uint16_t cellId) const
{
    NS_ASSERT_MSG(m_rrc->HasCellId(cellId), "Unknown cellId");
    if (m_rrc->HasCellId(cellId))
    {
        for (const auto& [key, cc] : m_ccMap)
        {
            if (cc->GetCellId() == cellId)
            {
                return cc->GetDlEarfcn();
            }
        }
    }
    return 0;
}

uint32_t
NrGnbNetDevice::GetCellIdUlEarfcn(uint16_t cellId) const
{
    NS_ASSERT_MSG(m_rrc->HasCellId(cellId), "Unknown cellId");
    if (m_rrc->HasCellId(cellId))
    {
        for (const auto& [key, cc] : m_ccMap)
        {
            if (cc->GetCellId() == cellId)
            {
                return cc->GetUlEarfcn();
            }
        }
    }
    return 0;
}

void NrGnbNetDevice::SetFlowMonitor(ns3::Ptr<ns3::FlowMonitor> monitor) {
  NS_LOG_FUNCTION(this << monitor);
  m_flowMonitor = monitor;
}

void NrGnbNetDevice::SetIpv4FlowClassifier(
    ns3::Ptr<ns3::Ipv4FlowClassifier> classifier) {

  NS_LOG_FUNCTION(this << classifier);
  m_flowClassifier = classifier;

  // Clear previous contents if you want a fresh list each call
  g_ueImsiList.clear();

  for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it) {
    Ptr<Node> node = *it;
    int nodeId = node->GetId();
    int nDevs = node->GetNDevices();
    for (int j = 0; j < nDevs; j++) {
      Ptr<NrUeNetDevice> nruedev =
          node->GetDevice(j)->GetObject<NrUeNetDevice>();
      if (nruedev) {
        uint64_t imsi = nruedev->GetImsi(); // keep as uint64_t

        // Optional: avoid duplicates
        if (std::find(g_ueImsiList.begin(), g_ueImsiList.end(), imsi) ==
            g_ueImsiList.end()) {
          g_ueImsiList.push_back(imsi);
        }

        // Debug: print which node/device we found a UE on (helpful to debug)
        // std::cout << "Found UE on Node " << nodeId << " Device " << j
        //   << " -> IMSI: " << imsi << std::endl;
      }
    }
  }

  // Print the collected IMSIs
  std::cout << "=== UE IMSI List ===" << std::endl;
  for (size_t i = 0; i < g_ueImsiList.size(); ++i) {
    std::cout << "UE index " << i << " -> IMSI: " << g_ueImsiList[i]
              << std::endl;
  }

  // rest of your method...
  if (m_flowMonitor && m_flowClassifier) {
    Simulator::Schedule(MilliSeconds(0), &NrGnbNetDevice::SampleThroughput,
                        this, m_flowMonitor, m_flowClassifier, 0.1);
  }
}

void NrGnbNetDevice::SampleThroughput(Ptr<FlowMonitor> monitor,
                                      Ptr<Ipv4FlowClassifier> classifier,
                                      double intervalSec) {
  if (!monitor || !classifier)
    return;
  // refresh
  monitor->CheckForLostPackets();
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
  double now = Simulator::Now().GetSeconds();

  // initialize prev map on first run (so first printed delta isn't huge)
  if (m_prevRxBytes.empty()) {
    for (const auto &kv : stats) {
      m_prevRxBytes[kv.first] = kv.second.rxBytes;
    }
    Simulator::Schedule(Seconds(intervalSec), &NrGnbNetDevice::SampleThroughput,
                        this, monitor, classifier, intervalSec);
    return;
  }

  // filter config
  const bool filterOnlyUdp = false;
  const bool filterByServerIp = true;
  const Ipv4Address dlServerIp =
      Ipv4Address("1.0.0.2"); // set to your DL server IP
  const uint64_t minBytesThreshold = 64;

  int id_for_imsi_it = 0;
  // Loop over flows in FlowMonitor
  for (const auto &kv : stats) {
    ns3::FlowId id = kv.first;
    const FlowMonitor::FlowStats &fs = kv.second;
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(id);

    // filters
    if (filterOnlyUdp && t.protocol != 17)
      continue;
    if (filterByServerIp && t.sourceAddress != dlServerIp)
      continue;

    uint64_t curr = fs.rxBytes;
    uint64_t prev = 0;
    auto itPrev = m_prevRxBytes.find(id);
    if (itPrev != m_prevRxBytes.end())
      prev = itPrev->second;
    uint64_t diff = (curr >= prev) ? (curr - prev) : curr;
    if (diff < minBytesThreshold) {
      m_prevRxBytes[id] = curr;
      continue;
    }
    double thrMbps =
        (static_cast<double>(diff) * 8.0) / (intervalSec * 1e6); // Mbps

    // assign IMSI index if this flow id is new to us
    int IMSI_tmp = g_ueImsiList[id_for_imsi_it];
    // save the throughput for this IMSI
    m_imsiToTp[IMSI_tmp] = thrMbps;

    /*    // print info
        std::cout << "t=" << now << "s  Flow " << id << " (IMSI=" << IMSI_tmp <<
       ") "
                  << "(" << t.sourceAddress << ":" << t.sourcePort << " -> "
                  << t.destinationAddress << ":" << t.destinationPort << ") "
                  << "inst DL throughput = " << thrMbps << " Mbps"
                  << "  rxBytes=" << curr << " prev=" << prev << std::endl;*/

    // update prev bytes
    m_prevRxBytes[id] = curr;
    id_for_imsi_it = id_for_imsi_it + 1;
  }
  int now_ms = Simulator::Now().GetMilliSeconds();
  current_sim_time = (sim_id + (uint64_t)now_ms) * 1000000ULL;

  Simulator::Schedule(MilliSeconds(0), &NrGnbNetDevice::Cell_KPI_tracker, this);
  Simulator::Schedule(MilliSeconds(0), &NrGnbNetDevice::UE_KPI_tracker, this);
  // schedule next run
  Simulator::Schedule(Seconds(intervalSec), &NrGnbNetDevice::SampleThroughput,
                      this, monitor, classifier, intervalSec);
}

// ... (rest of your code)

void NrGnbNetDevice::Cell_KPI_tracker() {
  NS_LOG_UNCOND( "---------------------------------------------");
  // Create a folder
  std::stringstream folderName;
  folderName << "trace_" << sim_id;

  if (!std::filesystem::exists(folderName.str())) {
    std::filesystem::create_directory(folderName.str());
  }

  // Construct the full file path within the folder
  std::stringstream cell_kpi_file;
  cell_kpi_file << folderName.str() << "/Cell_" << this->GetCellId()
                << "_Cell_stats_" << sim_id << ".csv";

  // Open the file in append mode
  std::ofstream traceFile(cell_kpi_file.str(), std::ios::out | std::ios::app);

  if (traceFile.is_open()) {
    // If the file is empty, write the header
    if (!headerWritten_Cell && traceFile.tellp() == 0) {
      traceFile << "TS,CELL_ID,PRB_USAGE,CURR_PRB\n";
      headerWritten_Cell = true;
    }
    Ptr<NrGnbPhy> gnbPhy = GetPhy(0);
    NrGnbPhy::RbStats stats = gnbPhy->GetRBStats();

    // Fill CellStats structure
    CellStats cellStats;
    cellStats.cellId = this->GetCellId();
    cellStats.prbUsagePercentage = stats.prbUsagePercentage;
    cellStats.averageLastRb = stats.averageLastRb;

    // Print struct contents with fixed notation and precision
    NS_LOG_UNCOND(
        "Cell stats-> gNB "
        << cellStats.cellId << " | PRB Usage: " << std::fixed
        << std::setprecision(0) << cellStats.prbUsagePercentage
        << " %" // No decimal places for percentage
        << " | Avg Last RB: " << std::fixed << std::setprecision(0)
        << cellStats.averageLastRb); // No decimal places for Avg Last RB

    traceFile << Simulator::Now().GetSeconds() << "," << cellStats.cellId << ","
              << std::fixed << std::setprecision(2)
              << cellStats.prbUsagePercentage
              << "," // Use 2 decimal places for CSV
              << std::fixed << std::setprecision(2) << cellStats.averageLastRb
              << "\n";

    if (report_to_db) {
      std::ostringstream payload;
      payload << "cell_stats,cell_id=" << cellStats.cellId
              << " prb_usage=" << cellStats.prbUsagePercentage
              << ",avg_last_rb=" << cellStats.averageLastRb
              << " " << current_sim_time;  // <-- Influx timestamp in ns

      SendToInfluxDB(payload.str());
    }
  }
  traceFile.close();
  // Reschedule KPI_tracker every 100 ms
}

void NrGnbNetDevice::UE_KPI_tracker() {
  // Create a folder
  std::stringstream folderName;
  folderName << "trace_" << sim_id;

  if (!std::filesystem::exists(folderName.str())) {
    std::filesystem::create_directory(folderName.str());
  }

  // Construct the full file path within the folder
  std::stringstream ue_kpi_file;
  ue_kpi_file << folderName.str() << "/Cell_" << this->GetCellId()
              << "_UE_stats_" << sim_id << ".csv";

  // Open the file in append mode
  std::ofstream traceFile(ue_kpi_file.str(), std::ios::out | std::ios::app);

  if (traceFile.is_open()) {
    // If the file is empty, write the header
    if (!headerWritten_UE && traceFile.tellp() == 0) {
      traceFile << "TS,IMSI,CELL_ID,SINR,RSRP,DL_TP,MCS,RI,CQI\n";
      headerWritten_UE = true;
    }

    std::unordered_map<uint64_t, UEStats> ueStatsMap;

    for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End();
         ++it) {
      Ptr<Node> node = *it;
      for (uint32_t i = 0; i < node->GetNDevices(); ++i) {
        Ptr<NrUeNetDevice> ueDevice =
            node->GetDevice(i)->GetObject<NrUeNetDevice>();
        if (!ueDevice || ueDevice->GetCellId() != this->GetCellId())
          continue;

        Ptr<NrUePhy> uePhy = ueDevice->GetPhy(0);
        if (!uePhy)
          continue;

        uePhy->ReportUeMeasurements();
        double rsrp = uePhy->GetRsrp();
        double sinr = uePhy->GetSINR();
        double sinr_dB = 10 * log10(sinr);

        // double dl_tp = uePhy->GetDLTP();
        UeKpiInfo kpi = uePhy->GetUEkpi();

        uint64_t ueImsi64 = ueDevice->GetImsi();
        uint32_t ueImsi =
            static_cast<uint32_t>(ueImsi64); // make types match your maps
        double dl_tp = 0.0;

        auto itTp = m_imsiToTp.find(ueImsi);
        if (itTp != m_imsiToTp.end()) {
          dl_tp = itTp->second;
          itTp->second = 0.0; // OK: modifying via iterator
        }

        UEStats &stats = ueStatsMap[ueImsi64];
        stats.IMSI = ueImsi64;
        stats.SINR = sinr_dB;
        stats.RSRP = rsrp;
        stats.dl_tp = dl_tp;
        stats.mcs = kpi.mcs;
        stats.ri = kpi.ri;
        stats.cqi = kpi.cqi;
        if (dl_tp != 0) {
          stats.tp_ongoing = 1;
        }
      }
    }

    // ✅ Logging outside the loop to avoid duplication
    for (auto &pair : ueStatsMap) {
      UEStats &stats = pair.second;

      if (stats.tp_ongoing) {
        NS_LOG_UNCOND("UE stats-> UE "
                      << stats.IMSI << " : Serving cell " << this->GetCellId()
                      << " | SINR: " << std::fixed << std::setprecision(1)
                      << stats.SINR << " dB" // One decimal for SINR
                      << " | RSRP: " << std::fixed << std::setprecision(0)
                      << stats.RSRP
                      << " dBm" // No decimal for RSRP as per example
                      << " | DL TP: " << std::fixed << std::setprecision(1)
                      << stats.dl_tp << " Mbps" // One decimal for DL TP
                      << " | MCS: " << static_cast<uint32_t>(stats.mcs)
                      << " | RI: " << static_cast<uint32_t>(stats.ri)
                      << " | CQI: " << static_cast<uint32_t>(stats.cqi));
        traceFile << Simulator::Now().GetSeconds() << "," << stats.IMSI << ","
                  << this->GetCellId() << "," << std::fixed
                  << std::setprecision(2) << stats.SINR
                  << "," // 2 decimals for CSV
                  << std::fixed << std::setprecision(2) << stats.RSRP
                  << "," // 2 decimals for CSV
                  << std::fixed << std::setprecision(2) << stats.dl_tp
                  << "," // 2 decimals for CSV
                  << static_cast<uint32_t>(stats.mcs) << ","
                  << static_cast<uint32_t>(stats.ri) << ","
                  << static_cast<uint32_t>(stats.cqi) << "\n";
        if (report_to_db) {
          std::ostringstream payload;
          payload << "ue_stats,ue=" << stats.IMSI
                  << " serving_cell=" << this->GetCellId()
                  << ",sinr=" << stats.SINR
                  << ",rsrp=" << stats.RSRP
                  << ",dl_tp=" << stats.dl_tp
                  << ",mcs=" << static_cast<uint32_t>(stats.mcs)
                  << ",ri=" << static_cast<uint32_t>(stats.ri)
                  << ",cqi=" << static_cast<uint32_t>(stats.cqi)
                  << " " << current_sim_time;  // <-- Influx timestamp in ns

          SendToInfluxDB(payload.str());
        }
      } else {
        NS_LOG_UNCOND("UE stats-> UE "
                      << stats.IMSI << " : Serving cell " << this->GetCellId()
                      << " | SINR: " << std::fixed << std::setprecision(1)
                      << stats.SINR << " dB"
                      << " | RSRP: " << std::fixed << std::setprecision(0)
                      << stats.RSRP << " dBm"
                      << " | DL TP: " << std::fixed << std::setprecision(1)
                      << stats.dl_tp << " Mbps");
        traceFile << Simulator::Now().GetSeconds() << "," << stats.IMSI << ","
                  << this->GetCellId() << "," << std::fixed
                  << std::setprecision(2) << stats.SINR << "," << std::fixed
                  << std::setprecision(2) << stats.RSRP << "," << std::fixed
                  << std::setprecision(2) << stats.dl_tp << "\n";
        if (report_to_db) {
          std::ostringstream payload;
          payload << "ue_stats,ue=" << stats.IMSI
                  << " serving_cell=" << this->GetCellId()
                  << ",sinr=" << stats.SINR << ",rsrp=" << stats.RSRP
                  << ",dl_tp=" << stats.dl_tp << ",mcs=" << 0 << ",ri=" << 0
                  << ",cqi=" << 0
                  << " " << current_sim_time;  // <-- Influx timestamp in ns
          SendToInfluxDB(payload.str());
        }
      }
      stats.tp_ongoing = false;
    }
    traceFile.close();
  } else {
    std::cerr << "Error opening file for writing: " << ue_kpi_file.str()
              << std::endl;
  }

  NS_LOG_UNCOND( "---------------------------------------------\n");
}

} // namespace ns3
