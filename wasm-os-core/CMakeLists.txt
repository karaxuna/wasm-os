# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Read version from the monorepo root package.json
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../package.json" PACKAGE_JSON)
string(JSON FIRMWARE_VERSION GET ${PACKAGE_JSON} "version")
message(STATUS "Firmware version: ${FIRMWARE_VERSION}")

add_compile_options(-Wno-error=missing-braces)
set(WAMR_BUILD_SHARED_HEAP 1)
add_definitions(
    -DCONFIG_IPV6
    -DESPIDF_DISABLE_PTHREAD
    -DWASM_ENABLE_MULTI_MODULE=1
    -DWASM_ENABLE_LIBC_BUILTIN=1
    -DWASM_ENABLE_BASE_LIB=0
    -DWASM_ENABLE_LIBC_EMCC=0
    -DWASM_ENABLE_LIB_RATS=0
    -DWASM_ENABLE_WASI_NN=0
    -DWASM_ENABLE_APP_FRAMEWORK=0
    -DWASM_ENABLE_LIBC_WASI=1
    -DWASM_ENABLE_LIB_PTHREAD=0
    -DWAMR_BUILD_THREAD_MGR=0
    -DWASM_ENABLE_LIB_WASI_THREADS=0
    -DWASM_ENABLE_WASI_EPHEMERAL_NN=0
    -DWASM_ENABLE_SHRUNK_MEMORY=1
    -DWASM_ENABLE_SHARED_HEAP=1
    -DFIRMWARE_VERSION="${FIRMWARE_VERSION}"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wasm-os)
