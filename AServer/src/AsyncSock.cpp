#include "AsyncSock.h"

#include <cstdint>

namespace AsyncSock
{

    bool Initialize()
    {
        static WSADATA WsaData;

        // Initialize Winsock
        int32_t iResult = WSAStartup(MAKEWORD(2, 2), &WsaData);
        if (iResult != 0) 
        {
            printf("WSAStartup failed: %d\n", iResult);
            return false;
        }

        return true;
    }

    void Cleanup()
    {
        WSACleanup();
    }

    Server::Server()
    {
        addrinfo addr = {};
        addr.ai_family = AF_INET;
        addr.ai_socktype = SOCK_STREAM;
        addr.ai_protocol = IPPROTO_TCP;
        m_listenerSocket = socket(addr.ai_family, addr.ai_socktype, addr.ai_protocol);
        if (m_listenerSocket == AsyncSock::InvalidSocket)
        {
            ASOCK_LOG("Error at socket(): {}\n", WSAGetLastError());
            return;
        }

        u_long param = 1; // TRUE, enable non-blocking I/O
        ASOCK_THROW_IF_FAILED( ioctlsocket(m_listenerSocket, FIONBIO, &param) );
    }

    Server::~Server()
    {
        if (m_listenerSocket != AsyncSock::InvalidSocket)
        {
            closesocket(m_listenerSocket);
        }
    }

    std::vector<ISocketCommunicator*> Server::PollRwClients()
    {
        // A few remarks we use in WSAPoll:
        // https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsapoll
        // we only use POLLRDNORM/POLLWRNORM flags as we dont care about others and separate ones
        m_clientRwPollfds.clear();
        m_clientRwPollfds.reserve(m_clients.size());

        // separately handle listener pollfd
        WSAPOLLFD listenerPollfd = WSAPOLLFD{
            .fd = m_listenerSocket,
            .events = POLLRDNORM,
            .revents = 0,
        };

        // handle listener socket of a server
        ASOCK_THROW_IF_FAILED( WSAPoll(&listenerPollfd, 1, 0) );

        // if listener is ready to accept new connections - do so
        if (listenerPollfd.revents & POLLRDNORM)
        {
            ISocketCommunicator* pCommunicator = Accept();
            ASOCK_THROW_IF_FALSE(pCommunicator != nullptr);

            const ISocketCommunicator::AddressInfo& addressInfo = pCommunicator->GetAddressInfo();
            ASOCK_LOG("Accepted a new communicator at {}:{}!\n", 
                addressInfo.IPv4,
                addressInfo.Port);
        }

        // iterate over all the clients and add them to the m_clientRwPollfds array 
        // newly accepted connections will also end up in this array after Server::Accept() call
        for (auto& pCommunicator : m_clients)
        {
            ClientCommunicator* client = static_cast<ClientCommunicator*>(pCommunicator.get());
            m_clientRwPollfds.push_back(WSAPOLLFD{
                .fd = client->m_client,
                .events = POLLRDNORM | POLLWRNORM,
                .revents = 0,
            });
        }

        std::vector<ISocketCommunicator*> result;
        if (!m_clients.empty())
        {
            // WSAPoll, wait indefinitely, provide with std::vector of WSAPOLLFDs
            // if failed - bail out, do not handle yet (failed if numFds == SOCKET_ERROR)
            int32_t numFds = WSAPoll(m_clientRwPollfds.data(), m_clientRwPollfds.size(), -1);
            ASOCK_THROW_IF_FALSE(numFds >= 0); // do not allow negative (SOCKET_ERROR and others)

            result.reserve(static_cast<size_t>(numFds)); // 0 is safe, reserve never shrinks

            for (const WSAPOLLFD& client : m_clientRwPollfds)
            {
                const BOOL bIsRwSock = (client.revents & (POLLRDNORM | POLLWRNORM)) == (POLLRDNORM | POLLWRNORM);
                if (bIsRwSock)
                {
                    // this is the socket we can read from and respond to
                    ISocketCommunicator* communicator = m_socketCommunicatorMap.at(client.fd);
                    result.push_back(communicator);
                }
            }
        }

        return result;
    }

    bool Server::Init(const BindInfo& info)
    {
        addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* result = nullptr;
        ASOCK_THROW_IF_FAILED( getaddrinfo(info.AddressIPv4.data(), info.AddressPort.data(), &hints, &result) );
        ASOCK_THROW_IF_FAILED( bind(m_listenerSocket, result->ai_addr, result->ai_addrlen) );
        ASOCK_THROW_IF_FAILED( listen(m_listenerSocket, info.NumMaxConnections) );

        m_bindInfo = info;
        return true;
    }

    ISocketCommunicator* Server::Accept()
    {
        if (m_listenerSocket == AsyncSock::InvalidSocket)
        {
            ASOCK_LOG("Server is not initialized!\n");
            return nullptr;
        }

        SOCKET client = accept(m_listenerSocket, nullptr, nullptr);
        if (client == AsyncSock::InvalidSocket)
        {
            // if no connections - just silently return nullptr
            if (WSAGetLastError() != WSAEWOULDBLOCK)
            {
                // if this is not WSAEWOULDBLOCK - yield a warning!
                ASOCK_LOG("Failed to accept a socket\n");
            }
            return nullptr;
        }

        std::string ipv4;
        uint16_t port = 0;
        if (!GetAddressInfo(client, ipv4, port))
        {
            ASOCK_LOG("Failed to retrieve address info!\n");
        }

        // Make socket non-blocking as well
        u_long bNonBlocking = 1;
        ASOCK_THROW_IF_FAILED( ioctlsocket(client, FIONBIO, &bNonBlocking) );

        // throw if somehow socket is already in communicator map
        ASOCK_THROW_IF_FALSE( !m_socketCommunicatorMap.contains(client) );

        auto& ptr = m_clients.emplace_back(std::make_unique<ClientCommunicator>(client, ipv4, port));
        m_socketCommunicatorMap[client] = ptr.get();

        return ptr.get();
    }

    bool Server::GetAddressInfo(SOCKET acceptedClient, std::string& ipv4, uint16_t& port) const
    {
        sockaddr_in clientAddr;
        int32_t clientAddrSize = sizeof(clientAddr);
        ASOCK_THROW_IF_FAILED( getpeername(acceptedClient, reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrSize) );

        char clientIPv4[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIPv4, INET_ADDRSTRLEN);
        ipv4 = std::string(clientIPv4);
        port = ntohs(clientAddr.sin_port);
        return true;
    }

    ClientCommunicator::ClientCommunicator(SOCKET client, const std::string& ipv4, uint16_t port)
        : m_addressInfo(ISocketCommunicator::AddressInfo{
                .IPv4 = ipv4,
                .Port = port
            })
        , m_client(client)
    {
        if (m_client == AsyncSock::InvalidSocket)
        {
            throw std::runtime_error("invalid socket");
        }
    }

    ClientCommunicator::~ClientCommunicator()
    {
        closesocket(m_client);
    }

    uint32_t ClientCommunicator::Read(void* dst, size_t numBytes)
    {
        int32_t numRead = recv(m_client, (char*)dst, numBytes, 0);

        // Otherwise, a value of SOCKET_ERROR is returned
        if (numRead == SOCKET_ERROR)
        {
            // throw if not WSAEWOULDBLOCK, otherwise return 0;
            if (WSAGetLastError() == WSAEWOULDBLOCK)
            {
                return 0;
            }

            ASOCK_THROW_IF_FALSE( false );
        }

        // here safe to assume non-negative number
        return static_cast<uint32_t>(numRead);
    }

    uint32_t ClientCommunicator::Write(const void* src, size_t numBytes)
    {
        int32_t numWritten = send(m_client, (const char*)src, numBytes, 0);

        // Otherwise, a value of SOCKET_ERROR is returned
        if (numWritten == SOCKET_ERROR)
        {
            // throw if not WSAEWOULDBLOCK, otherwise return 0;
            if (WSAGetLastError() == WSAEWOULDBLOCK)
            {
                return 0;
            }

            ASOCK_THROW_IF_FALSE( false );
        }

        // here safe to assume non-negative number
        return static_cast<uint32_t>(numWritten);
    }

}