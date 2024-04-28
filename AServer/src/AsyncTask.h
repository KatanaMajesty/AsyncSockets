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
        InProgress,
        Finished,   // able to retrieve result
        Failed,     // task is failed, could basically occur on Termination
        NotFound,   // no task is found on the server
    };

    // Chunk state describes the state of task chunk context
    // It is used by the ServerTaskHandler to determine whether or not particular part of a task was done
    // If all chunk partitions of a task are marked as EChunkState::Done the task is considered ETaskStatus::Finished
    // Although, if at least one chunk is marked as EChunkState::Aborted or EChunkState::Invalid - task is considered failed
    enum class EChunkState : uint8_t
    {
        Invalid = 0,
        InProgress = 1,
        Done = 2,
        Aborted = 3,
    };

    enum class ERequestType : uint8_t
    {
        Invalid = 0,
        TaskSubmition,
        TaskStatus,
        TaskResult,
    };
    
    struct TaskChunkContext
    {
        static_assert(std::atomic<ETaskStatus>::is_always_lock_free);

        // Accessing const data of ISocketCommunicator is generally thread-safe, although not guaranteed to be
        const AsyncSock::ISocketCommunicator* Client = nullptr;
        uint32_t ChunkID = uint32_t(-1);

        mutable std::mutex BodyMutex;
        std::vector<uint8_t> Body;
        std::atomic<EChunkState> State = EChunkState::Invalid;
    };

    // just a wrapper over unordered_map of tasks
    class ClientTaskMap
    {
    public:
        TaskChunkContext* AddChunkContext(SOCKET socket);
    
        bool HasChunkContexts(SOCKET socket) const;
        auto& GetAllChunkContexts(SOCKET socket) { return m_contextMap.at(socket); }
        auto& GetAllChunkContexts(SOCKET socket) const { return m_contextMap.at(socket); }

    private:
        // to handle tasks easier, it is just a SOCKET key
        std::unordered_map<SOCKET, std::vector<std::unique_ptr<TaskChunkContext>>> m_contextMap;
    };

    template<typename T>
    concept IsTriviallyCopyable = std::is_trivially_copyable_v<T>;

    template<IsTriviallyCopyable HeaderType>
    class ServerTaskHandler
    {
    public:
        // When task delegate is invoked with a new chunk context, latter's chunk state is set to "InProgress"
        // It is user's responsibility to assign correct chunk state on the task completion
        // Generally, if a task is completed, chunk's state should have a value of EChunkState::Done
        // But if critical error occured or termination was requested EChunkState::Aborted should be assigned, although it is not required
        // for more info see EChunkState ennum class
        using TaskDelegate = std::function<void(HeaderType header, TaskChunkContext* chunkContext)>;

        ServerTaskHandler() = default;
        
        inline ~ServerTaskHandler()
        {
            Wait();
            m_workerThreadPool->Terminate();
        }

        void Init(uint32_t numWorkerThreads, const TaskDelegate& taskDelegate);
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

        TaskDelegate m_taskDelegate = nullptr;
        std::unique_ptr<ThreadPool> m_workerThreadPool;

        ClientTaskMap m_clientTaskMap;
    };

    template<IsTriviallyCopyable  HeaderType>
    inline void ServerTaskHandler<HeaderType>::Init(uint32_t numWorkerThreads, const TaskDelegate& taskDelegate)
    {
        if (!m_server.Init(AsyncSock::BindInfo{}) || !taskDelegate)
        {
            ASOCK_LOG("Failed to init server!\n");
            return;
        }

        ASOCK_LOG("Server was successfully initialized on {}:{}\n", 
            m_server.GetBindInfo().AddressIPv4, 
            m_server.GetBindInfo().AddressPort);

        m_clientThread = std::thread(&ServerTaskHandler::UpdateClients, this);

        m_taskDelegate = taskDelegate;
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
        // First of all check if client already has any tasks on the server
        // if so - just ignore packets. No support for different tasks from one client
        bool bIgnorePackets = m_clientTaskMap.HasChunkContexts(client->GetSocket());

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

        // if we ignore packets, bail out here
        if (bIgnorePackets)
        {
            ASOCK_LOG("Client has already submitted tasks before, could not add a new one. Aborting...\n");
            return false;
        }

        ASOCK_LOG("Task Submition! Successfully received header and task data (as {} chunks)\n", numChunks);

        // Now for each chunk add new TaskChunkContext and submit a task to the thread pool
        for (uint32_t i = 0; i < chunks.size(); ++i)
        {
            BodyChunk& chunk = chunks[i];

            TaskChunkContext* chunkContext = m_clientTaskMap.AddChunkContext(client->GetSocket());
            ASOCK_THROW_IF_FALSE(chunkContext != nullptr);

            chunkContext->Client = client;
            chunkContext->ChunkID = i;
            chunkContext->Body = std::move(chunk);
            chunkContext->State = EChunkState::InProgress;

            ASOCK_THROW_IF_FALSE(m_taskDelegate != nullptr);
            m_workerThreadPool->SubmitTask(m_taskDelegate, header, chunkContext);
        }
        return true;
    }

    template<IsTriviallyCopyable  HeaderType>
    inline bool ServerTaskHandler<HeaderType>::ProcessTaskStatus(AsyncSock::ISocketCommunicator* client)
    {
        SOCKET socket = client->GetSocket();
        if (!m_clientTaskMap.HasChunkContexts(socket))
        {
            ASOCK_THROW_IF_FALSE(client->Write(ETaskStatus::NotFound) == sizeof(ETaskStatus));
            return true;
        }

        for (const auto& context : m_clientTaskMap.GetAllChunkContexts(socket))
        {
            EChunkState chunkState = context->State.load();
            switch (chunkState)
            {
            case EChunkState::InProgress: 
                ASOCK_THROW_IF_FALSE(client->Write(ETaskStatus::InProgress) == sizeof(ETaskStatus)); 
                return true;
            case EChunkState::Aborted:
                ASOCK_THROW_IF_FALSE(client->Write(ETaskStatus::Failed) == sizeof(ETaskStatus));
                return true;
            case EChunkState::Invalid:
                ASOCK_THROW_IF_FALSE(client->Write(ETaskStatus::NotFound) == sizeof(ETaskStatus));
                return true;
            case EChunkState::Done:
            default: continue;
            }
        }

        // If we still have not aborted, then all chunk states are done
        ASOCK_THROW_IF_FALSE(client->Write(ETaskStatus::Finished) == sizeof(ETaskStatus));
        return true;
    }
    
    template<IsTriviallyCopyable  HeaderType>
    inline bool ServerTaskHandler<HeaderType>::ProcessTaskResult(AsyncSock::ISocketCommunicator* client)
    {
        // In ProcessTaskResult we assume that task chunk contexts are already on the server (there are at least 1)
        SOCKET sock = client->GetSocket();

        // firstly count amount of bytes to send
        size_t totalNumBytes = 0;
        for (const auto& context : m_clientTaskMap.GetAllChunkContexts(sock))
        {
            totalNumBytes += context->Body.size();
        }
        ASOCK_THROW_IF_FALSE(client->Write(totalNumBytes) == sizeof(totalNumBytes));

        // copy bytes from chunks into a single range
        std::vector<uint8_t> bytes(totalNumBytes);
        auto it = bytes.begin();
        for (const auto& context : m_clientTaskMap.GetAllChunkContexts(sock))
        {
            std::unique_lock _(context->BodyMutex);

            // copy and advance
            std::ranges::copy(context->Body, it);
            std::advance(it, context->Body.size());
        }

        // write until totalWritten >= totalNumBytes
        size_t totalWritten = 0;
        while (totalWritten < totalNumBytes)
        {
            size_t leftToWrite = totalNumBytes - totalWritten;
            size_t bytesWritten = client->Write(bytes.data() + totalWritten, leftToWrite);
            totalWritten += bytesWritten;
        }

        if (totalWritten != totalNumBytes)
        {
            ASOCK_LOG("Written {} bytes for task result (instead of {})! This will ruin everything probs!\n",
                totalWritten,
                totalNumBytes);
        }

        const AsyncSock::ISocketCommunicator::AddressInfo& addressInfo = client->GetAddressInfo();
        ASOCK_LOG("Finished writing result data for {}:{} client. Written {} bytes\n", addressInfo.IPv4, addressInfo.Port, totalWritten);

        return true;
    }

}