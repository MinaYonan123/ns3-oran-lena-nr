/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Biljana Bojovic <bbojovic@cttc.es>
 *
 */

#ifndef COMPONENT_CARRIER_MMWAVE_UE_H
#define COMPONENT_CARRIER_MMWAVE_UE_H

#include <ns3/object.h>
#include <ns3/packet.h>
#include <ns3/nstime.h>
#include "ns3/mmwave-phy.h"
#include <ns3/mmwave-ue-phy.h>
#include <ns3/component-carrier.h>

namespace ns3 {

class MmWaveUeMac;
/**
 * \ingroup lte
 *
 * ComponentCarrierMmWaveUe
 */
class ComponentCarrierMmWaveUe : public ComponentCarrier
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  ComponentCarrierMmWaveUe ();

  virtual ~ComponentCarrierMmWaveUe (void);
  virtual void DoDispose (void);


  /**
   * \return a pointer to the physical layer.
   */
  Ptr<MmWaveUePhy> GetPhy (void) const;

  /**
   * \return a pointer to the MAC layer.
   */
  Ptr<MmWaveUeMac> GetMac (void) const;

  /**
   * Set LteUePhy
   * \param s a pointer to the LteUePhy
   */
  void SetPhy (Ptr<MmWaveUePhy> s);

  /**
   * Set the LteEnbMac
   * \param s a pointer to the LteEnbMac
   */
  void SetMac (Ptr<MmWaveUeMac> s);

  virtual void SetDlBandwidth (uint8_t bw) override { m_dlBandwidth = bw; }
  virtual void SetUlBandwidth (uint8_t bw) override { m_ulBandwidth = bw; }

protected:
  // inherited from Object
  virtual void DoInitialize (void);

private:
  Ptr<MmWaveUePhy> m_phy; ///< the Phy instance of this eNodeB component carrier
  Ptr<MmWaveUeMac> m_mac; ///< the MAC instance of this eNodeB component carrier

};



} // namespace ns3



#endif /* COMPONENT_CARRIER_UE_H */
