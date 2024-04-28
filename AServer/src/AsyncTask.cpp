#include "AsyncTask.h"

namespace AsyncTask
{

    TaskChunkContext* ClientTaskMap::AddChunkContext(SOCKET socket)
    {
        if (socket == AsyncSock::InvalidSocket)
        {
            ASOCK_LOG("Invalid socket handle!\n");
            return nullptr;
        }

        auto& context = m_contextMap[socket].emplace_back(new TaskChunkContext());
        return context.get();
    }

    bool ClientTaskMap::HasChunkContexts(SOCKET socket) const
    {
        return (m_contextMap.find(socket) != m_contextMap.end());
    }

}