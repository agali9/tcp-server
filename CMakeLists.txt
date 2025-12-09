cmake_minimum_required(VERSION 3.16)
project(tcp_data_server VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TDS_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(TDS_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(TDS_BUILD_TOOLS "Build benchmark and client tools" ON)

if(TDS_ENABLE_ASAN AND TDS_ENABLE_TSAN)
    message(FATAL_ERROR "Cannot enable ASan and TSan simultaneously")
endif()

add_library(tds_core
    src/protocol.cpp
    src/metrics.cpp
    src/connection.cpp
    src/server.cpp
    src/kv_store.cpp
)

target_include_directories(tds_core PUBLIC include)
target_compile_options(tds_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(tds_core PUBLIC pthread)

if(TDS_ENABLE_ASAN)
    target_compile_options(tds_core PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(tds_core PUBLIC -fsanitize=address)
endif()

if(TDS_ENABLE_TSAN)
    target_compile_options(tds_core PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(tds_core PUBLIC -fsanitize=thread)
endif()

add_executable(tds_server src/main.cpp)
target_link_libraries(tds_server PRIVATE tds_core)

if(TDS_BUILD_TOOLS)
    add_executable(tds_bench tools/benchmark_client.cpp)
    target_link_libraries(tds_bench PRIVATE tds_core)

    add_executable(tds_client tools/simple_client.cpp)
    target_link_libraries(tds_client PRIVATE tds_core)
endif()


