#pragma once
#include "SchedulerFactory.h"
#include "SchedulerImpl.h"


namespace bcos::scheduler
{
class SchedulerServer : public SchedulerInterface
{
public:
    SchedulerServer(SchedulerFactory::Ptr factory) : m_factory(factory)
    {
        // Notice: Not to initSchedulerIfNotExist here, because factory need to bind notifier after
        // this constructor
    }

    // by pbft & sync
    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool _sysBlock)>
            callback) override
    {
        initSchedulerIfNotExist();
        m_scheduler->executeBlock(block, verify, std::move(callback));
    };

    // by pbft & sync
    void commitBlock(bcos::protocol::BlockHeader::Ptr header,
        std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> callback)
        override
    {
        initSchedulerIfNotExist();
        ///*
        m_scheduler->commitBlock(header, std::move(callback));
        /*/
        /// this code just for testing, TODO: delete me
        m_scheduler->commitBlock(
            header, [this, callback = std::move(callback)](
                        bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&& config) {
                callback(std::move(error), std::move(config));

                if (utcTime() - m_scheduler->getSchedulerSession() > 10)
                {
                    refresh();
                }
            });
        //*/
    };

    // by console, query committed committing executing
    void status(
        std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)> callback) override
    {
        initSchedulerIfNotExist();
        m_scheduler->status(std::move(callback));
    };

    // by rpc
    void call(protocol::Transaction::Ptr tx,
        std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)> callback) override
    {
        initSchedulerIfNotExist();
        m_scheduler->call(tx, std::move(callback));
    };

    // by executor
    void registerExecutor(std::string name,
        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor,
        std::function<void(Error::Ptr&&)> callback) override
    {
        initSchedulerIfNotExist();
        m_scheduler->registerExecutor(name, executor, std::move(callback));
    };

    void unregisterExecutor(
        const std::string& name, std::function<void(Error::Ptr&&)> callback) override
    {
        initSchedulerIfNotExist();
        m_scheduler->unregisterExecutor(name, std::move(callback));
    };

    // clear all status
    void reset(std::function<void(Error::Ptr&&)> callback) override
    {
        initSchedulerIfNotExist();
        m_scheduler->reset(std::move(callback));
    };
    void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) override
    {
        initSchedulerIfNotExist();
        m_scheduler->getCode(contract, std::move(callback));
    };

    void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) override
    {
        initSchedulerIfNotExist();
        m_scheduler->getABI(contract, std::move(callback));
    };

    void refresh()
    {
        // Will update scheduler session, clear all scheduler & executor block pipeline cache and
        // re-dispatch executor

        if (m_scheduler)
        {
            m_scheduler->stop();
            m_oldScheduler = m_scheduler;
        }

        m_scheduler = m_factory->build();
    }

    void initSchedulerIfNotExist()
    {
        if (!m_scheduler)
        {
            static bcos::SharedMutex mutex;
            bcos::WriteGuard lock(mutex);
            if (m_scheduler)
            {
                return;
            }
            m_scheduler = m_factory->build();
        }
    }

    SchedulerFactory::Ptr getFactory() { return m_factory; }

private:
    SchedulerImpl::Ptr m_scheduler;
    SchedulerImpl::Ptr m_oldScheduler;
    SchedulerFactory::Ptr m_factory;
};

}  // namespace bcos::scheduler
