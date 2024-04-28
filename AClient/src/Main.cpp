#include <cstdint>

#include <vector>
#include <random>
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

using MatrixType = Math::Matrix<uint32_t>;
using MatrixWorkloadCallback = void(*)(MatrixType&, std::size_t, std::size_t);

static const uint32_t NumConcurrentThreads = std::thread::hardware_concurrency();

void WorkloadDistributor(MatrixWorkloadCallback callback, std::vector<std::jthread>& threadPool, MatrixType& matrix)
{
    size_t numThreads = threadPool.size();
    const size_t numThreadWorkUnits = matrix.NumCols / numThreads;
    const size_t leftoverWorkUnits = matrix.NumCols % numThreads;

    for (std::size_t threadIdx = 0; threadIdx < numThreads; ++threadIdx)
    {
        std::size_t FirstColumnIdx = numThreadWorkUnits * (threadIdx);
        std::size_t LastColumnIdx = numThreadWorkUnits * (threadIdx + 1);
        // If last thread is to be launched - add leftover work to it
        if (threadIdx == numThreads - 1)
        {
            LastColumnIdx += leftoverWorkUnits;
        }

        threadPool[threadIdx] = std::jthread(callback, std::ref(matrix), FirstColumnIdx, LastColumnIdx);
    }
}

void Matrix_FillRandomly(MatrixType& matrix)
{
    static auto RandomGenerator = [](MatrixType& Matrix, std::size_t FirstColumnIdx, std::size_t LastColumnIdx)
        {
            static thread_local std::default_random_engine RandomEngine(std::random_device{}());
            static thread_local std::uniform_int_distribution<uint32_t> UniformDistributor(1, 16384);
            for (std::size_t col = FirstColumnIdx; col < LastColumnIdx; ++col)
            {
                for (std::size_t row = 0; row < Matrix.NumRows; ++row)
                {
                    Matrix.At(col, row) = UniformDistributor(RandomEngine);
                }
            }
        };
    std::vector<std::jthread> ThreadPool(NumConcurrentThreads);

    WorkloadDistributor(RandomGenerator, std::ref(ThreadPool), std::ref(matrix));
    ThreadPool.clear();
}

void Communicate()
{
    ASOCK_LOG("Press any button to start server connection\n");
    getchar();

    MatrixType matrixResult = MatrixType(6); // 4x4 matrix
    Matrix_FillRandomly(matrixResult);

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

    // create a span for body to upload
    const uint8_t* matrixBegin = (const uint8_t*)(matrixResult.Elements.data());
    const uint8_t* matrixEnd   = (const uint8_t*)(matrixResult.Elements.data() + matrixResult.Elements.size());
    bodyPartition.Body = std::span(matrixBegin, matrixEnd);

    // describe chunk partitioning
    static constexpr size_t numChunks = 1; // we want 4 chunks per task
    static constexpr size_t MatrixElementStride = sizeof(MatrixType::ValueType);
    const size_t numBytesPerChunk = (matrixResult.NumCols / numChunks) * matrixResult.NumRows * MatrixElementStride;
    const size_t numLeftoverBytes = (matrixResult.NumCols % numChunks) * matrixResult.NumRows * MatrixElementStride;

    // here we partition our matrix data into chunks for server to compute in parallel
    // this can be totally avoided by just submitting one chunk of matrix data to the server - would make no difference
    for (size_t chunkIdx = 0; chunkIdx < numChunks; ++chunkIdx)
    {
        // each column has NumRows elements and each of them is MatrixElementStride bytes
        // thus each column is NumRows * MatrixElementStride bytes
        size_t beginInBytes = numBytesPerChunk * (chunkIdx);
        size_t endInBytes   = numBytesPerChunk * (chunkIdx + 1);
        
        // If last chunk is to be written - add leftover work to it
        if (chunkIdx == numChunks - 1)
            endInBytes += numLeftoverBytes;

        bodyPartition.ExecutionChunks.push_back(AsyncTask::ClientTaskHandler::BodyChunk{
            .NumBytes = (endInBytes - beginInBytes),
            .BufferOffsetInBytes = beginInBytes,
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

        // hardcode, but an easy workaround to copy data
        MatrixType::ValueType* data = (MatrixType::ValueType*)bytes.data();
        std::copy(data, data + (matrixResult.NumRows * matrixResult.NumCols), matrixResult.Elements.data());

        /*ASOCK_LOG("\n\n[[AFTER]] PRINTMATRIX!:\n");
        for (size_t col = 0; col < matrixResult.NumCols; ++col)
        {
            ASOCK_LOG("( ");
            for (size_t row = 0; row < matrixResult.NumRows; ++row)
            {
                ASOCK_LOG("{} ", matrixResult.At(col, row));
            }
            ASOCK_LOG(" )\n");
        }*/
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