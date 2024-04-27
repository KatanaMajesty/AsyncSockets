#pragma once

#include <thread>
#include <vector>
#include <mutex>
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
    concept IsHeaderType = std::is_trivially_copyable_v<T>;

    template<IsHeaderType HeaderType, typename BodyType, uint8_t NumTaskThreads = 4>
    class ServerTaskHandler
    {
    public:
        using TaskFunctor = std::function<void(const HeaderType& header, const std::vector<std::byte>& body)>;

        ServerTaskHandler()
            : m_threadPool(std::thread::hardware_concurrency()) // initialize with number of CPU concurrent threads
        {
        }
        
        ~ServerTaskHandler()
        {
            Wait();
            m_threadPool.Terminate();
        }

        void Init();
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
        ThreadPool m_threadPool;
    };

    template<IsHeaderType HeaderType, typename BodyType, uint8_t NumTaskThreads>
    inline void ServerTaskHandler<HeaderType, BodyType, NumTaskThreads>::Init()
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
    }

    template<IsHeaderType HeaderType, typename BodyType, uint8_t NumTaskThreads>
    inline void ServerTaskHandler<HeaderType, BodyType, NumTaskThreads>::UpdateClients()
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

    template<IsHeaderType HeaderType, typename BodyType, uint8_t NumTaskThreads>
    inline void ServerTaskHandler<HeaderType, BodyType, NumTaskThreads>::ProcessClientRequest(AsyncSock::ISocketCommunicator* client, ERequestType type)
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

    template<IsHeaderType HeaderType, typename BodyType, uint8_t NumTaskThreads>
    inline bool ServerTaskHandler<HeaderType, BodyType, NumTaskThreads>::ProcessTaskSubmition(AsyncSock::ISocketCommunicator* client)
    {
        HeaderType header;
        ASOCK_THROW_IF_FALSE(client->Read(header) == sizeof(HeaderType));

        // we must immediately know how much bytes to read in order to correctly proceed
        size_t numBytes = 0;
        ASOCK_THROW_IF_FALSE(client->Read(numBytes) == sizeof(size_t));

        std::vector<uint8_t> body(numBytes);

        // read until totalRead >= numBytes
        uint32_t totalRead = 0;
        while (totalRead < numBytes)
        {
            uint32_t leftToRead = numBytes - totalRead;
            uint32_t bodyBytesRead = client->Read(body.data() + totalRead, leftToRead);
            totalRead += bodyBytesRead;
        }

        if (totalRead != numBytes)
        {
            ASOCK_LOG("Read {} bytes (instead of {})! This will ruin everything probs!\n",
                totalRead,
                numBytes);
        }

        ASOCK_LOG("Task Submition! Successfully received task type and header ({} bytes of data)\n", numBytes);

        // Submit task here
        m_threadPool.SubmitTask([](const HeaderType& header, const std::vector<uint8_t>& body)
            {
                const uint8_t* bytes = body.data();
                std::cout << "Completed task! Processed " << body.size() << " bytes!\n";
            }, header, std::move(body));
        return true;
    }

    template<IsHeaderType HeaderType, typename BodyType, uint8_t NumTaskThreads>
    inline bool ServerTaskHandler<HeaderType, BodyType, NumTaskThreads>::ProcessTaskStatus(AsyncSock::ISocketCommunicator* client)
    {
        ASOCK_THROW_IF_FALSE(client->Write(ETaskStatus::Failed) == sizeof(ETaskStatus));
        ASOCK_LOG("Task Status!\n");
        return true;
    }
    
    template<IsHeaderType HeaderType, typename BodyType, uint8_t NumTaskThreads>
    inline bool ServerTaskHandler<HeaderType, BodyType, NumTaskThreads>::ProcessTaskResult(AsyncSock::ISocketCommunicator* client)
    {
        ASOCK_LOG("Task Result!\n");
        return true;
    }

}