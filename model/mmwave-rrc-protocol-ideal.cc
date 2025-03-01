/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
*   Copyright (c) 2011 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
*   Copyright (c) 2015, NYU WIRELESS, Tandon School of Engineering, New York University
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License version 2 as
*   published by the Free Software Foundation;
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program; if not, write to the Free Software
*   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*   Author: Marco Miozzo <marco.miozzo@cttc.es>
*           Nicola Baldo  <nbaldo@cttc.es>
*
*   Modified by: Marco Mezzavilla < mezzavilla@nyu.edu>
*                         Sourjya Dutta <sdutta@nyu.edu>
*                         Russell Ford <russell.ford@nyu.edu>
*                         Menglei Zhang <menglei@nyu.edu>
*/


#include "mmwave-rrc-protocol-ideal.h"

#include <ns3/fatal-error.h>
#include <ns3/log.h>
#include <ns3/nstime.h>
#include <ns3/node-list.h>
#include <ns3/node.h>
#include <ns3/simulator.h>

#include "ns3/lte-ue-rrc.h"
#include "ns3/lte-enb-rrc.h"
#include "mmwave-ue-net-device.h"
#include "mmwave-enb-net-device.h"

NS_LOG_COMPONENT_DEFINE ("mmWaveRrcProtocolIdeal");


namespace ns3 {


static const Time RRC_IDEAL_MSG_DELAY = MilliSeconds (0);

NS_OBJECT_ENSURE_REGISTERED (mmWaveUeRrcProtocolIdeal);

mmWaveUeRrcProtocolIdeal::mmWaveUeRrcProtocolIdeal ()
  :  m_ueRrcSapProvider (0),
  m_enbRrcSapProvider (0)
{
  m_ueRrcSapUser = new MemberLteUeRrcSapUser<mmWaveUeRrcProtocolIdeal> (this);
}

mmWaveUeRrcProtocolIdeal::~mmWaveUeRrcProtocolIdeal ()
{
}

void
mmWaveUeRrcProtocolIdeal::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  delete m_ueRrcSapUser;
  m_rrc = 0;
}

TypeId
mmWaveUeRrcProtocolIdeal::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::mmWaveUeRrcProtocolIdeal")
    .SetParent<Object> ()
    .AddConstructor<mmWaveUeRrcProtocolIdeal> ()
  ;
  return tid;
}

void
mmWaveUeRrcProtocolIdeal::SetLteUeRrcSapProvider (LteUeRrcSapProvider* p)
{
  m_ueRrcSapProvider = p;
}

LteUeRrcSapUser*
mmWaveUeRrcProtocolIdeal::GetLteUeRrcSapUser ()
{
  return m_ueRrcSapUser;
}

void
mmWaveUeRrcProtocolIdeal::SetUeRrc (Ptr<LteUeRrc> rrc)
{
  m_rrc = rrc;
}

void
mmWaveUeRrcProtocolIdeal::DoSetup (LteUeRrcSapUser::SetupParameters params)
{
  NS_LOG_FUNCTION (this);
  // We don't care about SRB0/SRB1 since we use ideal RRC messages.
}

void
mmWaveUeRrcProtocolIdeal::DoSendRrcConnectionRequest (LteRrcSap::RrcConnectionRequest msg)
{
  // initialize the RNTI and get the EnbLteRrcSapProvider for the
  // eNB we are currently attached to
  m_rnti = m_rrc->GetRnti ();
  SetEnbRrcSapProvider ();

  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                       &LteEnbRrcSapProvider::RecvRrcConnectionRequest,
                       m_enbRrcSapProvider,
                       m_rnti,
                       msg);
}

void
mmWaveUeRrcProtocolIdeal::DoSendRrcConnectionSetupCompleted (LteRrcSap::RrcConnectionSetupCompleted msg)
{
  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                       &LteEnbRrcSapProvider::RecvRrcConnectionSetupCompleted,
                       m_enbRrcSapProvider,
                       m_rnti,
                       msg);
}

void
mmWaveUeRrcProtocolIdeal::DoSendRrcConnectionReconfigurationCompleted (LteRrcSap::RrcConnectionReconfigurationCompleted msg)
{
  // re-initialize the RNTI and get the EnbLteRrcSapProvider for the
  // eNB we are currently attached to
  m_rnti = m_rrc->GetRnti ();
  SetEnbRrcSapProvider ();

  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                       &LteEnbRrcSapProvider::RecvRrcConnectionReconfigurationCompleted,
                       m_enbRrcSapProvider,
                       m_rnti,
                       msg);
}

void
mmWaveUeRrcProtocolIdeal::DoSendRrcConnectionReestablishmentRequest (LteRrcSap::RrcConnectionReestablishmentRequest msg)
{
  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                       &LteEnbRrcSapProvider::RecvRrcConnectionReestablishmentRequest,
                       m_enbRrcSapProvider,
                       m_rnti,
                       msg);
}

void
mmWaveUeRrcProtocolIdeal::DoSendRrcConnectionReestablishmentComplete (LteRrcSap::RrcConnectionReestablishmentComplete msg)
{
  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                       &LteEnbRrcSapProvider::RecvRrcConnectionReestablishmentComplete,
                       m_enbRrcSapProvider,
                       m_rnti,
                       msg);
}

void
mmWaveUeRrcProtocolIdeal::DoSendMeasurementReport (LteRrcSap::MeasurementReport msg)
{
  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                       &LteEnbRrcSapProvider::RecvMeasurementReport,
                       m_enbRrcSapProvider,
                       m_rnti,
                       msg);
}

void
mmWaveUeRrcProtocolIdeal::SetEnbRrcSapProvider ()
{
  uint16_t cellId = m_rrc->GetCellId ();

  // walk list of all nodes to get the peer eNB
  Ptr<MmWaveEnbNetDevice> enbDev;
  NodeList::Iterator listEnd = NodeList::End ();
  bool found = false;
  for (NodeList::Iterator i = NodeList::Begin ();
       (i != listEnd) && (!found);
       ++i)
    {
      Ptr<Node> node = *i;
      int nDevs = node->GetNDevices ();
      for (int j = 0;
           (j < nDevs) && (!found);
           j++)
        {
          enbDev = node->GetDevice (j)->GetObject <MmWaveEnbNetDevice> ();
          if (enbDev == 0)
            {
              continue;
            }
          else
            {
              if (enbDev->GetCellId () == cellId)
                {
                  found = true;
                  break;
                }
            }
        }
    }
  NS_ASSERT_MSG (found, " Unable to find eNB with CellId =" << cellId);
  m_enbRrcSapProvider = enbDev->GetRrc ()->GetLteEnbRrcSapProvider ();
  Ptr<MmWaveEnbRrcProtocolIdeal> enbRrcProtocolIdeal = enbDev->GetRrc ()->GetObject<MmWaveEnbRrcProtocolIdeal> ();
  enbRrcProtocolIdeal->SetUeRrcSapProvider (m_rnti, m_ueRrcSapProvider);
}


NS_OBJECT_ENSURE_REGISTERED (MmWaveEnbRrcProtocolIdeal);

MmWaveEnbRrcProtocolIdeal::MmWaveEnbRrcProtocolIdeal ()
  :  m_enbRrcSapProvider (0)
{
  NS_LOG_FUNCTION (this);
  m_enbRrcSapUser = new MemberLteEnbRrcSapUser<MmWaveEnbRrcProtocolIdeal> (this);
}

MmWaveEnbRrcProtocolIdeal::~MmWaveEnbRrcProtocolIdeal ()
{
  NS_LOG_FUNCTION (this);
}

void
MmWaveEnbRrcProtocolIdeal::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  delete m_enbRrcSapUser;
}

TypeId
MmWaveEnbRrcProtocolIdeal::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MmWaveEnbRrcProtocolIdeal")
    .SetParent<Object> ()
    .AddConstructor<MmWaveEnbRrcProtocolIdeal> ()
  ;
  return tid;
}

void
MmWaveEnbRrcProtocolIdeal::SetLteEnbRrcSapProvider (LteEnbRrcSapProvider* p)
{
  m_enbRrcSapProvider = p;
}

LteEnbRrcSapUser*
MmWaveEnbRrcProtocolIdeal::GetLteEnbRrcSapUser ()
{
  return m_enbRrcSapUser;
}

void
MmWaveEnbRrcProtocolIdeal::SetCellId (uint16_t cellId)
{
  m_cellId = cellId;
}

LteUeRrcSapProvider*
MmWaveEnbRrcProtocolIdeal::GetUeRrcSapProvider (uint16_t rnti)
{
  std::map<uint16_t, LteUeRrcSapProvider*>::const_iterator it;
  it = m_enbRrcSapProviderMap.find (rnti);
  NS_ASSERT_MSG (it != m_enbRrcSapProviderMap.end (), "could not find RNTI = " << rnti);
  return it->second;
}

void
MmWaveEnbRrcProtocolIdeal::SetUeRrcSapProvider (uint16_t rnti, LteUeRrcSapProvider* p)
{
  std::map<uint16_t, LteUeRrcSapProvider*>::iterator it;
  it = m_enbRrcSapProviderMap.find (rnti);
  NS_ASSERT_MSG (it != m_enbRrcSapProviderMap.end (), "could not find RNTI = " << rnti);
  it->second = p;
}

void
MmWaveEnbRrcProtocolIdeal::DoSetupUe (uint16_t rnti, LteEnbRrcSapUser::SetupUeParameters params)
{
  NS_LOG_FUNCTION (this << rnti);
  m_enbRrcSapProviderMap[rnti] = 0;

}

void
MmWaveEnbRrcProtocolIdeal::DoRemoveUe (uint16_t rnti)
{
  NS_LOG_FUNCTION (this << rnti);
  m_enbRrcSapProviderMap.erase (rnti);
}

void
MmWaveEnbRrcProtocolIdeal::DoSendSystemInformation (uint16_t cellId, LteRrcSap::SystemInformation msg)
{
  NS_LOG_FUNCTION (this << m_cellId);
  // walk list of all nodes to get UEs with this cellId
  Ptr<LteUeRrc> ueRrc;
  for (NodeList::Iterator i = NodeList::Begin (); i != NodeList::End (); ++i)
    {
      Ptr<Node> node = *i;
      int nDevs = node->GetNDevices ();
      for (int j = 0; j < nDevs; ++j)
        {
          Ptr<MmWaveUeNetDevice> mmWaveUeDev = node->GetDevice (j)->GetObject <MmWaveUeNetDevice> ();
          if (mmWaveUeDev != 0)
            {
              Ptr<LteUeRrc> ueRrc = mmWaveUeDev->GetRrc ();
              NS_LOG_LOGIC ("considering UE IMSI " << mmWaveUeDev->GetImsi () << " that has cellId " << ueRrc->GetCellId ());
              if (ueRrc->GetCellId () == m_cellId)
                {
                  NS_LOG_LOGIC ("sending SI to IMSI " << mmWaveUeDev->GetImsi ());
                  ueRrc->GetLteUeRrcSapProvider ()->RecvSystemInformation (msg);
                  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                                       &LteUeRrcSapProvider::RecvSystemInformation,
                                       ueRrc->GetLteUeRrcSapProvider (),
                                       msg);
                }
            }
        }
    }
}

void
MmWaveEnbRrcProtocolIdeal::DoSendRrcConnectionSetup (uint16_t rnti, LteRrcSap::RrcConnectionSetup msg)
{
  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                       &LteUeRrcSapProvider::RecvRrcConnectionSetup,
                       GetUeRrcSapProvider (rnti),
                       msg);
}

void
MmWaveEnbRrcProtocolIdeal::DoSendRrcConnectionReconfiguration (uint16_t rnti, LteRrcSap::RrcConnectionReconfiguration msg)
{
  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                       &LteUeRrcSapProvider::RecvRrcConnectionReconfiguration,
                       GetUeRrcSapProvider (rnti),
                       msg);
}

void
MmWaveEnbRrcProtocolIdeal::DoSendRrcConnectionReestablishment (uint16_t rnti, LteRrcSap::RrcConnectionReestablishment msg)
{
  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                       &LteUeRrcSapProvider::RecvRrcConnectionReestablishment,
                       GetUeRrcSapProvider (rnti),
                       msg);
}

void
MmWaveEnbRrcProtocolIdeal::DoSendRrcConnectionReestablishmentReject (uint16_t rnti, LteRrcSap::RrcConnectionReestablishmentReject msg)
{
  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                       &LteUeRrcSapProvider::RecvRrcConnectionReestablishmentReject,
                       GetUeRrcSapProvider (rnti),
                       msg);
}

void
MmWaveEnbRrcProtocolIdeal::DoSendRrcConnectionRelease (uint16_t rnti, LteRrcSap::RrcConnectionRelease msg)
{
  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                       &LteUeRrcSapProvider::RecvRrcConnectionRelease,
                       GetUeRrcSapProvider (rnti),
                       msg);
}

void
MmWaveEnbRrcProtocolIdeal::DoSendRrcConnectionReject (uint16_t rnti, LteRrcSap::RrcConnectionReject msg)
{
  Simulator::Schedule (RRC_IDEAL_MSG_DELAY,
                       &LteUeRrcSapProvider::RecvRrcConnectionReject,
                       GetUeRrcSapProvider (rnti),
                       msg);
}

/*
 * The purpose of MmWaveEnbRrcProtocolIdeal is to avoid encoding
 * messages. In order to do so, we need to have some form of encoding for
 * inter-node RRC messages like HandoverPreparationInfo and HandoverCommand. Doing so
 * directly is not practical (these messages includes a lot of
 * information elements, so encoding all of them would defeat the
 * purpose of MmWaveEnbRrcProtocolIdeal. The workaround is to store the
 * actual message in a global map, so that then we can just encode the
 * key in a header and send that between eNBs over X2.
 *
 */

static std::map<uint32_t, LteRrcSap::HandoverPreparationInfo> g_handoverPreparationInfoMsgMap;
static uint32_t g_handoverPreparationInfoMsgIdCounter = 0;

/*
 * This header encodes the map key discussed above. We keep this
 * private since it should not be used outside this file.
 *
 */
class MmWaveIdealHandoverPreparationInfoHeader : public Header
{
public:
  uint32_t GetMsgId ();
  void SetMsgId (uint32_t id);
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);

private:
  uint32_t m_msgId;
};

uint32_t
MmWaveIdealHandoverPreparationInfoHeader::GetMsgId ()
{
  return m_msgId;
}

void
MmWaveIdealHandoverPreparationInfoHeader::SetMsgId (uint32_t id)
{
  m_msgId = id;
}


TypeId
MmWaveIdealHandoverPreparationInfoHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MmWaveIdealHandoverPreparationInfoHeader")
    .SetParent<Header> ()
    .AddConstructor<MmWaveIdealHandoverPreparationInfoHeader> ()
  ;
  return tid;
}

TypeId
MmWaveIdealHandoverPreparationInfoHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

void MmWaveIdealHandoverPreparationInfoHeader::Print (std::ostream &os)  const
{
  os << " msgId=" << m_msgId;
}

uint32_t MmWaveIdealHandoverPreparationInfoHeader::GetSerializedSize (void) const
{
  return 4;
}

void MmWaveIdealHandoverPreparationInfoHeader::Serialize (Buffer::Iterator start) const
{
  start.WriteU32 (m_msgId);
}

uint32_t MmWaveIdealHandoverPreparationInfoHeader::Deserialize (Buffer::Iterator start)
{
  m_msgId = start.ReadU32 ();
  return GetSerializedSize ();
}



Ptr<Packet>
MmWaveEnbRrcProtocolIdeal::DoEncodeHandoverPreparationInformation (LteRrcSap::HandoverPreparationInfo msg)
{
  uint32_t msgId = ++g_handoverPreparationInfoMsgIdCounter;
  NS_ASSERT_MSG (g_handoverPreparationInfoMsgMap.find (msgId) == g_handoverPreparationInfoMsgMap.end (), "msgId " << msgId << " already in use");
  NS_LOG_INFO (" encoding msgId = " << msgId);
  g_handoverPreparationInfoMsgMap.insert (std::pair<uint32_t, LteRrcSap::HandoverPreparationInfo> (msgId, msg));
  MmWaveIdealHandoverPreparationInfoHeader h;
  h.SetMsgId (msgId);
  Ptr<Packet> p = Create<Packet> ();
  p->AddHeader (h);
  return p;
}

LteRrcSap::HandoverPreparationInfo
MmWaveEnbRrcProtocolIdeal::DoDecodeHandoverPreparationInformation (Ptr<Packet> p)
{
  MmWaveIdealHandoverPreparationInfoHeader h;
  p->RemoveHeader (h);
  uint32_t msgId = h.GetMsgId ();
  NS_LOG_INFO (" decoding msgId = " << msgId);
  std::map<uint32_t, LteRrcSap::HandoverPreparationInfo>::iterator it = g_handoverPreparationInfoMsgMap.find (msgId);
  NS_ASSERT_MSG (it != g_handoverPreparationInfoMsgMap.end (), "msgId " << msgId << " not found");
  LteRrcSap::HandoverPreparationInfo msg = it->second;
  g_handoverPreparationInfoMsgMap.erase (it);
  return msg;
}



static std::map<uint32_t, LteRrcSap::RrcConnectionReconfiguration> g_handoverCommandMsgMap;
static uint32_t g_handoverCommandMsgIdCounter = 0;

/*
 * This header encodes the map key discussed above. We keep this
 * private since it should not be used outside this file.
 *
 */
class MmWaveIdealHandoverCommandHeader : public Header
{
public:
  uint32_t GetMsgId ();
  void SetMsgId (uint32_t id);
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);

private:
  uint32_t m_msgId;
};

uint32_t
MmWaveIdealHandoverCommandHeader::GetMsgId ()
{
  return m_msgId;
}

void
MmWaveIdealHandoverCommandHeader::SetMsgId (uint32_t id)
{
  m_msgId = id;
}


TypeId
MmWaveIdealHandoverCommandHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MmWaveIdealHandoverCommandHeader")
    .SetParent<Header> ()
    .AddConstructor<MmWaveIdealHandoverCommandHeader> ()
  ;
  return tid;
}

TypeId
MmWaveIdealHandoverCommandHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

void MmWaveIdealHandoverCommandHeader::Print (std::ostream &os)  const
{
  os << " msgId=" << m_msgId;
}

uint32_t MmWaveIdealHandoverCommandHeader::GetSerializedSize (void) const
{
  return 4;
}

void MmWaveIdealHandoverCommandHeader::Serialize (Buffer::Iterator start) const
{
  start.WriteU32 (m_msgId);
}

uint32_t MmWaveIdealHandoverCommandHeader::Deserialize (Buffer::Iterator start)
{
  m_msgId = start.ReadU32 ();
  return GetSerializedSize ();
}



Ptr<Packet>
MmWaveEnbRrcProtocolIdeal::DoEncodeHandoverCommand (LteRrcSap::RrcConnectionReconfiguration msg)
{
  uint32_t msgId = ++g_handoverCommandMsgIdCounter;
  NS_ASSERT_MSG (g_handoverCommandMsgMap.find (msgId) == g_handoverCommandMsgMap.end (), "msgId " << msgId << " already in use");
  NS_LOG_INFO (" encoding msgId = " << msgId);
  g_handoverCommandMsgMap.insert (std::pair<uint32_t, LteRrcSap::RrcConnectionReconfiguration> (msgId, msg));
  MmWaveIdealHandoverCommandHeader h;
  h.SetMsgId (msgId);
  Ptr<Packet> p = Create<Packet> ();
  p->AddHeader (h);
  return p;
}

LteRrcSap::RrcConnectionReconfiguration
MmWaveEnbRrcProtocolIdeal::DoDecodeHandoverCommand (Ptr<Packet> p)
{
  MmWaveIdealHandoverCommandHeader h;
  p->RemoveHeader (h);
  uint32_t msgId = h.GetMsgId ();
  NS_LOG_INFO (" decoding msgId = " << msgId);
  std::map<uint32_t, LteRrcSap::RrcConnectionReconfiguration>::iterator it = g_handoverCommandMsgMap.find (msgId);
  NS_ASSERT_MSG (it != g_handoverCommandMsgMap.end (), "msgId " << msgId << " not found");
  LteRrcSap::RrcConnectionReconfiguration msg = it->second;
  g_handoverCommandMsgMap.erase (it);
  return msg;
}





} // namespace ns3
