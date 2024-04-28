#include <cstdint>

#include "AsyncTask.h"

// task -> fill square matrix with random values
// on the main diagonal set the maximum element of the column

namespace Math
{
    template<typename T>
    struct Matrix
    {
        using ValueType = T;

        Matrix() = default;
        Matrix(size_t dimensions)
            : Elements(dimensions* dimensions)
            , NumCols(dimensions)
            , NumRows(dimensions)
        {
        }

        T& At(size_t col, size_t row) { return Elements.at(row * NumCols + col); }
        const T& At(size_t col, size_t row) const { return Elements.at(row * NumCols + col); }

        std::vector<T> Elements;
        size_t NumCols = 0;
        size_t NumRows = 0;
    };
}

int32_t main()
{
    if (!AsyncSock::Initialize())
    {
        ASOCK_LOG("Failed to initialize sock!\n");
        return -1;
    }

    ASOCK_LOG("Press any button to startup a server\n");
    getchar();

    using MatrixType = Math::Matrix<uint32_t>;

    struct MatrixTaskHeader
    {
        size_t NumCols;
        size_t NumRows;
    };

    AsyncTask::ServerTaskHandler<MatrixTaskHeader> serverTaskHandler;
    serverTaskHandler.Init(std::thread::hardware_concurrency(), [](MatrixTaskHeader header, AsyncTask::TaskChunkContext* chunkContext)
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            // no need to use mutex as we are use that we will not be accessing the same data from multiple worker threads
            // as well as we won't be changing amount of bytes in array from other threads
            const AsyncSock::ISocketCommunicator::AddressInfo& addressInfo = chunkContext->Client->GetAddressInfo();
            ASOCK_LOG("[Chunk{}] -> Starting chunk for client with address {}:{}\n", chunkContext->ChunkID, addressInfo.IPv4, addressInfo.Port);
            ASOCK_LOG("[Chunk{}] -> Working on a chunk for {}x{} matrix\n", chunkContext->ChunkID, header.NumCols, header.NumRows);
            ASOCK_LOG("[Chunk{}] -> Received {} bytes in total for current chunk\n", chunkContext->ChunkID, chunkContext->Body.size());
        });
    serverTaskHandler.Wait();

    AsyncSock::Cleanup();
}