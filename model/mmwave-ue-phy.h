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


#ifndef SRC_MMWAVE_MODEL_MMWAVE_UE_PHY_H_
#define SRC_MMWAVE_MODEL_MMWAVE_UE_PHY_H_

#include <ns3/mmwave-phy.h>
#include "mmwave-phy-mac-common.h"
#include <ns3/ptr.h>
#include "mmwave-amc.h"
#include <map>
#include <ns3/lte-ue-phy-sap.h>
#include <ns3/lte-ue-cphy-sap.h>
#include <ns3/mmwave-harq-phy.h>


namespace ns3 {

class PacketBurst;
class mmwEnbPhy;

class MmWaveUePhy : public MmWavePhy
{
  friend class UeMemberLteUePhySapProvider;
  friend class MemberLteUeCphySapProvider<MmWaveUePhy>;

public:
  MmWaveUePhy ();

  MmWaveUePhy (Ptr<MmWaveSpectrumPhy> dlPhy, Ptr<MmWaveSpectrumPhy> ulPhy, const Ptr<Node> &n);

  virtual ~MmWaveUePhy () override;

  // inherited from Object
  static TypeId GetTypeId (void);
  virtual void DoInitialize (void) override;
  virtual void DoDispose (void) override;

  LteUeCphySapProvider* GetUeCphySapProvider ();
  void SetUeCphySapUser (LteUeCphySapUser* s);

  void SetTxPower (double pow);
  double GetTxPower () const;

  void SetNoiseFigure (double pf);
  double GetNoiseFigure () const;

  bool SendPacket (Ptr<Packet> packet);

  virtual Ptr<SpectrumValue> CreateTxPowerSpectralDensity (const std::vector<int> &rbIndexVector) const override;

  void DoSetSubChannels ();

  void SetSubChannelsForReception (std::vector <int> mask);
  std::vector <int> GetSubChannelsForReception (void);

  void SetSubChannelsForTransmission (std::vector <int> mask);
  std::vector <int> GetSubChannelsForTransmission (void);

  void DoSendControlMessage (Ptr<MmWaveControlMessage> msg);

  void RegisterToEnb (uint16_t cellId, Ptr<MmWavePhyMacCommon> config);
  Ptr<MmWaveSpectrumPhy> GetDlSpectrumPhy () const;
  Ptr<MmWaveSpectrumPhy> GetUlSpectrumPhy () const;

  void ReceiveControlMessageList (std::list<Ptr<MmWaveControlMessage> > msgList);

  void SlotIndication (uint16_t frameNum, uint8_t subframeNum, uint16_t slotNum);
  void StartVarTti ();
  void EndVarTti ();


  uint32_t GetSubframeNumber (void);

  void PhyDataPacketReceived (Ptr<Packet> p);

  void SendDataChannels (Ptr<PacketBurst> pb, std::list<Ptr<MmWaveControlMessage> > ctrlMsg, Time duration, uint8_t slotInd);

  void SendCtrlChannels (std::list<Ptr<MmWaveControlMessage> > ctrlMsg, Time prd);

  Ptr<MmWaveDlCqiMessage> CreateDlCqiFeedbackMessage (const SpectrumValue& sinr);

  void GenerateDlCqiReport (const SpectrumValue& sinr);

  bool IsReceptionEnabled ();

  void ResetReception ();

  uint16_t GetRnti ();

  void SetPhySapUser (MmWaveUePhySapUser* ptr);

  void SetHarqPhyModule (Ptr<MmWaveHarqPhy> harq);

  void ReceiveLteDlHarqFeedback (DlHarqInfo m);

  void SetPhyMacConfig (Ptr<MmWavePhyMacCommon> config);

  virtual AntennaArrayModel::BeamId GetBeamId (uint8_t rnti) const override
  {
    NS_UNUSED (rnti);
    NS_FATAL_ERROR ("ERROR");
  }

private:
  void DoReset ();
  void DoStartCellSearch (uint16_t dlEarfcn);
  void DoSynchronizeWithEnb (uint16_t cellId);
  void DoSynchronizeWithEnb (uint16_t cellId, uint16_t dlEarfcn);
  void DoSetPa (double pa);
  /**
   * \param rsrpFilterCoefficient value. Determines the strength of
   * smoothing effect induced by layer 3 filtering of RSRP
   * used for uplink power control in all attached UE.
   * If equals to 0, no layer 3 filtering is applicable.
   */
  void DoSetRsrpFilterCoefficient (uint8_t rsrpFilterCoefficient);
  void DoSetDlBandwidth (uint8_t ulBandwidth);
  void DoConfigureUplink (uint16_t ulEarfcn, uint8_t ulBandwidth);
  void DoConfigureReferenceSignalPower (int8_t referenceSignalPower);
  void DoSetRnti (uint16_t rnti);
  void DoSetTransmissionMode (uint8_t txMode);
  void DoSetSrsConfigurationIndex (uint16_t srcCi);

  void ReceiveDataPeriod (uint32_t slotNum);
  void QueueUlTbAlloc (TbAllocInfo tbAllocInfo);
  std::list<TbAllocInfo> DequeueUlTbAlloc ();

  MmWaveUePhySapUser* m_phySapUser;

  LteUeCphySapProvider* m_ueCphySapProvider;
  LteUeCphySapUser* m_ueCphySapUser;

  Ptr<MmWaveAmc> m_amc;
  std::vector <int> m_subChannelsforRx;

  uint32_t m_numRbg;

  Time m_wbCqiPeriod;       /**< Wideband Periodic CQI: 2, 5, 10, 16, 20, 32, 40, 64, 80 or 160 ms */
  Time m_wbCqiLast;

  VarTtiAllocInfo::TddMode m_prevSlotDir;

  SlotAllocInfo m_currSlotAllocInfo;
  std::vector< std::list<TbAllocInfo> > m_ulTbAllocQueue;       // for storing info on future UL TB transmissions
  bool m_ulGrant;               // true if no uplink grant in subframe, need to transmit UL control in PUCCH instead
  bool m_slotAllocInfoUpdated;

  Time m_dataPeriod;            // data period length in microseconds
  Time m_slotPeriod;
  Time m_lastSlotStart;

  bool m_dlConfigured;
  bool m_ulConfigured;

  TracedCallback< uint64_t, SpectrumValue&, SpectrumValue& > m_reportCurrentCellRsrpSinrTrace;

  TracedCallback<uint64_t, uint64_t> m_reportUlTbSize;
  TracedCallback<uint64_t, uint64_t> m_reportDlTbSize;

  bool m_receptionEnabled;
  uint16_t m_rnti;
  uint32_t m_currTbs {0};
  uint8_t m_currNumSym {0};

  Ptr<MmWaveHarqPhy> m_harqPhyModule;
};


}

#endif /* SRC_MMWAVE_MODEL_MMWAVE_UE_PHY_H_ */
