﻿/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

// Copyright (c) 2020 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
//
// SPDX-License-Identifier: GPL-2.0-only

#include "nr-sl-ue-mac-scheduler-ns3.h"

#include "nr-sl-ue-mac.h"

#include <ns3/boolean.h>
#include <ns3/log.h>
#include <ns3/pointer.h>
#include <ns3/uinteger.h>

#include <memory>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NrSlUeMacSchedulerNs3");
NS_OBJECT_ENSURE_REGISTERED(NrSlUeMacSchedulerNs3);

TypeId
NrSlUeMacSchedulerNs3::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::NrSlUeMacSchedulerNs3")
            .SetParent<NrSlUeMacScheduler>()
            .SetGroupName("nr")
            .AddAttribute("FixNrSlMcs",
                          "Fix MCS to value set in SetInitialNrSlMcs",
                          BooleanValue(false),
                          MakeBooleanAccessor(&NrSlUeMacSchedulerNs3::UseFixedNrSlMcs,
                                              &NrSlUeMacSchedulerNs3::IsNrSlMcsFixed),
                          MakeBooleanChecker())
            .AddAttribute("InitialNrSlMcs",
                          "The initial value of the MCS used for NR Sidelink",
                          UintegerValue(14),
                          MakeUintegerAccessor(&NrSlUeMacSchedulerNs3::SetInitialNrSlMcs,
                                               &NrSlUeMacSchedulerNs3::GetInitialNrSlMcs),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute("NrSlAmc",
                          "The NR SL AMC of this scheduler",
                          PointerValue(),
                          MakePointerAccessor(&NrSlUeMacSchedulerNs3::m_nrSlAmc),
                          MakePointerChecker<NrAmc>());
    return tid;
}

NrSlUeMacSchedulerNs3::NrSlUeMacSchedulerNs3()
{
    m_uniformVariable = CreateObject<UniformRandomVariable>();
}

NrSlUeMacSchedulerNs3::~NrSlUeMacSchedulerNs3()
{
    // just to make sure
    m_dstMap.clear();
}

void
NrSlUeMacSchedulerNs3::DoCschedNrSlLcConfigReq(
    const NrSlUeCmacSapProvider::SidelinkLogicalChannelInfo& params)
{
    NS_LOG_FUNCTION(this << params.dstL2Id << +params.lcId);

    auto dstInfo = CreateDstInfo(params);
    const auto& lcgMap = dstInfo->GetNrSlLCG(); // Map of unique_ptr should not copy
    auto itLcg = lcgMap.find(params.lcGroup);
    auto itLcgEnd = lcgMap.end();
    if (itLcg == itLcgEnd)
    {
        NS_LOG_DEBUG("Created new NR SL LCG for destination "
                     << dstInfo->GetDstL2Id()
                     << " LCG ID =" << static_cast<uint32_t>(params.lcGroup));
        itLcg = dstInfo->Insert(CreateLCG(params.lcGroup));
    }

    itLcg->second->Insert(CreateLC(params));
    NS_LOG_INFO("Added LC id " << +params.lcId << " in LCG " << +params.lcGroup);
    // send confirmation to UE MAC
    GetNrSlUeMac()->CschedNrSlLcConfigCnf(params.lcGroup, params.lcId);
}

std::shared_ptr<NrSlUeMacSchedulerDstInfo>
NrSlUeMacSchedulerNs3::CreateDstInfo(
    const NrSlUeCmacSapProvider::SidelinkLogicalChannelInfo& params)
{
    std::shared_ptr<NrSlUeMacSchedulerDstInfo> dstInfo = nullptr;
    auto itDst = m_dstMap.find(params.dstL2Id);
    if (itDst == m_dstMap.end())
    {
        NS_LOG_INFO("Creating destination info. Destination L2 id " << params.dstL2Id);

        dstInfo = std::make_shared<NrSlUeMacSchedulerDstInfo>(params.dstL2Id);
        dstInfo->SetDstMcs(m_initialNrSlMcs);

        itDst = m_dstMap.insert(std::make_pair(params.dstL2Id, dstInfo)).first;
    }
    else
    {
        NS_LOG_LOGIC("Doing nothing. You are seeing this because we are adding new LC "
                     << +params.lcId << " for Dst " << params.dstL2Id);
        dstInfo = itDst->second;
    }

    return dstInfo;
}

NrSlLCGPtr
NrSlUeMacSchedulerNs3::CreateLCG(uint8_t lcGroup) const
{
    NS_LOG_FUNCTION(this);
    return std::make_unique<NrSlUeMacSchedulerLCG>(lcGroup);
}

NrSlLCPtr
NrSlUeMacSchedulerNs3::CreateLC(
    const NrSlUeCmacSapProvider::SidelinkLogicalChannelInfo& params) const
{
    NS_LOG_FUNCTION(this);
    return std::make_unique<NrSlUeMacSchedulerLC>(params);
}

void
NrSlUeMacSchedulerNs3::DoSchedNrSlRlcBufferReq(
    const struct NrSlMacSapProvider::NrSlReportBufferStatusParameters& params)
{
    NS_LOG_FUNCTION(this << params.dstL2Id << static_cast<uint32_t>(params.lcid));

    GetSecond DstInfoOf;
    auto itDst = m_dstMap.find(params.dstL2Id);
    NS_ABORT_MSG_IF(itDst == m_dstMap.end(), "Destination " << params.dstL2Id << " info not found");

    for (const auto& lcg : DstInfoOf(*itDst)->GetNrSlLCG())
    {
        if (lcg.second->Contains(params.lcid))
        {
            NS_LOG_INFO(
                "Updating NR SL LC Info: "
                << "RNTI: " << params.rnti << " LCId: " << +params.lcid << " RLCTxQueueSize: "
                << params.txQueueSize << " B, RLCTXHolDel: " << params.txQueueHolDelay
                << " ms, RLCReTXQueueSize: " << params.retxQueueSize << " B, RLCReTXHolDel: "
                << params.retxQueueHolDelay << " ms, RLCStatusPduSize: " << params.statusPduSize
                << " B, source layer 2 id: " << params.srcL2Id << ", destination layer 2 id "
                << params.dstL2Id << " in LCG: " << +lcg.first);
            lcg.second->UpdateInfo(params);
            return;
        }
    }
    NS_FATAL_ERROR("The LC does not exist. Can't update");
}

void
NrSlUeMacSchedulerNs3::DoSchedNrSlTriggerReq(uint32_t dstL2Id,
                                             const std::list<NrSlSlotInfo>& params)
{
    NS_LOG_FUNCTION(this << dstL2Id);

    const auto itDst = m_dstMap.find(dstL2Id);
    NS_ABORT_MSG_IF(itDst == m_dstMap.end(), "Destination " << dstL2Id << "info not found");

    std::set<NrSlSlotAlloc> allocList;

    bool allocated = DoNrSlAllocation(params, itDst->second, allocList);

    if (!allocated)
    {
        return;
    }
    GetNrSlUeMac()->SchedNrSlConfigInd(allocList);
}

void
NrSlUeMacSchedulerNs3::InstallNrSlAmc(const Ptr<NrAmc>& nrSlAmc)
{
    NS_LOG_FUNCTION(this);
    m_nrSlAmc = nrSlAmc;
    // In NR it does not have any impact
    m_nrSlAmc->SetUlMode();
}

Ptr<const NrAmc>
NrSlUeMacSchedulerNs3::GetNrSlAmc() const
{
    NS_LOG_FUNCTION(this);
    return m_nrSlAmc;
}

void
NrSlUeMacSchedulerNs3::UseFixedNrSlMcs(bool fixMcs)
{
    NS_LOG_FUNCTION(this);
    m_fixedNrSlMcs = fixMcs;
}

bool
NrSlUeMacSchedulerNs3::IsNrSlMcsFixed() const
{
    NS_LOG_FUNCTION(this);
    return m_fixedNrSlMcs;
}

void
NrSlUeMacSchedulerNs3::SetInitialNrSlMcs(uint8_t mcs)
{
    NS_LOG_FUNCTION(this);
    m_initialNrSlMcs = mcs;
}

uint8_t
NrSlUeMacSchedulerNs3::GetInitialNrSlMcs() const
{
    NS_LOG_FUNCTION(this);
    return m_initialNrSlMcs;
}

uint8_t
NrSlUeMacSchedulerNs3::GetRv(uint8_t txNumTb) const
{
    NS_LOG_FUNCTION(this << +txNumTb);
    uint8_t modulo = txNumTb % 4;
    // we assume rvid = 0, so RV would take 0, 2, 3, 1
    // see TS 38.21 table 6.1.2.1-2
    uint8_t rv = 0;
    switch (modulo)
    {
    case 0:
        rv = 0;
        break;
    case 1:
        rv = 2;
        break;
    case 2:
        rv = 3;
        break;
    case 3:
        rv = 1;
        break;
    default:
        NS_ABORT_MSG("Wrong modulo result to deduce RV");
    }

    return rv;
}

int64_t
NrSlUeMacSchedulerNs3::AssignStreams(int64_t stream)
{
    NS_LOG_FUNCTION(this << stream);
    m_uniformVariable->SetStream(stream);
    return 1;
}

} // namespace ns3