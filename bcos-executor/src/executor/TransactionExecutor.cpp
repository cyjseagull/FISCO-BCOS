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
 * @brief TransactionExecutor
 * @file TransactionExecutor.cpp
 * @author: xingqiangbai
 * @date: 2021-09-01
 */

#include "TransactionExecutor.h"
#include "../Common.h"
#include "../dag/Abi.h"
#include "../dag/ClockCache.h"
#include "../dag/CriticalFields.h"
#include "../dag/ScaleUtils.h"
#include "../dag/TxDAG.h"
#include "../dag/TxDAG2.h"
#include "../executive/BlockContext.h"
#include "../executive/TransactionExecutive.h"
#include "../precompiled/Common.h"
#include "../precompiled/ConsensusPrecompiled.h"
#include "../precompiled/CryptoPrecompiled.h"
#include "../precompiled/FileSystemPrecompiled.h"
#include "../precompiled/KVTableFactoryPrecompiled.h"
#include "../precompiled/ParallelConfigPrecompiled.h"
#include "../precompiled/PrecompiledResult.h"
#include "../precompiled/SystemConfigPrecompiled.h"
#include "../precompiled/TableFactoryPrecompiled.h"
#include "../precompiled/Utilities.h"
#include "../precompiled/extension/ContractAuthPrecompiled.h"
#include "../precompiled/extension/DagTransferPrecompiled.h"
#include "../vm/Precompiled.h"
#include "../vm/gas_meter/GasInjector.h"
#include "bcos-codec/abi/ContractABIType.h"
#include "bcos-framework/interfaces/dispatcher/SchedulerInterface.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/executor/PrecompiledTypeDef.h"
#include "bcos-framework/interfaces/ledger/LedgerTypeDef.h"
#include "bcos-framework/interfaces/protocol/ProtocolTypeDef.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-protocol/LogEntry.h"
#include "bcos-table/src/StateStorage.h"
#include "tbb/flow_graph.h"
#include <bcos-utilities/Error.h>
#include <bcos-utilities/ThreadPool.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/format.hpp>
#include <boost/format/format_fwd.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/latch.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <cassert>
#include <exception>
#include <functional>
#include <gsl/gsl_util>
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

using namespace bcos;
using namespace std;
using namespace bcos::executor;
using namespace bcos::executor::critical;
using namespace bcos::wasm;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace tbb::flow;

crypto::Hash::Ptr GlobalHashImpl::g_hashImpl;

TransactionExecutor::TransactionExecutor(txpool::TxPoolInterface::Ptr txpool,
    storage::MergeableStorageInterface::Ptr cachedStorage,
    storage::TransactionalStorageInterface::Ptr backendStorage,
    protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
    bcos::crypto::Hash::Ptr hashImpl, bool isAuthCheck)
  : m_txpool(std::move(txpool)),
    m_cachedStorage(std::move(cachedStorage)),
    m_backendStorage(std::move(backendStorage)),
    m_executionMessageFactory(std::move(executionMessageFactory)),
    m_hashImpl(std::move(hashImpl)),
    m_isAuthCheck(isAuthCheck),
    m_isWasm(false)
{
    assert(m_backendStorage);

    GlobalHashImpl::g_hashImpl = m_hashImpl;
    m_abiCache = make_shared<ClockCache<bcos::bytes, FunctionAbi>>(32);
    m_gasInjector = std::make_shared<wasm::GasInjector>(wasm::GetInstructionTable());
}

void TransactionExecutor::nextBlockHeader(const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
    std::function<void(bcos::Error::UniquePtr)> callback)
{
    try
    {
        EXECUTOR_LOG(INFO) << "NextBlockHeader request: "
                           << LOG_KV("number", blockHeader->number());

        {
            std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
            bcos::storage::StateStorage::Ptr stateStorage;
            bcos::storage::StorageInterface::Ptr lastStateStorage;
            if (m_stateStorages.empty())
            {
                if (m_cachedStorage)
                {
                    stateStorage = std::make_shared<bcos::storage::StateStorage>(m_cachedStorage);
                }
                else
                {
                    stateStorage = std::make_shared<bcos::storage::StateStorage>(m_backendStorage);
                }
                lastStateStorage = m_lastStateStorage;
            }
            else
            {
                auto& prev = m_stateStorages.back();

                if (blockHeader->number() - prev.number != 1)
                {
                    auto fmt =
                        boost::format("Block number mismatch! request: %d - 1, current: %d") %
                        blockHeader->number() % prev.number;
                    EXECUTOR_LOG(ERROR) << fmt;
                    callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR, fmt.str()));
                    return;
                }

                prev.storage->setReadOnly(true);
                lastStateStorage = prev.storage;
                stateStorage = std::make_shared<bcos::storage::StateStorage>(prev.storage);
            }
            // set last commit state storage to blockContext, to auth read last block state
            m_blockContext = createBlockContext(blockHeader, stateStorage, lastStateStorage);
            m_stateStorages.emplace_back(blockHeader->number(), stateStorage);
        }

        EXECUTOR_LOG(INFO) << "NextBlockHeader success";
        callback(nullptr);
    }
    catch (std::exception& e)
    {
        EXECUTOR_LOG(ERROR) << "NextBlockHeader error: " << boost::diagnostic_information(e);

        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "nextBlockHeader unknown error", e));
    }
}

void TransactionExecutor::call(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    EXECUTOR_LOG(TRACE) << "Call request" << LOG_KV("ContextID", input->contextID())
                        << LOG_KV("seq", input->seq()) << LOG_KV("Message type", input->type())
                        << LOG_KV("To", input->to()) << LOG_KV("Create", input->create());

    BlockContext::Ptr blockContext;
    switch (input->type())
    {
    case protocol::ExecutionMessage::MESSAGE:
    {
        bcos::protocol::BlockNumber number = m_lastCommittedBlockNumber;
        storage::StorageInterface::Ptr prev;

        if (m_cachedStorage)
        {
            prev = m_cachedStorage;
        }
        else
        {
            prev = m_backendStorage;
        }

        // Create a temp storage
        auto storage = std::make_shared<storage::StateStorage>(std::move(prev));

        // Create a temp block context
        blockContext = createBlockContext(
            number, h256(), 0, 0, std::move(storage));  // TODO: complete the block info
        auto inserted = m_calledContext.emplace(
            std::tuple{input->contextID(), input->seq()}, CallState{blockContext});

        if (!inserted)
        {
            auto message =
                "Call error, contextID: " + boost::lexical_cast<std::string>(input->contextID()) +
                " seq: " + boost::lexical_cast<std::string>(input->seq()) + " exists";
            EXECUTOR_LOG(ERROR) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
            return;
        }

        break;
    }
    case protocol::ExecutionMessage::FINISHED:
    case protocol::ExecutionMessage::REVERT:
    {
        decltype(m_calledContext)::accessor it;
        m_calledContext.find(it, std::tuple{input->contextID(), input->seq()});

        if (it.empty())
        {
            auto message =
                "Call error, contextID: " + boost::lexical_cast<std::string>(input->contextID()) +
                " seq: " + boost::lexical_cast<std::string>(input->seq()) + " does not exists";
            EXECUTOR_LOG(ERROR) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
            return;
        }

        blockContext = it->second.blockContext;

        break;
    }
    default:
    {
        auto message =
            "Call error, Unknown call type: " + boost::lexical_cast<std::string>(input->type());
        EXECUTOR_LOG(ERROR) << message;
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
        return;

        break;
    }
    }

    asyncExecute(std::move(blockContext), std::move(input), true,
        [this, callback = std::move(callback)](
            Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::string errorMessage = "Call failed: " + boost::diagnostic_information(*error);
                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, errorMessage, *error), nullptr);
                return;
            }

            if (result->type() == protocol::ExecutionMessage::FINISHED ||
                result->type() == protocol::ExecutionMessage::REVERT)
            {
                auto erased = m_calledContext.erase(std::tuple{result->contextID(), result->seq()});

                if (!erased)
                {
                    auto message = "Call error, erase contextID: " +
                                   boost::lexical_cast<std::string>(result->contextID()) +
                                   " seq: " + boost::lexical_cast<std::string>(result->seq()) +
                                   " does not exists";
                    EXECUTOR_LOG(ERROR) << message;

                    callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
                    return;
                }
            }

            EXECUTOR_LOG(TRACE) << "Call success";
            callback(std::move(error), std::move(result));
        });
}

void TransactionExecutor::executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    EXECUTOR_LOG(TRACE) << "ExecuteTransaction request" << LOG_KV("ContextID", input->contextID())
                        << LOG_KV("seq", input->seq()) << LOG_KV("message type", input->type())
                        << LOG_KV("to", input->to()) << LOG_KV("create", input->create());

    if (!m_blockContext)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(
                     ExecuteError::EXECUTE_ERROR, "Execute failed with empty blockContext!"),
            nullptr);
        return;
    }

    asyncExecute(m_blockContext, std::move(input), false,
        [callback = std::move(callback)](
            Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::string errorMessage =
                    "ExecuteTransaction failed: " + boost::diagnostic_information(*error);
                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, errorMessage, *error), nullptr);
                return;
            }

            callback(std::move(error), std::move(result));
        });
}

void TransactionExecutor::getHash(bcos::protocol::BlockNumber number,
    std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback)
{
    EXECUTOR_LOG(INFO) << "GetTableHashes" << LOG_KV("number", number);

    if (m_stateStorages.empty())
    {
        EXECUTOR_LOG(ERROR) << "GetTableHashes error: No uncommitted state";
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::GETHASH_ERROR, "No uncommitted state"),
            crypto::HashType());
        return;
    }

    auto& last = m_stateStorages.back();
    if (last.number != number)
    {
        auto errorMessage =
            "GetTableHashes error: Request block number: " +
            boost::lexical_cast<std::string>(number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(last.number);

        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::GETHASH_ERROR, errorMessage), crypto::HashType());

        return;
    }

    auto hash = last.storage->hash(m_hashImpl);
    EXECUTOR_LOG(INFO) << "GetTableHashes success" << LOG_KV("hash", hash.hex());

    callback(nullptr, std::move(hash));
}

void TransactionExecutor::prepare(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    EXECUTOR_LOG(INFO) << "Prepare request" << LOG_KV("params", params.number);

    auto first = m_stateStorages.begin();
    if (first == m_stateStorages.end())
    {
        auto errorMessage = "Prepare error: empty stateStorages";
        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    if (first->number != params.number)
    {
        auto errorMessage =
            "Prepare error: Request block number: " +
            boost::lexical_cast<std::string>(params.number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(first->number);

        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(ExecuteError::PREPARE_ERROR, errorMessage));

        return;
    }

    bcos::storage::TransactionalStorageInterface::TwoPCParams storageParams;  // TODO: add tikv
                                                                              // params
    storageParams.number = params.number;

    m_backendStorage->asyncPrepare(
        storageParams, *(first->storage), [callback = std::move(callback)](auto&& error, uint64_t) {
            if (error)
            {
                auto errorMessage = "Prepare error: " + boost::diagnostic_information(*error);

                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(
                    BCOS_ERROR_WITH_PREV_PTR(ExecuteError::PREPARE_ERROR, errorMessage, *error));
                return;
            }

            EXECUTOR_LOG(INFO) << "Prepare success";
            callback(nullptr);
        });
}

void TransactionExecutor::commit(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    EXECUTOR_LOG(TRACE) << "Commit request" << LOG_KV("number", params.number);

    auto first = m_stateStorages.begin();
    if (first == m_stateStorages.end())
    {
        auto errorMessage = "Commit error: empty stateStorages";
        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(INVALID_BLOCKNUMBER, errorMessage));

        return;
    }

    if (first->number != params.number)
    {
        auto errorMessage =
            "Commit error: Request block number: " +
            boost::lexical_cast<std::string>(params.number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(first->number);

        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(INVALID_BLOCKNUMBER, errorMessage));

        return;
    }

    bcos::storage::TransactionalStorageInterface::TwoPCParams storageParams;  // Add tikv params
    storageParams.number = params.number;
    m_backendStorage->asyncCommit(storageParams,
        [this, callback = std::move(callback), blockNumber = params.number](Error::Ptr&& error) {
            if (error)
            {
                auto errorMessage = "Commit error: " + boost::diagnostic_information(*error);

                EXECUTOR_LOG(ERROR) << errorMessage;
                callback(
                    BCOS_ERROR_WITH_PREV_PTR(ExecuteError::COMMIT_ERROR, errorMessage, *error));
                return;
            }

            EXECUTOR_LOG(DEBUG) << "Commit success" << LOG_KV("number", blockNumber);

            m_lastCommittedBlockNumber = blockNumber;

            removeCommittedState();

            callback(nullptr);
        });
}

void TransactionExecutor::rollback(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    EXECUTOR_LOG(INFO) << "Rollback request: " << LOG_KV("number", params.number);

    auto first = m_stateStorages.begin();
    if (first == m_stateStorages.end())
    {
        auto errorMessage = "Rollback error: empty stateStorages";
        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    if (first->number != params.number)
    {
        auto errorMessage =
            "Rollback error: Request block number: " +
            boost::lexical_cast<std::string>(params.number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(first->number);

        EXECUTOR_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(ExecuteError::ROLLBACK_ERROR, errorMessage));

        return;
    }

    bcos::storage::TransactionalStorageInterface::TwoPCParams storageParams;
    storageParams.number = params.number;
    m_backendStorage->asyncRollback(storageParams, [callback = std::move(callback)](auto&& error) {
        if (error)
        {
            auto errorMessage = "Rollback error: " + boost::diagnostic_information(*error);

            EXECUTOR_LOG(ERROR) << errorMessage;
            callback(BCOS_ERROR_WITH_PREV_PTR(-1, errorMessage, *error));
            return;
        }

        EXECUTOR_LOG(INFO) << "Rollback success";
        callback(nullptr);
    });
}

void TransactionExecutor::reset(std::function<void(bcos::Error::Ptr)> callback)
{
    m_stateStorages.clear();

    callback(nullptr);
}

void TransactionExecutor::getCode(
    std::string_view contract, std::function<void(bcos::Error::Ptr, bcos::bytes)> callback)
{
    EXECUTOR_LOG(INFO) << "Get code request" << LOG_KV("Contract", contract);

    storage::StorageInterface::Ptr storage;

    if (m_cachedStorage)
    {
        storage = m_cachedStorage;
    }
    else
    {
        storage = m_backendStorage;
    }

    auto tableName = getContractTableName(contract, m_isWasm);
    storage->asyncGetRow(tableName, "code",
        [callback = std::move(callback)](Error::UniquePtr error, std::optional<Entry> entry) {
            if (error)
            {
                EXECUTOR_LOG(ERROR) << "Get code error: " << boost::diagnostic_information(*error);

                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "Get code error", *error), {});
                return;
            }

            if (!entry)
            {
                EXECUTOR_LOG(WARNING) << "Get code success, empty code";

                callback(nullptr, bcos::bytes());
                return;
            }

            auto code = entry->getField(0);
            EXECUTOR_LOG(INFO) << "Get code success" << LOG_KV("code size", code.size());

            auto codeBytes = bcos::bytes(code.begin(), code.end());
            callback(nullptr, std::move(codeBytes));
        });
}

void TransactionExecutor::asyncExecute(std::shared_ptr<BlockContext> blockContext,
    bcos::protocol::ExecutionMessage::UniquePtr input, bool staticCall,
    std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>
        callback)
{
    EXECUTOR_LOG(TRACE) << "Import key locks size: " << input->keyLocks().size();

    switch (input->type())
    {
    case bcos::protocol::ExecutionMessage::TXHASH:
    {
        // Get transaction first
        auto txHashes = std::make_shared<bcos::crypto::HashList>(1);
        (*txHashes)[0] = (input->transactionHash());

        m_txpool->asyncFillBlock(std::move(txHashes),
            [this, inputPtr = input.release(), blockContext = std::move(blockContext), callback](
                Error::Ptr error, bcos::protocol::TransactionsPtr transactions) mutable {
                auto input = std::unique_ptr<bcos::protocol::ExecutionMessage>(inputPtr);

                if (error)
                {
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                                 "Transaction does not exists: " + input->transactionHash().hex(),
                                 *error),
                        nullptr);
                    return;
                }

                if (!transactions || transactions->empty())
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                                 "Transaction does not exists: " + input->transactionHash().hex()),
                        nullptr);
                    return;
                }

                auto tx = (*transactions)[0];
                if (!tx)
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                                 "Transaction is null: " + input->transactionHash().hex()),
                        nullptr);
                    return;
                }

                auto contextID = input->contextID();
                auto seq = input->seq();
                auto callParameters = createCallParameters(*input, *tx);

                auto executive =
                    createExecutive(blockContext, callParameters->codeAddress, contextID, seq);
                blockContext->insertExecutive(contextID, seq, {executive});

                try
                {
                    auto output = executive->start(std::move(callParameters));

                    auto message = toExecutionResult(*executive, std::move(output));
                    callback(nullptr, std::move(message));
                    return;
                }
                catch (std::exception& e)
                {
                    EXECUTOR_LOG(ERROR) << "Execute error: " << boost::diagnostic_information(e);
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "Execute error", e), nullptr);
                }
            });
        break;
    }
    case bcos::protocol::ExecutionMessage::MESSAGE:
    case bcos::protocol::ExecutionMessage::REVERT:
    case bcos::protocol::ExecutionMessage::FINISHED:
    {
        auto contextID = input->contextID();
        auto seq = input->seq();
        auto callParameters = createCallParameters(*input, staticCall);

        auto it = blockContext->getExecutive(contextID, seq);
        if (it)
        {
            // REVERT or FINISHED
            auto& [executive] = *it;

            // Call callback
            EXECUTOR_LOG(TRACE) << "Entering responseFunc";
            executive->setExchangeMessage(std::move(callParameters));
            auto output = executive->resume();
            auto message = toExecutionResult(*executive, std::move(output));

            callback(nullptr, std::move(message));
            return;

            EXECUTOR_LOG(TRACE) << "Exiting responseFunc";
        }
        else
        {
            // new external call MESSAGE
            auto executive =
                createExecutive(blockContext, callParameters->codeAddress, contextID, seq);
            blockContext->insertExecutive(contextID, seq, {executive});

            try
            {
                auto output = executive->start(std::move(callParameters));

                auto message = toExecutionResult(*executive, std::move(output));
                callback(nullptr, std::move(message));
                return;
            }
            catch (std::exception& e)
            {
                EXECUTOR_LOG(ERROR) << "Execute error: " << boost::diagnostic_information(e);
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "Execute error", e), nullptr);
            }
        }

        break;
    }
    case bcos::protocol::ExecutionMessage::KEY_LOCK:
    {
        auto contextID = input->contextID();
        auto seq = input->seq();
        auto callParameters = createCallParameters(*input, staticCall);

        auto it = blockContext->getExecutive(contextID, seq);
        if (it)
        {
            // REVERT or FINISHED
            auto& [executive] = *it;

            executive->setExchangeMessage(std::move(callParameters));
            auto output = executive->resume();

            auto message = toExecutionResult(*executive, std::move(output));

            callback(nullptr, std::move(message));
            return;
        }
        else
        {
            EXECUTOR_LOG(ERROR) << "KEY LOCK not found executive, contextID: " << contextID
                                << " seq: " << seq;
            callback(
                BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR, "KEY LOCK not found executive"),
                nullptr);
            return;
        }
        break;
    }
    default:
    {
        EXECUTOR_LOG(ERROR) << "Unknown message type: " << input->type();
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                     "Unknown type" + boost::lexical_cast<std::string>(input->type())),
            nullptr);
        return;
    }
    }
}

std::function<void(const TransactionExecutive& executive, std::unique_ptr<CallParameters> input)>
TransactionExecutor::createExternalFunctionCall(
    std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>&
        callback)
{
    return
        [this, &callback](const TransactionExecutive& executive, CallParameters::UniquePtr input) {
            auto message = toExecutionResult(executive, std::move(input));
            callback(nullptr, std::move(message));
        };
}

std::unique_ptr<ExecutionMessage> TransactionExecutor::toExecutionResult(
    const TransactionExecutive& executive, std::unique_ptr<CallParameters> params)
{
    auto message = toExecutionResult(std::move(params));

    message->setContextID(executive.contextID());
    message->setSeq(executive.seq());

    return message;
}

std::unique_ptr<protocol::ExecutionMessage> TransactionExecutor::toExecutionResult(
    std::unique_ptr<CallParameters> params)
{
    auto message = m_executionMessageFactory->createExecutionMessage();
    switch (params->type)
    {
    case CallParameters::MESSAGE:
        message->setFrom(std::move(params->senderAddress));
        message->setTo(std::move(params->receiveAddress));
        message->setType(ExecutionMessage::MESSAGE);
        message->setKeyLocks(std::move(params->keyLocks));
        break;
    case CallParameters::KEY_LOCK:
        message->setFrom(params->senderAddress);
        message->setTo(std::move(params->senderAddress));
        message->setType(ExecutionMessage::KEY_LOCK);
        message->setKeyLockAcquired(std::move(params->acquireKeyLock));
        message->setKeyLocks(std::move(params->keyLocks));

        break;
    case CallParameters::FINISHED:
        // Response message, Swap the from and to
        message->setFrom(std::move(params->receiveAddress));
        message->setTo(std::move(params->senderAddress));
        message->setType(ExecutionMessage::FINISHED);
        break;
    case CallParameters::REVERT:
        // Response message, Swap the from and to
        message->setFrom(std::move(params->receiveAddress));
        message->setTo(std::move(params->senderAddress));
        message->setType(ExecutionMessage::REVERT);
        break;
    }

    message->setContextID(params->contextID);
    message->setSeq(params->seq);
    message->setOrigin(std::move(params->origin));
    message->setGasAvailable(params->gas);
    message->setData(std::move(params->data));
    message->setStaticCall(params->staticCall);
    message->setCreate(params->create);
    if (params->createSalt)
    {
        message->setCreateSalt(*params->createSalt);
    }

    message->setStatus(params->status);
    message->setMessage(std::move(params->message));
    message->setLogEntries(std::move(params->logEntries));
    message->setNewEVMContractAddress(std::move(params->newEVMContractAddress));

    return message;
}


TransactionExecutive::Ptr TransactionExecutor::createExecutive(
    const std::shared_ptr<BlockContext>& _blockContext, const std::string& _contractAddress,
    int64_t contextID, int64_t seq)
{
    auto executive = std::make_shared<TransactionExecutive>(
        _blockContext, _contractAddress, contextID, seq, m_gasInjector);
    executive->setConstantPrecompiled(m_constantPrecompiled);
    executive->setEVMPrecompiled(m_precompiledContract);
    executive->setBuiltInPrecompiled(m_builtInPrecompiled);

    // TODO: register User developed Precompiled contract
    // registerUserPrecompiled(context);
    return executive;
}

void TransactionExecutor::removeCommittedState()
{
    if (m_stateStorages.empty())
    {
        EXECUTOR_LOG(ERROR) << "Remove committed state failed, empty states";
        return;
    }

    bcos::protocol::BlockNumber number;
    bcos::storage::StateStorage::Ptr storage;

    {
        std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
        auto it = m_stateStorages.begin();
        number = it->number;
        storage = it->storage;
    }

    if (m_cachedStorage)
    {
        EXECUTOR_LOG(INFO) << "Merge state number: " << number << " to cachedStorage start";
        m_cachedStorage->merge(true, *storage);
        EXECUTOR_LOG(INFO) << "Merge state number: " << number << " to cachedStorage end";

        std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
        auto it = m_stateStorages.begin();
        m_lastStateStorage = m_stateStorages.back().storage;
        EXECUTOR_LOG(DEBUG) << "LatestStateStorage"
                            << LOG_KV("storageNumber", m_stateStorages.back().number)
                            << LOG_KV("commitNumber", number);
        it = m_stateStorages.erase(it);
        if (it != m_stateStorages.end())
        {
            EXECUTOR_LOG(INFO) << "Set state number: " << it->number << " prev to cachedStorage";
            it->storage->setPrev(m_cachedStorage);
        }
    }
    else if (m_backendStorage)
    {
        std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
        auto it = m_stateStorages.begin();
        m_lastStateStorage = m_stateStorages.back().storage;
        EXECUTOR_LOG(DEBUG) << LOG_DESC("removeCommittedState")
                            << LOG_KV("LatestStateStorage", m_stateStorages.back().number)
                            << LOG_KV("commitNumber", number) << LOG_KV("erasedStorage", it->number)
                            << LOG_KV("stateStorageSize", m_stateStorages.size());
        it = m_stateStorages.erase(it);
        if (it != m_stateStorages.end())
        {
            it->storage->setPrev(m_backendStorage);
        }
    }
}

std::unique_ptr<CallParameters> TransactionExecutor::createCallParameters(
    bcos::protocol::ExecutionMessage& input, bool staticCall)
{
    auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);

    switch (input.type())
    {
    case ExecutionMessage::MESSAGE:
    {
        break;
    }
    case ExecutionMessage::REVERT:
    {
        callParameters->type = CallParameters::REVERT;
        break;
    }
    case ExecutionMessage::FINISHED:
    {
        callParameters->type = CallParameters::FINISHED;
        break;
    }
    case ExecutionMessage::KEY_LOCK:
    {
        break;
    }
    case ExecutionMessage::SEND_BACK:
    case ExecutionMessage::REVERT_KEY_LOCK:
    case ExecutionMessage::TXHASH:
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(
            ExecuteError::EXECUTE_ERROR, "Unexpected execution message type: " +
                                             boost::lexical_cast<std::string>(input.type())));
    }
    }

    callParameters->contextID = input.contextID();
    callParameters->seq = input.seq();
    callParameters->origin = input.origin();
    callParameters->senderAddress = input.from();
    callParameters->receiveAddress = input.to();
    callParameters->codeAddress = input.to();
    callParameters->create = input.create();
    callParameters->data = input.takeData();
    callParameters->gas = input.gasAvailable();
    callParameters->staticCall = staticCall;
    callParameters->newEVMContractAddress = input.newEVMContractAddress();
    callParameters->status = input.status();
    callParameters->keyLocks = input.takeKeyLocks();

    return callParameters;
}

std::unique_ptr<CallParameters> TransactionExecutor::createCallParameters(
    bcos::protocol::ExecutionMessage& input, const bcos::protocol::Transaction& tx)
{
    auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);

    callParameters->contextID = input.contextID();
    callParameters->seq = input.seq();
    callParameters->origin = toHex(tx.sender());
    callParameters->senderAddress = callParameters->origin;
    callParameters->receiveAddress = input.to();
    callParameters->codeAddress = input.to();
    callParameters->gas = input.gasAvailable();
    callParameters->staticCall = input.staticCall();
    callParameters->create = input.create();
    callParameters->data = tx.input().toBytes();
    callParameters->keyLocks = input.takeKeyLocks();
    callParameters->abi = tx.abi();
    return callParameters;
}

void TransactionExecutor::executeTransactionsWithCriticals(
    critical::CriticalFieldsInterface::Ptr criticals,
    gsl::span<std::unique_ptr<CallParameters>> inputs,
    vector<protocol::ExecutionMessage::UniquePtr>& executionResults)
{
    // DAG run
    shared_ptr<TxDAGInterface> txDag = make_shared<TxDAG2>();
    txDag->init(criticals, [this, &inputs, &executionResults](ID id) {
        auto& input = inputs[id];
        auto executive =
            createExecutive(m_blockContext, input->codeAddress, input->contextID, input->seq);

        EXECUTOR_LOG(TRACE) << LOG_BADGE("executeTransactionsWithCriticals")
                            << LOG_DESC("Start transaction") << LOG_KV("to", input->receiveAddress)
                            << LOG_KV("data", toHexStringWithPrefix(input->data));
        try
        {
            auto output = executive->start(std::move(input));

            executionResults[id] = toExecutionResult(*executive, std::move(output));
        }
        catch (std::exception& e)
        {
            EXECUTOR_LOG(ERROR) << "Execute error: " << boost::diagnostic_information(e);
        }
    });

    txDag->run(m_DAGThreadNum);
}
