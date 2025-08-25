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
    void SetE2Termination(Ptr<E2Termination> e2term); //// Added to set the E2 termination object
    Ptr<E2Termination> GetE2Termination() const; //// Added to get the E2 termination object
    void KpmSubscriptionCallback(E2AP_PDU_t *sub_req_pdu); //// Added to handle KPM subscription requests
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
    bool m_stopSendingMessages; 
    bool m_isReportingEnabled;
    uint64_t m_startTime;///// Added to set the start time
    Time m_checkPeriod;
    Ptr<NrBearerStatsCalculator> m_e2PdcpStatsCalculator;
    E2Termination::RicSubscriptionRequest_rval_s m_lastSubscriptionParams;
    Ptr<KpmIndicationHeader> BuildRicIndicationHeader(std::string plmId, std::string gnbId, uint16_t nrCellId); //// Added to build the KPM indication header
    



};

} // namespace ns3

#endif /* NR_GNB_NET_DEVICE_H */
