/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file RouterTableImpl.h
 * @author: yujiechen
 * @date 2022-5-24
 */
#pragma once
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "RouterTableInterface.h"
#include <bcos-tars-protocol/tars/RouterTable.h>
#include <memory>

namespace bcos
{
namespace gateway
{
class RouterTableEntry : public RouterTableEntryInterface
{
public:
    using Ptr = std::shared_ptr<RouterTableEntry>;
    RouterTableEntry()
      : m_inner([m_entry = bcostars::RouterTableEntry()]() mutable { return &m_entry; })
    {}
    RouterTableEntry(std::function<bcostars::RouterTableEntry*()> _inner)
      : m_inner(std::move(_inner))
    {}
    RouterTableEntry(RouterTableEntry&&) = delete;
    RouterTableEntry(const RouterTableEntry&) = delete;
    RouterTableEntry& operator=(const RouterTableEntry&) = delete;
    RouterTableEntry& operator=(RouterTableEntry&&) = delete;
    ~RouterTableEntry() override = default;

    void setDstNode(RouterNodeID const& _dstNode) override
    {
        m_dstNode = _dstNode;
        // for compatibility
        m_inner()->dstNode = _dstNode.rawP2pID;
    }
    void setNextHop(RouterNodeID const& _nextHop) override
    {
        m_nextHop = _nextHop;
        // for compatibility
        m_inner()->nextHop = _nextHop.rawP2pID;
    }
    void clearNextHop() override
    {
        m_nextHop.reset();
        m_inner()->nextHop = std::string();
    }
    void setDistance(int32_t _distance) override { m_inner()->distance = _distance; }
    void incDistance(int32_t _deltaDistance) override { m_inner()->distance += _deltaDistance; }

    RouterNodeID const& dstNode() const override { return m_dstNode; }
    RouterNodeID const& nextHop() const override { return m_nextHop; }
    int32_t distance() const override { return m_inner()->distance; }

    bcostars::RouterTableEntry const& inner() const { return *(m_inner()); }

    // encode dstNodeInfo and nextHopNodeInfo into m_inner before encode
    virtual void prepareToEncode()
    {
        assignNodeIDInfo(m_inner()->dstNodeInfo, m_dstNode);
        assignNodeIDInfo(m_inner()->nextHopInfo, m_nextHop);
    }
    // populate RouterNodeID after decode
    void populateNodeIDInfo()
    {
        auto ret = populateRouterNodeID(m_dstNode, m_inner()->dstNodeInfo);
        // the old node case, use dstNode directly
        if (!ret)
        {
            m_dstNode.p2pID = m_inner()->dstNode;
            m_dstNode.rawP2pID = m_inner()->dstNode;
        }
        ret = populateRouterNodeID(m_nextHop, m_inner()->nextHopInfo);
        // the old node case, use nextHop directly
        if (!ret)
        {
            m_nextHop.p2pID = m_inner()->nextHop;
            m_nextHop.rawP2pID = m_inner()->nextHop;
        }
    }

private:
    void assignNodeIDInfo(bcostars::NodeIDInfo& nodeIDInfo, RouterNodeID const& routerNodeID)
    {
        nodeIDInfo.p2pID = routerNodeID.p2pID;
        nodeIDInfo.rawP2pID = routerNodeID.rawP2pID;
    }

    bool populateRouterNodeID(RouterNodeID& routerNodeID, bcostars::NodeIDInfo const& nodeIDInfo)
    {
        // the nodeInfo not setted, the old node case
        if (nodeIDInfo.p2pID.empty() && nodeIDInfo.rawP2pID.empty())
        {
            return false;
        }
        routerNodeID.p2pID = nodeIDInfo.p2pID;
        routerNodeID.rawP2pID = nodeIDInfo.rawP2pID;
        return true;
    }

private:
    std::function<bcostars::RouterTableEntry*()> m_inner;
    RouterNodeID m_dstNode;
    RouterNodeID m_nextHop;
};

class RouterTable : public RouterTableInterface
{
public:
    using Ptr = std::shared_ptr<RouterTable>;
    RouterTable() : m_inner([m_table = bcostars::RouterTable()]() mutable { return &m_table; }) {}
    RouterTable(bytesConstRef _decodedData) : RouterTable() { decode(_decodedData); }
    RouterTable(RouterTable&&) = delete;
    RouterTable(const RouterTable&) = delete;
    RouterTable& operator=(const RouterTable&) = delete;
    RouterTable& operator=(RouterTable&&) = delete;
    ~RouterTable() override = default;

    void encode(bcos::bytes& _encodedData) override;
    void decode(bcos::bytesConstRef _decodedData) override;

    std::map<RouterNodeID, RouterTableEntryInterface::Ptr> routerEntries() const override
    {
        bcos::ReadGuard l(x_routerEntries);
        return m_routerEntries;
    }

    uint32_t routerEntrySize() const override
    {
        bcos::ReadGuard l(x_routerEntries);
        return m_routerEntries.size();
    }
    // append the unreachableNodes into param _unreachableNodes
    bool update(std::set<std::string>& _unreachableNodes, RouterNodeID const& _generatedFrom,
        RouterTableEntryInterface::Ptr _entry) override;
    // append the unreachableNodes into param _unreachableNodes
    bool erase(std::set<std::string>& _unreachableNodes, std::string const& _p2pNodeID) override;

    void setNodeInfo(RouterNodeID const& _p2pInfo) override { m_selfInfo = _p2pInfo; }
    std::string const& nodeID() const override { return m_selfInfo.p2pID; }

    void setUnreachableDistance(int _unreachableDistance) override
    {
        m_unreachableDistance = _unreachableDistance;
    }

    std::string getNextHop(std::string const& _nodeID) override;
    std::set<std::string> getAllReachableNode() override;

protected:
    bool updateDstNodeEntry(
        RouterNodeID const& _generatedFrom, RouterTableEntryInterface::Ptr _entry);
    void updateDistanceForAllRouterEntries(std::set<std::string>& _unreachableNodes,
        RouterNodeID const& _nextHop, int32_t _newDistance);

private:
    bool nodeSelf(RouterNodeID const& p2pID) { return m_selfInfo == p2pID; }

private:
    RouterNodeID m_selfInfo;
    std::function<bcostars::RouterTable*()> m_inner;
    std::map<RouterNodeID, RouterTableEntryInterface::Ptr> m_routerEntries;
    mutable SharedMutex x_routerEntries;

    int m_unreachableDistance = 10;
};

class RouterTableFactoryImpl : public RouterTableFactory
{
public:
    using Ptr = std::shared_ptr<RouterTableFactoryImpl>;
    RouterTableInterface::Ptr createRouterTable() override
    {
        return std::make_shared<RouterTable>();
    }
    RouterTableInterface::Ptr createRouterTable(bcos::bytesConstRef _decodedData) override
    {
        return std::make_shared<RouterTable>(_decodedData);
    }

    RouterTableEntryInterface::Ptr createRouterEntry() override
    {
        return std::make_shared<RouterTableEntry>();
    }
};

}  // namespace gateway
}  // namespace bcos