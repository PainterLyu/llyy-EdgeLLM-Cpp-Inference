cmake_minimum_required(VERSION 3.12)
project(llama_server)

# 打印项目信息
message(STATUS "Project name: ${PROJECT_NAME}")
message(STATUS "Project source dir: ${PROJECT_SOURCE_DIR}")

# 设置编译类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找线程库
find_package(Threads REQUIRED)

# 检查目录结构
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp/CMakeLists.txt")
    message(FATAL_ERROR "llama.cpp/CMakeLists.txt not found")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cJSON/cJSON.h")
    message(FATAL_ERROR "cJSON.h not found in ${CMAKE_CURRENT_SOURCE_DIR}/cJSON")
endif()

# 查找CUDA
find_package(CUDA QUIET)
if(CUDA_FOUND)
    message(STATUS "CUDA found - enabling GPU support")
    add_definitions(-DHAVE_CUDA)
    
    # TX2 使用 sm_62 架构
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -arch=sm_62")
    else()
        # 其他 NVIDIA GPU 使用更通用的设置
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -arch=sm_50")
    endif()
    
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGGML_USE_CUDA")
else()
    message(WARNING "CUDA not found - falling back to CPU only mode")
endif()

# 添加cJSON库
add_library(cjson STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/cJSON/cJSON.c
)
target_include_directories(cjson PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/cJSON
)

# 设置llama.cpp的编译选项
set(LLAMA_BUILD_COMMON ON CACHE BOOL "Build common library" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "Build server" FORCE)
set(LLAMA_STANDALONE OFF CACHE BOOL "Standalone build" FORCE)

# 添加llama.cpp作为子项目
add_subdirectory(llama.cpp)

# 确认common库已经被编译
if(NOT TARGET common)
    message(FATAL_ERROR "common library was not built. Check LLAMA_BUILD_COMMON setting.")
endif()

# 设置包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp/common
    ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp/ggml/include
    ${CMAKE_CURRENT_SOURCE_DIR}/cJSON
)

if(CUDA_FOUND)
    include_directories(${CUDA_INCLUDE_DIRS})
endif()

# 设置可执行文件输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 添加服务器可执行文件
add_executable(server 
    final-server.cpp
)

# 链接必要的库
target_link_libraries(server PRIVATE
    llama
    common
    ${CMAKE_THREAD_LIBS_INIT}
    cjson
)

# 如果有CUDA，添加CUDA相关库
if(CUDA_FOUND)
    target_link_libraries(server PRIVATE
        ${CUDA_LIBRARIES}
        cuda
        cudart
    )
endif()

# 打印配置信息
message(STATUS "Configuration summary:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  CUDA support: ${CUDA_FOUND}")
if(CUDA_FOUND)
    message(STATUS "  CUDA version: ${CUDA_VERSION_STRING}")
    message(STATUS "  CUDA architecture: ${CMAKE_CUDA_FLAGS}")
endif()
message(STATUS "  System processor: ${CMAKE_SYSTEM_PROCESSOR}")
