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

    // BodyChunk is solely for internal use but is also available as a part of public API
    // it stores the amount of bytes in the current chunk as well as the bytes itself
    struct BodyChunk
    {
        // Number of bytes for chunk of data
        size_t NumBytes = 0;

        // Offset in bytes into global body buffer for where the chunk begins
        size_t BufferOffsetInBytes = 0;
    };

    struct ExecutionChunk
    {
        static_assert(std::atomic<EChunkState>::is_always_lock_free);

        size_t ChunkID = size_t(-1);
        std::atomic<EChunkState> State = EChunkState::Invalid;

        // chunk mutex is required for client/server synchronization
        // when chunk task is being executed, ChunkMutex must be acquired thus the client would not be able
        // to retrieve task result without it being complete
        mutable std::mutex ChunkMutex;
        BodyChunk Chunk;
    };
    
    struct TaskContext
    {
        ExecutionChunk* GetExecutionChunk(size_t chunkID) const { return ExecutionChunks.at(chunkID).get(); }

        // Accessing const data of ISocketCommunicator is generally thread-safe, although not guaranteed to be
        const AsyncSock::ISocketCommunicator* Client = nullptr;

        mutable std::mutex   BodyMutex;
        std::vector<uint8_t> Body;
        std::vector<std::unique_ptr<ExecutionChunk>> ExecutionChunks;
    };

    // just a wrapper over unordered_map of tasks
    class ClientTaskMap
    {
    public:
        bool HasTaskContext(SOCKET socket) const;
        TaskContext* AddTaskContext(SOCKET socket);
        TaskContext* GetTaskContext(SOCKET socket) const { return m_contextMap.at(socket).get(); }
        
    private:
        // to handle tasks easier, it is just a SOCKET key
        std::unordered_map<SOCKET, std::unique_ptr<TaskContext>> m_contextMap;
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
        using TaskDelegate = std::function<void(HeaderType header, TaskContext* taskContext, size_t chunkID)>;

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
        const AsyncSock::ISocketCommunicator::AddressInfo& addressInfo = client->GetAddressInfo();
        if (m_clientTaskMap.HasTaskContext(client->GetSocket()))
        {
            ASOCK_LOG("Client {}:{} already has a task in progress!\n", addressInfo.IPv4, addressInfo.Port);
            return false;
        }

        HeaderType header;
        ASOCK_THROW_IF_FALSE(client->Read(header) == sizeof(HeaderType));

        // we must immediately know how much chunks to read in order to correctly proceed
        size_t numExecutionChunks = 0;
        ASOCK_THROW_IF_FALSE(client->Read(numExecutionChunks) == sizeof(size_t));

        // TODO: Add reasonable limit for the chunks, not just hardcoded value
        // TODO: Also chunks might be empty and zero should become acceptable
        if (numExecutionChunks == 0 || numExecutionChunks > 256)
        {
            ASOCK_LOG("Incorrect amount of chunks is read by the server ({} chunks)\n", numExecutionChunks);
            return false;
        }

        // create and fill basic data for task context
        TaskContext* taskContext = m_clientTaskMap.AddTaskContext(client->GetSocket());
        taskContext->Client = client;

        // now read chunk information!
        taskContext->ExecutionChunks.resize(numExecutionChunks);
        for (size_t i = 0; i < numExecutionChunks; ++i)
        {
            taskContext->ExecutionChunks[i] = std::make_unique<ExecutionChunk>();

            ExecutionChunk* executionChunk = taskContext->GetExecutionChunk(i);
            executionChunk->ChunkID = i;
            executionChunk->State = EChunkState::InProgress;

            BodyChunk& bodyChunk = executionChunk->Chunk;
            ASOCK_THROW_IF_FALSE(client->Read(bodyChunk) == sizeof(bodyChunk));
        }

        // finally we can read global body data
        // read until totalRead >= numBytes
        size_t numBytes = 0;
        ASOCK_THROW_IF_FALSE(client->Read(numBytes) == sizeof(numBytes) && numBytes > 0);

        // resize body to numBytes
        taskContext->Body.resize(numBytes);

        size_t totalRead = 0;
        while (totalRead < numBytes)
        {
            size_t bytesLeft = numBytes - totalRead;
            size_t bytesRead = client->Read(taskContext->Body.data() + totalRead, bytesLeft);
            totalRead += bytesRead;
        }

        if (totalRead != numBytes)
        {
            ASOCK_LOG("Read {} bytes of body (instead of {})! This will ruin everything probs!\n",
                totalRead,
                numBytes);
        }

        // finally submit tasks to the thread pool
        for (size_t i = 0; i < numExecutionChunks; ++i)
        {
            m_workerThreadPool->SubmitTask(m_taskDelegate, header, taskContext, i);
        }

        ASOCK_LOG("Task Submition successful! Successfully received body data from {}:{} (total of {} bytes)\n", addressInfo.IPv4, addressInfo.Port, totalRead);
        return true;
    }

    template<IsTriviallyCopyable  HeaderType>
    inline bool ServerTaskHandler<HeaderType>::ProcessTaskStatus(AsyncSock::ISocketCommunicator* client)
    {
        SOCKET socket = client->GetSocket();
        if (!m_clientTaskMap.HasTaskContext(socket))
        {
            ASOCK_THROW_IF_FALSE(client->Write(ETaskStatus::NotFound) == sizeof(ETaskStatus));
            return true;
        }

        // TODO: Handle zero chunks here as well. If no chunks are provided, whole data should be checked

        TaskContext* taskContext = m_clientTaskMap.GetTaskContext(socket);
        for (const auto& executionChunk : taskContext->ExecutionChunks)
        {
            EChunkState chunkState = executionChunk->State.load();
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
        TaskContext* taskContext = m_clientTaskMap.GetTaskContext(sock);

        // firstly count amount of bytes to send
        size_t totalNumBytes = taskContext->Body.size();
        ASOCK_THROW_IF_FALSE(client->Write(totalNumBytes) == sizeof(totalNumBytes));

        // instead of using body and bodymutex we should separately send bytes to the client for each chunk
        // This is needed in order to avoid blocking task execution here and allow for task parallelism
        std::vector<uint8_t> bytes(totalNumBytes);
        for (const auto& executionChunk : taskContext->ExecutionChunks)
        {
            std::unique_lock _(executionChunk->ChunkMutex);

            const BodyChunk& chunk = executionChunk->Chunk;
            auto begin  = std::next(taskContext->Body.begin(), chunk.BufferOffsetInBytes);
            auto end    = std::next(begin, chunk.NumBytes);
            auto dest   = std::next(bytes.begin(), chunk.BufferOffsetInBytes);
            std::copy(begin, end, dest);
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