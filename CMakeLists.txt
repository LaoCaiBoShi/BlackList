cmake_minimum_required(VERSION 3.16)
project(blacklist_checker)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /EHsc /MT")
    add_compile_options(/wd4090)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
endif()

if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

set(ZLIB_SOURCES
    src/zlib/adler32.c
    src/zlib/compress.c
    src/zlib/crc32.c
    src/zlib/deflate.c
    src/zlib/gzclose.c
    src/zlib/gzlib.c
    src/zlib/gzread.c
    src/zlib/gzwrite.c
    src/zlib/infback.c
    src/zlib/inffast.c
    src/zlib/inflate.c
    src/zlib/inftrees.c
    src/zlib/trees.c
    src/zlib/uncompr.c
    src/zlib/zutil.c
)

set(MINIZIP_SOURCES
    src/minizip/unzip.c
    src/minizip/zip.c
    src/minizip/ioapi.c
    src/minizip/iowin32.c
)

set(BLACKLIST_SOURCES
    src/main.cpp
    src/core/blacklist_checker.cpp
    src/core/blacklist_service.cpp
    src/core/memory_pool.cpp
    src/core/thread_pool.cpp
    src/core/system_utils.cpp
    src/core/log_manager.cpp
    src/shared_bloom_filter.cpp
    src/zip/zip_basic.cpp
    src/zip/zip_utils.cpp
    src/zip/ZipExtractor.cpp
    include/simdjson/simdjson.cpp
    ${ZLIB_SOURCES}
    ${MINIZIP_SOURCES}
)

add_executable(blacklist_checker ${BLACKLIST_SOURCES})

target_include_directories(blacklist_checker PRIVATE
    include
    src
    src/core
    src/zip
    src/zlib
    src/minizip
    coost-master/include
    include/simdjson
)

if(WIN32)
    target_link_libraries(blacklist_checker PRIVATE
        ws2_32
    )
endif()

# 设置输出目录为项目文件夹的上一级的 BlackList 文件夹
set(OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../BlackList")

set_target_properties(blacklist_checker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
)

# 测试程序
add_executable(test_thread_config
    test/test_thread_config.cpp
    src/core/system_utils.cpp
)

target_include_directories(test_thread_config PRIVATE
    include
    src
    src/core
    src/zip
    src/zlib
    src/minizip
    coost-master/include
    include/simdjson
)

if(WIN32)
    target_link_libraries(test_thread_config PRIVATE
        ws2_32
    )
endif()

set_target_properties(test_thread_config PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
)
