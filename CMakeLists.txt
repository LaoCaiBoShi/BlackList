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

add_executable(blacklist_checker
    src/main.cpp
    src/core/blacklist_checker.cpp
    src/core/blacklist_service.cpp
    src/core/persist_reader.cpp
    src/core/memory_pool.cpp
    src/core/thread_pool.cpp
    src/net/tcp_server.cpp
    src/zip/zip_basic.cpp
    src/zip/zip_utils.cpp
    src/zip/ZipExtractor.cpp
    include/simdjson/simdjson.cpp
    ${ZLIB_SOURCES}
    ${MINIZIP_SOURCES}
)

target_link_libraries(blacklist_checker PRIVATE
    D:/Coder/socket/coost-master/build/src/libco.a
)

target_include_directories(blacklist_checker PRIVATE
    include
    src
    src/core
    src/net
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

install(TARGETS blacklist_checker DESTINATION bin)
