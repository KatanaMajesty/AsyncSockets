#include "AsyncTask.h"

namespace AsyncTask
{

    bool ClientTaskMap::HasTaskContext(SOCKET socket) const
    {
        ReadSyncGuard _(m_mapMutex);
        return (m_contextMap.find(socket) != m_contextMap.end());
    }

    TaskContext* ClientTaskMap::AddTaskContext(SOCKET socket)
    {
        if (socket == AsyncSock::InvalidSocket)
        {
            ASOCK_LOG("Invalid socket handle!\n");
            return nullptr;
        }

        if (HasTaskContext(socket))
        {
            return nullptr;
        }

        WriteSyncGuard _(m_mapMutex);
        m_contextMap[socket] = std::make_unique<TaskContext>();
        return m_contextMap[socket].get();
    }

    TaskContext* ClientTaskMap::GetTaskContext(SOCKET socket) const
    {
        ReadSyncGuard _(m_mapMutex);
        return m_contextMap.at(socket).get();
    }

}