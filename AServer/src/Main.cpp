#include <cstdint>
#include <cassert>
#include <span>
#include "AsyncTask.h"

// task -> fill square matrix with random values
// on the main diagonal set the maximum element of the column

namespace Math
{
    template<typename T>
    struct MatrixView
    {
        using ValueType = T;

        MatrixView() = default;
        MatrixView(T* begin, T* end, size_t numCols, size_t numRows)
            : View(begin, end)
            , NumCols(numCols)
            , NumRows(numRows)
        {
            assert(View.size() == (numCols * numRows));
        }

        T& At(size_t col, size_t row) { return View[col * NumRows + row]; }
        const T& At(size_t col, size_t row) const { return View[col * NumRows + row]; }

        std::span<T> View;
        const size_t NumCols = 0;
        const size_t NumRows = 0;
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

    using MatrixViewType = Math::MatrixView<uint32_t>;

    struct MatrixTaskHeader
    {
        size_t NumCols;
        size_t NumRows;
    };

    AsyncTask::ServerTaskHandler<MatrixTaskHeader> serverTaskHandler;
    serverTaskHandler.Init(std::thread::hardware_concurrency(), 
        [](MatrixTaskHeader header, AsyncTask::TaskContext* taskContext, size_t chunkID)
        {
            const AsyncSock::ISocketCommunicator::AddressInfo& addressInfo = taskContext->Client->GetAddressInfo();
            AsyncTask::ExecutionChunk* executionChunk = taskContext->GetExecutionChunk(chunkID);
            ASOCK_LOG("[Chunk{}:{}] -> Starting chunk for client\n", chunkID, addressInfo.Port);
            ASOCK_LOG("[Chunk{}:{}] -> Working on a chunk for {}x{} matrix\n", chunkID, addressInfo.Port, header.NumCols, header.NumRows);
            ASOCK_LOG("[Chunk{}:{}] -> Received {} bytes in total for current chunk\n", chunkID, addressInfo.Port, executionChunk->Chunk.NumBytes);
            
            // we should always block chunk mutex to synchronize with task handler's packet handling
            std::unique_lock _(executionChunk->ChunkMutex);

            const AsyncTask::BodyChunk& chunk = executionChunk->Chunk;

            // now we can start reinterpreting bytes and begin calculations
            static constexpr size_t MatrixElementStride = sizeof(MatrixViewType::ValueType);
            const size_t chunkNumElements = chunk.NumBytes / MatrixElementStride;
            const size_t chunkNumColumns  = chunkNumElements / header.NumRows;
            // local sanity checks
            assert(chunk.NumBytes % MatrixElementStride == 0);
            assert(chunkNumElements % header.NumRows == 0);

            // calculate column offset of the current chunk respectively to the whole matrix body
            const size_t taskElementOffset  = chunk.BufferOffsetInBytes / MatrixElementStride;
            const size_t taskColumnOffset   = taskElementOffset / header.NumRows;
            // task sanity-checks
            assert(chunk.BufferOffsetInBytes % MatrixElementStride == 0);
            assert(taskElementOffset % header.NumRows == 0);

            MatrixViewType::ValueType* begin  = (MatrixViewType::ValueType*)(taskContext->Body.data());
            MatrixViewType::ValueType* end    = (MatrixViewType::ValueType*)(taskContext->Body.data() + taskContext->Body.size());
            MatrixViewType matrixView(begin, end, header.NumCols, header.NumRows);
        
            for (size_t localColIdx = 0; localColIdx < chunkNumColumns; ++localColIdx)
            {
                const size_t columnIndex = localColIdx + taskColumnOffset;

                MatrixViewType::ValueType localMax = std::numeric_limits<MatrixViewType::ValueType>::min();
                for (std::size_t rowIndex = 0; rowIndex < matrixView.NumRows; ++rowIndex)
                {
                    localMax = std::max(localMax, matrixView.At(columnIndex, rowIndex));
                }
                matrixView.At(columnIndex, columnIndex) = localMax;
            }

            // mark chunk as done
            ASOCK_LOG("[Chunk{}:{}] -> Done!\n", chunkID, addressInfo.Port);
            executionChunk->State = AsyncTask::EChunkState::Done;
        });
    serverTaskHandler.Wait();

    AsyncSock::Cleanup();
}