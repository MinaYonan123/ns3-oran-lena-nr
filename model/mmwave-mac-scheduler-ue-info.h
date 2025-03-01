/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 *   Copyright (c) 2018 Natale Patriciello <natale.patriciello@gmail.com>
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
 */
#pragma once

#include <ns3/antenna-array-model.h>
#include "mmwave-mac-sched-sap.h"
#include "mmwave-mac-harq-vector.h"
#include "mmwave-mac-scheduler-lcg.h"
#include "mmwave-amc.h"
#include <unordered_map>
#include <functional>

namespace ns3 {

class MmWaveMacSchedulerUeInfo;
/**
 * \brief Shared pointer to an instance of MmWaveMacSchedulerUeInfo
 */
typedef std::shared_ptr<MmWaveMacSchedulerUeInfo> UePtr;

/**
 * \ingroup mac-schedulers
 * \brief The representation of an user for any Mac scheduler
 *
 * Basic representation for an UE inside any scheduler. The class is responsible
 * to store all the UE-related field that can be used by any scheduler.
 *
 * If a scheduler needs to store additional information, it is necessary to
 * create a subclass and store the information there. Then, the scheduler
 * will return a pointer to a newly created instance through
 * MmWaveMacSchedulerNs3::CreateUeRepresentation.
 *
 * The class stores information such as RBG, MCS, and CQI. Information that
 * should be reset after each slot (such as RBG count) should be reset in the
 * method ResetDlSchedInfo() and ResetUlSchedInfo().
 *
 * When a scheduler assign new resources to the UE, it will call
 * the method UpdateDlMetric() or UpdateUlMetric(). Make sure all the relevant
 * information that should be updated for a correct sorting are updated there.
 *
 * \see MmWaveMacSchedulerUeInfoRR
 * \see MmWaveMacSchedulerUeInfoPF
 * \see MmWaveMacSchedulerUeInfoMR
 */
class MmWaveMacSchedulerUeInfo
{
public:
  /**
   * \brief Default Constructor (deleted)
   */
  MmWaveMacSchedulerUeInfo () = delete;

  /**
   * \brief Create a new UE representation
   * \param rnti the RNTI of the UE
   * \param beamId the BeamID of the UE (can be updated later)
   */
  MmWaveMacSchedulerUeInfo (uint16_t rnti, AntennaArrayModel::BeamId beamId) :
    m_rnti (rnti),
    m_beamId (beamId)
  {
  }

  /**
   * \brief ~MmWaveMacSchedulerUeInfo deconstructor
   */
  virtual ~MmWaveMacSchedulerUeInfo ()
  {
  }

  static uint16_t & GetDlRBG (const UePtr &ue)
  {
    return ue->m_dlRBG;
  }
  static uint16_t & GetUlRBG (const UePtr &ue)
  {
    return ue->m_ulRBG;
  }
  static uint8_t & GetDlSym (const UePtr &ue)
  {
    return ue->m_dlSym;
  }
  static uint8_t & GetUlSym (const UePtr &ue)
  {
    return ue->m_ulSym;
  }
  static uint8_t & GetDlMcs (const UePtr &ue)
  {
    return ue->m_dlMcs;
  }
  static uint8_t & GetUlMcs (const UePtr &ue)
  {
    return ue->m_ulMcs;
  }
  static uint32_t & GetDlTBS (const UePtr &ue)
  {
    return ue->m_dlTbSize;
  }
  static uint32_t & GetUlTBS (const UePtr &ue)
  {
    return ue->m_ulTbSize;
  }
  static std::unordered_map<uint8_t, LCGPtr> & GetDlLCG (const UePtr &ue)
  {
    return ue->m_dlLCG;
  }
  static std::unordered_map<uint8_t, LCGPtr> & GetUlLCG (const UePtr &ue)
  {
    return ue->m_ulLCG;
  }
  static MmWaveMacHarqVector & GetDlHarqVector (const UePtr &ue)
  {
    return ue->m_dlHarq;
  }
  static MmWaveMacHarqVector & GetUlHarqVector (const UePtr &ue)
  {
    return ue->m_ulHarq;
  }

  typedef std::function<std::unordered_map<uint8_t, LCGPtr> &(const UePtr &ue)> GetLCGFn;
  typedef std::function<MmWaveMacHarqVector& (const UePtr &ue)> GetHarqVectorFn;

  /**
   * \brief Reset DL information
   *
   * Called after each slot. It should reset all the information that are
   * slot-dependent.
   */
  virtual void ResetDlSchedInfo ()
  {
    m_dlMRBRetx = 0;
    m_dlRBG = 0;
    m_dlTbSize = 0;
    m_dlSym = 0;
  }

  /**
   * \brief Reset UL information
   *
   * Called after each slot. It should reset all the information that are
   * slot-dependent.
   */
  virtual void ResetUlSchedInfo ()
  {
    m_ulMRBRetx = 0;
    m_ulRBG = 0;
    m_ulSym = 0;
    m_ulTbSize = 0;
  }

  /**
   * \brief Update DL metrics after resources have been assigned
   *
   * The amount of assigned resources is stored inside m_dlRBG by the scheduler.
   */
  virtual void UpdateDlMetric (const Ptr<MmWavePhyMacCommon> &config, const Ptr<MmWaveAmc> &amc)
  {
    m_dlTbSize = static_cast<uint32_t> (amc->GetSpectralEfficiency (m_dlMcs,
                                                                    m_dlRBG * config->GetNumRbPerRbg ()) / 8);
  }

  /**
   * \brief ResetDlMetric
   *
   * Called when the scheduler has assigned RBGs, but the sum does not arrive
   * to a TBS > 0. The assignation is, therefore, not transformed in DCI.
   * These RBG will not be assigned, they will be empty in the slot.
   */
  virtual void ResetDlMetric ()
  {
    m_dlTbSize = 0;
  }

  /**
   * \brief Update UL metrics after resources have been assigned
   *
   * The amount of assigned resources is stored inside m_ulRBG by the scheduler.
   */
  virtual void UpdateUlMetric (const Ptr<MmWavePhyMacCommon> &config, const Ptr<MmWaveAmc> &amc)
  {
    m_ulTbSize = static_cast<uint32_t> (amc->GetSpectralEfficiency (m_ulMcs,
                                                                    m_ulRBG * config->GetNumRbPerRbg ()) / 8);
  }

  /**
   * \brief ResetDlMetric
   *
   * Called when the scheduler has assigned RBGs, but the sum does not arrive
   * to a TBS > 0. The assignation is, therefore, not transformed in DCI.
   * These RBG will not be assigned, they will be empty in the slot.
   */
  virtual void ResetUlMetric ()
  {
    m_ulTbSize = 0;
  }

  /**
   * \brief Received CQI information
   */
  struct CqiInfo
  {
    /**
     * \brief Type of CQI
     */
    enum CqiType
    {
      WB,             //!< Wide-band
      SB              //!< Sub-band
    } m_cqiType {WB}; //!< CQI type

    std::vector<double> m_sinr;   //!< Vector of SINR for the entire band
    std::vector<int16_t> m_rbCqi; //!< CQI for each Rsc Block, set to -1 if SINR < Threshold
    uint8_t m_cqi    {0};  //!< CQI reported value
    uint32_t m_timer {0};  //!< Timer (in slot number). When the timer is 0, the value is discarded
  };

  uint16_t m_rnti            {0};             //!< RNTI of the UE
  AntennaArrayModel::BeamId   m_beamId;       //!< Beam ID of the UE (kept updated as much as possible by MAC)

  std::unordered_map<uint8_t, LCGPtr> m_dlLCG;//!< DL LCG
  std::unordered_map<uint8_t, LCGPtr> m_ulLCG;//!< UL LCG

  uint16_t        m_dlMRBRetx {0};  //!< MRB assigned for retx. To update the name, what is MRB is not defined
  uint16_t        m_ulMRBRetx {0};  //!< MRB assigned for retx. To update the name, what is MRB is not defined
  uint16_t        m_dlRBG     {0};  //!< DL Resource Block Group assigned in this slot
  uint16_t        m_ulRBG     {0};  //!< UL Resource Block Group assigned in this slot
  uint8_t         m_dlSym     {0};  //!< Number of (new data) symbols assigned in this slot.
  uint8_t         m_ulSym     {0};  //!< Number of (new data) symbols assigned in this slot.

  uint8_t         m_dlMcs     {0};  //!< DL MCS
  uint8_t         m_ulMcs     {0};  //!< UL MCS

  uint32_t m_dlTbSize         {0};  //!< DL Transport Block Size, depends on MCS and RBG, updated in UpdateDlMetric()
  uint32_t m_ulTbSize         {0};  //!< UL Transport Block Size, depends on MCS and RBG, updated in UpdateDlMetric()

  CqiInfo m_dlCqi;                  //!< DL CQI information
  CqiInfo m_ulCqi;                  //!< UL CQI information

  MmWaveMacHarqVector m_dlHarq;     //!< HARQ process vector for DL
  MmWaveMacHarqVector m_ulHarq;     //!< HARQ process vector for UL
};

} // namespace ns3
