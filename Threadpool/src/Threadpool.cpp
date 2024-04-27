#include "ThreadPool.h"

ThreadPool::ThreadPool(uint32_t numWorkerThreads)
    : m_Metrics(TpMetrics{
            .TaskQueueSize = 0,
            .NumTasksDone = 0,
            .AvgTaskExecutionTimeMs = Milliseconds(0),
            .AvgThreadStalledTimeMs = Milliseconds(0),
        })
{
    m_WorkerThreads.resize(numWorkerThreads);
    for (uint32_t threadID = 0; threadID < numWorkerThreads; ++threadID)
    {
        m_WorkerThreads[threadID] = std::thread(&ThreadPool::Work, this);
    }
    NotifyInitialized();
}

void ThreadPool::Terminate()
{
    if (IsTerminated())
    {
        return;
    }

    m_Queue.Cleanup();
    if (!IsInitialized())
    {
        // wait a reasonable time for thread pool to perhaps get initialized
        WaitUntilInitialized();
    }

    NotifyTerminated(); // Flag a thread pool as terminated

    for (std::thread& workerThread : m_WorkerThreads)
    {
        if (workerThread.joinable())
            workerThread.join();
    }
}

TpMetrics ThreadPool::GetMetrics() const
{
    ReadSyncGuard _(m_MetricsLock);
    return m_Metrics;
}

void ThreadPool::NotifyInitialized()
{
    WriteSyncGuard _(m_RwLock);
    NotifyState(EThreadPoolState::Initialized);
    m_InitCondVar.notify_all();
}

bool ThreadPool::IsInitialized() const
{
    ReadSyncGuard _(m_RwLock);
    return IsInitialized_Nosync();
}

bool ThreadPool::WaitUntilInitialized()
{
    static constexpr std::chrono::seconds ReasonableWaitTimeout = std::chrono::seconds(2);

    WriteSyncGuard _(m_RwLock);

    // use sync-version because m_RwLock is released while not waiting
    // timeout in ReasonableWaitTimeout seconds if not notified
    const auto waitPredicate = std::bind(&ThreadPool::IsInitialized_Nosync, this);
    return m_InitCondVar.wait_for(_, ReasonableWaitTimeout, waitPredicate);
}

void ThreadPool::NotifyTerminated()
{
    WriteSyncGuard _(m_RwLock);
    NotifyState(EThreadPoolState::Terminated);
    m_WorkerThreadCondVar.notify_all();
}

bool ThreadPool::IsTerminated() const
{
    ReadSyncGuard _(m_RwLock);
    return IsTerminated_Nosync();
}

void ThreadPool::Work()
{
    if (IsTerminated())
    {
        return;
    }

    enum class EQueueWaitResult : uint8_t
    {
        Ok,
        EmptyQueue,
        Terminated,
    } result;
    ExecutionQueueType::BindingType task;

    // while queue is empty - wait
    const auto waitPredicate = [this, &task, &result]() -> bool
        {
            result = (!m_Queue.PopTask(task) ? EQueueWaitResult::EmptyQueue : EQueueWaitResult::Ok);
            result = (IsTerminated_Nosync() ? EQueueWaitResult::Terminated : result);
            return (result != EQueueWaitResult::EmptyQueue);
        };

    while (true)
    {
        Milliseconds stalled = Milliseconds(0);

        // try to retrieve a task from queue. If cant - stall
        {
            WriteSyncGuard _(m_RwLock);
            TimepointMs stallBegin = Timings::Now();
            {
                m_WorkerThreadCondVar.wait(_, waitPredicate);
            }
            TimepointMs stallEnd = Timings::Now();
            stalled = Timings::Elapsed(stallBegin, stallEnd);
        }

        if (result == EQueueWaitResult::Terminated)
        {
            break;
        }

        TimepointMs  taskBegin = Timings::Now();
        {
            std::invoke(task);
        }
        TimepointMs  taskEnd = Timings::Now();
        Milliseconds taskElapsed = Timings::Elapsed(taskBegin, taskEnd);

        // lock to update metrics
        WriteSyncGuard _(m_MetricsLock);
        {
            if (m_Metrics.TaskQueueSize > 0)
                m_Metrics.TaskQueueSize--;

            size_t prevNumTasks = m_Metrics.NumTasksDone;
            size_t nextNumTasks = m_Metrics.NumTasksDone + 1;
            m_Metrics.AvgTaskExecutionTimeMs = Milliseconds((prevNumTasks * m_Metrics.AvgTaskExecutionTimeMs.count()) + taskElapsed.count()) / nextNumTasks;
            m_Metrics.NumTasksDone = nextNumTasks;

            size_t prevStallMs = m_Metrics.AvgThreadStalledTimeMs.count();
            size_t currStallMs = stalled.count();
            m_Metrics.AvgThreadStalledTimeMs = Milliseconds((GetNumWorkerThreads() * prevStallMs) + currStallMs) / GetNumWorkerThreads();
        }
    }
}