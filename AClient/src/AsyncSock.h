#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>

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
            ASOCK_LOG("\'{}\' Failed with iResult {}\n", ##expr, WSAGetLastError()); \
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

    using Socket_t = SOCKET;
    static constexpr Socket_t InvalidSocket = INVALID_SOCKET;

    bool Initialize();
    void Cleanup();
}

namespace AsyncSock
{

    struct ConnectionInfo
    {
        std::string_view AddressIPv4 = AsyncSock::DefaultIPv4;
        std::string_view AddressPort = AsyncSock::DefaultPort;
    };

    class Client
    {
    public:
        Client();
        ~Client();

        bool Connect(const ConnectionInfo& connectionInfo);
        bool IsConnected() const noexcept { return m_socket != AsyncSock::InvalidSocket; }

        bool Disconnect();

        uint32_t Read(void* dst, size_t numBytes);
        uint32_t Write(const void* src, size_t numBytes);

        template<typename T> uint32_t Read(T& t) { return Read(&t, sizeof(T)); }
        template<typename T> uint32_t Write(T&& t) { return Write(&t, sizeof(T)); }

    private:
        Socket_t m_socket = AsyncSock::InvalidSocket;
    };

}