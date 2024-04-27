#include <cstdint>
#include <iostream>
#include <random>
#include <array>
#include <format>

#include "ThreadPool.h"

template<uint32_t MinTaskMs = 3000u, uint32_t MaxTaskMs = 6000u>
struct TaskGenerator
{
    using TaskType = std::function<void()>;

    TaskGenerator(uint32_t threadID)
        : ThreadID(threadID)
    {
    }

    TaskType operator()()
    {
        static thread_local std::random_device RandomDevice;
        static thread_local std::default_random_engine RandomEngine(RandomDevice());
        std::uniform_int_distribution<uint32_t> TimeDistribution(MinTaskMs, MaxTaskMs);
        
        uint32_t sleepMs = TimeDistribution(RandomEngine);
        uint32_t taskID  = LastTaskID++;
        return [sleepMs, taskID, threadID = ThreadID]() 
        {
            
            std::cout << std::format("[ThreadID: {}] -> Started task {}\n", threadID, taskID);
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            std::cout << std::format("[ThreadID: {}] -> Finished task {}\n", threadID, taskID);
        };
    }

    uint32_t ThreadID   = 0;
    uint32_t LastTaskID = 0;
};

using TaskGenType = TaskGenerator<3000, 6000>;

void PopulateWithTasks(uint32_t threadID, ThreadPool& threadPool, uint32_t numTasks, uint32_t sleepBetweenTasksMs)
{
    TaskGenType taskGenerator(threadID);

    const std::chrono::milliseconds sleepTime = std::chrono::milliseconds(sleepBetweenTasksMs);
    for (uint32_t i = 0; i < numTasks; ++i)
    {
        threadPool.SubmitTask(taskGenerator());
        std::this_thread::sleep_for(sleepTime);
    }
}

int32_t main()
{
    ThreadPool threadPool;

    uint32_t threadID = 0;
    uint32_t numTasksPerPopulator = 10;
    uint32_t populatorSleepTimeMs = 1200;
    std::array<std::jthread, 2> TaskPopulators;
    for (std::jthread& thread : TaskPopulators)
    {
        thread = std::jthread(PopulateWithTasks, threadID++, std::ref(threadPool), numTasksPerPopulator, populatorSleepTimeMs);
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));
    threadPool.Terminate();

    TpMetrics metrics = threadPool.GetMetrics(); // how to handle these cases?
    std::cout << std::format("TpMetrics -> Num tasks in queue after termination: {}\n", metrics.TaskQueueSize);
    std::cout << std::format("TpMetrics -> Num tasks processed: {}\n", metrics.NumTasksDone);
    std::cout << std::format("TpMetrics -> Average task execution time: {}\n", metrics.AvgTaskExecutionTimeMs);
    std::cout << std::format("TpMetrics -> Average thread stall time:   {}\n", metrics.AvgThreadStalledTimeMs);
}