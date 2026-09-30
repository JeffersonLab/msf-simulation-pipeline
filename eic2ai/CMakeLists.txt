# eic2ai — one program reads each event once and writes every CSV role.
#
# Build inside a container that ships podio, EDM4HEP and EDM4EIC (eic_xl, eic-full, eic-claude):
#     cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#     cmake --build build -j
#
# minizip-ng (the zip writer) is downloaded at configure time. Offline alternatives:
#     -DFETCHCONTENT_SOURCE_DIR_MINIZIP=/path/to/minizip-ng-4.2.2   an unpacked source tree
#     -DEIC2AI_FETCH_MINIZIP=OFF                                    a system minizip-ng (find_package)
cmake_minimum_required(VERSION 3.18)
project(eic2ai LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(EIC2AI_FETCH_MINIZIP "Download minizip-ng 4.2.2 with FetchContent; OFF uses find_package(minizip-ng)" ON)

find_package(podio CONFIG REQUIRED)
find_package(EDM4HEP CONFIG REQUIRED)
find_package(EDM4EIC CONFIG REQUIRED)
find_package(fmt REQUIRED)
find_package(ZLIB REQUIRED)

# ---- minizip-ng: streaming ZIP64 writer, deflate through the container's zlib -----------------
if(EIC2AI_FETCH_MINIZIP)
    include(FetchContent)
    # The sink needs deflate alone; every other minizip-ng feature and its dependency stays off.
    # MZ_ZLIB_FLAVOR=zlib: ROOT already links the container's libz; one zlib flavor per process
    # avoids symbol clashes with zlib-ng, which eic_xl also ships.
    set(MZ_COMPAT OFF CACHE BOOL "" FORCE)
    set(MZ_ZLIB ON CACHE BOOL "" FORCE)
    set(MZ_ZLIB_FLAVOR "zlib" CACHE STRING "" FORCE)
    set(MZ_BZIP2 OFF CACHE BOOL "" FORCE)
    set(MZ_LZMA OFF CACHE BOOL "" FORCE)
    set(MZ_PPMD OFF CACHE BOOL "" FORCE)
    set(MZ_ZSTD OFF CACHE BOOL "" FORCE)
    set(MZ_FETCH_LIBS OFF CACHE BOOL "" FORCE)
    set(MZ_FORCE_FETCH_LIBS OFF CACHE BOOL "" FORCE)
    set(MZ_PKCRYPT OFF CACHE BOOL "" FORCE)
    set(MZ_WZAES OFF CACHE BOOL "" FORCE)
    set(MZ_OPENSSL OFF CACHE BOOL "" FORCE)
    set(MZ_ICONV OFF CACHE BOOL "" FORCE)
    set(MZ_LIBBSD OFF CACHE BOOL "" FORCE)
    set(MZ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MZ_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(minizip
        URL      https://github.com/zlib-ng/minizip-ng/archive/refs/tags/4.2.2.tar.gz
        URL_HASH SHA256=71af7b9799856d8b03619df3949e9c1be9703f8de0795af71399ba283cb27aac)
    FetchContent_MakeAvailable(minizip)
    set(EIC2AI_MINIZIP_TARGET minizip-ng)
    # minizip-ng keeps its headers at the source root; this makes "mz_zip_rw.h" resolvable.
    set(EIC2AI_MINIZIP_INCLUDE "${minizip_SOURCE_DIR}")
else()
    find_package(minizip-ng CONFIG REQUIRED)
    set(EIC2AI_MINIZIP_TARGET MINIZIP::minizip-ng)
    set(EIC2AI_MINIZIP_INCLUDE "")
endif()

# ---- the program ------------------------------------------------------------------------------
add_executable(eic2ai
    src/main.cpp
    src/Options.cpp
    src/PairReader.cpp
    src/CsvSink.cpp
    src/Writer.cpp
    src/WritersRegistry.cpp)
target_include_directories(eic2ai PRIVATE src)
if(EIC2AI_MINIZIP_INCLUDE)
    target_include_directories(eic2ai PRIVATE "${EIC2AI_MINIZIP_INCLUDE}")
endif()
target_compile_options(eic2ai PRIVATE -Wall -Wextra)
# The link set is the one csv_convert/CMakeLists.txt uses: the *Dict targets carry the ROOT
# dictionaries the readers need at run time.
target_link_libraries(eic2ai PRIVATE
    podio::podio podio::podioRootIO podio::podioDict
    EDM4HEP::edm4hep EDM4HEP::edm4hepDict
    EDM4EIC::edm4eic EDM4EIC::edm4eicDict
    fmt::fmt
    ZLIB::ZLIB
    ${EIC2AI_MINIZIP_TARGET})
