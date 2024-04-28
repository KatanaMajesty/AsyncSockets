#include <cstdint>

#include <vector>
#include "AsyncTask.h"
#include "AsyncSock.h"

namespace Math
{
    template<typename T>
    struct Matrix
    {
        using ValueType = T;

        Matrix() = default;
        Matrix(size_t dimensions)
            : Elements(dimensions * dimensions)
            , NumCols(dimensions)
            , NumRows(dimensions)
        {
        }

        T& At(size_t col, size_t row) { return Elements.at(col * NumRows + row); }
        const T& At(size_t col, size_t row) const { return Elements.at(col * NumRows + row); }

        T* GetColumn(size_t col) { return Elements.data() + (col * NumRows); }

        std::vector<T> Elements;
        size_t NumCols = 0;
        size_t NumRows = 0;
    };
}

void Communicate()
{
    ASOCK_LOG("Press any button to start server connection\n");
    getchar();

    using MatrixType = Math::Matrix<uint32_t>;
    MatrixType matrixResult = MatrixType(16); // 4x4 matrix

    struct MatrixTaskHeader
    {
        size_t NumCols;
        size_t NumRows;
    };

    AsyncTask::ClientTaskHandler clientTaskHandler;
    if (clientTaskHandler.Connect(AsyncSock::ConnectionInfo{}) != AsyncTask::EConnectionResult::Ok)
    {
        ASOCK_LOG("Failed to connect client task handler");
        return;
    }

    AsyncTask::ClientTaskHandler::BodyPartition bodyPartition = {};
    static constexpr size_t numChunks = 4; // we want 4 chunks per task
    const size_t numWorkunitsPerChunk = matrixResult.NumCols / numChunks;
    const size_t numLeftoverWorkunits = matrixResult.NumCols % numChunks;

    // here we partition our matrix data into chunks for server to compute in parallel
    // this can be totally avoided by just submitting one chunk of matrix data to the server - would make no difference
    for (size_t chunkIdx = 0; chunkIdx < numChunks; ++chunkIdx)
    {
        static constexpr size_t MatrixElementStride = sizeof(MatrixType::ValueType);

        // each column has NumRows elements and each of them is MatrixElementStride bytes
        // thus each column is NumRows * MatrixElementStride bytes
        size_t firstColumnIdx = numWorkunitsPerChunk * (chunkIdx);
        size_t lastColumnIdx  = numWorkunitsPerChunk * (chunkIdx + 1);
        
        // If last chunk is to be written - add leftover work to it
        if (chunkIdx == numChunks - 1)
        {
            lastColumnIdx += numLeftoverWorkunits;
        }

        const size_t bytesToWrite = (lastColumnIdx - firstColumnIdx) * matrixResult.NumRows * sizeof(MatrixType::ValueType);
        bodyPartition.push_back(AsyncTask::ClientTaskHandler::BodyChunk{
            .NumBytes = static_cast<uint32_t>(bytesToWrite),
            .Buffer = (AsyncTask::ClientTaskHandler::BodyChunk::ByteBuffer)matrixResult.GetColumn(firstColumnIdx),
        });
    }

    MatrixTaskHeader header = MatrixTaskHeader{
        .NumCols = matrixResult.NumCols,
        .NumRows = matrixResult.NumRows,
    };
    clientTaskHandler.SubmitTask(header, bodyPartition);
    
    // Not the best and safest way to wait for task to be finished
    // simply just loop while task is in progress or in unknown state (state of submission)
    std::vector<uint8_t> bytes;
    AsyncTask::ETaskStatus status = AsyncTask::ETaskStatus::Unknown;
    do
    {
        status = clientTaskHandler.GetTaskResult(bytes);
        if (status == AsyncTask::ETaskStatus::Failed)
        {
            ASOCK_LOG("Failed to execute task on server!\n");
            break;
        }

        ASOCK_LOG("Task is still in progress! Waiting a bit...\n");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    while (status == AsyncTask::ETaskStatus::InProgress || status == AsyncTask::ETaskStatus::Unknown);

    if (status == AsyncTask::ETaskStatus::Finished)
    {
        ASOCK_LOG("Task is successfully finished on the server!\n");
        ASOCK_LOG("Received {} bytes from the server!\n", bytes.size());
    }
    else
    {
        ASOCK_LOG("Failed to execute task on server!\n");
    }

    ASOCK_LOG("Press any button to finish client execution!\n");
    getchar();
}

int32_t main()
{
    if (!AsyncSock::Initialize())
    {
        ASOCK_LOG("Failed to initialize sock!\n");
        return -1;
    }

    Communicate();
    AsyncSock::Cleanup();
}