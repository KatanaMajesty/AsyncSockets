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

    Client::Client()
    {
        m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ASOCK_THROW_IF_FALSE( m_socket != AsyncSock::InvalidSocket );
    }

    Client::~Client()
    {
        Disconnect();
    }

    bool Client::Connect(const ConnectionInfo& connectionInfo)
    {
        addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        // Resolve the server address and port
        addrinfo* addr = nullptr;
        ASOCK_THROW_IF_FAILED( getaddrinfo(connectionInfo.AddressIPv4.data(), connectionInfo.AddressPort.data(), &hints, &addr) );

        // Connect to server.
        if (connect(m_socket, addr->ai_addr, static_cast<int32_t>(addr->ai_addrlen)) == SOCKET_ERROR)
        {
            Disconnect();
        }

        freeaddrinfo(addr);
        if (!IsConnected())
        {
            ASOCK_LOG("Unable to connect to server!\n");
            return false;
        }

        return true;
    }

    bool Client::Disconnect()
    {
        if (!IsConnected())
        {
            return false;
        }

        shutdown(m_socket, SD_SEND); // just ignore if failed
        ASOCK_THROW_IF_FAILED( closesocket(m_socket) );

        m_socket = AsyncSock::InvalidSocket;
        return true;
    }

    uint32_t Client::Read(void* dst, size_t numBytes)
    {
        int32_t numRead = recv(m_socket, (char*)dst, numBytes, 0);

        // Otherwise, a value of SOCKET_ERROR is returned, which is checked with ASOCK_THROW_IF_FAILED macro
        ASOCK_THROW_IF_FAILED(numRead);

        // here safe to assume non-negative number
        return static_cast<uint32_t>(numRead);
    }

    uint32_t Client::Write(const void* src, size_t numBytes)
    {
        int32_t numWritten = send(m_socket, (const char*)src, numBytes, 0);

        // Otherwise, a value of SOCKET_ERROR is returned, which is checked with ASOCK_THROW_IF_FAILED macro
        ASOCK_THROW_IF_FAILED(numWritten);

        // here safe to assume non-negative number
        return static_cast<uint32_t>(numWritten);
    }

}
