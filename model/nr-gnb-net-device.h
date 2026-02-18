// Copyright (c) 2019 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
//
// SPDX-License-Identifier: GPL-2.0-only

#ifndef NR_GNB_NET_DEVICE_H
#define NR_GNB_NET_DEVICE_H

#include "nr-fh-control.h"
#include "nr-net-device.h"
#include "ns3/nstime.h"
#include <vector>
#include <functional>
#include <map>

#include "ns3/traced-callback.h"
#include "ns3/nr-bearer-stats-calculator.h"
#include "nr-radio-bearer-info.h"
#include <ns3/oran-interface.h>
#include "E2SM-KPM-ActionDefinition.h"

#include "ns3/flow-monitor-module.h"     // for Ptr<FlowMonitor>
#include "ns3/ipv4-flow-classifier.h"    // for Ptr<Ipv4FlowClassifier>

namespace ns3
{

class Packet;
class PacketBurst;
class Node;
class NrGnbPhy;
class NrGnbMac;
class NrGnbRrc;
class BandwidthPartGnb;
class NrGnbComponentCarrierManager;
class BwpManagerGnb;
class NrMacScheduler;

/**
 * \ingroup gnb
 * \brief The NrGnbNetDevice class
 *
 * This class represent the GNB NetDevice.
 */
bool lessThan(int x, int y);
bool greaterThan(int x, int y);
bool equal(int x, int y);

  // Declare the MATH_CALL_BACKS vector
extern std::vector<std::function<bool(int, int)>> MATH_CALL_BACKS;
class NrGnbNetDevice : public NrNetDevice
{
  public:
    static TypeId GetTypeId();

    NrGnbNetDevice();

    ~NrGnbNetDevice() override;

    Ptr<NrMacScheduler> GetScheduler(uint8_t index) const;

    Ptr<NrGnbMac> GetMac(uint8_t index) const;

    Ptr<NrGnbPhy> GetPhy(uint8_t index) const;

    Ptr<BwpManagerGnb> GetBwpManager() const;

    uint16_t GetBwpId(uint8_t index) const;

    /**
     * \return the cell id
     */
    uint16_t GetCellId() const;

    /**
     * \return the cell ids belonging to this gNB
     */
    std::vector<uint16_t> GetCellIds() const;

    /**
     * \brief Set this gnb cell id
     * \param cellId the cell id
     */
    void SetCellId(uint16_t cellId);

    uint16_t GetEarfcn(uint8_t index) const;

    void SetRrc(Ptr<NrGnbRrc> rrc);

    Ptr<NrGnbRrc> GetRrc();

    void SetCcMap(const std::map<uint8_t, Ptr<BandwidthPartGnb>>& ccm);

    /**
     * \brief Get the size of the component carriers map
     * \return the number of cc that we have
     */
    uint32_t GetCcMapSize() const;

    /**
     * \brief Set the NrFhControl for this cell
     * \param nrFh The ptr to the NrFhControl
     */
    void SetNrFhControl(Ptr<NrFhControl> nrFh);

    /**
     * \brief Get the NrFhControl for this cell
     * \return the ptr to NrFhControl
     */
    Ptr<NrFhControl> GetNrFhControl();

    /**
     * \brief The gNB received a CTRL message list.
     *
     * The gNB should divide the messages to the BWP they pertain to.
     *
     * \param msgList Message list
     * \param sourceBwpId BWP Id from which the list originated
     */
    void RouteIngoingCtrlMsgs(const std::list<Ptr<NrControlMessage>>& msgList, uint8_t sourceBwpId);

    /**
     * \brief Route the outgoing messages to the right BWP
     * \param msgList the list of messages
     * \param sourceBwpId the source bwp of the messages
     */
    void RouteOutgoingCtrlMsgs(const std::list<Ptr<NrControlMessage>>& msgList,
                               uint8_t sourceBwpId);

    /**
     * \brief Update the RRC config. Must be called only once.
     */
    void UpdateConfig();

    /**
     * \brief Get downlink bandwidth for a given physical cell Id
     * \param cellId Physical cell Id
     * \return number of RBs
     */
    uint16_t GetCellIdDlBandwidth(uint16_t cellId) const;

    /**
     * \brief Get uplink bandwidth for a given physical cell Id
     * \param cellId Physical cell Id
     * \return number of RBs
     */
    uint16_t GetCellIdUlBandwidth(uint16_t cellId) const;

    /**
     * \brief Get uplink earfcn for a given physical cell Id
     * \param cellId Physical cell Id
     * \return downlink earfcn
     */
    uint32_t GetCellIdDlEarfcn(uint16_t cellId) const;

    /**
     * \brief Get uplink earfcn for a given physical cell Id
     * \param cellId Physical cell Id
     * \return uplink earfcn
     */
    uint32_t GetCellIdUlEarfcn(uint16_t cellId) const;
    std::string GetImsiString(uint64_t imsi);
    void BuildAndSendReportMessage (E2Termination::RicSubscriptionRequest_rval_s params);
    Ptr<KpmIndicationMessage> BuildRicIndicationMessageCuUp(std::string plmId);
    void BuildGUICuUp (); // Periodic GUI reporting for CSV logging
    void SetE2Termination(Ptr<E2Termination> e2term); //// Added to set the E2 termination object
    Ptr<E2Termination> GetE2Termination() const; //// Added to get the E2 termination object
    void KpmSubscriptionCallback(E2AP_PDU_t *sub_req_pdu); //// Added to handle KPM subscription requests
    void CCCcontrolMessageReceivedCallback(E2AP_PDU_t *sub_req_pdu); //// Added to handle CCC subscription requests
    void ControlMessageReceivedCallback(E2AP_PDU_t *sub_req_pdu); //// Added to handle control messages
    void stopSendingAndCancelSchedule();  //// Added to stop sending messages and cancel schedule
    void CheckReportingFlag (void);
    bool m_forceE2FileLogging;  //// A flag to force E2 file logging
    bool m_reducedPmValues; //< if true use a reduced subset of pmvalues
    double m_e2Periodicity;
    bool m_is_reported = false;
    bool m_hasValidSubscription ;
    bool m_sendCuUp;
    std::string m_cuUpFileName;

    struct CellStats {
        uint16_t cellId = 0;         // Cell ID
        double prbUsagePercentage = 0; // PRB usage percentage
        double averageLastRb= 0;    // Store the average value of the last RBG
    };

    struct UEStats {
        uint64_t IMSI;
        double SINR = 0;       // dB
        double RSRP = 0;       // dBm
        double dl_tp = 0;      // Mbps
        bool tp_ongoing = false;
        uint8_t mcs = 0;
        uint8_t ri  = 0;
        uint8_t cqi = 0;
        double pktLoss = 0.0;  // Packet loss ratio [0..1]
        double delay   = 0.0;  // Mean delay [ms]
        double jitter  = 0.0;  // Mean jitter [ms]
        int cell_id = 0;
    };

    void Cell_KPI_tracker();

    void UE_KPI_tracker();

    void SetFlowMonitor(ns3::Ptr<ns3::FlowMonitor> monitor);
    void SetIpv4FlowClassifier(ns3::Ptr<ns3::Ipv4FlowClassifier> classifier);
    void SampleThroughput(Ptr<FlowMonitor> monitor,
                          Ptr<Ipv4FlowClassifier> classifier,
                          double intervalSec);

    /**
     * \brief Set port power allocation for all BWPs
     * \param portPowerVec Vector of port power values (must sum to ~1.0)
     */
    void SetPortPower(const std::vector<double>& portPowerVec);

    /**
     * \brief Get current port power allocation from first BWP
     * \return Vector of port power values
     */
    std::vector<double> GetPortPower() const;

    /**
     * \brief Calculate and sample current transmit power
     * Called periodically to track average power
     */
    void SampleTransmitPower();

    /**
     * \brief Get average transmit power over sampling period
     * \return Average power in dBm
     */
    double GetAveragePower() const;

    /**
     * \brief Clear power samples
     */
    void ClearPowerSamples();

  protected:
    void DoInitialize() override;

    void DoDispose() override;
    bool DoSend(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber) override;
    void SetStartTime (uint64_t); ////Added to set the start time


  private:
    Ptr<NrGnbRrc> m_rrc;

    uint16_t m_cellId; //!< Cell ID. Set by the helper.

    std::map<uint8_t, Ptr<BandwidthPartGnb>> m_ccMap; /**< NrComponentCarrier map */

    Ptr<NrGnbComponentCarrierManager>
        m_componentCarrierManager; ///< the component carrier manager of this gNB
    Ptr<NrFhControl> m_nrFhControl;
    Ptr<E2Termination> m_e2term;  /// A pointer to the E2 termination object
    double  rc_e2_func_id ; // to RC  function id
    double e2_func_id; //to pass kpm function id
    double ccc_func_id; //to pass ccc function id
      
    bool m_stopSendingMessages;
    bool m_isReportingEnabled;
    bool m_flagControlMessageReceived;
    bool m_flagIndicationSent; 
    std::map<uint64_t, double> m_prevTxBytesPerUe;
    std::map<uint64_t, double> m_lastThroughputPerUe; ///< Last calculated throughput per UE (Mbps)
    uint16_t m_NewportsOn;
    uint16_t m_NewportsOff;
    uint64_t m_startTime;///// Added to set the start time
    Time m_checkPeriod;
    Ptr<NrBearerStatsCalculator> m_e2PdcpStatsCalculator;
    E2Termination::RicSubscriptionRequest_rval_s m_lastSubscriptionParams;
    Ptr<KpmIndicationHeader> BuildRicIndicationHeader(std::string plmId, std::string gnbId, uint16_t nrCellId); //// Added to build the KPM indication header


    bool m_isCellConfigured{false}; ///< variable to check whether the RRC has been configured
    uint64_t sim_id;
    bool report_to_db = false;

    ns3::Ptr<ns3::FlowMonitor> m_flowMonitor;            // store monitor if needed
    ns3::Ptr<ns3::Ipv4FlowClassifier> m_flowClassifier; // store classifier
    std::map<ns3::FlowId, uint64_t> m_prevRxBytes;
    std::map<ns3::FlowId, uint32_t> m_flowIdToImsi; // map FlowId -> IMSI index (1..N)
    std::map<uint32_t, double> m_imsiToTp;         // map IMSI -> last throughput (Mbps)
    std::map<uint32_t, double> m_imsiToDelay;
    std::map<uint32_t, double> m_imsiToJitter;
    std::map<uint32_t, double> m_imsiToPacketLoss;
    uint32_t m_nextImsiIndex = 1;
    std::vector<double> m_powerSamples;
    std::vector<double> m_portPowerConfig; ///< Configured port power allocation
    double m_currentPowerWatts; 
// Power consumption tracking for comparison
    bool m_xAppActive; ///< Flag to indicate if xApp has modified port configuration
    double m_baselineMinPower; ///< Minimum power without xApp (only throughput changes)
    double m_baselineMaxPower; ///< Maximum power without xApp (only throughput changes)
    double m_xAppMinPower; ///< Minimum power with xApp (throughput + port changes)
    double m_xAppMaxPower; ///< Maximum power with xApp (throughput + port changes)
    double m_baselineCurrentPower; ///< Current power in baseline scenario (calculated)
    double m_xAppCurrentPower; ///< Current power with xApp (actual measured)
    Time m_xAppActivationTime; ///< Time when xApp first modified ports
    // Add after existing power tracking variables (around line 302)
    double m_baselineAccumulatedPower; ///< Accumulated power during baseline period (0-25s)
    uint32_t m_baselineSampleCount; ///< Number of samples during baseline period
    double m_xAppAccumulatedPower; ///< Accumulated power during xApp period (after 25s)
    uint32_t m_xAppSampleCount; ///< Number of samples during xApp period
    double m_baselineAvgPower; ///< Average power during baseline period
    double m_xAppAvgPower; ///< Average power during xApp period
};

} // namespace ns3

#endif /* NR_GNB_NET_DEVICE_H */
