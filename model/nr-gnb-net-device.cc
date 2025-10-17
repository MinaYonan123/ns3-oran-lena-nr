// Copyright (c) 2019 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
//
// SPDX-License-Identifier: GPL-2.0-only

#include "nr-gnb-net-device.h"

#include "../../core/model/log-macros-enabled.h"
#include "bandwidth-part-gnb.h"
#include "bwp-manager-gnb.h"
#include "encode_e2apv1.hpp"
#include "nr-gnb-component-carrier-manager.h"
#include "nr-gnb-mac.h"
#include "nr-gnb-phy.h"
#include "nr-gnb-rrc.h"
#include "nr-ue-net-device.h"
#include "nr-ue-phy.h"

#include "ns3/Lena-indication-message-helper.h"
#include <ns3/abort.h>
#include <ns3/double.h>
#include <ns3/ipv4-l3-protocol.h>
#include <ns3/ipv6-l3-protocol.h>
#include <ns3/log.h>
#include <ns3/node-list.h>
#include <ns3/node.h>
#include <ns3/object-map.h>
#include <ns3/pointer.h>

#include <cmath>
#include <curl/curl.h>
#include <filesystem> // For filesystem utilities, available since C++17
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/time.h>
#include <vector>

std::vector<int> g_ueImsiList;
uint64_t start_sim_time = 0;
uint64_t current_sim_time = 0;
// in your class definition
std::unordered_map<uint32_t, bool> headerWritten_Cell;
std::unordered_map<uint32_t, bool> headerWritten_UE;

void
SendToInfluxDB(const std::string& payload)
{
    CURL* curl = curl_easy_init();
    std::string influx_host = "localhost";
    std::string influx_port = "8086";
    std::string influx_user = "root";
    std::string influx_password = "root";
    std::string db_name = "influx";

    if (curl)
    {
        const std::string url = "http://" + influx_host + ":" + influx_port +
                                "/api/v2/write?bucket=influx&precision=ns";
        struct curl_slist* headers = nullptr;
        const std::string auth = "Authorization: Token " + influx_user + ":" + influx_password;
        headers = curl_slist_append(headers, auth.c_str());
        headers = curl_slist_append(headers, "Content-Type: text/plain");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            std::cerr << "InfluxDB POST failed: " << curl_easy_strerror(res) << std::endl;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

namespace ns3
{

CellStats g_cellStats;

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
            .AddAttribute("CellId",
                          "Cell Identifier",
                          UintegerValue(0),
                          MakeUintegerAccessor(&NrGnbNetDevice::m_cellId),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("E2PdcpCalculator",
                          "The PDCP calculator object for E2 reporting",
                          PointerValue(),
                          MakePointerAccessor(&NrGnbNetDevice::m_e2PdcpStatsCalculator),
                          MakePointerChecker<NrBearerStatsCalculator>())
            .AddAttribute("E2Termination",
                          "The E2 termination object associated to this node",
                          PointerValue(),
                          MakePointerAccessor(&NrGnbNetDevice::SetE2Termination,
                                              &NrGnbNetDevice::GetE2Termination),
                          MakePointerChecker<E2Termination>())
            .AddAttribute("EnableE2FileLogging",
                          "If true, force E2 indication generation and write E2 fields in csv file",
                          BooleanValue(false),
                          MakeBooleanAccessor(&NrGnbNetDevice::m_forceE2FileLogging),
                          MakeBooleanChecker())
            .AddAttribute("KPM_E2functionID",
                          "Function ID to subscribe",
                          DoubleValue(2),
                          MakeDoubleAccessor(&NrGnbNetDevice::e2_func_id),
                          MakeDoubleChecker<double>())
            .AddAttribute("RC_E2functionID",
                          "Function ID to subscribe",
                          DoubleValue(3),
                          MakeDoubleAccessor(&NrGnbNetDevice::rc_e2_func_id),
                          MakeDoubleChecker<double>())
            .AddAttribute("EnableCuUpReport",
                          "If true, send CuUpReport",
                          BooleanValue(true),
                          MakeBooleanAccessor(&NrGnbNetDevice::m_sendE2KPIs),
                          MakeBooleanChecker())

            .AddAttribute("E2Periodicity",
                          "Periodicity of E2 reporting (value in seconds)",
                          DoubleValue(0.1),
                          MakeDoubleAccessor(&NrGnbNetDevice::m_e2Periodicity),
                          MakeDoubleChecker<double>())

            .AddAttribute("ReducedPmValues",
                          "If true, send only a subset of pmValues",
                          BooleanValue(false),
                          MakeBooleanAccessor(&NrGnbNetDevice::m_reducedPmValues),
                          MakeBooleanChecker())
            .AddAttribute("sim_id",
                          "ID of simulation",
                          UintegerValue(0),
                          MakeUintegerAccessor(&NrGnbNetDevice::sim_id),
                          MakeUintegerChecker<uint64_t>())
            .AddAttribute("report_to_db",
                          "Reporting to InfluxDB",
                          BooleanValue(false),
                          MakeBooleanAccessor(&NrGnbNetDevice::report_to_db),
                          MakeBooleanChecker());

    return tid;
}

NrGnbNetDevice::NrGnbNetDevice()
    : m_forceE2FileLogging(false),
      m_cellId(0),
      m_reducedPmValues(false),
      m_e2Periodicity(0.1),
      m_cuUpFileName(),
      m_stopSendingMessages(false),
      m_isReportingEnabled(false),
      m_hasValidSubscription(false),
      m_checkPeriod(MilliSeconds(100))
{
    NS_LOG_FUNCTION(this);
}

NrGnbNetDevice::~NrGnbNetDevice()
{
    NS_LOG_FUNCTION(this);
}

bool
lessThan(int x, int y)
{
    return x < y;
}

bool
greaterThan(int x, int y)
{
    return x > y;
}

bool
equal(int x, int y)
{
    return x == y;
}

std::vector<std::function<bool(int, int)>> MATH_CALL_BACKS = {equal, greaterThan, lessThan};

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
    m_lastSubscriptionParams = m_e2term->ProcessRicSubscriptionRequest(sub_req_pdu);
    NS_LOG_DEBUG("requestorId " << +m_lastSubscriptionParams.requestorId << ", instanceId "
                                << +m_lastSubscriptionParams.instanceId << ", ranFuncionId "
                                << +m_lastSubscriptionParams.ranFuncionId << ", actionId "
                                << +m_lastSubscriptionParams.actionId);
    m_hasValidSubscription = true;
    const auto& sub_map = m_e2term->SubscriptionMapRef();
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
            case E2SM_KPM_ActionDefinition__actionDefinition_formats_PR_actionDefinition_Format4: {
                // Clear PRB history at subscription start
                // m_prbHistory.clear();

                // Start periodic PRB checking
                if (!m_stopSendingMessages)
                {
                    Simulator::ScheduleWithContext(1,
                                                   m_checkPeriod,
                                                   &NrGnbNetDevice::CheckReportingFlag,
                                                   this);

                    NS_LOG_DEBUG("Started PRB monitoring with period "
                                 << m_checkPeriod.GetMilliSeconds() << "ms");
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

std::string
NrGnbNetDevice::GetImsiString(uint64_t imsi)
{
    std::string ueImsi = std::to_string(imsi);
    std::string ueImsiComplete{};
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
flip_pair(const std::pair<A, B>& p)
{
    return std::pair<B, A>(p.second, p.first);
}

template <typename A, typename B>
std::multimap<B, A>
flip_map(const std::map<A, B>& src)
{
    std::multimap<B, A> dst;
    std::transform(src.begin(), src.end(), std::inserter(dst, dst.begin()), flip_pair<A, B>);
    return dst;
}

Ptr<KpmIndicationHeader>
NrGnbNetDevice::BuildRicIndicationHeader(std::string plmId, std::string gnbId, uint16_t nrCellId)
{
    if (!m_forceE2FileLogging)
    {
        KpmIndicationHeader::KpmRicIndicationHeaderValues headerValues;
        headerValues.m_plmId = plmId;
        headerValues.m_gnbId = gnbId;
        headerValues.m_nrCellId = nrCellId;
        auto time = Simulator::Now();
        uint64_t timestamp = m_startTime + (uint64_t)time.GetMilliSeconds();
        NS_LOG_DEBUG("NR plmid " << plmId << " gnbId " << gnbId << " nrCellId " << nrCellId);
        NS_LOG_DEBUG("Timestamp " << timestamp);
        headerValues.m_timestamp = timestamp;

        Ptr<KpmIndicationHeader> header =
            Create<KpmIndicationHeader>(KpmIndicationHeader::GlobalE2nodeType::gNB, headerValues);
        return header;
    }
    else
    {
        return nullptr;
    }
}

/////////////////////////////////////////////////////////////////////

void
NrGnbNetDevice::BuildAndSendReportMessage(E2Termination::RicSubscriptionRequest_rval_s params)
{
    std::cout << "[DEBUG] BuildAndSendReportMessage called" << std::endl;
    std::string plmId = "111";
    std::string gnbId = std::to_string(m_cellId);

    // TODO here we can get something from RRC and onward
    NS_LOG_DEBUG("NrGnbNetDevice " << m_cellId << " BuildAndSendMessage at time "
                                   << Simulator::Now().GetSeconds());
    if (m_sendE2KPIs)
    {
        // Create CU-UP
        Ptr<KpmIndicationHeader> header = BuildRicIndicationHeader(plmId, gnbId, m_cellId);
        std ::cout << " after BuildRicIndicationHeader for cell: " << m_cellId << std::endl;
        Ptr<KpmIndicationMessage> cuUpMsg = BuildRicIndicationMessageE2KPIs(plmId);
        std ::cout << " after BuildRicIndicationMessageE2KPIs for cell: " << m_cellId << std::endl;
        //TODO Need to update further filling message CuUp to PHY, we need to discuss logic at xApp side.
        // Send CU-UP only if offline logging is disabled
        if (header != nullptr && cuUpMsg != nullptr)
        {
            NS_LOG_DEBUG("Send NR CU-UP");
            std ::cout << "[DEBUG] Send NR CU-UP" << std::endl;
            E2AP_PDU* pdu_cuup_ue = new E2AP_PDU;
            encoding::generate_e2apv1_indication_request_parameterized(
                pdu_cuup_ue,
                params.requestorId,
                params.instanceId,
                params.ranFuncionId,
                params.actionId,
                1,                           // TODO sequence number
                (uint8_t*)header->m_buffer,  // buffer containing the encoded header
                header->m_size,              // size of the encoded header
                (uint8_t*)cuUpMsg->m_buffer, // buffer containing the encoded message
                cuUpMsg->m_size);            // size of the encoded message
            std ::cout << "[DEBUG] E2AP PDU generated for CU-UP" << std::endl;
            m_e2term->SendE2Message(pdu_cuup_ue);
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
        Simulator::ScheduleWithContext(1,
                                       Seconds(m_e2Periodicity),
                                       &NrGnbNetDevice::BuildAndSendReportMessage,
                                       this,
                                       params);
    }
}

Ptr<KpmIndicationMessage>
NrGnbNetDevice::BuildRicIndicationMessageE2KPIs(std::string plmId)
{
    Ptr<LenaIndicationMessageHelper> indicationMessageHelper =
        Create<LenaIndicationMessageHelper>(IndicationMessageHelper::IndicationMessageType::CuUp,
                                            m_forceE2FileLogging,
                                            m_reducedPmValues);

    // bool send_gnb_pdcp_stats = false;
    // bool send_ue_pdcp_stats = false;
    // bool send_gnb_pdcp_cp_stats = false;
    // bool send_ue_pdcp_cp_stats = false;
    // bool send_gnb_rlc_stats = false;
    // bool send_ue_rlc_stats = false;
   // bool send_gnb_mac_stats = true;
    // bool send_ue_mac_stats = false;
     bool send_gnb_phy_stats = true;
    // bool send_ue_phy_stats = false;
    if (send_gnb_phy_stats && !indicationMessageHelper->IsOffline())
    {
        indicationMessageHelper->AddPhyGnbPmItem(g_cellStats.averageLastRb);
        //TODO Need to update further filling message CuUp to PHY, we need to discuss logic at xApp side.
        indicationMessageHelper->FillCuUpValues(plmId);

        return indicationMessageHelper->CreateIndicationMessage();
    }
}

//////////////////////////////////////////////////////////////
void
NrGnbNetDevice::SetStartTime(uint64_t st)
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
        NS_LOG_DEBUG("E2sim start in cell " << m_cellId << " force CSV logging "
                                            << m_forceE2FileLogging);
        //
        if (!m_forceE2FileLogging)
        {
            Simulator::Schedule(MicroSeconds(0), &E2Termination::Start, m_e2term);
        }
    }
    if (m_is_reported)
    {
        Simulator::Schedule(MicroSeconds(500),
                            &NrGnbNetDevice::BuildAndSendReportMessage,
                            this,
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
        std ::cout << "m_is_reported: " << m_is_reported
                   << " m_isReportingEnabled: " << m_isReportingEnabled << std::endl;
        const auto& sub_map = m_e2term->SubscriptionMapRef();
        if (!sub_map.empty())
        {
            std ::cout << "sub_map is not empty" << std::endl;
            try
            {
                const auto& expr = sub_map.at("Test Condition Expression");
                const auto& value = sub_map.at("Test Condition Value");
                std::cout << "expr type: " << expr.type().name() << std::endl;
                std::cout << "value type: " << value.type().name() << std::endl;

                int index = std::any_cast<int>(expr);
                int threshold = 0;
                if (value.type() == typeid(int)) {
                    threshold = std::any_cast<int>(value);
                } else if (value.type() == typeid(std::string)) {
                    threshold = std::stoi(std::any_cast<std::string>(value));
                } else {
                    std::cout << "Unexpected value type: " << value.type().name() << std::endl;
                }

                // Get current PRB average
                // double currentPrbAvg = CalculatePrbAverage();
                // std ::cout << "Current PRB Average: " << currentPrbAvg << std::endl;
                // Only check conditions if we have enough points

                // bool shouldReport = MATH_CALL_BACKS[index](currentPrbAvg, threshold);
                m_is_reported = false;

                std::cout << " Threshold: " << threshold << " Should Report: " << m_is_reported
                          << " m_isReportingEnabled: " << m_isReportingEnabled << std::endl;

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
        Simulator::ScheduleWithContext(1, m_checkPeriod, &NrGnbNetDevice::CheckReportingFlag, this);
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

void
NrGnbNetDevice::SetFlowMonitor(ns3::Ptr<ns3::FlowMonitor> monitor)
{
    NS_LOG_FUNCTION(this << monitor);
    m_flowMonitor = monitor;
}

void
NrGnbNetDevice::SetIpv4FlowClassifier(ns3::Ptr<ns3::Ipv4FlowClassifier> classifier)
{
    NS_LOG_FUNCTION(this << classifier);
    m_flowClassifier = classifier;

    // Clear previous contents
    g_ueImsiList.clear();
    m_flowIdToImsi.clear();
    m_prevRxBytes.clear();

    // Collect all UEs
    for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it)
    {
        Ptr<Node> node = *it;
        int nDevs = node->GetNDevices();
        for (int j = 0; j < nDevs; j++)
        {
            Ptr<NrUeNetDevice> ueDev = node->GetDevice(j)->GetObject<NrUeNetDevice>();
            if (!ueDev)
                continue;

            uint64_t imsi = ueDev->GetImsi();
            if (std::find(g_ueImsiList.begin(), g_ueImsiList.end(), imsi) == g_ueImsiList.end())
            {
                g_ueImsiList.push_back(imsi);
            }
        }
    }

    std::cout << "=== UE IMSI List ===" << std::endl;
    for (size_t i = 0; i < g_ueImsiList.size(); ++i)
    {
        std::cout << "UE index " << i << " -> IMSI: " << g_ueImsiList[i] << std::endl;
    }

    // Schedule first throughput sampling
    if (m_flowMonitor && m_flowClassifier)
    {
        Simulator::Schedule(Seconds(0.1),
                            &NrGnbNetDevice::SampleThroughput,
                            this,
                            m_flowMonitor,
                            m_flowClassifier,
                            0.1);
    }
}

void
NrGnbNetDevice::SampleThroughput(Ptr<FlowMonitor> monitor,
                                 Ptr<Ipv4FlowClassifier> classifier,
                                 double intervalSec)
{
    if (!monitor || !classifier)
        return;

    monitor->CheckForLostPackets();
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    for (const auto& kv : stats)
    {
        FlowId id = kv.first;
        const FlowMonitor::FlowStats& fs = kv.second;
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(id);

        // Only downlink flows: destination = UE, source = server
        if (t.sourceAddress != Ipv4Address("1.0.0.2"))
            continue;

        // Map FlowID to UE IMSI using destination IP
        uint64_t imsi = 0;
        for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it)
        {
            Ptr<Node> node = *it;
            for (int j = 0; j < node->GetNDevices(); j++)
            {
                Ptr<NrUeNetDevice> ueDev = node->GetDevice(j)->GetObject<NrUeNetDevice>();
                if (!ueDev)
                    continue;
                Ptr<Ipv4> ueIp = node->GetObject<Ipv4>();
                for (uint32_t k = 1; k < ueIp->GetNInterfaces(); k++)
                {
                    Ipv4Address addr = ueIp->GetAddress(k, 0).GetLocal();
                    if (addr == t.destinationAddress)
                    {
                        imsi = ueDev->GetImsi();
                        break;
                    }
                }
                if (imsi != 0)
                    break;
            }
            if (imsi != 0)
                break;
        }

        if (imsi == 0)
            continue; // Flow not matched to any UE yet

        // Store mapping if first time
        if (m_flowIdToImsi.find(id) == m_flowIdToImsi.end())
        {
            m_flowIdToImsi[id] = imsi;
        }

        // --- Throughput calculation ---
        uint64_t prev = 0;
        auto itPrev = m_prevRxBytes.find(id);
        if (itPrev != m_prevRxBytes.end())
            prev = itPrev->second;

        uint64_t curr = fs.rxBytes;
        uint64_t diff = (curr >= prev) ? (curr - prev) : curr;
        double thrMbps = static_cast<double>(diff) * 8.0 / (intervalSec * 1e6);

        m_imsiToTp[imsi] = thrMbps;
        m_prevRxBytes[id] = curr;

        // --- Packet loss, delay, jitter ---
        double packetLossRatio = 0.0;
        if (fs.txPackets > 0)
        {
            packetLossRatio = 1.0 - (double)fs.rxPackets / (double)fs.txPackets;
        }

        double meanDelayMs = 0.0;
        double meanJitterMs = 0.0;
        if (fs.rxPackets > 0)
        {
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
    current_sim_time = (sim_id + (uint64_t)now_ms) * 1000000ULL;

    // Schedule KPI tracking
    Simulator::Schedule(MilliSeconds(0), &NrGnbNetDevice::Cell_KPI_tracker, this);
    Simulator::Schedule(MilliSeconds(0), &NrGnbNetDevice::UE_KPI_tracker, this);

    // Schedule next throughput sample
    Simulator::Schedule(Seconds(intervalSec),
                        &NrGnbNetDevice::SampleThroughput,
                        this,
                        monitor,
                        classifier,
                        intervalSec);
}

void
NrGnbNetDevice::Cell_KPI_tracker()
{
    NS_LOG_UNCOND("---------------------------------------------");

    // === 1. Create folder for traces ===
    std::stringstream folderName;
    folderName << "trace_" << sim_id;
    if (!std::filesystem::exists(folderName.str()))
    {
        std::filesystem::create_directory(folderName.str());
    }

    // === 2. Build CSV file path ===
    std::stringstream cell_kpi_file;
    cell_kpi_file << folderName.str() << "/Cell_" << this->GetCellId() << "_Cell_stats_" << sim_id
                  << ".csv";

    std::ofstream traceFile(cell_kpi_file.str(), std::ios::out | std::ios::app);
    if (!traceFile.is_open())
    {
        std::cerr << "Error opening file for writing: " << cell_kpi_file.str() << std::endl;
        return;
    }

    // === 3. Write header (once per file) ===
    if (!headerWritten_Cell[this->GetCellId()] && traceFile.tellp() == 0)
    {
        traceFile << "TS,CELL_ID,PRB_USAGE,CURR_PRB,"
                  << "AVG_TP,AVG_PKT_LOSS,AVG_DELAY_MS,AVG_JITTER_MS,"
                  << "TOT_TP,UE_COUNT\n";
        headerWritten_Cell[this->GetCellId()] = true;
    }

    // === 4. Retrieve PHY stats ===
    Ptr<NrGnbPhy> gnbPhy = GetPhy(0);
    NrGnbPhy::RbStats stats = gnbPhy->GetRBStats();

    // === 5. Initialize the CellStats struct ===

    CellStats cellStats;
    cellStats.cellId = this->GetCellId();
    cellStats.prbUsagePercentage = stats.prbUsagePercentage;
    cellStats.averageLastRb = stats.averageLastRb;

    // === 6. Compute UE-based KPIs ===
    double sumTp = 0.0, sumLoss = 0.0, sumDelay = 0.0, sumJitter = 0.0;
    uint32_t ueCount = 0;

    for (auto& kv : m_imsiToTp)
    {
        uint64_t imsi = kv.first;
        double tp = kv.second;
        if (tp <= 0.0)
            continue;

        Ptr<NrUeNetDevice> ueDev = nullptr;
        for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it)
        {
            Ptr<Node> node = *it;
            for (uint32_t i = 0; i < node->GetNDevices(); ++i)
            {
                Ptr<NrUeNetDevice> dev = node->GetDevice(i)->GetObject<NrUeNetDevice>();
                if (dev && dev->GetImsi() == imsi && dev->GetCellId() == this->GetCellId())
                {
                    ueDev = dev;
                    break;
                }
            }
            if (ueDev)
                break;
        }

        if (!ueDev)
            continue;

        sumTp += tp;
        if (m_imsiToPacketLoss.count(imsi))
            sumLoss += m_imsiToPacketLoss[imsi];
        if (m_imsiToDelay.count(imsi))
            sumDelay += m_imsiToDelay[imsi];
        if (m_imsiToJitter.count(imsi))
            sumJitter += m_imsiToJitter[imsi];

        ueCount++;
    }

    // === 7. Finalize averages ===
    cellStats.ueCount = ueCount;
    if (ueCount > 0)
    {
        cellStats.avgThroughputMbps = sumTp / ueCount;
        cellStats.avgPacketLoss = sumLoss / ueCount;
        cellStats.avgDelayMs = sumDelay / ueCount;
        cellStats.avgJitterMs = sumJitter / ueCount;
    }
    cellStats.totalThroughputMbps = sumTp;

    // === 8. Console Log ===
    NS_LOG_UNCOND("Cell stats-> gNB "
                  << cellStats.cellId << " | PRB Usage: " << std::fixed << std::setprecision(0)
                  << cellStats.prbUsagePercentage << " %"
                  << " | Avg Last RB: " << std::fixed << std::setprecision(0)
                  << cellStats.averageLastRb << " | Avg TP: " << std::fixed << std::setprecision(2)
                  << cellStats.avgThroughputMbps << " Mbps"
                  << " | Tot TP: " << std::fixed << std::setprecision(2)
                  << cellStats.totalThroughputMbps << " Mbps"
                  << " | Avg Loss: " << std::fixed << std::setprecision(2)
                  << cellStats.avgPacketLoss * 100 << " %"
                  << " | Avg Delay: " << std::fixed << std::setprecision(2) << cellStats.avgDelayMs
                  << " ms"
                  << " | Avg Jitter: " << std::fixed << std::setprecision(2)
                  << cellStats.avgJitterMs << " ms"
                  << " | UE Count: " << cellStats.ueCount);

    // === 9. CSV output ===
    traceFile << Simulator::Now().GetSeconds() << "," << cellStats.cellId << "," << std::fixed
              << std::setprecision(2) << cellStats.prbUsagePercentage << "," << std::fixed
              << std::setprecision(2) << cellStats.averageLastRb << "," << std::fixed
              << std::setprecision(2) << cellStats.avgThroughputMbps << "," << std::fixed
              << std::setprecision(4) << cellStats.avgPacketLoss << "," << std::fixed
              << std::setprecision(2) << cellStats.avgDelayMs << "," << std::fixed
              << std::setprecision(2) << cellStats.avgJitterMs << "," << std::fixed
              << std::setprecision(2) << cellStats.totalThroughputMbps << "," << cellStats.ueCount
              << "\n";
    traceFile.flush();

    // === 10. Send to InfluxDB ===
    if (report_to_db)
    {
        std::ostringstream payload;
        payload << "cell_stats,cell_id=" << cellStats.cellId
                << " prb_usage=" << cellStats.prbUsagePercentage
                << ",avg_last_rb=" << cellStats.averageLastRb
                << ",avg_tp=" << cellStats.avgThroughputMbps
                << ",tot_tp=" << cellStats.totalThroughputMbps
                << ",avg_pkt_loss=" << cellStats.avgPacketLoss
                << ",avg_delay_ms=" << cellStats.avgDelayMs
                << ",avg_jitter_ms=" << cellStats.avgJitterMs << ",ue_count=" << cellStats.ueCount
                << " " << current_sim_time;

        SendToInfluxDB(payload.str());
    }
    g_cellStats = cellStats;

    // Optionally pass the filled struct to other PM item helpers:
    // AddPhyGnbPmItem(cellStats);
    // AddMacGnbPmItem(cellStats);
    // AddRlcGnbPmItem(cellStats);
    // AddPdcpGnbPmItem(cellStats);
    // AddPdcpCpGnbPmItem(cellStats);
}

void
NrGnbNetDevice::UE_KPI_tracker()
{
    std::stringstream folderName;
    folderName << "trace_" << sim_id;
    if (!std::filesystem::exists(folderName.str()))
    {
        std::filesystem::create_directory(folderName.str());
    }

    std::stringstream ue_kpi_file;
    ue_kpi_file << folderName.str() << "/Cell_" << this->GetCellId() << "_UE_stats_" << sim_id
                << ".csv";

    std::ofstream traceFile(ue_kpi_file.str(), std::ios::out | std::ios::app);
    if (!traceFile.is_open())
    {
        std::cerr << "Error opening file for writing: " << ue_kpi_file.str() << std::endl;
        return;
    }

    // Header
    if (!headerWritten_UE[this->GetCellId()] && traceFile.tellp() == 0)
    {
        traceFile << "TS,IMSI,CELL_ID,SINR,RSRP,DL_TP,MCS,RI,CQI,"
                  << "PKT_LOSS,DELAY_MS,JITTER_MS\n";
        headerWritten_UE[this->GetCellId()] = true;
    }

    std::unordered_map<uint64_t, UEStats> ueStatsMap;

    // Collect per UE
    for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it)
    {
        Ptr<Node> node = *it;
        for (uint32_t i = 0; i < node->GetNDevices(); ++i)
        {
            Ptr<NrUeNetDevice> ueDevice = node->GetDevice(i)->GetObject<NrUeNetDevice>();
            if (!ueDevice || ueDevice->GetCellId() != this->GetCellId())
                continue;

            Ptr<NrUePhy> uePhy = ueDevice->GetPhy(0);
            if (!uePhy)
                continue;

            uePhy->ReportUeMeasurements();
            double rsrp = uePhy->GetRsrp();
            double sinrLin = uePhy->GetSINR();
            double sinrDb = 10 * log10(sinrLin);

            UeKpiInfo kpi = uePhy->GetUEkpi();

            uint64_t imsi = ueDevice->GetImsi();

            double dl_tp = 0.0;
            auto itTp = m_imsiToTp.find(imsi);
            if (itTp != m_imsiToTp.end())
                dl_tp = itTp->second;

            double pktLoss = 0.0, delayMs = 0.0, jitterMs = 0.0;
            auto itLoss = m_imsiToPacketLoss.find(imsi);
            auto itDelay = m_imsiToDelay.find(imsi);
            auto itJitter = m_imsiToJitter.find(imsi);

            if (itLoss != m_imsiToPacketLoss.end())
                pktLoss = itLoss->second;
            if (itDelay != m_imsiToDelay.end())
                delayMs = itDelay->second;
            if (itJitter != m_imsiToJitter.end())
                jitterMs = itJitter->second;

            UEStats& stats = ueStatsMap[imsi];
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
    for (auto& pair : ueStatsMap)
    {
        UEStats& stats = pair.second;

        NS_LOG_UNCOND("UE stats -> UE "
                      << stats.IMSI << " | Cell ID: " << stats.cell_id // ✅ Explicit cell_id
                      << " | SINR: " << std::fixed << std::setprecision(1) << stats.SINR << " dB"
                      << " | RSRP: " << std::fixed << std::setprecision(0) << stats.RSRP << " dBm"
                      << " | DL TP: " << std::fixed << std::setprecision(1) << stats.dl_tp
                      << " Mbps"
                      << " | MCS: " << static_cast<uint32_t>(stats.mcs)
                      << " | RI: " << static_cast<uint32_t>(stats.ri)
                      << " | CQI: " << static_cast<uint32_t>(stats.cqi) << " | Loss: " << std::fixed
                      << std::setprecision(2) << stats.pktLoss * 100 << " %"
                      << " | Delay: " << std::fixed << std::setprecision(2) << stats.delay << " ms"
                      << " | Jitter: " << std::fixed << std::setprecision(2) << stats.jitter
                      << " ms");

        traceFile << Simulator::Now().GetSeconds() << "," << stats.IMSI << "," << stats.cell_id
                  << "," // ✅ Explicit cell_id
                  << std::fixed << std::setprecision(2) << stats.SINR << "," << std::fixed
                  << std::setprecision(2) << stats.RSRP << "," << std::fixed << std::setprecision(2)
                  << stats.dl_tp << "," << static_cast<uint32_t>(stats.mcs) << ","
                  << static_cast<uint32_t>(stats.ri) << "," << static_cast<uint32_t>(stats.cqi)
                  << "," << std::fixed << std::setprecision(4) << stats.pktLoss << "," << std::fixed
                  << std::setprecision(2) << stats.delay << "," << std::fixed
                  << std::setprecision(2) << stats.jitter << "\n";
        if (report_to_db)
        {
            if (stats.tp_ongoing)
            {
                std::ostringstream payload;
                payload << "ue_stats,ue=" << stats.IMSI
                        << ",cell_id=" << stats.cell_id // ✅ Use cell_id consistently
                        << " sinr=" << stats.SINR << ",rsrp=" << stats.RSRP
                        << ",dl_tp=" << stats.dl_tp << ",mcs=" << static_cast<uint32_t>(stats.mcs)
                        << ",ri=" << static_cast<uint32_t>(stats.ri)
                        << ",cqi=" << static_cast<uint32_t>(stats.cqi)
                        << ",pkt_loss=" << stats.pktLoss << ",delay_ms=" << stats.delay
                        << ",jitter_ms=" << stats.jitter << " " << current_sim_time;
                SendToInfluxDB(payload.str());
            }
            else
            {
                std::ostringstream payload;
                payload << "ue_stats,ue=" << stats.IMSI
                        << ",cell_id=" << stats.cell_id // ✅ Use cell_id consistently
                        << " sinr=" << stats.SINR << ",rsrp=" << stats.RSRP << " "
                        << current_sim_time;
                SendToInfluxDB(payload.str());
            }
        }
        stats.tp_ongoing = false;
    }

    traceFile.flush();

    // Reset TP
    for (auto& pair : m_imsiToTp)
    {
        pair.second = 0.0;
    }

    NS_LOG_UNCOND("---------------------------------------------");
}
} // namespace ns3