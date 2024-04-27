#pragma once

#include <vector>
#include <thread>
#include "AsyncSock.h"

namespace AsyncTask
{

    enum class EConnectionResult : uint8_t
    {
        Ok = 0,
        Failed = 1,
        Failed_Handshake = 2,
    };

    enum class ETaskStatus : uint8_t
    {
        Unknown = 0, // was not able to retrieve status
        Finished,   // able to retrieve result
        Failed,     // task is failed
        Terminated, // task is terminated, no result submitted
        NotFound,   // no task is found on the server
    };

    enum class ERequestType : uint8_t
    {
        Invalid = 0,
        TaskSubmition,
        TaskStatus,
        TaskResult,
    };

    class ClientTaskHandler
    {
    public:
        // BodyChunk is solely for internal use but is also available as a part of public API
        // it stores the amount of bytes in the current chunk as well as the bytes itself
        struct BodyChunk
        {
            using ByteBuffer = const uint8_t*;

            // Number of bytes for chunk of data
            uint32_t NumBytes = 0;

            // chunks allow to split task execution into multiple threads
            ByteBuffer Buffer = nullptr;
        };

        // BodyPartition struct allows to split task execution into multiple threads on server
        // by providing N chunks of the same data server will queue N works to execute it
        // Task will only be considered Finished when all chunk tasks are executed successfully
        using BodyPartition = std::vector<BodyChunk>;

        ClientTaskHandler() = default;

        EConnectionResult Connect(const AsyncSock::ConnectionInfo& connectionInfo);

        // How task submition is performed
        // firstly we send the data to the server to signalize that the task submition is requested
        // after that the following sequence of events is performed in order:
        //  1) Provided custom header is sent to the server. The server-sided header handling also depends on programmable user behavior
        //  2) Basic body partition information is written - amount of chunks (8 bytes)
        //  3) Client then starts writes bytesPerChunk and chunk data one-by-one starting from the first to the server
        template<typename HeaderType>
        auto SubmitTask(const HeaderType& header, const BodyPartition& bodyPartition)
        {
            static_assert(std::is_trivially_copyable_v<HeaderType>, "Header type should be trivially copyable!");

            if (!m_client.IsConnected() || bodyPartition.empty())
            {
                return false;
            }

            ASOCK_THROW_IF_FALSE( m_client.Write(ERequestType::TaskSubmition) == sizeof(ERequestType) );
            ASOCK_THROW_IF_FALSE( m_client.Write(header) == sizeof(HeaderType) );

            // Write body partition info (number of chunks) and start partitioned data upload
            ASOCK_THROW_IF_FALSE( m_client.Write(bodyPartition.size()) == sizeof(size_t) );
            for (const BodyChunk& chunk: bodyPartition)
            {
                // firstly write amount of bytes in chunk
                ASOCK_THROW_IF_FALSE( m_client.Write(chunk.NumBytes) == sizeof(uint32_t) );
                if (m_client.Write(chunk.Buffer, chunk.NumBytes) != chunk.NumBytes)
                {
                    ASOCK_LOG("Client -> Critical! Failed to upload body chunk data of size {}! Aborting...\n", chunk.NumBytes);
                    return false;
                }
            }

            return true;
        }

        ETaskStatus GetTaskStatus();

    private:
        bool InternalConnect(const AsyncSock::ConnectionInfo& connectionInfo);

        AsyncSock::Client m_client;
    };

}