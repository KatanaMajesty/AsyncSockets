#include "AsyncTask.h"

namespace AsyncTask
{

    EConnectionResult ClientTaskHandler::Connect(const AsyncSock::ConnectionInfo& connectionInfo)
    {
        // Connect and send a hand-shake and ensure this is the server that is ready to work with our data
        if (!InternalConnect(connectionInfo))
        {
            ASOCK_LOG("ClientTaskHandler::Connect -> InternalConnect error\n");
            return EConnectionResult::Failed;
        }

        return EConnectionResult::Ok;
    }

    ETaskStatus ClientTaskHandler::GetTaskStatus()
    {
        ASOCK_THROW_IF_FALSE(m_client.Write(ERequestType::TaskStatus));

        ETaskStatus status = ETaskStatus::Unknown;
        ASOCK_THROW_IF_FALSE(m_client.Read(status));

        return status;
    }

    bool ClientTaskHandler::InternalConnect(const AsyncSock::ConnectionInfo& connectionInfo)
    {
        static constexpr uint32_t MaxConnectionAttempts = 5;
        static constexpr std::chrono::seconds SleepTime = std::chrono::seconds(2);

        uint32_t connectionAttempts = 0;
        while (connectionAttempts < MaxConnectionAttempts)
        {
            if (m_client.Connect(connectionInfo))
            {
                return true;
            }

            std::this_thread::sleep_for(SleepTime);
            ++connectionAttempts;
            ASOCK_LOG("Client -> Failed to connect to {}:{}. Retrying {} time\n",
                connectionInfo.AddressIPv4,
                connectionInfo.AddressPort,
                connectionAttempts);
        }

        return false;
    }

}