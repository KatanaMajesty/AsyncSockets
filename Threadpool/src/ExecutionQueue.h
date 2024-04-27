#pragma once

#include <queue>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <functional>

#include "RwLockSync.h"

// !!! https://www.youtube.com/watch?v=zt7ThwVfap0 !!!
// Do not use std::bind -> use lambdas instead! std::bind is not recommended in Modern C++

class TpExecutionQueue
{
public:
    using BindingType = std::function<void()>;

    TpExecutionQueue() = default;

    TpExecutionQueue(const TpExecutionQueue&) = delete;
    TpExecutionQueue& operator=(const TpExecutionQueue&) = delete;

    template<typename T, typename... Args>
    auto PushTask(T&& task, Args&&... args) -> void /*-> std::enable_if_t<std::is_invocable_v<T, Args&&...>>*/
    {
        WriteSyncGuard _(m_RwLock);

        // as of C++20 there is lambda perfect capture
        // https://stackoverflow.com/questions/47496358/c-lambdas-how-to-capture-variadic-parameter-pack-from-the-upper-scope
        auto binding = [&task, args...]() noexcept
        { 
            task(args...); // easier to debug this way
            //std::invoke(task, std::forward<Args>(args)...);
        };
        m_Queue.push(binding);
    }

    bool PopTask(BindingType& t);
    bool IsEmpty() const;

    uint32_t GetNumTasks() const;
    void Cleanup();

private:
    mutable RwLock m_RwLock;
    std::queue<BindingType> m_Queue;
};