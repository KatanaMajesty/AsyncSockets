#pragma once

#include <vector>
#include <thread>
#include <span>
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
        InProgress,
        Finished,   // able to retrieve result
        Failed,     // task is failed, could basically occur on Termination
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
            // Number of bytes for chunk of data
            size_t NumBytes = 0;

            // Offset in bytes into global body buffer for where the chunk begins
            size_t BufferOffsetInBytes = 0;
        };

        // BodyPartition struct allows to split task execution into multiple threads on server
        // by providing N chunks that describe body partitioning, thus server will queue N works to execute it
        // Task will only be considered Finished when all chunk tasks are executed successfully
        struct BodyPartition
        {
            std::span<const uint8_t> Body;

            // If data partitioning is not desired, array should be left empty, thus the body will be submitted as a whole
            std::vector<BodyChunk> ExecutionChunks;
        };

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

            if (!m_client.IsConnected())
            {
                return false;
            }

            ASOCK_THROW_IF_FALSE( m_client.Write(ERequestType::TaskSubmition) == sizeof(ERequestType) );
            ASOCK_THROW_IF_FALSE( m_client.Write(header) == sizeof(HeaderType) );

            // Write body chunk partition info before starting to upload the data
            ASOCK_THROW_IF_FALSE( m_client.Write(bodyPartition.ExecutionChunks.size()) == sizeof(size_t) );
            for (const BodyChunk& chunk: bodyPartition.ExecutionChunks)
            {
                // firstly write amount of bytes in chunk
                ASOCK_THROW_IF_FALSE( m_client.Write(chunk) == sizeof(chunk) );
            }

            // And finally in the end write amount of body bytes and body of bytes itself
            ASOCK_THROW_IF_FALSE( m_client.Write(bodyPartition.Body.size()) == sizeof(size_t) );
            if (m_client.Write(bodyPartition.Body.data(), bodyPartition.Body.size()) != bodyPartition.Body.size())
            {
                ASOCK_LOG("Client -> Critical! Failed to upload body chunk data of size {}! Aborting...\n", bodyPartition.Body.size());
                return false;
            }

            return true;
        }

        // Queries task status. If task is done on the server and can be retrieved returns ETaskStatus::Finished
        // otherwise returns respective result thus the client can make some decisions regarding their communication with server
        ETaskStatus GetTaskStatus();

        // Queries task result. If ClientTaskHandler::GetTaskStatus return value is not ETaskStatus::Finished
        // no result is written and ETaskStatus from the server is queried (and returned)
        // otherwise tries to read a result from server as a single body of data without chunk partitioning applied
        // Also, if body of bytes could not be read correctly from server, returns ETaskStatus::Failed and stops querying
        ETaskStatus GetTaskResult(std::vector<uint8_t>& bytes);

    private:
        bool InternalConnect(const AsyncSock::ConnectionInfo& connectionInfo);

        AsyncSock::Client m_client;
    };

}