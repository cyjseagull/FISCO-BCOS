#pragma once
#include "SchedulerFactory.h"
#include "SchedulerImpl.h"


namespace bcos::scheduler
{
class SchedulerServer : public SchedulerInterface
{
public:
    SchedulerServer(int64_t schedulerSeq, SchedulerFactory::Ptr factory)
      : m_factory(factory), m_schedulerTerm(schedulerSeq)
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
        m_scheduler->commitBlock(header, std::move(callback));
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

    void asyncSwitchTerm(int64_t schedulerSeq, std::function<void(Error::Ptr&&)> callback)
    {
        // Will update scheduler session, clear all scheduler & executor block pipeline cache and
        // re-dispatch executor

        switchTerm(schedulerSeq);
        callback(nullptr);
    }


    void initSchedulerIfNotExist()
    {
        if (!m_scheduler)
        {
            static bcos::SharedMutex mutex;
            bcos::WriteGuard lock(mutex);
            updateScheduler(m_schedulerTerm.getSchedulerTermID());
        }

        testTriggerSwitch();  // Just a test code, TODO: remove me
    }

    void registerOnSwitchTermHandler(std::function<void(bcos::protocol::BlockNumber)> onSwitchTerm)
    {
        // onSwitchTerm(latest Uncommitted blockNumber)
        m_onSwitchTermHandlers.push_back(std::move(onSwitchTerm));
    }

    void testTriggerSwitch()
    {
        if (utcTime() - m_scheduler->getSchedulerTermId() > 20000)
        {
            static bcos::SharedMutex mutex;
            bcos::WriteGuard l(mutex);
            if (utcTime() - m_scheduler->getSchedulerTermId() > 20000)
            {
                selfSwitchTerm();
            }
        }
    }

    SchedulerFactory::Ptr getFactory() { return m_factory; }

    class SchedulerTerm
    {
    public:
        SchedulerTerm(int64_t schedulerSeq) : m_schedulerSeq(schedulerSeq), m_executorSeq(utcTime())
        {}

        SchedulerTerm next() { return SchedulerTerm(m_schedulerSeq); }
        int64_t getSchedulerTermID()
        {
            int64_t id = m_schedulerSeq * 10e14 + m_executorSeq;
            if (id <= 0)
            {
                BCOS_LOG(FATAL) << "SchedulerTermID overflow!"
                                << LOG_KV("m_schedulerSeq", m_schedulerSeq)
                                << LOG_KV("m_executorSeq", m_executorSeq)
                                << LOG_KV("SchedulerTermID", id);
            }
            return id;
        }


    private:
        int64_t m_schedulerSeq;
        int64_t m_executorSeq;
    };

private:
    SchedulerImpl::Ptr m_scheduler;
    SchedulerImpl::Ptr m_oldScheduler;
    SchedulerFactory::Ptr m_factory;
    SchedulerTerm m_schedulerTerm;
    std::vector<std::function<void(bcos::protocol::BlockNumber)>> m_onSwitchTermHandlers;

    mutable bcos::SharedMutex x_switchTermMutex;

    void updateScheduler(int64_t schedulerTermId)
    {
        if (m_scheduler)
        {
            m_scheduler->stop();
            m_oldScheduler = m_scheduler;
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("Switch") << " scheduler switch "
                                 << m_oldScheduler->getSchedulerTermId() << "->" << schedulerTermId
                                 << std::endl;
        }

        m_scheduler = m_factory->build(schedulerTermId);
    }

    void switchTerm(int64_t schedulerSeq)
    {
        {
            bcos::WriteGuard l(x_switchTermMutex);
            m_schedulerTerm = SchedulerTerm(schedulerSeq);
            updateScheduler(m_schedulerTerm.getSchedulerTermID());
        }
        onSwitchTermNotify();
    }

    void selfSwitchTerm()
    {
        {
            bcos::WriteGuard l(x_switchTermMutex);
            m_schedulerTerm = m_schedulerTerm.next();
            updateScheduler(m_schedulerTerm.getSchedulerTermID());
        }
        onSwitchTermNotify();
    }

    void onSwitchTermNotify()
    {
        m_factory->getLedger()->asyncGetBlockNumber(
            [this](Error::Ptr error, protocol::BlockNumber blockNumber) {
                if (error)
                {
                    SCHEDULER_LOG(ERROR)
                        << "Could not get blockNumber from ledger on scheduler switch term";
                    return;
                }

                for (auto& onSwitchTerm : m_onSwitchTermHandlers)
                {
                    onSwitchTerm(blockNumber);
                }
            });
    }
};

}  // namespace bcos::scheduler
