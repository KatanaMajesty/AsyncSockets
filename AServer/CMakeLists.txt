cmake_minimum_required(VERSION 3.28)
project(AsyncServer LANGUAGES CXX)

set(SERVER_SRC_FILES
    "src/AsyncSock.cpp"
    "src/AsyncSock.h"
    "src/AsyncTask.cpp"
    "src/AsyncTask.h"
    "src/Main.cpp"
)

set(THREADPOOL_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../Threadpool/src"
)

set(THREADPOOL_SRC_FILES
    "${THREADPOOL_INCLUDE_DIRS}/ExecutionQueue.h"
    "${THREADPOOL_INCLUDE_DIRS}/ExecutionQueue.cpp"
    "${THREADPOOL_INCLUDE_DIRS}/RwLockSync.h"
    "${THREADPOOL_INCLUDE_DIRS}/ThreadPool.h"
    "${THREADPOOL_INCLUDE_DIRS}/ThreadPool.cpp"
    "${THREADPOOL_INCLUDE_DIRS}/Timings.h"
)

add_executable(AsyncServer)
target_sources(AsyncServer PRIVATE ${SERVER_SRC_FILES})
target_sources(AsyncServer PRIVATE ${THREADPOOL_SRC_FILES})

message(STATUS "CURRENT THREADPOOL INCLUDE DIRS ARE ${THREADPOOL_INCLUDE_DIRS}")
target_include_directories(AsyncServer PRIVATE ${THREADPOOL_INCLUDE_DIRS})

target_link_libraries(AsyncServer 
PRIVATE
    "Ws2_32.lib"
)