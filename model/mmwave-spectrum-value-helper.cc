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



#include <map>
#include <cmath>
#include <ns3/log.h>
#include <ns3/fatal-error.h>
#include <ns3/string.h>
#include <ns3/abort.h>

#include "mmwave-spectrum-value-helper.h"

namespace std {

// ostream&
// operator << (ostream& os, const vector<int>& v)
// {
//   vector<int>::const_iterator it = v.begin ();
//   while (it != v.end ())
//     {
//       os << *it << " ";
//       ++it;
//     }
//   os << endl;
//   return os;
// }

}
namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("MmWaveSpectrumValueHelper");


struct MmWaveSpectrumModelId
{
  /**
   * Constructor
   *
   * \param f center frequency
   * \param b bandwidth
   */
  MmWaveSpectrumModelId (double f, uint8_t b);
  double frequency; ///<
  uint8_t  bandwidth; ///< bandwidth
};

MmWaveSpectrumModelId::MmWaveSpectrumModelId (double f, uint8_t b)
  : frequency (f),
  bandwidth (b)
{
}

/**
 * Constructor
 *
 * \param a lhs
 * \param b rhs
 * \returns true if earfcn less than of if earfcn equal and bandwidth less than
 */
bool
operator < (const MmWaveSpectrumModelId& a, const MmWaveSpectrumModelId& b)
{
  return ( (a.frequency < b.frequency) || ( (a.frequency == b.frequency) && (a.bandwidth < b.bandwidth) ) );
}


//Ptr<SpectrumModel> MmWaveSpectrumValueHelper::m_model = 0;

static std::map<MmWaveSpectrumModelId, Ptr<SpectrumModel> > g_mmWaveSpectrumModelMap; ///< mmwave spectrum model map

Ptr<SpectrumModel>
MmWaveSpectrumValueHelper::GetSpectrumModel (Ptr<MmWavePhyMacCommon> ptrConfig)
{
  NS_LOG_FUNCTION (ptrConfig->GetCenterFrequency () << (uint32_t) ptrConfig->GetBandwidthInRbs ());

  MmWaveSpectrumModelId modelId = MmWaveSpectrumModelId (ptrConfig->GetCenterFrequency (), ptrConfig->GetBandwidthInRbs ());

  /*if (m_model != 0 && m_model->GetNumBands () != 0)
  {
        return m_model;
  }*/

  if (g_mmWaveSpectrumModelMap.find (modelId) != g_mmWaveSpectrumModelMap.end ())
    {
      return g_mmWaveSpectrumModelMap.find (modelId)->second;
    }

  double fc = ptrConfig->GetCenterFrequency ();
  double f = 0.00;

  NS_ASSERT_MSG (fc != 0, "The carrier frequency cannot be set to 0");

  f = fc - (ptrConfig->GetBandwidthInRbs () * ptrConfig->GetSubcarrierSpacing () * ptrConfig->GetNumScsPerRb () / 2.0);

  Bands rbs; // A vector representing each resource block
  for (uint32_t numrb = 0; numrb < ptrConfig->GetBandwidthInRbs (); ++numrb)
    {
      BandInfo rb;
      rb.fl = f;
      f += ptrConfig->GetSubcarrierSpacing () * ptrConfig->GetNumScsPerRb () / 2;
      rb.fc = f;
      f += ptrConfig->GetSubcarrierSpacing () * ptrConfig->GetNumScsPerRb () / 2;
      rb.fh = f;

      rbs.push_back (rb);
    }

  Ptr<SpectrumModel> model = Create<SpectrumModel> (rbs);

  g_mmWaveSpectrumModelMap.insert (std::pair<MmWaveSpectrumModelId, Ptr<SpectrumModel> > (modelId, model));

  return model;
}

Ptr<SpectrumValue>
MmWaveSpectrumValueHelper::CreateTxPowerSpectralDensity (Ptr<MmWavePhyMacCommon> ptrConfig, double powerTx, std::vector <int> activeRbs)
{
  Ptr<SpectrumModel> model = GetSpectrumModel (ptrConfig);
  Ptr<SpectrumValue> txPsd = Create <SpectrumValue> (model);

  double powerTxW = std::pow (10., (powerTx - 30) / 10);

  double txPowerDensity = 0;
  txPowerDensity = (powerTxW / (ptrConfig->GetBandwidth ()));

  for (std::vector <int>::iterator it = activeRbs.begin (); it != activeRbs.end (); it++)
    {
      int rbId = (*it);
      (*txPsd)[rbId] = txPowerDensity;
    }

  NS_LOG_LOGIC (*txPsd);

  return txPsd;

}

Ptr<SpectrumValue>
MmWaveSpectrumValueHelper::CreateTxPowerSpectralDensity (Ptr<MmWavePhyMacCommon> ptrConfig, double powerTx, std::map<int, double> powerTxMap, std::vector <int> activeRbs)
{
  Ptr<SpectrumValue> dummy;
  return dummy;
}



Ptr<SpectrumValue>
MmWaveSpectrumValueHelper::CreateNoisePowerSpectralDensity (Ptr<MmWavePhyMacCommon> ptrConfig, double noiseFigure)
{
  Ptr<SpectrumModel> model = GetSpectrumModel (ptrConfig);
  Ptr<SpectrumValue> noisePsd = CreateNoisePowerSpectralDensity (noiseFigure, model);
  return noisePsd;
}

Ptr<SpectrumValue>
MmWaveSpectrumValueHelper::CreateNoisePowerSpectralDensity (double noiseFigureDb, Ptr<SpectrumModel> spectrumModel)
{
  NS_LOG_FUNCTION (noiseFigureDb << spectrumModel);
  const double kT_dBm_Hz = -174.0;  // dBm/Hz
  double kT_W_Hz = std::pow (10.0, (kT_dBm_Hz - 30) / 10.0);
  double noiseFigureLinear = std::pow (10.0, noiseFigureDb / 10.0);
  double noisePowerSpectralDensity =  kT_W_Hz * noiseFigureLinear;

  Ptr<SpectrumValue> noisePsd = Create <SpectrumValue> (spectrumModel);
  (*noisePsd) = noisePowerSpectralDensity;
  return noisePsd;
}

} // namespace ns3
