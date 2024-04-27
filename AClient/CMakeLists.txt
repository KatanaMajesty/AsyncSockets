cmake_minimum_required(VERSION 3.28)
project(AsyncClient LANGUAGES CXX)

set(CLIENT_SRC_FILES
    "src/AsyncSock.cpp"
    "src/AsyncSock.h"
    "src/AsyncTask.cpp"   
    "src/AsyncTask.h"   
    "src/Main.cpp"
)

add_executable(AsyncClient)
target_sources(AsyncClient PRIVATE ${CLIENT_SRC_FILES})

target_link_libraries(AsyncClient 
PRIVATE
    "Ws2_32.lib"
)