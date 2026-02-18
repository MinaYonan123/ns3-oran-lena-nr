// Copyright (c) 2019 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
//
// SPDX-License-Identifier: GPL-2.0-only

#include "nr-gnb-net-device.h"

#include "bandwidth-part-gnb.h"
#include "bwp-manager-gnb.h"
#include "encode_e2apv1.hpp"
#include "nr-gnb-component-carrier-manager.h"
#include "nr-gnb-mac.h"
#include "nr-gnb-phy.h"
#include "nr-gnb-rrc.h"
#include "nr-ue-net-device.h"
#include "nr-ue-phy.h"
#include <ns3/abort.h>
#include <ns3/double.h>
#include <ns3/ipv4-l3-protocol.h>
#include <ns3/ipv6-l3-protocol.h>
#include <ns3/log.h>
#include <ns3/object-map.h>
#include <ns3/pointer.h>
#include <ns3/double.h>
#include <ns3/node-list.h>
#include <ns3/node.h>
#include <algorithm>
#include <cmath>
#include <filesystem> // For filesystem utilities, available since C++17
#include <fstream>
#include <iostream>
#include <ostream>
#include <sstream>
#include <sys/time.h>
#include <vector>
#include "encode_e2apv1.hpp"
#include "ns3/Lena-indication-message-helper.h"
#include "ns3/kpm-function-description.h"
#include "ns3/ric-control-function-description.h"
#include "ns3/ccc-function-description.h"
#include "ns3/ccc-message.h"

#include <curl/curl.h>
#include <string>

std::vector<int> g_ueImsiList;
uint64_t start_sim_time = 0;
uint64_t current_sim_time = 0;
// in your class definition
std::unordered_map<uint32_t, bool> headerWritten_Cell;
std::unordered_map<uint32_t, bool> headerWritten_UE;


void SendToInfluxDB(const std::string &payload) {
    CURL *curl = curl_easy_init();
    std::string influx_host = "localhost";
    std::string influx_port = "8086";
    std::string db_name = "influx";
    
    // Use InfluxDB 1.8 API format (not v2)
    static bool firstCall = true;

    if (curl) {
        const std::string url = "http://" + influx_host + ":" + influx_port +
                                "/write?db=" + db_name;
        
        if (firstCall) {
            std::cout << "InfluxDB: Sending data to " << url << std::endl;
            firstCall = false;
        }
        
        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: text/plain");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L); // 2 second timeout

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            static int errorCount = 0;
            if (errorCount < 5) {  // Only show first 5 errors
                std::cerr << "⚠️  InfluxDB connection failed: " << curl_easy_strerror(res)
                          << " (Make sure InfluxDB is running on " << influx_host << ":" << influx_port << ")"
                          << std::endl;
                errorCount++;
                if (errorCount == 5) {
                    std::cerr << "   (Further InfluxDB errors will be suppressed)" << std::endl;
                }
            }
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}
namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NrGnbNetDevice");

NS_OBJECT_ENSURE_REGISTERED(NrGnbNetDevice);

TypeId
NrGnbNetDevice::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::NrGnbNetDevice")
            .SetParent<NrNetDevice>()
            .AddConstructor<NrGnbNetDevice>()
            .AddAttribute("NrGnbComponentCarrierManager",
                          "The component carrier manager associated to this GnbNetDevice",
                          PointerValue(),
                          MakePointerAccessor(&NrGnbNetDevice::m_componentCarrierManager),
                          MakePointerChecker<NrGnbComponentCarrierManager>())
            .AddAttribute("BandwidthPartMap",
                          "List of Bandwidth Part container.",
                          ObjectMapValue(),
                          MakeObjectMapAccessor(&NrGnbNetDevice::m_ccMap),
                          MakeObjectMapChecker<BandwidthPartGnb>())
            .AddAttribute("NrGnbRrc",
                          "The RRC layer associated with the gNB",
                          PointerValue(),
                          MakePointerAccessor(&NrGnbNetDevice::m_rrc),
                          MakePointerChecker<NrGnbRrc>())
            .AddAttribute ("CellId",
                            "Cell Identifier",
                            UintegerValue (0),
                            MakeUintegerAccessor (&NrGnbNetDevice::m_cellId),
                            MakeUintegerChecker<uint16_t> ())
            .AddAttribute ("E2PdcpCalculator", "The PDCP calculator object for E2 reporting",
                         PointerValue (),
                         MakePointerAccessor (&NrGnbNetDevice::m_e2PdcpStatsCalculator),
                         MakePointerChecker<NrBearerStatsCalculator> ())
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
                           MakeDoubleChecker<double>())
            .AddAttribute("CCC_E2functionID", "Function ID to subscribe", DoubleValue(4),
                           MakeDoubleAccessor(&NrGnbNetDevice::ccc_func_id),
                           MakeDoubleChecker<double>())
            .AddAttribute ("EnableCuUpReport", "If true, send CuUpReport", BooleanValue (true),
                          MakeBooleanAccessor (&NrGnbNetDevice::m_sendCuUp),
                          MakeBooleanChecker ())

            .AddAttribute ("E2Periodicity", "Periodicity of E2 reporting (value in seconds)",
                         DoubleValue (0.1),
                         MakeDoubleAccessor (&NrGnbNetDevice::m_e2Periodicity),
                         MakeDoubleChecker<double> ())

            .AddAttribute (
              "ReducedPmValues", "If true, send only a subset of pmValues", BooleanValue (false),
              MakeBooleanAccessor (&NrGnbNetDevice::m_reducedPmValues), MakeBooleanChecker ())
              .AddAttribute("sim_id", "ID of simulation", UintegerValue(0),
                                      MakeUintegerAccessor(&NrGnbNetDevice::sim_id),
                                      MakeUintegerChecker<uint64_t>())
                        .AddAttribute("report_to_db", "Reporting to InfluxDB",
                                      BooleanValue(false),
                                      MakeBooleanAccessor(&NrGnbNetDevice::report_to_db),
                                      MakeBooleanChecker());

    return tid;
}

NrGnbNetDevice::NrGnbNetDevice()
    : m_forceE2FileLogging (false),m_cellId(0),
    m_reducedPmValues (false),
    m_e2Periodicity (0.1),
    m_cuUpFileName (),
    m_stopSendingMessages(false),
    m_isReportingEnabled (false),
    m_flagControlMessageReceived (false),
    m_flagIndicationSent (false),
    m_NewportsOn(0),
    m_NewportsOff(0),
    m_hasValidSubscription(false),
    m_checkPeriod(MilliSeconds(100)),
    m_currentPowerWatts(0.0),
    m_xAppActive(false),
    m_baselineMinPower(0.0),
    m_baselineMaxPower(0.0),
    m_xAppMinPower(0.0),  // Change from std::numeric_limits<double>::max()
    m_xAppMaxPower(0.0),
    m_baselineCurrentPower(0.0),
    m_xAppCurrentPower(0.0),
    m_xAppActivationTime(Seconds(0)),
    m_baselineAccumulatedPower(0.0),
    m_baselineSampleCount(0),
    m_xAppAccumulatedPower(0.0),
    m_xAppSampleCount(0),
    m_baselineAvgPower(0.0),
    m_xAppAvgPower(0.0)
{
    NS_LOG_FUNCTION(this);
}

NrGnbNetDevice::~NrGnbNetDevice()
{
    NS_LOG_FUNCTION(this);
}

bool lessThan(int x, int y) {
  return x < y;
}

bool greaterThan(int x, int y) {
  return x > y;
}

bool equal(int x, int y) {
  return x == y;
}

std::vector<std::function<bool(int, int)>> MATH_CALL_BACKS = {
  equal, greaterThan, lessThan
};

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
  NS_LOG_DEBUG ("\nReceived RIC Subscription Request, cellId= " << m_cellId << "\n");
  m_lastSubscriptionParams = m_e2term->ProcessRicSubscriptionRequest (sub_req_pdu);
  NS_LOG_DEBUG ("requestorId " << +m_lastSubscriptionParams.requestorId <<
                 ", instanceId " << +m_lastSubscriptionParams.instanceId <<
                 ", ranFuncionId " << +m_lastSubscriptionParams.ranFuncionId <<
                 ", actionId " << +m_lastSubscriptionParams.actionId);
  m_hasValidSubscription = true;
  const auto &sub_map = m_e2term->SubscriptionMapRef();
  if (!sub_map.empty())
  {
    try
    {
      // Check if keys exist
      if (sub_map.find("Test Condition Expression") == sub_map.end() ||
          sub_map.find("Action Definition Format") == sub_map.end() ||
          sub_map.find("Test Condition Value") == sub_map.end())
      {
        NS_LOG_ERROR("Required keys not found in sub_map");
        return;
      }

      const auto& expr = sub_map.at("Test Condition Expression");
      const auto& action = sub_map.at("Action Definition Format");

      int index = std::any_cast<int>(expr);
      int action_def = std::any_cast<int>(action);

      if (index < 0 || index >= static_cast<int>(MATH_CALL_BACKS.size()))
      {
        NS_LOG_ERROR("Invalid index: " << index);
        return;
      }

      switch (action_def)
      {
        case E2SM_KPM_ActionDefinition__actionDefinition_formats_PR_actionDefinition_Format4:
          {
            // Clear PRB history at subscription start
            //m_prbHistory.clear();

            // Start periodic PRB checking
            if (!m_stopSendingMessages)
            {
              Simulator::ScheduleWithContext(1, m_checkPeriod,
                  &NrGnbNetDevice::CheckReportingFlag, this);

              NS_LOG_DEBUG("Started PRB monitoring with period " <<
                          m_checkPeriod.GetMilliSeconds() << "ms");
            }
          }
          break;

        default:
          NS_LOG_ERROR("Action Definition NOT supported");
          break;
      }
    }
    catch (const std::exception& e)
    {
      NS_LOG_ERROR("Error in KpmSubscriptionCallback: " << e.what());
    }
  }
}

void
NrGnbNetDevice::CCCcontrolMessageReceivedCallback(E2AP_PDU_t *sub_req_pdu)
{
  NS_LOG_FUNCTION(this);
  NS_LOG_DEBUG("\nReceived CCC RIC Control Message, cellId= " << m_cellId << "\n");
  m_flagControlMessageReceived = true;
  // Create CCC control message handler
  Ptr<CccControlMessage> cccMsg = Create<CccControlMessage>(sub_req_pdu);
  
  // Print message for debugging
  cccMsg->PrintControlMessage();
  
  // Get RIC style type
  uint32_t ricStyleType = cccMsg->GetRicStyleType();
  NS_LOG_INFO("RIC Style Type: " << ricStyleType);
  
  // Get cells controlled
  std::vector<CellControlInfo> cells = cccMsg->GetCellsControlled();
  NS_LOG_INFO("Number of cells to control: " << cells.size());
  
  // Process each cell
  for (size_t i = 0; i < cells.size(); ++i)
  {
      std::string cellGlobalId = cccMsg->GetCellGlobalId(i);
      NS_LOG_INFO("Processing control for cell: " << cellGlobalId);
      
      // Get configuration structures for this cell
      const CellControlInfo& cellInfo = cells[i];
      
      for (const auto& config : cellInfo.configuration_structures)
      {
          NS_LOG_INFO("Configuration structure: " << config.ran_configuration_structure_name);
          
          // Handle O-NESPolicy
          if (config.ran_configuration_structure_name == "O-NESPolicy")
          {
              json newValues = config.new_values_of_attributes;
              
              // Extract antenna mask and apply as port power
              if (newValues.contains("antennaMask"))
              {
                  std::string antennaMask = newValues["antennaMask"].get<std::string>();
                  NS_LOG_UNCOND("Received antenna mask: " << antennaMask);
                  
                  // Parse antenna mask as binary string (e.g., "1010" -> [1.0, 0.0, 1.0, 0.0])
                  // Each character is either '1' (port enabled) or '0' (port disabled)
                  std::vector<double> portPowerVec;
                  uint16_t portsOn = 0;  // Count of ports with value = 1 (enabled)
                  uint16_t portsOff = 0; // Count of ports with value = 0 (disabled)
                  for (char c : antennaMask)
                  {
                      if (c == '1')
                      {
                          portPowerVec.push_back(1.0);  // Port enabled
                          portsOn++;
                      }
                      else if (c == '0')
                      {
                          portPowerVec.push_back(0.0);  // Port disabled
                          portsOff++;
                      }
                      else if (!std::isspace(c))
                      {
                          NS_LOG_WARN("Invalid character '" << c << "' in antenna mask, skipping");
                      }
                  }
                  m_NewportsOn = portsOn;
                  m_NewportsOff = portsOff;
                  if (!portPowerVec.empty())
                  {
                      NS_LOG_UNCOND("Applying port power with " << portPowerVec.size() << " ports");
                      
                      // Apply port power configuration
                      SetPortPower(portPowerVec);

                      // Log applied values
                      std::cout << "Cell " << m_cellId << " - Port power set to: [";
                      for (size_t i = 0; i < portPowerVec.size(); ++i)
                      {
                          std::cout << portPowerVec[i];
                          if (i < portPowerVec.size() - 1) std::cout << ", ";
                      }
                      std::cout << "] (" << portPowerVec.size() << " ports)" << std::endl;
                      
                      // Count active ports
                      int activePorts = 0;
                      for (double val : portPowerVec)
                      {
                          if (val > 0.0) activePorts++;
                      }
                      std::cout << "Cell " << m_cellId << " - Active ports: " << activePorts 
                                << "/" << portPowerVec.size() << std::endl;
                  }
                  else
                  {
                      NS_LOG_WARN("Antenna mask is empty or invalid");
                  }
              }

          }
      }
  }
  
  NS_LOG_INFO("CCC Control Message processed successfully");
}

void
    NrGnbNetDevice::ControlMessageReceivedCallback(E2AP_PDU_t *sub_req_pdu) {
        NS_LOG_DEBUG("\n\nLteEnbNetDevice::ControlMessageReceivedCallback: Received RIC Control Message");

    // Create RIC Control ACK
    Ptr<RicControlMessage> controlMessage = Create<RicControlMessage>(sub_req_pdu);
    NS_LOG_INFO("After RicControlMessage::RicControlMessage constructor");
    NS_LOG_INFO("Request type " << controlMessage->m_requestType);
}

void
NrGnbNetDevice::SetE2Termination(Ptr<E2Termination> e2term)
{
    m_e2term = e2term;

  if (!m_forceE2FileLogging) {
       long m_e2_func_id = long (e2_func_id);
       long m_rc_e2_func_id = long(rc_e2_func_id);
       long m_ccc_func_id = long(ccc_func_id);
      Ptr<KpmFunctionDescription> kpmFd = Create<KpmFunctionDescription> ();
      e2term->RegisterKpmCallbackToE2Sm (
              m_e2_func_id, kpmFd,std::bind (&NrGnbNetDevice::KpmSubscriptionCallback, this, std::placeholders::_1));

      Ptr <RicControlFunctionDescription> ricCtrlFd = Create<RicControlFunctionDescription>();
      e2term->RegisterSmCallbackToE2Sm(m_rc_e2_func_id, ricCtrlFd,
                                      std::bind(&NrGnbNetDevice::ControlMessageReceivedCallback,
                                                this, std::placeholders::_1));

      Ptr <CccFunctionDescription> cccFd = Create<CccFunctionDescription>();
      e2term->RegisterSmCallbackToE2Sm(m_ccc_func_id, cccFd,
                                      std::bind(&NrGnbNetDevice::CCCcontrolMessageReceivedCallback, this, std::placeholders::_1));

      e2term->RegisterCallbackFunctionToE2Sm(1, std::bind(&NrGnbNetDevice::stopSendingAndCancelSchedule, this));
    }
}

Ptr<E2Termination>
NrGnbNetDevice::GetE2Termination() const
{
    return m_e2term;
}

std::string
NrGnbNetDevice::GetImsiString(uint64_t imsi)
{
  std::string ueImsi = std::to_string(imsi);
  std::string ueImsiComplete {};
  if (ueImsi.length() == 1)
  {
    ueImsiComplete = "0000" + ueImsi;
  }
  else if (ueImsi.length() == 2)
  {
    ueImsiComplete = "000" + ueImsi;
  }
  else
  {
    ueImsiComplete = "00" + ueImsi;
  }
  return ueImsiComplete;
}


template <typename A, typename B>
std::pair<B, A>
flip_pair (const std::pair<A, B> &p)
{
  return std::pair<B, A> (p.second, p.first);
}

template <typename A, typename B>
std::multimap<B, A>
flip_map (const std::map<A, B> &src)
{
  std::multimap<B, A> dst;
  std::transform (src.begin (), src.end (), std::inserter (dst, dst.begin ()), flip_pair<A, B>);
  return dst;
}

Ptr<KpmIndicationHeader>
NrGnbNetDevice::BuildRicIndicationHeader (std::string plmId, std::string gnbId,
                                              uint16_t nrCellId)
{
  if (!m_forceE2FileLogging)
    {
      KpmIndicationHeader::KpmRicIndicationHeaderValues headerValues;
      headerValues.m_plmId = plmId;
      headerValues.m_gnbId = gnbId;
      headerValues.m_nrCellId = nrCellId;
      auto time = Simulator::Now ();
      uint64_t timestamp = m_startTime + (uint64_t) time.GetMilliSeconds ();
      NS_LOG_DEBUG ("NR plmid " << plmId << " gnbId " << gnbId << " nrCellId " << nrCellId);
      NS_LOG_DEBUG ("Timestamp " << timestamp);
      headerValues.m_timestamp = timestamp;


      Ptr<KpmIndicationHeader> header =
          Create<KpmIndicationHeader> (KpmIndicationHeader::GlobalE2nodeType::gNB, headerValues);
      return header;
    }
  else
    {
      return nullptr;
    }
}

/////////////////////////////////////////////////////////////////////

void
NrGnbNetDevice::BuildAndSendReportMessage (E2Termination::RicSubscriptionRequest_rval_s params)
{
  std::cout << "[DEBUG] BuildAndSendReportMessage called" << std::endl;
  m_flagIndicationSent = true;
  std::string plmId = "111";
  std::string gnbId = std::to_string (m_cellId);

  // TODO here we can get something from RRC and onward
  NS_LOG_DEBUG ("NrGnbNetDevice " << m_cellId << " BuildAndSendMessage at time "
                                      << Simulator::Now ().GetSeconds ());
  if (m_sendCuUp)
    {
      // Create CU-UP
      Ptr<KpmIndicationHeader> header = BuildRicIndicationHeader (plmId, gnbId, m_cellId);
      std :: cout << " after BuildRicIndicationHeader "<< std::endl;
      Ptr<KpmIndicationMessage> cuUpMsg = BuildRicIndicationMessageCuUp (plmId);

      // Send CU-UP only if offline logging is disabled
      if (header != nullptr && cuUpMsg != nullptr)
        {
          NS_LOG_DEBUG ("Send NR CU-UP");
          std ::cout << "[DEBUG] Send NR CU-UP" << std::endl;
          E2AP_PDU *pdu_cuup_ue = new E2AP_PDU;
          encoding::generate_e2apv1_indication_request_parameterized (
              pdu_cuup_ue, params.requestorId, params.instanceId, params.ranFuncionId,
              params.actionId,
              1, // TODO sequence number
              (uint8_t *) header->m_buffer, // buffer containing the encoded header
              header->m_size, // size of the encoded header
              (uint8_t *) cuUpMsg->m_buffer, // buffer containing the encoded message
              cuUpMsg->m_size); // size of the encoded message
              std ::cout << "[DEBUG] E2AP PDU generated for CU-UP" << std::endl;
          m_e2term->SendE2Message (pdu_cuup_ue);
          delete pdu_cuup_ue;
        }
    }

  if (m_stopSendingMessages)
    {
      return;
    }

  if (!m_stopSendingMessages && m_is_reported)
    {
      // TODO: replace by global system preodicity(GranularityPeriod).
      // uint64_t perodicity = m_e2term->SubscriptionMapRef ().find ("Granularity Period") !=
      //                               m_e2term->SubscriptionMapRef ().end ()
      //                           ? m_e2term->SubscriptionMapRef ()["Granularity Period"]
      //                           : m_e2Periodicity;
      Simulator::ScheduleWithContext (1, Seconds (m_e2Periodicity),
                                      &NrGnbNetDevice::BuildAndSendReportMessage, this, params);
    }
}




Ptr<KpmIndicationMessage>
NrGnbNetDevice::BuildRicIndicationMessageCuUp(std::string plmId)
{

  Ptr<LenaIndicationMessageHelper> indicationMessageHelper =
      Create<LenaIndicationMessageHelper> (IndicationMessageHelper::IndicationMessageType::CuUp,
                                             m_forceE2FileLogging, m_reducedPmValues);

  // get <rnti, UeManager> map of connected UEs
  auto ueMap = m_rrc->GetUeMap();
  // gNB-wide PDCP volume in downlink
  double cellDlTxVolume = 0;
  // rx bytes in downlink
  double cellDlRxVolume = 0;
  uint16_t numActiveUes = static_cast<uint16_t>(ueMap.size());

  // sum of the per-user average latency
  double perUserAverageLatencySum = 0;

  std::unordered_map<uint64_t, std::string> uePmString {};

  std::vector<double> portPowerVec = GetPortPower();
  double averagePower = GetAveragePower();
  uint16_t portsOn = 0;  // Count of ports with value = 1 (enabled)
  uint16_t portsOff = 0; // Count of ports with value = 0 (disabled)

  
    
  if (!portPowerVec.empty())
  {
    for (size_t i = 0; i < portPowerVec.size(); ++i)
      {
        // Consider values close to 1.0 as "on" and close to 0.0 as "off"
        if (portPowerVec[i] >= 0.5)  // Port enabled (value >= 0.5)
          {
            portsOn++;
          }
        else  // Port disabled (value < 0.5)
          {
            portsOff++;
          }
      }
  }
  for (auto ue : ueMap)
  {
    uint64_t imsi = ue.second->GetImsi();
    std::string ueImsiComplete = GetImsiString (imsi);

    // Accumulate PDCP and RLC stats from all DRBs
    long txDlPackets = 0;
    double txBytes = 0;
    double rxBytes = 0;
    double totalPdcpDelay = 0;
    long txPdcpPduNrRlc = 0;
    double txPdcpPduBytesNrRlc = 0;

    auto drbMap = ue.second->GetDrbMap();
    std::cout << "  [UE IMSI " << imsi << "] has " << drbMap.size() << " DRB(s)" << std::endl;
    
    // FIRST: Get previous cumulative value BEFORE reading/resetting stats
    double prevTxBytes = 0.0;
    auto itPrev = m_prevTxBytesPerUe.find(imsi);
    if (itPrev != m_prevTxBytesPerUe.end())
      {
        prevTxBytes = itPrev->second;
      }
    
    // SECOND: Collect all PDCP stats from all DRBs (cumulative values BEFORE reset)
    for (auto drb : drbMap)
    {
      uint8_t lcid = drb.second->m_logicalChannelIdentity;
      std::cout << "  [DRB_ID " << (int)drb.first << " -> LCID " << (int)lcid << "] ";
      
      long txPacketsForLcid = m_e2PdcpStatsCalculator->GetDlTxPackets(imsi, lcid);
      double txBytesForLcid = m_e2PdcpStatsCalculator->GetDlTxData(imsi, lcid) * 8 / 1e3; // in kbit
      double rxBytesForLcid = m_e2PdcpStatsCalculator->GetDlRxData(imsi, lcid) * 8 / 1e3; // in kbit
      double delayForLcid = m_e2PdcpStatsCalculator->GetDlDelay(imsi, lcid);
      
      std::cout << "PDCP: txPkt=" << txPacketsForLcid << " txKb=" << txBytesForLcid 
                << " rxKb=" << rxBytesForLcid << " delay=" << delayForLcid;
      
      txDlPackets += txPacketsForLcid; 
      txBytes += txBytesForLcid;
      rxBytes += rxBytesForLcid;
      totalPdcpDelay += delayForLcid;
      
      // Get RLC stats
      long rlcTxPkts = drb.second->m_rlc->GetTxPacketsInReportingPeriod();
      double rlcTxBytes = drb.second->m_rlc->GetTxBytesInReportingPeriod();
      
      std::cout << " | RLC: txPkt=" << rlcTxPkts << " txBytes=" << rlcTxBytes << std::endl;
      
      txPdcpPduNrRlc += rlcTxPkts;
      txPdcpPduBytesNrRlc += rlcTxBytes;
    }
    
    // USE THE SAME THROUGHPUT CALCULATED BY BuildGUICuUp
    double pdcpThroughput = 0.0;
    auto itTp = m_lastThroughputPerUe.find(imsi);
    if (itTp != m_lastThroughputPerUe.end())
      {
        pdcpThroughput = itTp->second;
      }
    
    double pdcpThroughputRx = rxBytes / m_e2Periodicity; // unit kBps
    
    // Process RLC bytes
    txPdcpPduBytesNrRlc *= 8 / 1e3;
    double pdcpLatency = totalPdcpDelay / 1e5; // unit: x 0.1 ms
    perUserAverageLatencySum += pdcpLatency;
    
    cellDlTxVolume += txBytes;
    cellDlRxVolume += rxBytes;
    
    std::cout<<"BuildRicIndicationMessageCuUp e2periodicity: " << m_e2Periodicity << std::endl;
    std::cout<<"BuildRicIndicationMessageCuUp: Using throughput from BuildGUICuUp = " 
             << pdcpThroughput << " Mbps" << std::endl;

    std::cout << Simulator::Now().GetSeconds() << " " << m_cellId << " cell, connected UE with IMSI " << imsi
      << " ueImsiString " << ueImsiComplete
      << " txDlPackets " << txDlPackets
      << " txDlPacketsNr " << txPdcpPduNrRlc
      << " txBytes " << txBytes
      << " rxBytes " << rxBytes
      << " txDlBytesNr " << txPdcpPduBytesNrRlc
      << " pdcpLatency " << pdcpLatency
      << " pdcpThroughput " << pdcpThroughput << std::endl;

    // PDCP stats are reset inside the DRB loop above (per LCID)
    if (!indicationMessageHelper->IsOffline ())
      {
        indicationMessageHelper->AddPdcpUePmItem (ueImsiComplete, txPdcpPduBytesNrRlc,
                                                 txPdcpPduNrRlc, pdcpThroughput);
        indicationMessageHelper->AddPHYGnbConfiguration (numActiveUes, m_cellId, portsOn, portsOff);
      }

    uePmString.insert(std::make_pair(imsi, std::to_string(pdcpThroughput)));
  }

  if (!indicationMessageHelper->IsOffline ())
    {
      //indicationMessageHelper->FillCuUpValues (plmId);
    }

  if (m_forceE2FileLogging)
    {
      std::ofstream csv{};
      csv.open (m_cuUpFileName.c_str (), std::ios_base::app);
      if (!csv.is_open ())
        {
          NS_FATAL_ERROR ("Can't open file " << m_cuUpFileName.c_str ());
        }

      uint64_t timestamp = m_startTime + (uint64_t) Simulator::Now ().GetMilliSeconds ();

      for (auto ue : ueMap)
        {
          uint64_t imsi = ue.second->GetImsi ();
          std::string ueImsiComplete = GetImsiString (imsi);

          auto uePmsIt = uePmString.find (imsi);
          std::string pdcpThroughputStr = "0";
          if (uePmsIt != uePmString.end())
            {
              pdcpThroughputStr = uePmsIt->second;
            }

          // Format: timestamp,ueImsiComplete,txBytes,txDlPackets,pdcpThroughput,numActiveUes,cellId
          std::string to_print = std::to_string (timestamp) + "," + ueImsiComplete + ",0,0," +
                                 pdcpThroughputStr + "," + 
                                 std::to_string(numActiveUes) + "," + 
                                 std::to_string(m_cellId) + "\n";

          csv << to_print;
        }
      csv.close ();
      // Schedule next GUI update
      Simulator::Schedule (MilliSeconds (100), &NrGnbNetDevice::BuildGUICuUp, this);
      return nullptr;
    }
  else
    {
      return indicationMessageHelper->CreateIndicationMessage ();
    }
}
//////////////////////////////////////////////////////////////
// Periodic GUI reporting function (similar to mmwave)
void
NrGnbNetDevice::BuildGUICuUp ()
{

  auto ueMap = m_rrc->GetUeMap();
  uint16_t numActiveUes = ueMap.size();

  std::ofstream csv{};
  csv.open (m_cuUpFileName.c_str (), std::ios_base::app);
  if (!csv.is_open ())
    {
      NS_FATAL_ERROR ("Can't file " << m_cuUpFileName.c_str ());
    }

  uint64_t timestamp = m_startTime + (uint64_t) Simulator::Now ().GetMilliSeconds ();
  
  // ===================================================================
  // Check if we're in the first 26 seconds (baseline period)
  // During this period, set certain values to 0
  // ===================================================================
  double currentTime = Simulator::Now().GetSeconds();
  bool isBaselinePeriod = (currentTime < 26.0);  // First 26 seconds
  
  std::vector<double> portPowerVec = GetPortPower();
  double averagePower = GetAveragePower();
  uint16_t portsOn = 0;  // Count of ports with value = 1 (enabled)
  uint16_t portsOff = 0; // Count of ports with value = 0 (disabled)

  
    
  if (!portPowerVec.empty())
  {
    for (size_t i = 0; i < portPowerVec.size(); ++i)
      {
        // Consider values close to 1.0 as "on" and close to 0.0 as "off"
        if (portPowerVec[i] >= 0.5)  // Port enabled (value >= 0.5)
          {
            portsOn++;
          }
        else  // Port disabled (value < 0.5)
          {
            portsOff++;
          }
      }
  }
  std::cout << "BuildGUICuUp: portsOn " << portsOn << " portsOff " << portsOff << std::endl;
  uint8_t indicationFlag = m_flagIndicationSent ? 1 : 0;
  uint8_t controlFlag = m_flagControlMessageReceived ? 1 : 0;
  
  // Reset indication flag (set in BuildAndSendReportMessage, reset after CSV write)
  m_flagIndicationSent = false;
  
  // For control flag: Only reset if it was 1 (meaning it was captured in this write)
  // This ensures the flag stays true until it's written at least once
  if (controlFlag == 1)
  {
      // Control message was received and written, now safe to reset for next cycle
      // Schedule reset after a small delay to ensure CSV is written
      Simulator::Schedule(MilliSeconds(300), [this]() {
          m_flagControlMessageReceived = false;
      });
  }
  
  for (auto ue : ueMap)
    {
      uint64_t imsi = ue.second->GetImsi ();
      std::string ueImsiComplete = GetImsiString (imsi);
      
      // Calculate throughput for this UE using difference-based method
      double pdcpThroughput = 0;
      if (m_e2PdcpStatsCalculator)
        {
          auto drbMap = ue.second->GetDrbMap();
          double txBytes = 0;
          
          // FIRST: Collect ALL data from ALL DRBs (cumulative, no reset)
          // This gives us the total cumulative bytes since simulation start
          for (auto drb : drbMap)
            {
              uint8_t lcid = drb.second->m_logicalChannelIdentity;
              double txBytesForLcid = m_e2PdcpStatsCalculator->GetDlTxData(imsi, lcid) * 8 / 1e3; // in kbit
              txBytes += txBytesForLcid;
              std::cout << "  [DRB_ID " << (int)drb.first << " -> LCID " << (int)lcid 
                        << "] PDCP: txKb=" << txBytesForLcid << std::endl;
            }
          
          std::cout<<"e2periodicity: " << m_e2Periodicity << std::endl;
          std::cout<<"BuildGUICuUp txBytes (cumulative): " << txBytes << std::endl;
          
          // SECOND: Calculate throughput from DIFFERENCE (not absolute value)
          // Get previous cumulative value for this UE
          double prevTxBytes = 0.0;
          auto itPrev = m_prevTxBytesPerUe.find(imsi);
          if (itPrev != m_prevTxBytesPerUe.end())
            {
              prevTxBytes = itPrev->second;
            }
          
          // Calculate bytes transmitted in this period (difference)
          double bytesInPeriod = txBytes - prevTxBytes;
          
          // Handle case where stats were reset by BuildRicIndicationMessageCuUp
          if (bytesInPeriod < 0 || (prevTxBytes > 0 && txBytes < prevTxBytes))
            {
              // Stats were reset, so current value is the bytes in this period since reset
              bytesInPeriod = txBytes;
              // Update previous to 0 since stats were reset
              m_prevTxBytesPerUe[imsi] = 0.0;
            }
          else
            {
              // Normal case: update previous value
              m_prevTxBytesPerUe[imsi] = txBytes;
            }
          
          // Update previous value for next calculation
          m_prevTxBytesPerUe[imsi] = txBytes;
          
          // THIRD: Calculate throughput: bytes_in_period / time_period
          // BuildGUICuUp runs every 100ms = 0.1s
          double guiPeriod = 0.1; // seconds
          pdcpThroughput = bytesInPeriod / (guiPeriod * 1000.0); // Mbps
          m_lastThroughputPerUe[imsi] = pdcpThroughput;
          std::cout<<"BuildGUICuUp: prevTxBytes=" << prevTxBytes 
                   << ", bytesInPeriod=" << bytesInPeriod 
                   << ", throughput=" << pdcpThroughput << " Mbps" << std::endl;
        }
        
        // ===================================================================
        // For first 26 seconds: Set baseline power values to 0
        // After 26 seconds: Use actual calculated values
        // ===================================================================
        double baselineMinPower =  m_baselineMinPower;
        double baselineMaxPower = isBaselinePeriod ? 0.0 : m_baselineMaxPower;
        uint16_t newPortsOff = isBaselinePeriod ? 0 : m_NewportsOff;
        
  // Calculate savings ONLY after both periods complete (after 50s)
  double powerSaving = 0.0;
  double powerSavingPercent = 0.0;

  double currentTime = Simulator::Now().GetSeconds();
  if (currentTime >= 50.0 && m_baselineAccumulatedPower > 0)
  {
      // Power saving using accumulated values
      powerSaving = m_baselineAccumulatedPower - m_xAppAccumulatedPower;
      powerSavingPercent = (powerSaving / m_baselineAccumulatedPower) * 100.0;
  }
  
  // CSV: Parse ACCUMULATED power instead of average
  std::string to_print = std::to_string(timestamp) + "," + ueImsiComplete + ",0,0," +
                    std::to_string(pdcpThroughput) + "," + 
                    std::to_string(numActiveUes) + "," + 
                    std::to_string(m_cellId) + "," + std::to_string(portsOn) + "," +
                    std::to_string(portsOff) + "," + std::to_string(averagePower) + ","+ 
                    std::to_string(indicationFlag) + "," + std::to_string(controlFlag) + "," +
                    std::to_string(m_NewportsOn) + "," + std::to_string(m_NewportsOff) + "," +
                    std::to_string(m_baselineMinPower) + "," + std::to_string(m_baselineMaxPower) + "," +
                    std::to_string(m_baselineAccumulatedPower) + "," +  // ACCUMULATED, not avg
                    std::to_string(m_baselineCurrentPower) + "," +
                    std::to_string(m_xAppMinPower) + "," +
                    std::to_string(m_xAppMaxPower) + "," +
                    std::to_string(m_xAppAccumulatedPower) + "," +      // ACCUMULATED, not avg
                    std::to_string(m_xAppCurrentPower) + "," +
                    std::to_string(powerSaving) + "," +
                    std::to_string(powerSavingPercent) + "," +
                    std::to_string(m_xAppActive ? 1 : 0) + "," +
                    std::to_string(m_baselineSampleCount) + "," +
                    std::to_string(m_xAppSampleCount)+ "\n";
                         
      csv << to_print;
    }
    
  // Write at least one row with cell data if no UEs
  if (ueMap.size() == 0)
    {
      // ===================================================================
      // For first 26 seconds: Set baseline power values to 0
      // After 26 seconds: Use actual calculated values
      // ===================================================================
      double baselineMinPower = isBaselinePeriod ? 0.0 : m_baselineMinPower;
      double baselineMaxPower = isBaselinePeriod ? 0.0 : m_baselineMaxPower;
      uint16_t newPortsOff = isBaselinePeriod ? 0 : m_NewportsOff;
      
      std::string to_print = std::to_string (timestamp) + ",00000,0,0,0," + 
                             std::to_string(numActiveUes) + "," + 
                             std::to_string(m_cellId) + "," + std::to_string(portsOn) + "," 
                             + std::to_string(portsOff) + "," + std::to_string(averagePower) + "," 
                             + std::to_string (indicationFlag) + "," + std::to_string (controlFlag) + "," 
                             + std::to_string(m_NewportsOn) + "," + std::to_string(newPortsOff) + "\n";
      csv << to_print;
    }

  csv.close ();
  Simulator::Schedule (MilliSeconds (100), &NrGnbNetDevice::BuildGUICuUp, this);
}
void
NrGnbNetDevice::SetStartTime (uint64_t st)
{
  m_startTime = st;
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

    // Start power sampling after 100ms
    Simulator::Schedule(MilliSeconds(50), &NrGnbNetDevice::SampleTransmitPower, this);
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
              NS_LOG_DEBUG ("E2sim start in cell " << m_cellId << " force CSV logging "
                                                   << m_forceE2FileLogging);
              //
              if(!m_forceE2FileLogging) {
                  Simulator::Schedule (MicroSeconds (0), &E2Termination::Start, m_e2term);
                }
            }
    
    // Create CSV file for GUI logging (similar to mmwave)
    
        m_cuUpFileName = "nr-cu-up-cell-" + std::to_string (m_cellId) + ".txt";
        std::ofstream csv{};
        csv.open (m_cuUpFileName.c_str ());
        csv << "timestamp,ueImsiComplete,DRB.PdcpSduVolumeDl_Filter.UEID (txBytes),"
               "Tot.PdcpSduNbrDl.UEID (txDlPackets),DRB.PdcpSduBitRateDl.UEID"
               "(pdcpThroughput),"
               "numActiveUes,cellId,portsOn,portsOff,averagePower,indicationflag,controlflag,"
               "newportson,newportsoff,baselineminpower,baselinemaxpower,"
               "baselineaccumulatedpower,baselinecurrentpower,xappminpower,xappmaxpower,xappaccumulatedpower,"
               "xappcurrentpower,powersaving,powersavingpercent,xappactive,baselinesamplecount,xappsamplecount\n";
        csv.close ();
        // Schedule periodic GUI reporting
        Simulator::Schedule (MilliSeconds (100), &NrGnbNetDevice::BuildGUICuUp, this);
      

    if (m_is_reported)
      {
        Simulator::Schedule (MicroSeconds (500),
                             &NrGnbNetDevice::BuildAndSendReportMessage, this,
                             E2Termination::RicSubscriptionRequest_rval_s{});
      }
}



void
NrGnbNetDevice::CheckReportingFlag()
{
  std::cout << " Debug: CheckReportingFlag called for cell " << m_cellId << std::endl;
  NS_LOG_FUNCTION(this);
  if (!m_stopSendingMessages && m_hasValidSubscription)
  {
    std :: cout << "m_is_reported: " << m_is_reported << " m_isReportingEnabled: " << m_isReportingEnabled << std::endl;
    const auto &sub_map = m_e2term->SubscriptionMapRef();
    if (!sub_map.empty())
    { std :: cout << "sub_map is not empty" << std::endl;
        int threshold=0;
      try
      {
        const auto& expr = sub_map.at("Test Condition Expression");
        const auto& value = sub_map.at("Test Condition Value");
        std::cout << "expr type: " << expr.type().name() << std::endl;
        std::cout << "value type: " << value.type().name() << std::endl;
        int index = std::any_cast<int>(expr);
        if (value.type() == typeid(int)) {
            threshold = std::any_cast<int>(value);
          } else if (value.type() == typeid(double)) {
            threshold = static_cast<int>(std::any_cast<double>(value));
          } else if (value.type() == typeid(bool)) {
            threshold = std::any_cast<bool>(value) ? 1 : 0;
          } else if (value.type() == typeid(unsigned char*)) {
            auto p = std::any_cast<unsigned char*>(value);
            if (p != nullptr) threshold = static_cast<int>(p[0]);
          } else if (value.type() == typeid(char*)) {
            auto p = std::any_cast<char*>(value);
            if (p != nullptr) threshold = static_cast<unsigned char>(p[0]);
          } else {
            NS_LOG_ERROR("Unsupported Test Condition Value type: " << value.type().name());
            return;
          }
  
        std::cout << "index: " << index << " threshold: " << threshold << " m_isReportingEnabled: " << m_isReportingEnabled << std::endl;
                        m_is_reported = true;
          // If we haven't started reporting yet, check if we should start
          if (!m_isReportingEnabled)
          {

              m_is_reported = true;
              m_isReportingEnabled = true;
              BuildAndSendReportMessage(m_lastSubscriptionParams);

          }
          else
          {
            // If reporting is already enabled, keep sending reports
           m_is_reported = true;
           m_isReportingEnabled = true;

          }

      }
      catch (const std::exception& e)
      {
        NS_LOG_ERROR("Error checking PRB usage: " << e.what());
      }
    }
    // Schedule next check
    Simulator::ScheduleWithContext(1, m_checkPeriod,
        &NrGnbNetDevice::CheckReportingFlag, this);
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

        // Clear previous contents
        g_ueImsiList.clear();
        m_flowIdToImsi.clear();
        m_prevRxBytes.clear();

        // Collect all UEs
        for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it) {
            Ptr<Node> node = *it;
            int nDevs = node->GetNDevices();
            for (int j = 0; j < nDevs; j++) {
                Ptr<NrUeNetDevice> ueDev = node->GetDevice(j)->GetObject<NrUeNetDevice>();
                if (!ueDev) continue;

                uint64_t imsi = ueDev->GetImsi();
                if (std::find(g_ueImsiList.begin(), g_ueImsiList.end(), imsi) == g_ueImsiList.end()) {
                    g_ueImsiList.push_back(imsi);
                }
            }
        }

        std::cout << "=== UE IMSI List ===" << std::endl;
        for (size_t i = 0; i < g_ueImsiList.size(); ++i) {
            std::cout << "UE index " << i << " -> IMSI: " << g_ueImsiList[i] << std::endl;
        }

        // Schedule first throughput sampling
        // if (m_flowMonitor && m_flowClassifier) {
        //     Simulator::Schedule(Seconds(0.1),
        //                         &NrGnbNetDevice::SampleThroughput, this, m_flowMonitor, m_flowClassifier, 0.1);
        // }
    }

    void NrGnbNetDevice::SampleThroughput(
            Ptr<FlowMonitor> monitor,
            Ptr<Ipv4FlowClassifier> classifier,
            double intervalSec) {
        if (!monitor || !classifier) return;

        monitor->CheckForLostPackets();
        FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

        for (const auto &kv: stats) {
            FlowId id = kv.first;
            const FlowMonitor::FlowStats &fs = kv.second;
            Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(id);

            // Only downlink flows: destination = UE, source = server
            if (t.sourceAddress != Ipv4Address("1.0.0.2")) continue;

            // Map FlowID to UE IMSI using destination IP
            uint64_t imsi = 0;
            for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it) {
                Ptr<Node> node = *it;
                for (int j = 0; j < node->GetNDevices(); j++) {
                    Ptr<NrUeNetDevice> ueDev = node->GetDevice(j)->GetObject<NrUeNetDevice>();
                    if (!ueDev) continue;
                    Ptr<Ipv4> ueIp = node->GetObject<Ipv4>();
                    for (uint32_t k = 1; k < ueIp->GetNInterfaces(); k++) {
                        Ipv4Address addr = ueIp->GetAddress(k, 0).GetLocal();
                        if (addr == t.destinationAddress) {
                            imsi = ueDev->GetImsi();
                            break;
                        }
                    }
                    if (imsi != 0) break;
                }
                if (imsi != 0) break;
            }

            if (imsi == 0) continue; // Flow not matched to any UE yet

            // Store mapping if first time
            if (m_flowIdToImsi.find(id) == m_flowIdToImsi.end()) {
                m_flowIdToImsi[id] = imsi;
            }

            // --- Throughput calculation ---
            uint64_t prev = 0;
            auto itPrev = m_prevRxBytes.find(id);
            if (itPrev != m_prevRxBytes.end()) prev = itPrev->second;

            uint64_t curr = fs.rxBytes;
            uint64_t diff = (curr >= prev) ? (curr - prev) : curr;
            double thrMbps = static_cast<double>(diff) * 8.0 / (intervalSec * 1e6);

            m_imsiToTp[imsi] = thrMbps;
            m_prevRxBytes[id] = curr;

            // --- Packet loss, delay, jitter ---
            double packetLossRatio = 0.0;
            if (fs.txPackets > 0) {
                packetLossRatio = 1.0 - (double) fs.rxPackets / (double) fs.txPackets;
            }

            double meanDelayMs = 0.0;
            double meanJitterMs = 0.0;
            if (fs.rxPackets > 0) {
                meanDelayMs = 1000.0 * fs.delaySum.GetSeconds() / fs.rxPackets;
                meanJitterMs = 1000.0 * fs.jitterSum.GetSeconds() / fs.rxPackets;
            }

            // Store KPI per-UE
            m_imsiToPacketLoss[imsi] = packetLossRatio;
            m_imsiToDelay[imsi] = meanDelayMs;
            m_imsiToJitter[imsi] = meanJitterMs;

            // Debug logging (optional)
            /*
            NS_LOG_UNCOND("UE " << imsi << " Flow " << id
                           << "  Thr=" << thrMbps << " Mbps"
                           << "  Loss=" << packetLossRatio * 100 << " %"
                           << "  Delay=" << meanDelayMs << " ms"
                           << "  Jitter=" << meanJitterMs << " ms");
            */
        }

        int now_ms = Simulator::Now().GetMilliSeconds();
        current_sim_time = (sim_id + (uint64_t) now_ms) * 1000000ULL;

        // Schedule KPI tracking
        Simulator::Schedule(MilliSeconds(0), &NrGnbNetDevice::Cell_KPI_tracker, this);
        Simulator::Schedule(MilliSeconds(0), &NrGnbNetDevice::UE_KPI_tracker, this);

        // Schedule next throughput sample
        Simulator::Schedule(Seconds(intervalSec),
                            &NrGnbNetDevice::SampleThroughput, this,
                            monitor, classifier, intervalSec);
    }

      void NrGnbNetDevice::Cell_KPI_tracker()
       {
        NS_LOG_UNCOND("---------------------------------------------");

        // Create a folder
        std::stringstream folderName;
        folderName << "trace_" << sim_id;
        if (!std::filesystem::exists(folderName.str())) {
            std::filesystem::create_directory(folderName.str());
        }

        // File path
        std::stringstream cell_kpi_file;
        cell_kpi_file << folderName.str() << "/Cell_" << this->GetCellId()
                << "_Cell_stats_" << sim_id << ".csv";

        std::ofstream traceFile(cell_kpi_file.str(), std::ios::out | std::ios::app);
        if (!traceFile.is_open()) {
            std::cerr << "Error opening file for writing: " << cell_kpi_file.str() << std::endl;
            return;
        }

        // Write header once
        if (!headerWritten_Cell[this->GetCellId()] && traceFile.tellp() == 0) {
            traceFile << "TS,CELL_ID,PRB_USAGE,CURR_PRB,"
                    << "AVG_TP,AVG_PKT_LOSS,AVG_DELAY_MS,AVG_JITTER_MS,"
                    << "TOT_TP,UE_COUNT\n";
            headerWritten_Cell[this->GetCellId()] = true;
        }

        Ptr<NrGnbPhy> gnbPhy = GetPhy(0);
        NrGnbPhy::RbStats stats = gnbPhy->GetRBStats();

        // Cell stats
        CellStats cellStats;
        cellStats.cellId = this->GetCellId();
        cellStats.prbUsagePercentage = stats.prbUsagePercentage;
        cellStats.averageLastRb = stats.averageLastRb;

        double sumTp = 0.0, sumLoss = 0.0, sumDelay = 0.0, sumJitter = 0.0;
        uint32_t ueCount = 0;

        for (auto &kv: m_imsiToTp) {
            uint64_t imsi = kv.first;
            double tp = kv.second;

            Ptr<NetDevice> dev = nullptr;
            for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it) {
                Ptr<Node> node = *it;
                for (uint32_t i = 0; i < node->GetNDevices(); ++i) {
                    Ptr<NrUeNetDevice> ueDev = node->GetDevice(i)->GetObject<NrUeNetDevice>();
                    if (ueDev && ueDev->GetImsi() == imsi && ueDev->GetCellId() == this->GetCellId()) {
                        dev = ueDev;
                        break;
                    }
                }
                if (dev) break;
            }
            if (!dev) continue;
            if (tp <= 0.0) continue;

            sumTp += tp;

            auto itLoss = m_imsiToPacketLoss.find(imsi);
            auto itDelay = m_imsiToDelay.find(imsi);
            auto itJitter = m_imsiToJitter.find(imsi);

            if (itLoss != m_imsiToPacketLoss.end()) sumLoss += itLoss->second;
            if (itDelay != m_imsiToDelay.end()) sumDelay += itDelay->second;
            if (itJitter != m_imsiToJitter.end()) sumJitter += itJitter->second;

            ueCount++;
        }

        double avgTp = (ueCount > 0) ? sumTp / ueCount : 0.0;
        double avgLoss = (ueCount > 0) ? sumLoss / ueCount : 0.0;
        double avgDelay = (ueCount > 0) ? sumDelay / ueCount : 0.0;
        double avgJitter = (ueCount > 0) ? sumJitter / ueCount : 0.0;

        // Console log
        NS_LOG_UNCOND("Cell stats-> gNB " << cellStats.cellId
                      << " | PRB Usage: " << std::fixed << std::setprecision(0)
                      << cellStats.prbUsagePercentage << " %"
                      << " | Avg Last RB: " << std::fixed << std::setprecision(0)
                      << cellStats.averageLastRb
                      << " | Avg TP: " << std::fixed << std::setprecision(2) << avgTp << " Mbps"
                      << " | Tot TP: " << std::fixed << std::setprecision(2) << sumTp << " Mbps"
                      << " | Avg Loss: " << std::fixed << std::setprecision(2) << avgLoss * 100
                      << " %"
                      << " | Avg Delay: " << std::fixed << std::setprecision(2) << avgDelay
                      << " ms"
                      << " | Avg Jitter: " << std::fixed << std::setprecision(2) << avgJitter
                      << " ms"
                      << " | UE Count: " << ueCount);

        // CSV
        traceFile << Simulator::Now().GetSeconds() << ","
                << cellStats.cellId << ","
                << std::fixed << std::setprecision(2) << cellStats.prbUsagePercentage << ","
                << std::fixed << std::setprecision(2) << cellStats.averageLastRb << ","
                << std::fixed << std::setprecision(2) << avgTp << ","
                << std::fixed << std::setprecision(4) << avgLoss << ","
                << std::fixed << std::setprecision(2) << avgDelay << ","
                << std::fixed << std::setprecision(2) << avgJitter << ","
                << std::fixed << std::setprecision(2) << sumTp << ","
                << ueCount << "\n";
        traceFile.flush();
        if (report_to_db) {
            if (avgTp > 0) {
                std::ostringstream payload;
                payload << "cell_stats,cell_id=" << cellStats.cellId
                        << " prb_usage=" << cellStats.prbUsagePercentage
                        << ",avg_last_rb=" << cellStats.averageLastRb
                        << ",avg_tp=" << avgTp
                        << ",tot_tp=" << sumTp
                        << ",avg_pkt_loss=" << avgLoss
                        << ",avg_delay_ms=" << avgDelay
                        << ",avg_jitter_ms=" << avgJitter
                        << ",ue_count=" << ueCount // ✅ Added in DB export
                        << " " << current_sim_time;

                SendToInfluxDB(payload.str());
            } else {
                std::ostringstream payload;
                payload << "cell_stats,cell_id=" << cellStats.cellId
                        << " prb_usage=" << cellStats.prbUsagePercentage
                        << ",avg_last_rb=" << cellStats.averageLastRb
                        << ",ue_count=" << ueCount // ✅ Added in DB export
                        << " " << current_sim_time;

                SendToInfluxDB(payload.str());
            }
        }
    }


    void NrGnbNetDevice::UE_KPI_tracker() {
        std::stringstream folderName;
        folderName << "trace_" << sim_id;
        if (!std::filesystem::exists(folderName.str())) {
            std::filesystem::create_directory(folderName.str());
        }

        std::stringstream ue_kpi_file;
        ue_kpi_file << folderName.str() << "/Cell_" << this->GetCellId()
                << "_UE_stats_" << sim_id << ".csv";

        std::ofstream traceFile(ue_kpi_file.str(), std::ios::out | std::ios::app);
        if (!traceFile.is_open()) {
            std::cerr << "Error opening file for writing: " << ue_kpi_file.str() << std::endl;
            return;
        }

        // Header
        if (!headerWritten_UE[this->GetCellId()] && traceFile.tellp() == 0) {
            traceFile << "TS,IMSI,CELL_ID,SINR,RSRP,DL_TP,MCS,RI,CQI,"
                    << "PKT_LOSS,DELAY_MS,JITTER_MS\n";
            headerWritten_UE[this->GetCellId()] = true;
        }

        std::unordered_map<uint64_t, UEStats> ueStatsMap;

        // Collect per UE
        for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it) {
            Ptr<Node> node = *it;
            for (uint32_t i = 0; i < node->GetNDevices(); ++i) {
                Ptr<NrUeNetDevice> ueDevice = node->GetDevice(i)->GetObject<NrUeNetDevice>();
                if (!ueDevice || ueDevice->GetCellId() != this->GetCellId())
                    continue;

                Ptr<NrUePhy> uePhy = ueDevice->GetPhy(0);
                if (!uePhy) continue;

                uePhy->ReportUeMeasurements();
                double rsrp = uePhy->GetRsrp();
                double sinrLin = uePhy->GetSINR();
                double sinrDb = 10 * log10(sinrLin);

                UeKpiInfo kpi = uePhy->GetUEkpi();

                uint64_t imsi = ueDevice->GetImsi();

                double dl_tp = 0.0;
                auto itTp = m_imsiToTp.find(imsi);
                if (itTp != m_imsiToTp.end()) dl_tp = itTp->second;

                double pktLoss = 0.0, delayMs = 0.0, jitterMs = 0.0;
                auto itLoss = m_imsiToPacketLoss.find(imsi);
                auto itDelay = m_imsiToDelay.find(imsi);
                auto itJitter = m_imsiToJitter.find(imsi);

                if (itLoss != m_imsiToPacketLoss.end()) pktLoss = itLoss->second;
                if (itDelay != m_imsiToDelay.end()) delayMs = itDelay->second;
                if (itJitter != m_imsiToJitter.end()) jitterMs = itJitter->second;

                UEStats &stats = ueStatsMap[imsi];
                stats.IMSI = imsi;
                stats.cell_id = this->GetCellId();
                stats.SINR = sinrDb;
                stats.RSRP = rsrp;
                stats.dl_tp = dl_tp;
                stats.mcs = kpi.mcs;
                stats.ri = kpi.ri;
                stats.cqi = kpi.cqi;
                stats.pktLoss = pktLoss;
                stats.delay = delayMs;
                stats.jitter = jitterMs;
                stats.tp_ongoing = (dl_tp > 0.0);
            }
        }

        // Write + log
        for (auto &pair: ueStatsMap) {
            UEStats &stats = pair.second;

            NS_LOG_UNCOND("UE stats -> UE " << stats.IMSI
                          << " | Cell ID: " << stats.cell_id // ✅ Explicit cell_id
                          << " | SINR: " << std::fixed << std::setprecision(1) << stats.SINR
                          << " dB"
                          << " | RSRP: " << std::fixed << std::setprecision(0) << stats.RSRP
                          << " dBm"
                          << " | DL TP: " << std::fixed << std::setprecision(1) << stats.dl_tp
                          << " Mbps"
                          << " | MCS: " << static_cast<uint32_t>(stats.mcs)
                          << " | RI: " << static_cast<uint32_t>(stats.ri)
                          << " | CQI: " << static_cast<uint32_t>(stats.cqi)
                          << " | Loss: " << std::fixed << std::setprecision(2)
                          << stats.pktLoss * 100 << " %"
                          << " | Delay: " << std::fixed << std::setprecision(2) << stats.delay
                          << " ms"
                          << " | Jitter: " << std::fixed << std::setprecision(2) << stats.jitter
                          << " ms");

            traceFile << Simulator::Now().GetSeconds() << "," << stats.IMSI << ","
                    << stats.cell_id << "," // ✅ Explicit cell_id
                    << std::fixed << std::setprecision(2) << stats.SINR << ","
                    << std::fixed << std::setprecision(2) << stats.RSRP << ","
                    << std::fixed << std::setprecision(2) << stats.dl_tp << ","
                    << static_cast<uint32_t>(stats.mcs) << ","
                    << static_cast<uint32_t>(stats.ri) << ","
                    << static_cast<uint32_t>(stats.cqi) << ","
                    << std::fixed << std::setprecision(4) << stats.pktLoss << ","
                    << std::fixed << std::setprecision(2) << stats.delay << ","
                    << std::fixed << std::setprecision(2) << stats.jitter << "\n";
            if (report_to_db) {
                if (stats.tp_ongoing) {
                    std::ostringstream payload;
                    payload << "ue_stats,ue=" << stats.IMSI
                            << ",cell_id=" << stats.cell_id // ✅ Use cell_id consistently
                            << " sinr=" << stats.SINR
                            << ",rsrp=" << stats.RSRP
                            << ",dl_tp=" << stats.dl_tp
                            << ",mcs=" << static_cast<uint32_t>(stats.mcs)
                            << ",ri=" << static_cast<uint32_t>(stats.ri)
                            << ",cqi=" << static_cast<uint32_t>(stats.cqi)
                            << ",pkt_loss=" << stats.pktLoss
                            << ",delay_ms=" << stats.delay
                            << ",jitter_ms=" << stats.jitter
                            << " " << current_sim_time;
                    SendToInfluxDB(payload.str());
                } else {
                    std::ostringstream payload;
                    payload << "ue_stats,ue=" << stats.IMSI
                            << ",cell_id=" << stats.cell_id // ✅ Use cell_id consistently
                            << " sinr=" << stats.SINR
                            << ",rsrp=" << stats.RSRP
                            << " " << current_sim_time;
                    SendToInfluxDB(payload.str());
                }
            }
            stats.tp_ongoing = false;
        }

        traceFile.flush();

        // Reset TP
        for (auto &pair: m_imsiToTp) {
            pair.second = 0.0;
        }

        NS_LOG_UNCOND("---------------------------------------------");
    }

    void
NrGnbNetDevice::SetPortPower(const std::vector<double>& portPowerVec)
{
    NS_LOG_FUNCTION(this);
    
    // Validate port power values
    double sum = 0.0;
    for (double power : portPowerVec)
    {
        NS_ASSERT_MSG(power >= 0.0, "Port power must be non-negative");
        sum += power;
    }
    
    NS_LOG_INFO("Setting port power configuration with " << portPowerVec.size() 
                << " ports, sum=" << sum);
                bool isXAppChange = false;
                if (!m_portPowerConfig.empty() && m_portPowerConfig.size() == portPowerVec.size())
                {
                    // Compare with current config - if different, it's an xApp change
                    for (size_t i = 0; i < portPowerVec.size(); ++i)
                    {
                        if (std::abs(m_portPowerConfig[i] - portPowerVec[i]) > 0.01)
                        {
                            isXAppChange = true;
                            break;
                        }
                    }
                }
                else if (m_portPowerConfig.empty() && portPowerVec.size() == 4)
                {
                    // First time setting - check if it's not all ports on (default would be all on)
                    int activePorts = 0;
                    for (double p : portPowerVec) if (p > 0.5) activePorts++;
                    if (activePorts < 4)
                    {
                        isXAppChange = true; // xApp reduced ports from default
                    }
                }
                
                // Mark xApp as active if port configuration changed
                if (isXAppChange && !m_xAppActive)
                {
                    m_xAppActive = true;
                    m_xAppActivationTime = Simulator::Now();
                    NS_LOG_UNCOND("=== xApp ACTIVATED: Port configuration changed at time " 
                                  << Simulator::Now().GetSeconds() << "s ===");
                    std::cout << "=== xApp ACTIVATED: Starting power tracking with xApp ===" << std::endl;
                }
    // Store the configuration
    m_portPowerConfig = portPowerVec;
    
    // Clear old power samples to ensure sharp change (no smoothing from old data)
    m_powerSamples.clear();
    NS_LOG_INFO("Cleared power samples for sharp response to port configuration change");
    
    // Calculate port power scaling factor
    double portPowerScaling = 1.0;
    if (!portPowerVec.empty())
    {
        // Sum of port powers divided by number of ports
        // Example: [1,1,0,0] -> (1+1+0+0)/4 = 0.5 (50% effective power)
        portPowerScaling = sum / portPowerVec.size();
    }
    
    // Apply port power scaling to all PHY instances (BWPs)
    for (auto& bwp : m_ccMap)
    {
        Ptr<NrGnbPhy> phy = bwp.second->GetPhy();
        if (phy)
        {
            phy->SetPortPowerScaling(portPowerScaling);
            NS_LOG_INFO("Set port power scaling " << portPowerScaling 
                        << " on BWP " << (int)bwp.first);
        }
    }
    
    std::cout << "Port power configuration set for gNB " << m_cellId 
    << " with " << portPowerVec.size() << " ports"
    << " (Active ports: " << (sum > 0 ? static_cast<int>(sum) : 0) << ")"
    << " (scaling factor: " << portPowerScaling << ")"
    << (isXAppChange ? " [xApp]" : " [Initial]") << std::endl;
}

std::vector<double>
NrGnbNetDevice::GetPortPower() const
{
    NS_LOG_FUNCTION(this);
    return m_portPowerConfig;
}

void
NrGnbNetDevice::SampleTransmitPower()
{
    NS_LOG_FUNCTION(this);
    
    double totalPower = 0.0;
    uint32_t numBwps = 0;
    
    // Get PRB usage statistics
    Ptr<NrGnbPhy> gnbPhy = GetPhy(0);
    double prbUsageFactor = 1.0; // Default to 100% if stats not available
    uint16_t numActiveUes = 0;
    
    if (gnbPhy)
    {
        NrGnbPhy::RbStats stats = gnbPhy->GetRBStats();
        
        // PRB usage percentage (0-100) converted to factor (0.0-1.0)
        if (stats.iterations > 0)
        {
            prbUsageFactor = (stats.prbUsagePercentage / stats.iterations) / 100.0;
        }
        else
        {
            prbUsageFactor = stats.prbUsagePercentage / 100.0;
        }
        
        // Clamp to valid range [0.0, 1.0]
        prbUsageFactor = std::max(0.0, std::min(1.0, prbUsageFactor));
        
        // Get number of active UEs
        auto ueMap = m_rrc->GetUeMap();
        numActiveUes = static_cast<uint16_t>(ueMap.size());
    }
    
    // Calculate number of active ports (ports with power > 0)
    uint16_t numActivePorts = 0;
    if (!m_portPowerConfig.empty())
    {
        for (double portPower : m_portPowerConfig)
        {
            if (portPower > 0.0)
            {
                numActivePorts++;
            }
        }
    }
    else
    {
        // Default: assume all ports active if config not set
        numActivePorts = 4; // Default number of ports
    }
    
    // Get total number of ports (typically 4)
    uint16_t totalPorts = m_portPowerConfig.empty() ? 4 : static_cast<uint16_t>(m_portPowerConfig.size());
    
    // Calculate current transmit power from all BWPs
    for (auto& bwp : m_ccMap)
    {
        Ptr<NrGnbPhy> phy = bwp.second->GetPhy();
        if (phy)
        {
            // Get base TX power from PHY (50 dBm from scenario - this is TOTAL power when all ports are on)
            // Note: GetTxPower() may return effective power with port scaling, so we need the base
            // The scenario sets TxPower attribute to 50 dBm
            double baseTxPowerDbm = phy->GetTxPower(); // This gets effective power (may include port scaling)
            
            // If GetTxPower() returns effective power, we need to get the base power
            // Check if port scaling is applied - if so, reverse it to get base power
            double portScaling = phy->GetPortPowerScaling();
            if (portScaling > 0.0 && portScaling < 1.0)
            {
                // Reverse the port scaling to get base power
                // Effective = Base + 10*log10(scaling), so Base = Effective - 10*log10(scaling)
                baseTxPowerDbm = baseTxPowerDbm - 10.0 * std::log10(portScaling);
            }
            
            if (baseTxPowerDbm <= 0)
            {
                baseTxPowerDbm = 50.0; // Default: 50 dBm = 100 W (from scenario)
            }
            
            // Convert total base power to watts (50 dBm = 100 W when all 4 ports are on)
            double totalMaxPowerWatts = std::pow(10.0, (baseTxPowerDbm - 30.0) / 10.0);
            
            // ============================================================
            // REAL 5G gNB POWER CONSUMPTION MODEL (Telecom Expert)
            // ============================================================
            // Model: P_total = P_base + (N_active / N_total) × P_dynamic × traffic_factor
            //
            // Where:
            // - P_base = 25% of max power (baseband, cooling, control plane) = 25 W
            // - P_dynamic = 75% of max power (RF amplifiers, scales with active ports) = 75 W
            // - N_active = number of active ports
            // - N_total = total number of ports (4)
            // - traffic_factor = small adjustment (0.9 to 1.0) based on PRB usage
            //
            // Real values for 50 dBm (100W) gNB:
            // - 4 ports on, 100% PRB: 25W + (4/4)×75W×1.0 = 100W
            // - 4 ports on, 50% PRB: 25W + (4/4)×75W×0.95 = 96.25W (small reduction)
            // - 2 ports on, 100% PRB: 25W + (2/4)×75W×1.0 = 62.5W
            // - 1 port on, 100% PRB: 25W + (1/4)×75W×1.0 = 43.75W
            // ============================================================
            
            const double basePowerRatio = 0.25;  // 25% base power (always on)
            const double dynamicPowerRatio = 0.75; // 75% dynamic power (scales with ports)
            
            double basePowerWatts = totalMaxPowerWatts * basePowerRatio;
            double dynamicPowerWatts = totalMaxPowerWatts * dynamicPowerRatio;
            
            // Calculate port scaling factor
            double portRatio = static_cast<double>(numActivePorts) / static_cast<double>(totalPorts);
            
            // Traffic factor: small adjustment (5-10%) based on PRB usage
            // This accounts for power amplifier efficiency variation with load
            // Range: 0.90 (low PRB) to 1.0 (high PRB)
            double trafficFactor = 0.90 + (0.10 * prbUsageFactor);
            
            // Final effective power
            // Base power is constant, dynamic power scales with ports and traffic
            double effectivePowerWatts = basePowerWatts + 
                                        (dynamicPowerWatts * portRatio * trafficFactor);
            
            totalPower += effectivePowerWatts;
            numBwps++;
            
            // Convert to dBm for logging
            double effectivePowerDbm = 10.0 * std::log10(std::max(1e-6, effectivePowerWatts)) + 30.0;
            
            std::cout << std::fixed << std::setprecision(2)
                      << "Power calculation for BWP " << (int)bwp.first << ": "
                      << "Base_TX=" << baseTxPowerDbm << " dBm (" << totalMaxPowerWatts << " W max), "
                      << "Active_ports=" << numActivePorts << "/" << totalPorts << ", "
                      << "Base_power=" << basePowerWatts << " W, "
                      << "Dynamic_power=" << (dynamicPowerWatts * portRatio) << " W, "
                      << "PRB_factor=" << std::setprecision(2) << prbUsageFactor << ", "
                      << "Traffic_factor=" << std::setprecision(2) << trafficFactor << ", "
                      << "Final=" << std::setprecision(2) << effectivePowerDbm << " dBm, "
                      << "Watts=" << std::setprecision(3) << effectivePowerWatts << " W" << std::endl;
        }
    }
    
    if (numBwps > 0)
    {
        // Store current power directly (NO SAMPLING, NO SMOOTHING)
        m_currentPowerWatts = totalPower; // Store in watts
        
 // ============================================================
        // ACCUMULATE POWER FOR BASELINE vs xAPP COMPARISON
        // Both periods are exactly 25 seconds (250 samples) for fair comparison
        // Baseline: 0-25 seconds (WITHOUT xApp - use actual power)
        // xApp: 25-50 seconds (WITH xApp - use actual power)
        // ============================================================
        double currentTime = Simulator::Now().GetSeconds();
        double baselinePeriodStart = 0.0;
        double baselinePeriodEnd = 25.0;
        double xAppPeriodStart = 25.0;
        double xAppPeriodEnd = 50.0;
        
        // BASELINE PERIOD: 0 to 25 seconds
        // Accumulate ALL samples during 0-25s (no m_xAppActive check)
        // For baseline period
        if (currentTime >= baselinePeriodStart && currentTime < baselinePeriodEnd)
        {
            m_baselineAccumulatedPower += totalPower;
            m_baselineSampleCount++;
            
            // Update min/max/current
            // If first sample OR power is less than current min
            if (m_baselineSampleCount == 1 || totalPower < m_baselineMinPower) {
                m_baselineMinPower = totalPower;
            }
            if (totalPower > m_baselineMaxPower) m_baselineMaxPower = totalPower;
            m_baselineCurrentPower = totalPower;
        }

        // For xApp period  
        if (currentTime >= xAppPeriodStart && currentTime < xAppPeriodEnd)
        {
            m_xAppAccumulatedPower += totalPower;
            m_xAppSampleCount++;
            
            // Update min/max/current
            // If first sample OR power is less than current min
            if (m_xAppSampleCount == 1 || totalPower < m_xAppMinPower) {
                m_xAppMinPower = totalPower;
            }
            if (totalPower > m_xAppMaxPower) m_xAppMaxPower = totalPower;
            m_xAppCurrentPower = totalPower;
        }
        else if (currentTime > xAppPeriodEnd)
        {
            // xApp period completed - stop accumulating
            // The average is already calculated and stored in m_xAppAvgPower
            if (m_xAppActive && m_xAppSampleCount > 0)
            {
                NS_LOG_DEBUG("xApp period completed at " << currentTime << "s. "
                             << "Final avg=" << m_xAppAvgPower << " W over " 
                             << m_xAppSampleCount << " samples (period: 25-50s)");
            }
        }

        // ============================================================
        // POWER TRACKING FOR COMPARISON: Baseline vs xApp
        // ============================================================
        
        if (!m_xAppActive)
        {
            // BASELINE SCENARIO: Only throughput changes, no port changes
            // Calculate what power would be with all ports on (baseline)
            // This simulates the scenario without xApp
            double baselinePower = totalPower; // Current power (all ports on, throughput varies)
            
            // Update baseline min/max
            if (baselinePower < m_baselineMinPower)
            {
                m_baselineMinPower = baselinePower;
            }
            if (baselinePower > m_baselineMaxPower)
            {
                m_baselineMaxPower = baselinePower;
            }
            m_baselineCurrentPower = baselinePower;
            
            NS_LOG_DEBUG("Baseline power: " << baselinePower << " W (min=" 
                         << m_baselineMinPower << ", max=" << m_baselineMaxPower << ")");
        }
        else
        {
            // xAPP SCENARIO: Throughput changes + port changes
            m_xAppCurrentPower = totalPower;
            
            // Update xApp min/max
            if (totalPower < m_xAppMinPower)
            {
                m_xAppMinPower = totalPower;
            }
            if (totalPower > m_xAppMaxPower)
            {
                m_xAppMaxPower = totalPower;
            }
            
            // Calculate baseline power for comparison (what it would be with all ports on)
            // Recalculate with all ports active to simulate baseline scenario
            double baselinePower = 0.0;
            for (auto& bwp : m_ccMap)
            {
                Ptr<NrGnbPhy> phy = bwp.second->GetPhy();
                if (phy)
                {
                    double baseTxPowerDbm = phy->GetTxPower();
                    double portScaling = phy->GetPortPowerScaling();
                    if (portScaling > 0.0 && portScaling < 1.0)
                    {
                        baseTxPowerDbm = baseTxPowerDbm - 10.0 * std::log10(portScaling);
                    }
                    if (baseTxPowerDbm <= 0) baseTxPowerDbm = 50.0;
                    
                    double totalMaxPowerWatts = std::pow(10.0, (baseTxPowerDbm - 30.0) / 10.0);
                    const double basePowerRatio = 0.25;
                    const double dynamicPowerRatio = 0.75;
                    double basePowerWatts = totalMaxPowerWatts * basePowerRatio;
                    double dynamicPowerWatts = totalMaxPowerWatts * dynamicPowerRatio;
                    
                    // Calculate with all ports on (baseline scenario)
                    double portRatio = 1.0; // All ports on
                    double trafficFactor = 0.90 + (0.10 * prbUsageFactor);
                    double baselineBwpPower = basePowerWatts + (dynamicPowerWatts * portRatio * trafficFactor);
                    baselinePower += baselineBwpPower;
                }
            }
            
            // Update baseline stats even when xApp is active (for comparison)
            if (baselinePower < m_baselineMinPower)
            {
                m_baselineMinPower = baselinePower;
            }
            if (baselinePower > m_baselineMaxPower)
            {
                m_baselineMaxPower = baselinePower;
            }
            m_baselineCurrentPower = baselinePower;
            
            NS_LOG_DEBUG("xApp power: " << totalPower << " W (min=" 
                         << m_xAppMinPower << ", max=" << m_xAppMaxPower << "), "
                         << "Baseline (simulated): " << baselinePower << " W");
        }
        
        NS_LOG_DEBUG("Current power: " << (10.0 * std::log10(std::max(1e-6, totalPower)) + 30.0) 
                     << " dBm (" << totalPower << " W) from " 
                     << numBwps << " BWPs, Active ports: " << numActivePorts
                     << ", PRB usage: " << (prbUsageFactor * 100.0) 
                     << "%, UEs: " << numActiveUes);
    }
    
    // Schedule next sample in 100ms
    Simulator::Schedule(MilliSeconds(100), &NrGnbNetDevice::SampleTransmitPower, this);
}

double
NrGnbNetDevice::GetAveragePower() const
{
    NS_LOG_FUNCTION(this);
    
    // Return current power directly (NO AVERAGING, NO SMOOTHING)
    // This ensures sharp response to port configuration changes
    if (m_currentPowerWatts <= 0.0)
    {
        return 0.0;
    }
    
    double currentWatts = m_currentPowerWatts;
    double currentDbm = 10.0 * std::log10(currentWatts) + 30.0;
    
    NS_LOG_DEBUG("Current power (no averaging): " << currentDbm << " dBm (" << currentWatts << " W)");
    
    return currentWatts;  // Return watts
}

void
NrGnbNetDevice::ClearPowerSamples()
{
    NS_LOG_FUNCTION(this);
    m_powerSamples.clear();
}

} // namespace ns3
