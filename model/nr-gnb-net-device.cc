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
#include <ns3/object-map.h>
#include <ns3/pointer.h>
#include <ns3/double.h>
#include <ns3/node-list.h>
#include <ns3/node.h>
#include <cmath>
#include <filesystem> // For filesystem utilities, available since C++17
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <vector>
#include "encode_e2apv1.hpp"
#include "ns3/Lena-indication-message-helper.h"

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
            .AddAttribute ("EnableCuUpReport", "If true, send CuUpReport", BooleanValue (true),
                          MakeBooleanAccessor (&NrGnbNetDevice::m_sendCuUp),
                          MakeBooleanChecker ())
            .AddAttribute ("E2Periodicity", "Periodicity of E2 reporting (value in seconds)",
                         DoubleValue (0.1),
                         MakeDoubleAccessor (&NrGnbNetDevice::m_e2Periodicity),
                         MakeDoubleChecker<double> ())       
            .AddAttribute (
              "ReducedPmValues", "If true, send only a subset of pmValues", BooleanValue (false),
              MakeBooleanAccessor (&NrGnbNetDevice::m_reducedPmValues), MakeBooleanChecker ());                         
    return tid;
}

NrGnbNetDevice::NrGnbNetDevice()
    : m_forceE2FileLogging (false),m_cellId(0),
    m_reducedPmValues (false),
    m_e2Periodicity (0.1),
    m_cuUpFileName (),
    m_stopSendingMessages(false),
    m_isReportingEnabled (false),
    m_hasValidSubscription(false),
    m_checkPeriod(MilliSeconds(100))
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


void NrGnbNetDevice::stopSendingAndCancelSchedule() {
    m_stopSendingMessages = true;
}

void
NrGnbNetDevice::KpmSubscriptionCallback (E2AP_PDU_t* sub_req_pdu)
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
    NrGnbNetDevice::ControlMessageReceivedCallback(E2AP_PDU_t *sub_req_pdu) {
        NS_LOG_DEBUG("\n\nLteEnbNetDevice::ControlMessageReceivedCallback: Received RIC Control Message");

        // Create RIC Control ACK
        Ptr <RicControlMessage> controlMessage = Create<RicControlMessage>(sub_req_pdu);
        NS_LOG_INFO("After RicControlMessage::RicControlMessage constructor");
        NS_LOG_INFO("Request type " << controlMessage->m_requestType);
        
    }
void
NrGnbNetDevice::SetE2Termination(Ptr<E2Termination> e2term)
{
  m_e2term = e2term;

  NS_LOG_DEBUG("Register E2SM NR");

  if (!m_forceE2FileLogging) {
       long m_e2_func_id = long (e2_func_id);
       long m_rc_e2_func_id = long(rc_e2_func_id);
      Ptr<KpmFunctionDescription> kpmFd = Create<KpmFunctionDescription> ();
      e2term->RegisterKpmCallbackToE2Sm (
              m_e2_func_id, kpmFd,std::bind (&NrGnbNetDevice::KpmSubscriptionCallback, this, std::placeholders::_1));

      Ptr <RicControlFunctionDescription> ricCtrlFd = Create<RicControlFunctionDescription>();
      e2term->RegisterSmCallbackToE2Sm(m_rc_e2_func_id, ricCtrlFd,
                                      std::bind(&NrGnbNetDevice::ControlMessageReceivedCallback,
                                                this, std::placeholders::_1));

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

  // sum of the per-user average latency
  double perUserAverageLatencySum = 0;
  std ::cout << "heeeeeer 5 "<< std::endl;
  std::unordered_map<uint64_t, std::string> uePmString {};

  for (auto ue : ueMap)
  {
    uint64_t imsi = ue.second->GetImsi();
    std::string ueImsiComplete = GetImsiString (imsi);

    // double rxDlPackets = m_e2PdcpStatsCalculator->GetDlRxPackets(imsi, 3); // LCID 3 is used for data
    long txDlPackets = m_e2PdcpStatsCalculator->GetDlTxPackets(imsi, 3); // LCID 3 is used for data

    double txBytes = m_e2PdcpStatsCalculator->GetDlTxData(imsi, 3)  * 8 / 1e3; // in kbit, not byte

    double rxBytes = m_e2PdcpStatsCalculator->GetDlRxData(imsi, 3)  * 8 / 1e3; // in kbit, not byte
    cellDlTxVolume += txBytes;
    cellDlRxVolume += rxBytes;

    long txPdcpPduNrRlc = 0;
    double txPdcpPduBytesNrRlc = 0;

    auto drbMap = ue.second->GetDrbMap();
    for (auto drb : drbMap)
    {
      txPdcpPduNrRlc += drb.second->m_rlc->GetTxPacketsInReportingPeriod();
      txPdcpPduBytesNrRlc += drb.second->m_rlc->GetTxBytesInReportingPeriod();
      drb.second->m_rlc->ResetRlcCounters();
    }

    // auto rlcMap = ue.second->GetRlcMap(); // secondary-connected RLCs
    // for (auto drb : rlcMap)
    // {
    //   txPdcpPduNrRlc += drb.second->m_rlc->GetTxPacketsInReportingPeriod();
    //   txPdcpPduBytesNrRlc += drb.second->m_rlc->GetTxBytesInReportingPeriod();
    //   drb.second->m_rlc->ResetRlcCounters();
    // }
    txPdcpPduBytesNrRlc *= 8 / 1e3;

    double pdcpLatency = m_e2PdcpStatsCalculator->GetDlDelay(imsi, 3) / 1e5; // unit: x 0.1 ms
    perUserAverageLatencySum += pdcpLatency;

    double pdcpThroughput = txBytes / m_e2Periodicity; // unit kbps
    double pdcpThroughputRx = rxBytes / m_e2Periodicity; // unit kbps

    std::cout << Simulator::Now().GetSeconds() << " " << m_cellId << " cell, connected UE with IMSI " << imsi
      << " ueImsiString " << ueImsiComplete
      << " txDlPackets " << txDlPackets 
      << " txDlPacketsNr " << txPdcpPduNrRlc
      << " txBytes " << txBytes 
      << " rxBytes " << rxBytes 
      << " txDlBytesNr " << txPdcpPduBytesNrRlc
      << " pdcpLatency " << pdcpLatency
      << " pdcpThroughput " << pdcpThroughput << std::endl;

    m_e2PdcpStatsCalculator->ResetResultsForImsiLcid (imsi, 3);
////
    if (!indicationMessageHelper->IsOffline ())
      {
        indicationMessageHelper->AddCuUpUePmItem (ueImsiComplete, txPdcpPduBytesNrRlc,
                                                  txPdcpPduNrRlc);
      }

    uePmString.insert(std::make_pair(imsi, ",,,," + std::to_string(txPdcpPduBytesNrRlc) + "," +
      std::to_string(txPdcpPduNrRlc)));
  }

  if (!indicationMessageHelper->IsOffline ())
    {
      indicationMessageHelper->FillCuUpValues (plmId);
    }
/////
  NS_LOG_DEBUG(Simulator::Now().GetSeconds() << " " << m_cellId << " cell volume " << cellDlTxVolume);
  if (m_forceE2FileLogging)
    {
      std::ofstream csv{};
      csv.open (m_cuUpFileName.c_str (), std::ios_base::app);
      if (!csv.is_open ())
        {
          NS_FATAL_ERROR ("Can't open file " << m_cuUpFileName.c_str ());
        }

      uint64_t timestamp = m_startTime + (uint64_t) Simulator::Now ().GetMilliSeconds ();

      // the string is timestamp, ueImsiComplete, DRB.PdcpSduDelayDl (cellAverageLatency),
      // m_pDCPBytesUL (0), m_pDCPBytesDL (cellDlTxVolume), DRB.PdcpSduVolumeDl_Filter.UEID (txBytes),
      // Tot.PdcpSduNbrDl.UEID (txDlPackets), DRB.PdcpSduBitRateDl.UEID (pdcpThroughput),
      // DRB.PdcpSduDelayDl.UEID (pdcpLatency), QosFlow.PdcpPduVolumeDL_Filter.UEID (txPdcpPduBytesNrRlc),
      // DRB.PdcpPduNbrDl.Qos.UEID (txPdcpPduNrRlc)

      for (auto ue : ueMap)
        {
          uint64_t imsi = ue.second->GetImsi ();
          std::string ueImsiComplete = GetImsiString (imsi);

          auto uePms = uePmString.find (imsi)->second;

          std::string to_print = std::to_string (timestamp) + "," + ueImsiComplete + "," + "," +
                                 "," + "," + uePms + "\n";

          csv << to_print;
        }
      csv.close ();
      return nullptr;
    }
  else
    {
      return indicationMessageHelper->CreateIndicationMessage ();
    }
}

//////////////////////////////////////////////////////////////
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
      try 
      {
        const auto& expr = sub_map.at("Test Condition Expression");
        const auto& value = sub_map.at("Test Condition Value");
        std::cout << "expr type: " << expr.type().name() << std::endl;
        std::cout << "value type: " << value.type().name() << std::endl;
        int index = std::any_cast<int>(expr);
        int threshold = std::any_cast<int>(value);

        // Get current PRB average
       // double currentPrbAvg = CalculatePrbAverage();
        //std ::cout << "Current PRB Average: " << currentPrbAvg << std::endl;
        // Only check conditions if we have enough points
        
        
          //bool shouldReport = MATH_CALL_BACKS[index](currentPrbAvg, threshold);

          std::cout <<
                       " Threshold: " << threshold << 
                       " Should Report: " << m_is_reported << " m_isReportingEnabled: " << m_isReportingEnabled << std::endl;
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

} // namespace ns3
