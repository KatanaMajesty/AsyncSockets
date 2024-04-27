#pragma once

/*
The thread pool is serviced by 4 worker threads and has a single execution queue. 
Tasks are added directly to the end of the execution queue. 
A task is taken for execution from the buffer as soon as a free worker thread is available. 
The task takes a random time between 3 to 6 seconds to complete.

Implement a custom thread pool with the characteristics specified above. 
Mandatory characteristics for each variant: the thread pool must be correctly written 
with respect to the chosen programming language, it must have the ability to terminate 
its work correctly (instantly, abandoning all active tasks, as well as completing active tasks), 
the ability to temporarily pause its work, and work using conditional variables.

Initialization and destruction operations of the pool, adding and removing tasks from the queue 
should be safe in terms of parallelism.

Create a program that will execute tasks according to the selected variant, using the thread pool written by the student. 
The code responsible for adding tasks to the thread pool, and the thread pool itself, should be in different execution threads. 
Tasks in the thread pool should be added from several threads.

Verify and prove the correctness of the program's operation using an input/output information system in the console (or another tool - profiler).

Perform time-limited testing and calculate the number of created threads and the average time a thread spends in the waiting state. 
For tasks with an unlimited queue - determine the average length of each queue and the average task execution time. 
For queues limited by size - determine the maximum and minimum time until the queue was filled, and the number of discarded tasks.
*/

#include <cstdint>
#include <thread>
#include <condition_variable>

#include "RwLockSync.h"
#include "ExecutionQueue.h"

#include "Timings.h"

struct TpMetrics
{
    // Amount of tasks in execution queue
    size_t TaskQueueSize;

    size_t NumTasksDone;

    // Average task execution time in milliseconds
    Milliseconds AvgTaskExecutionTimeMs;

    // Average time between all worker threads each of them spends in stalled state
    // waiting for a task from execution queue
    Milliseconds AvgThreadStalledTimeMs;
};

enum class EThreadPoolState : uint8_t
{
    Invalid = 0,
    Initialized,
    Terminated,
};

class ThreadPool
{
public:
    using ExecutionQueueType    = TpExecutionQueue;
    using ConditionSyncType     = std::condition_variable_any;

    ThreadPool() = default;
    ThreadPool(uint32_t numWorkerThreads);

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    inline ~ThreadPool()
    {
        Terminate();
    }

    template<typename TaskType, typename... Args>
    void SubmitTask(TaskType&& task, Args&&... args);
    void Terminate();

    uint32_t GetNumWorkerThreads() { return static_cast<uint32_t>(m_WorkerThreads.size()); }

    TpMetrics GetMetrics() const;

private:
    void NotifyState(EThreadPoolState state) { m_State = state; }
    void NotifyInitialized();

    bool IsInitialized_Nosync() const { return m_State == EThreadPoolState::Initialized; }
    bool IsInitialized() const;

    bool WaitUntilInitialized();
    void NotifyTerminated();

    bool IsTerminated_Nosync() const { return m_State == EThreadPoolState::Terminated; }
    bool IsTerminated() const;
    void Work();

    mutable RwLock m_RwLock;
    EThreadPoolState m_State = EThreadPoolState::Invalid;

    ExecutionQueueType  m_Queue;
    ConditionSyncType   m_InitCondVar;
    ConditionSyncType   m_WorkerThreadCondVar;
    std::vector<std::thread> m_WorkerThreads;

    mutable RwLock  m_MetricsLock;
    TpMetrics       m_Metrics;
};

template<typename TaskType, typename ...Args>
inline void ThreadPool::SubmitTask(TaskType&& task, Args && ...args)
{
    // if not terminated assume it to be soon started
    // if could not wait until initialized - throw runtime exception
    if (!IsTerminated() && !WaitUntilInitialized())
    {
        throw std::runtime_error("ThreadPool::SubmitTask -> Could not submit task before queue got initialized");
        return;
    }

    m_Queue.PushTask(std::forward<TaskType>(task), std::forward<Args>(args)...);
    m_Metrics.TaskQueueSize = m_Queue.GetNumTasks(); // we could just increment using ++, but whatever
    m_WorkerThreadCondVar.notify_one();
}