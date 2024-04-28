#include "AsyncTask.h"

namespace AsyncTask
{

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

        m_contextMap[socket] = std::make_unique<TaskContext>();
        return m_contextMap[socket].get();
    }

    bool ClientTaskMap::HasTaskContext(SOCKET socket) const
    {
        return (m_contextMap.find(socket) != m_contextMap.end());
    }

}