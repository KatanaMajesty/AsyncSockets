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
        ASOCK_THROW_IF_FALSE(m_client.Write(ERequestType::TaskStatus) == sizeof(ERequestType));

        ETaskStatus status = ETaskStatus::Unknown;
        ASOCK_THROW_IF_FALSE(m_client.Read(status) == sizeof(status));

        return status;
    }

    ETaskStatus ClientTaskHandler::GetTaskResult(std::vector<uint8_t>& bytes)
    {
        // bail out if task status is not marked as finished
        ETaskStatus status = GetTaskStatus();
        if (status != ETaskStatus::Finished)
        {
            ASOCK_LOG("ClientTaskHandler::GetTaskResult -> Could not retrieve result. Task is not finished!\n");
            return status;
        }

        // request task result
        ASOCK_THROW_IF_FALSE(m_client.Write(ERequestType::TaskResult) == sizeof(ERequestType));

        // firstly read amount of bytes in body
        // if we cannot read amount of bytes - throw, do not handle
        size_t numBytes = 0;
        ASOCK_THROW_IF_FALSE(m_client.Read(numBytes) == sizeof(numBytes) && numBytes > 0);

        bytes.clear();
        bytes.resize(numBytes);
        
        size_t readBytes = m_client.Read(bytes.data(), numBytes);
        if (readBytes != numBytes)
        {
            // if did not read correct amount of data, return cleared bytes and bail out with failed state
            ASOCK_LOG("ClientTaskHandler::GetTaskResult -> Failed to read body data (read {} bytes instead of {})\n", readBytes, numBytes);
            bytes.clear();
            bytes.shrink_to_fit();
            return ETaskStatus::Failed;
        }

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