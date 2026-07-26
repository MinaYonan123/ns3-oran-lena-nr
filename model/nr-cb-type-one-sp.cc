// Copyright (c) 2024 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
//
// SPDX-License-Identifier: GPL-2.0-only

#include "nr-cb-type-one-sp.h"

#include <ns3/double.h>
#include <ns3/enum.h>
#include <ns3/log.h>
#include <ns3/math.h>
#include <ns3/nr-spectrum-value-helper.h>
#include <ns3/string.h>
#include <ns3/uinteger.h>
#include <ns3/simulator.h> // Added to use Simulator::Now()
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cmath>
#include <complex.h>
#include <sstream>

namespace ns3
{

using namespace std::complex_literals;

NS_LOG_COMPONENT_DEFINE("NrCbTypeOneSp");
NS_OBJECT_ENSURE_REGISTERED(NrCbTypeOneSp);

// For each rank -> for each layer/column in the precoding matrix W: the index of the beamforming
// vector in uniqueBfvs. See CreateUniqueBfvs().
// Comments like 5.2.2.2.1-x refer to 3GPP TS 38.214, Rel. 15, Table 5.2.2.2.1-x
static const std::vector<std::vector<size_t>> uniqueBfvIndsPerRank{
    {0},          // 5.2.2.2.1-5
    {0, 1},       // 5.2.2.2.1-6
    {0, 1, 0},    // 5.2.2.2.1-7 (both cases nPorts<16 and >=16)
    {0, 1, 0, 1}, // 5.2.2.2.1-8 (both cases nPorts<16 and >=16)
};

// For each rank -> for each layer/column in the precoding matrix W: the sign before phi_n (+1 / -1)
// in the lower half of the matrix W (second polarization)
static const std::vector<std::vector<double>> signPhiNPerRank{
    {1.0},                  // 5.2.2.2.1-5
    {1.0, -1.0},            // 5.2.2.2.1-6
    {1.0, 1.0, -1.0},       // 5.2.2.2.1-7 (both cases nPorts<16 and >=16)
    {1.0, 1.0, -1.0, -1.0}, // 5.2.2.2.1-8 (both cases nPorts<16 and >=16)
};

TypeId
NrCbTypeOneSp::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::NrCbTypeOneSp").SetParent<NrCbTypeOne>().AddConstructor<NrCbTypeOneSp>()
        .AddAttribute("PortPower", 
                     "Power allocation for each port (must sum to approximately 1.0)",
                     StringValue(""),
                     MakeStringAccessor(&NrCbTypeOneSp::SetPortPowerString),
                     MakeStringChecker());
    return tid;
}

void
NrCbTypeOneSp::Init()
{
    NS_ASSERT_MSG(m_codebookMode == 1, "Only codebook mode 1 is currently supported");
    NS_ASSERT_MSG(m_rank > 0, "Rank must not be 0");
    NS_ASSERT_MSG(m_rank <= 4, "This implementation supports at most rank 4 MIMO");

    m_o1 = (m_n1 > 1) ? 4 : 1;
    m_o2 = (m_n2 > 1) ? 4 : 1;
    // Save the old number of ports before recalculating
    size_t oldNPorts = m_nPorts;
    // Calculate the new number of ports
    m_nPorts = (m_isDualPol) ? 2 * m_n1 * m_n2 : m_n1 * m_n2;
    
    NS_LOG_INFO("Initializing codebook: n1=" << m_n1 << ", n2=" << m_n2 
                << ", isDualPol=" << m_isDualPol << ", ports=" << m_nPorts);

    NS_ASSERT_MSG(m_nPorts > 0, "Number of CSI-RS ports must not be 0");
    NS_ASSERT_MSG(m_isDualPol || (m_nPorts <= 2),
                  "For > 2 antenna ports, dual polarization is required");
    NS_ASSERT_MSG(m_nPorts <= 32, "Number of CSI-RS ports must not be greater than 32");
    
    // If the number of ports has changed and we have stored port power values
    if (oldNPorts != m_nPorts && !m_portpower.empty())
    {
        NS_LOG_INFO("Number of ports changed from " << oldNPorts << " to " << m_nPorts 
                    << ". Adjusting port power vector.");
                    
        // If port power vector size doesn't match the new port count, resize it
        if (m_portpower.size() != m_nPorts)
        {
            // If we have fewer power values than ports, duplicate the last value
            if (m_portpower.size() < m_nPorts)
            {
                double lastValue = m_portpower.back();
                m_portpower.resize(m_nPorts, lastValue);
            }
            // If we have more power values than ports, truncate
            else
            {
                m_portpower.resize(m_nPorts);
            }
            
            NS_LOG_INFO("Adjusted port power vector size to " << m_portpower.size());
        }
    }

    InitNumI11();
    InitNumI12();
    InitNumI13();
    InitNumI1();
    InitNumI2();
    InitWParams();
};

ComplexMatrixArray
NrCbTypeOneSp::GetBasePrecMat(size_t i1, size_t i2) const
{
    auto i11 = MapToI11(i1);
    auto i12 = MapToI12(i1);
    auto i13 = MapToI13(i1);
    return GetBasePrecMatFromIndex(i11, i12, i13, i2);
}

void NrCbTypeOneSp::SetPortPowerString(const std::string& powerStr)
{
    NS_LOG_FUNCTION(this);
    
    if (powerStr.empty())
    {
        return; // Use default values
    }
    
    std::vector<double> powerVec;
    std::stringstream ss(powerStr);
    std::string token;
    
    while (std::getline(ss, token, ',')) 
    {
        // Convert token to double and add to vector
        try 
        {
            double value = std::stod(token);
            powerVec.push_back(value);
        } 
        catch (const std::exception& e) 
        {
            NS_LOG_ERROR("Error parsing port power value: " << token);
        }
    }
    
    if (!powerVec.empty()) 
    {
        // If codebook is not properly initialized yet, just store the values
        // and they'll be used when m_nPorts is properly set
        if (m_nPorts <= 1)
        {
            NS_LOG_INFO("Codebook not fully initialized yet (nPorts=" << m_nPorts 
                        << "). Storing port power for later.");
            m_portpower = powerVec;
        }
        else
        {
            // Normal case when codebook is properly initialized
            SetPortPower(powerVec);
        }
    }
}

void NrCbTypeOneSp::SetPortPower(const std::vector<double>& powerVec)
{
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("Setting port power: vector size=" << powerVec.size() 
                << ", nPorts=" << m_nPorts);
    
    if (powerVec.size() != m_nPorts)
    {
        NS_LOG_WARN("Power vector size (" << powerVec.size() 
                    << ") doesn't match number of ports (" << m_nPorts << ")");
        
        if (m_nPorts <= 1)
        {
            NS_LOG_INFO("Codebook not fully initialized yet. Storing port power for later use.");
            m_portpower = powerVec;
            return;
        }
        
        // Create a new vector with the correct size
        std::vector<double> adjustedVec;
        adjustedVec.reserve(m_nPorts);
        
        // Copy as many values as we can from the input vector
        for (size_t i = 0; i < std::min(powerVec.size(), m_nPorts); ++i)
        {
            adjustedVec.push_back(powerVec[i]);
        }
        
        // If we need more values, duplicate the last one
        if (adjustedVec.size() < m_nPorts && !powerVec.empty())
        {
            double lastValue = powerVec.back();
            while (adjustedVec.size() < m_nPorts)
            {
                adjustedVec.push_back(lastValue);
            }
        }
        
        // Store the adjusted vector
        m_portpower = adjustedVec;
        NS_LOG_INFO("Adjusted port power vector to size " << m_portpower.size());
    }
    else
    {
        // Normal case - vector size matches port count
        for (auto it = powerVec.begin(); it != powerVec.end(); it++)
        {
            NS_ASSERT_MSG(*it >= 0.0, "Power allocation must be positive");
        }
        m_portpower = powerVec;
    }

    // Log the configuration
    NS_LOG_INFO("Port power allocation updated:");
    for (size_t i = 0; i < m_portpower.size(); ++i)
    {
        NS_LOG_INFO("  Port " << i << ": " << m_portpower[i]);
    }
}

const std::vector<double>& NrCbTypeOneSp::GetPortPower() const
{
    return m_portpower;
}
// making sure that all the power allocation sums to 1.0
bool NrCbTypeOneSp::IsPowerAllocationActive() const
{
    return !m_portpower.empty() && 
          !std::all_of(m_portpower.begin(), m_portpower.end(),
                      [](double p) { return p == 1.0; });
}
ComplexMatrixArray
NrCbTypeOneSp::GetBasePrecMatFromIndex(size_t i11, size_t i12, size_t i13, size_t i2) const
{
    if (m_nPorts == 1)
    {
        auto res = ComplexMatrixArray(1, 1);
        res(0, 0) = 1.0;
        return res;
    }

    // m_nPorts is even-numbered. The upper half of ports represent the first polarization angle
    auto precMat = ComplexMatrixArray(m_nPorts, m_rank);
    auto phase = M_PI * static_cast<double>(i2) / 2.0;
    auto phiN = std::complex<double>{cos(phase), sin(phase)}; // phi_n as defined in 5.2.2.2.1
    auto normalizer = 1.0 / sqrt(m_nPorts * m_rank);
    auto uniqueBfvs = CreateUniqueBfvs(i11, i12, i13);
    for (size_t layer = 0; layer < m_rank; layer++)
    {
        // The beamforming vector for the first polarization
        auto v = uniqueBfvs[m_uniqueBfvInds[layer]];
        NS_ASSERT_MSG(v.size() == m_nPorts / 2,
                      "Size of a per-polarization beamforming vector must be nPorts/2");
        for (size_t vIdx = 0; vIdx < v.size(); vIdx++)
        {
            // Fill in the precoding matrix W for both the first and second polarization
            // Apply port power scaling if power allocation is active
            if (IsPowerAllocationActive())
            {
                // First polarization
                precMat(vIdx, layer) =  normalizer *   v[vIdx]*(m_portpower[vIdx]);
                // Second polarization
                precMat(vIdx + v.size(), layer) =   normalizer *   m_signPhiN[layer] * phiN * v[vIdx] 
                                                * (m_portpower[vIdx + v.size()]);
            }
            else
            {
                // Use original values without port power scaling
                precMat(vIdx, layer) = normalizer *  v[vIdx];
                precMat(vIdx + v.size(), layer) = normalizer * m_signPhiN[layer] * phiN * v[vIdx];
            }
        }
    }
    return precMat;
}

size_t
NrCbTypeOneSp::GetNumI11() const
{
    return m_numI11;
}

size_t
NrCbTypeOneSp::GetNumI12() const
{
    return m_numI12;
}

size_t
NrCbTypeOneSp::GetNumI13() const
{
    return m_numI13;
}

void
NrCbTypeOneSp::InitNumI11()
{
    if (!m_isDualPol && (m_n1 == 2) && (m_n2 == 1))
    {
        // Two antenna ports, this is covered by 5.2.2.2.1-1.
        // Iteration over entries of 5.2.2.2.1-1 is interpreted as i2; i1 value remains 1.
        m_numI11 = 1;
    }
    else if (IsRank34AndAtLeast16Ports())
    {
        // Lower part of 5.2.2.2.1-7 and 5.2.2.2.1-8
        NS_ASSERT(m_n1 > 2);
        m_numI11 = m_n1 * m_o1 / 2;
    }
    else
    {
        // Set default number of beams in horizontal direction
        m_numI11 = m_n1 * m_o1;
    }
    NS_ASSERT(m_numI11 > 0);
}

void
NrCbTypeOneSp::InitNumI12()
{
    if (!m_isDualPol && (m_n1 == 1) && (m_n2 == 2))
    {
        // Two antenna ports, this is covered by 5.2.2.2.1-1.
        // Iteration over entries of 5.2.2.2.1-1 is interpreted as i2; i1 value remains 1.
        m_numI12 = 1;
    }
    else
    {
        // Set default number of beams in vertical direction
        m_numI12 = m_n2 * m_o2;
    }
    NS_ASSERT(m_numI12 > 0);
}

void
NrCbTypeOneSp::InitNumI13()
{
    InitK1K2();

    if (m_rank == 1)
    {
        m_numI13 = 1;
    }
    else if (!m_k1Factors.empty())
    {
        m_numI13 = m_k1Factors.size();
    }
    else if (IsRank34AndAtLeast16Ports())
    {
        // MIMO rank 3 or 4 with >= 16 ports: lower part of tables 5.2.2.2.1-7 and 5.2.2.2.1-8
        m_numI13 = 4;
    }
    else
    {
        NS_FATAL_ERROR("Unsupported configuration");
    }
    NS_ASSERT(m_numI13 > 0);
}

void
NrCbTypeOneSp::InitK1K2()
{
    // NOLINTBEGIN(bugprone-branch-clone)
    if (m_rank == 1)
    {
        m_k1Factors = {};
        m_k2Factors = {};
    }
    else if (m_rank == 2)
    {
        DoInitK1K2Rank2();
    }
    else if (IsRank34AndBelow16Ports())
    {
        DoInitK1K2Rank34();
    }
    else if (IsRank34AndAtLeast16Ports())
    {
        // No k1-k2; i13 is mapped to different theta values multiplied with v-tilde
        m_k1Factors = {};
        m_k2Factors = {};
    }
    else
    {
        NS_FATAL_ERROR("Codebook configuration not supported");
    }
    // NOLINTEND(bugprone-branch-clone)
}

void
NrCbTypeOneSp::DoInitK1K2Rank2()
{
    // The factors before O1 and O2 in Table 5.2.2.2.1-3
    if (m_n1 > m_n2 && m_n2 > 1)
    {
        m_k1Factors = {0, 1, 0, 2};
        m_k2Factors = {0, 0, 1, 0};
    }
    else if (m_n1 == m_n2)
    {
        m_k1Factors = {0, 1, 0, 1};
        m_k2Factors = {0, 0, 1, 1};
    }
    else if (m_n1 == 2 && m_n2 == 1)
    {
        m_k1Factors = {0, 1};
        m_k2Factors = {0, 0};
    }
    else if (m_n1 > 2 && m_n2 == 1)
    {
        m_k1Factors = {0, 1, 2, 3};
        m_k2Factors = {0, 0, 0, 0};
    }
    else
    {
        NS_FATAL_ERROR("Invalid n1-n2 configuration");
    }
}

void
NrCbTypeOneSp::DoInitK1K2Rank34()
{
    // The factors before O1 and O2 in Table 5.2.2.2.1-4
    if (m_n1 == 2 && m_n2 == 1)
    {
        m_k1Factors = {1};
        m_k2Factors = {0};
    }
    else if (m_n1 == 4 && m_n2 == 1)
    {
        m_k1Factors = {1, 2, 3};
        m_k2Factors = {0, 0, 0};
    }
    else if (m_n1 == 6 && m_n2 == 1)
    {
        m_k1Factors = {1, 2, 3, 4};
        m_k2Factors = {0, 0, 0, 0};
    }
    else if (m_n1 == 2 && m_n2 == 2)
    {
        m_k1Factors = {1, 0, 1};
        m_k2Factors = {0, 1, 1};
    }
    else if (m_n1 == 3 && m_n2 == 2)
    {
        m_k1Factors = {1, 0, 1, 2};
        m_k2Factors = {0, 1, 1, 0};
    }
    else
    {
        NS_FATAL_ERROR("Invalid n1-n2 configuration");
    }
}

void
NrCbTypeOneSp::InitNumI1()
{
    NS_ASSERT(m_numI11 > 0);
    NS_ASSERT(m_numI12 > 0);
    NS_ASSERT(m_numI13 > 0);
    m_numI1 = m_numI11 * m_numI12 * m_numI13;
}

void
NrCbTypeOneSp::InitNumI2()
{
    if (m_nPorts == 1)
    {
        m_numI2 = 1;
    }
    else
    {
        if (m_rank == 1)
        {
            m_numI2 = 4; // 5.2.2.2.1-1 (left) or 5.2.2.2.1-5
        }
        else
        {
            m_numI2 = 2; // 5.2.2.2.1-1 (right), 5.2.2.2.1-6, 5.2.2.2.1-7, 5.2.2.2.1-8
        }
    }
}

void
NrCbTypeOneSp::InitWParams()
{
    m_uniqueBfvInds = uniqueBfvIndsPerRank[m_rank - 1];
    m_signPhiN = signPhiNPerRank[m_rank - 1];
    if ((m_nPorts == 2) && (m_rank == 2))
    {
        // When m_nPorts == 2, uniqueBfvs only has a single vector
        m_uniqueBfvInds = {0, 0};
    }
    NS_ASSERT_MSG(m_uniqueBfvInds.size() == m_rank,
                  "Precoding matrix index definitions must have m_rank columns");
}

size_t
NrCbTypeOneSp::MapToI11(size_t i1) const
{
    return i1 % m_numI11;
}

size_t
NrCbTypeOneSp::MapToI12(size_t i1) const
{
    return (i1 / m_numI11) % m_numI12;
}

size_t
NrCbTypeOneSp::MapToI13(size_t i1) const
{
    auto i13 = i1 / (m_numI11 * m_numI12);
    NS_ASSERT(i13 < m_numI13);
    return i13;
}

size_t
NrCbTypeOneSp::MapToK1(size_t i13) const
{
    NS_ASSERT_MSG(!m_k1Factors.empty(), "Cannot get k1 value for this configuration");
    NS_ASSERT(i13 < m_k1Factors.size());
    return m_k1Factors[i13] * m_o1;
}

size_t
NrCbTypeOneSp::MapToK2(size_t i13) const
{
    NS_ASSERT_MSG(!m_k2Factors.empty(), "Cannot get k2 value for this configuration");
    NS_ASSERT(i13 < m_k2Factors.size());
    return m_k2Factors[i13] * m_o2;
}

std::vector<std::vector<std::complex<double>>>
NrCbTypeOneSp::CreateUniqueBfvs(size_t i11, size_t i12, size_t i13) const
{
    auto uniqueBfvs = std::vector<std::vector<std::complex<double>>>{};

    NS_ASSERT_MSG(m_nPorts > 1, "Cannot use multiple polarizations for single port codebook");

    if (m_nPorts == 2)
    {
        // For 2 ports, there is only a single wideband value
        uniqueBfvs.push_back({1.0 + 0.0i});
    }
    else if (m_rank == 1)
    {
        uniqueBfvs.push_back(CreateVecV(i11, i12)); // v_{l,m} in 5.2.2.2.1-5
    }
    else if (m_rank == 2 || IsRank34AndBelow16Ports())
    {
        // 5.2.2.2.1-6, and upper parts of 5.2.2.2.1-7, 5.2.2.2.1-8
        auto k1 = MapToK1(i13);
        auto k2 = MapToK2(i13);
        uniqueBfvs.push_back(CreateVecV(i11, i12));           // v_{l,m}
        uniqueBfvs.push_back(CreateVecV(i11 + k1, i12 + k2)); // v_{l',m'}
    }
    else if (IsRank34AndAtLeast16Ports())
    {
        // Lower parts of 5.2.2.2.1-7, 5.2.2.2.1-8
        auto vTilde = CreateVecVtilde(i11, i12);
        auto phase = M_PI * static_cast<double>(i13) / 4.0;
        auto thetaP = std::complex<double>{cos(phase), sin(phase)};
        uniqueBfvs.push_back(ConcatVtildeThetaVtilde(vTilde, thetaP));
        uniqueBfvs.push_back(ConcatVtildeThetaVtilde(vTilde, -thetaP));
    }
    else
    {
        NS_FATAL_ERROR("Codebook configuration not supported");
    }

    NS_ASSERT(!uniqueBfvs.empty());
    return uniqueBfvs;
}

std::vector<std::complex<double>>
NrCbTypeOneSp::CreateVecV(size_t l, size_t m) const
{
    auto vecH = std::vector<std::complex<double>>{};
    for (size_t i = 0; i < m_n1; i++)
    {
        auto phase = (2.0 * M_PI * l * i) / static_cast<double>(m_o1 * m_n1);
        vecH.push_back({cos(phase), sin(phase)});
    }
    NS_ASSERT_MSG(vecH.size() == m_n1, "Horizontal beamforming vector size mismatch");
    return Kroneckerproduct(vecH, CreateVecU(m));
}

std::vector<std::complex<double>>
NrCbTypeOneSp::CreateVecVtilde(size_t l, size_t m) const
{
    auto vecH = std::vector<std::complex<double>>{};
    for (size_t i = 0; i < m_n1 / 2; i++)
    {
        auto phase = (4.0 * M_PI * l * i) / static_cast<double>(m_o1 * m_n1);
        vecH.push_back({cos(phase), sin(phase)});
    }
    NS_ASSERT_MSG(vecH.size() == m_n1 / 2, "Horizontal beamforming vector size mismatch");
    return Kroneckerproduct(vecH, CreateVecU(m));
}

std::vector<std::complex<double>>
NrCbTypeOneSp::ConcatVtildeThetaVtilde(const std::vector<std::complex<double>>& vTilde,
                                       std::complex<double> signedTheta) const
{
    // Create the lower half of the concatenated vector
    auto vLower = vTilde;
    for (auto& el : vLower)
    {
        el *= signedTheta;
    }

    // Concatenate the vectors
    auto vConcat = vTilde;
    vConcat.insert(vConcat.end(), vLower.begin(), vLower.end());
    return vConcat;
}

std::vector<std::complex<double>>
NrCbTypeOneSp::CreateVecU(size_t m) const
{
    auto vecU = std::vector<std::complex<double>>{};
    if (m_n2 == 1)
    {
        vecU.push_back(1.0 + 0.0i);
    }
    else
    {
        for (size_t i = 0; i < m_n2; i++)
        {
            auto phase = (2 * M_PI * m * i) / (m_o2 * m_n2);
            vecU.push_back({cos(phase), sin(phase)});
        }
    }
    return vecU;
}

std::vector<std::complex<double>>
NrCbTypeOneSp::Kroneckerproduct(std::vector<std::complex<double>> vecA,
                                std::vector<std::complex<double>> vecB)
{
    std::vector<std::complex<double>> v;
    for (const auto& elemA : vecA)
    {
        for (const auto& elemB : vecB)
        {
            v.push_back(elemA * elemB);
        }
    }
    return v;
}

bool
NrCbTypeOneSp::IsRank34AndBelow16Ports() const
{
    // Condition for upper part of Tables 5.2.2.2.1-7, 5.2.2.2.1-8
    return (m_rank == 3 || m_rank == 4) && m_nPorts < 16;
}

bool
NrCbTypeOneSp::IsRank34AndAtLeast16Ports() const
{
    // Condition for lower part of Tables 5.2.2.2.1-7, 5.2.2.2.1-8
    return (m_rank == 3 || m_rank == 4) && m_nPorts >= 16;
}

} // namespace ns3
