#include "ExecutionQueue.h"

bool TpExecutionQueue::PopTask(BindingType& t)
{
    WriteSyncGuard _(m_RwLock);

    if (m_Queue.empty())
    {
        return false;
    }

    t = std::move(m_Queue.front());
    m_Queue.pop();
    return true;
}

bool TpExecutionQueue::IsEmpty() const
{
    ReadSyncGuard _(m_RwLock);
    return m_Queue.empty();
}

uint32_t TpExecutionQueue::GetNumTasks() const
{
    ReadSyncGuard _(m_RwLock);
    return static_cast<uint32_t>(m_Queue.size());
}

void TpExecutionQueue::Cleanup()
{
    WriteSyncGuard _(m_RwLock);
    while (!m_Queue.empty())
    {
        m_Queue.pop();
    }
}