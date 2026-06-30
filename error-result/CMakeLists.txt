cmake_minimum_required(VERSION 3.18.0)
project(pulley-runner)

# C++17 (matches wasmline)
set(CMAKE_CXX_STANDARD 17)

# Source
add_executable(pulley-runner pulley-runner.cpp)

# Wasmtime headers
include_directories(${WASMTIME_CAPI_DIR}/include)

# Link wasmtime static library
set(WASMTIME_LIB "${WASMTIME_CAPI_DIR}/lib/libwasmtime.a")

target_link_libraries(pulley-runner
    -Wl,--whole-archive
    ${WASMTIME_LIB}
    -Wl,--no-whole-archive
    m
    dl
)
