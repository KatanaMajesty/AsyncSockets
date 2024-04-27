#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <format>
#include <iostream>
#include <string_view>

#define ASOCK_LOG(msg, ...) (std::print(std::cout, msg, __VA_ARGS__))
#define ASOCK_THROW_IF_FAILED(expr) \
    { \
        int32_t iResult = expr; \
        if (iResult == SOCKET_ERROR) \
        { \
            throw std::runtime_error("ASOCK_THROW_IF_FAILED"); \
        } \
    }
#define ASOCK_THROW_IF_FALSE(expr) \
    { \
        if (expr == FALSE) \
        { \
            throw std::runtime_error("ASOCK_THROW_IF_FALSE"); \
        } \
    }

namespace AsyncSock
{
    static constexpr std::string_view DefaultIPv4 = "127.0.0.1";
    static constexpr std::string_view DefaultPort = "27015";
    static constexpr uint32_t DefaultNumMaxConnections = 4u;

    using Socket_t = SOCKET;
    static constexpr Socket_t InvalidSocket = INVALID_SOCKET;

    bool Initialize();
    void Cleanup();
}

namespace AsyncSock
{

    struct BindInfo
    {
        std::string_view AddressIPv4 = AsyncSock::DefaultIPv4;
        std::string_view AddressPort = AsyncSock::DefaultPort;
        uint32_t NumMaxConnections = AsyncSock::DefaultNumMaxConnections;
    };

    class ISocketCommunicator
    {
    public:
        ISocketCommunicator() = default;
        virtual ~ISocketCommunicator() = default;

        virtual uint32_t Read(void* dst, size_t numBytes) = 0;
        virtual uint32_t Write(const void* src, size_t numBytes) = 0;

        template<typename T> uint32_t Read(T& t) { return Read(&t, sizeof(T)); }
        template<typename T> uint32_t Write(T&& t) { return Write(&t, sizeof(T)); }
    };

    class ClientCommunicator : public ISocketCommunicator
    {
    public:
        ClientCommunicator() = default;
        ClientCommunicator(SOCKET client, const std::string& ipv4, uint16_t port);
        virtual ~ClientCommunicator();

        uint32_t Read(void* dest, size_t numBytes) override;
        uint32_t Write(const void* src, size_t numBytes) override;

        const std::string& GetAddressIPv4() const { return m_addressIPv4; }
        uint16_t GetAddressPort() const { return m_addressPort; }

    private:
        friend class Server;

        std::string m_addressIPv4;
        uint16_t    m_addressPort;
        SOCKET m_client = AsyncSock::InvalidSocket;
    };

    class Server
    {
    public:
        using ClientCommunicatorContainer = std::vector<std::unique_ptr<ISocketCommunicator>>;

        Server();
        ~Server();

        const BindInfo& GetBindInfo() const { return m_bindInfo; }

        // returns communicators that are ready to be Read and Written at the same time
        // used to ensure non-blocking Read/Write communication
        // also handles new server connections
        std::vector<ISocketCommunicator*> PollRwClients();

        // binds socket to the address
        bool Init(const BindInfo& info);
        ISocketCommunicator* Accept();
        ISocketCommunicator* GetClientCommunicator(size_t index) { return m_clients.at(index).get(); }

        ClientCommunicatorContainer& GetAllCommunicators() { return m_clients; }
        const ClientCommunicatorContainer& GetAllCommunicators() const { return m_clients; }

    private:
        bool GetAddressInfo(SOCKET acceptedClient, std::string& ipv4, uint16_t& port) const;

        BindInfo m_bindInfo;

        ClientCommunicatorContainer m_clients;
        Socket_t m_listenerSocket = AsyncSock::InvalidSocket;

        // cached client pollfds that are queried each time Server::PollRwClients is called
        std::vector<WSAPOLLFD> m_clientRwPollfds;

        // we need socket communicator map to retrieve communicator interface of a socket in Server::Update
        // table is filled in in Server::Accept method
        std::unordered_map<SOCKET, ISocketCommunicator*> m_socketCommunicatorMap;
    };

}