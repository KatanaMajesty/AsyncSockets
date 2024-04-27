#pragma once

#include <thread>
#include <vector>
#include <mutex>
#include <memory>
#include "AsyncSock.h"

#include "ThreadPool.h"

namespace AsyncTask
{

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

    template<typename T>
    concept IsTriviallyCopyable = std::is_trivially_copyable_v<T>;

    template<IsTriviallyCopyable HeaderType>
    class ServerTaskHandler
    {
    public:
        using TaskFunctor = std::function<void(const HeaderType& header, const std::vector<uint8_t>& body)>;

        ServerTaskHandler() = default;
        
        inline ~ServerTaskHandler()
        {
            Wait();
            m_workerThreadPool->Terminate();
        }

        void Init(uint32_t numWorkerThreads);
        void Wait()
        {
            if (m_clientThread.joinable())
                m_clientThread.join();
        }

    private:
        // BodyChunk is solely for internal use (unlike one the Client side of the API)
        // Also quite differs from the client side, as it is basically just an array of bytes
        using BodyChunk = std::vector<uint8_t>;

        void UpdateClients();
        void ProcessClientRequest(AsyncSock::ISocketCommunicator* client, ERequestType type);
        bool ProcessTaskSubmition(AsyncSock::ISocketCommunicator* client);
        bool ProcessTaskStatus(AsyncSock::ISocketCommunicator* client);
        bool ProcessTaskResult(AsyncSock::ISocketCommunicator* client);

        AsyncSock::Server m_server;
        std::thread m_clientThread;
        std::unique_ptr<ThreadPool> m_workerThreadPool;
    };

    template<IsTriviallyCopyable  HeaderType>
    inline void ServerTaskHandler<HeaderType>::Init(uint32_t numWorkerThreads)
    {
        if (!m_server.Init(AsyncSock::BindInfo{}))
        {
            ASOCK_LOG("Failed to init server!\n");
            return;
        }

        ASOCK_LOG("Server was successfully initialized on {}:{}\n", 
            m_server.GetBindInfo().AddressIPv4, 
            m_server.GetBindInfo().AddressPort);

        m_clientThread = std::thread(&ServerTaskHandler::UpdateClients, this);
        m_workerThreadPool = std::make_unique<ThreadPool>(numWorkerThreads);
    }

    template<IsTriviallyCopyable  HeaderType>
    inline void ServerTaskHandler<HeaderType>::UpdateClients()
    {
        // while not terminated, wait for connections and play around :)
        while (true)
        {
            std::vector<AsyncSock::ISocketCommunicator*> rwClients = m_server.PollRwClients();

            // iterate over all clients and handle them separately, submit them to the queue
            for (AsyncSock::ISocketCommunicator* client : rwClients)
            {
                ERequestType requestType = ERequestType::Invalid;

                // we assume that first unread packet is an ERequestType type
                if (client->Read(requestType) != 0)
                {
                    ProcessClientRequest(client, requestType);
                }
            }
        }
    }

    template<IsTriviallyCopyable  HeaderType>
    inline void ServerTaskHandler<HeaderType>::ProcessClientRequest(AsyncSock::ISocketCommunicator* client, ERequestType type)
    {
        // if client has written something - handle their request type
        switch (type)
        {
        case ERequestType::TaskSubmition:   ProcessTaskSubmition(client); break;
        case ERequestType::TaskStatus:      ProcessTaskStatus(client); break;
        case ERequestType::TaskResult:      ProcessTaskResult(client); break;
            // if default - just ignore and do nothing, but warn that smth is wrong with packets
        default:
            // just ignore! error messages will yield from other places
            // ASOCK_LOG("Something might be wrong with packets from a client!\n");
            return;
        }
    }

    template<IsTriviallyCopyable  HeaderType>
    inline bool ServerTaskHandler<HeaderType>::ProcessTaskSubmition(AsyncSock::ISocketCommunicator* client)
    {
        HeaderType header;
        ASOCK_THROW_IF_FALSE(client->Read(header) == sizeof(HeaderType));

        // we must immediately know how much chunks to read in order to correctly proceed
        size_t numChunks = 0;
        ASOCK_THROW_IF_FALSE(client->Read(numChunks) == sizeof(size_t));

        // TODO: Add reasonable limit for the chunks, not just hardcoded value
        if (numChunks == 0 || numChunks > 256)
        {
            ASOCK_LOG("Incorrect amount of chunks is read by the server ({} chunks)\n", numChunks);
            return false;
        }

        // read partitioned data by chunks here
        std::vector<BodyChunk> chunks(numChunks);
        for (size_t chunkIdx = 0; chunkIdx < numChunks; ++chunkIdx)
        {
            BodyChunk& chunk = chunks[chunkIdx];

            // read amount of bytes in chunk
            uint32_t numBytesInChunk = 0;
            ASOCK_THROW_IF_FALSE(client->Read(numBytesInChunk) == sizeof(numBytesInChunk));

            chunk.resize(numBytesInChunk);
            
            // read until totalRead >= numBytesInChunk
            uint32_t totalRead = 0;
            while (totalRead < numBytesInChunk)
            {
                uint32_t leftToRead = numBytesInChunk - totalRead;
                uint32_t bodyBytesRead = client->Read(chunk.data() + totalRead, leftToRead);
                totalRead += bodyBytesRead;
            }

            if (totalRead != numBytesInChunk)
            {
                ASOCK_LOG("Read {} bytes for chunk (instead of {})! This will ruin everything probs!\n",
                    totalRead,
                    numBytesInChunk);
            }

            ASOCK_LOG("Finished reading data from chunk {}. Read {} bytes\n", chunkIdx, totalRead);
        }

        ASOCK_LOG("Task Submition! Successfully received header and task data (as {} chunks)\n", numChunks);

        // Submit task here
        /*m_threadPool.SubmitTask([](const HeaderType& header, const std::vector<uint8_t>& body)
            {
                const uint8_t* bytes = body.data();
                std::cout << "Completed task! Processed " << body.size() << " bytes!\n";
            }, header, std::move(body));*/
        return true;
    }

    template<IsTriviallyCopyable  HeaderType>
    inline bool ServerTaskHandler<HeaderType>::ProcessTaskStatus(AsyncSock::ISocketCommunicator* client)
    {
        ASOCK_THROW_IF_FALSE(client->Write(ETaskStatus::Failed) == sizeof(ETaskStatus));
        ASOCK_LOG("Task Status!\n");
        return true;
    }
    
    template<IsTriviallyCopyable  HeaderType>
    inline bool ServerTaskHandler<HeaderType>::ProcessTaskResult(AsyncSock::ISocketCommunicator* client)
    {
        ASOCK_LOG("Task Result!\n");
        return true;
    }

}