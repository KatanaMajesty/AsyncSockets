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
            const AsyncSock::ISocketCommunicator::AddressInfo& addressInfo = chunkContext->Client->GetAddressInfo();
            ASOCK_LOG("[Chunk{}:{}] -> Starting chunk for client\n", chunkContext->ChunkID, addressInfo.Port);
            ASOCK_LOG("[Chunk{}:{}] -> Working on a chunk for {}x{} matrix\n", chunkContext->ChunkID, addressInfo.Port, header.NumCols, header.NumRows);
            ASOCK_LOG("[Chunk{}:{}] -> Received {} bytes in total for current chunk\n", chunkContext->ChunkID, addressInfo.Port, chunkContext->Body.size());

            // we need to lock mutex in order to be sure that the client request to receive data would not copy bytes while we write
            std::unique_lock _(chunkContext->BodyMutex);

            // now we can start reinterpreting bytes and begin calculations
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // mark chunk as done
            ASOCK_LOG("[Chunk{}:{}] -> Done!\n", chunkContext->ChunkID, addressInfo.Port);
            chunkContext->State = AsyncTask::EChunkState::Done;
        });
    serverTaskHandler.Wait();

    AsyncSock::Cleanup();
}