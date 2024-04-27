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
    serverTaskHandler.Init(std::thread::hardware_concurrency());
    serverTaskHandler.Wait();

    AsyncSock::Cleanup();
}