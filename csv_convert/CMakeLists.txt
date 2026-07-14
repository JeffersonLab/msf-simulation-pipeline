# -----------------------------------------------------------------------------
#  csv_convert — EIC edm4hep / edm4eic  ->  CSV converters
#
#  Every *.cxx here is self-contained: it has an int main(), so it builds as a
#  standalone executable AND still runs as a ROOT macro, e.g.
#      root -x -l -b -q 'edm4eic_trk_hits.cxx("in.edm4eic.root","out.csv")'
#
#  This CMakeLists exists so the converters can be opened in an IDE
#  (CLion / VS Code), get full code-completion against ROOT/podio/EDM4*,
#  and be compiled & debugged as real executables instead of only being
#  interpreted by ROOT with `-q`.
#
#  Build inside the eic_xl container (where ROOT/podio/EDM4HEP/EDM4EIC live):
#      cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#      cmake --build build -j
#      ./build/edm4eic_trk_hits input.edm4eic.root output.csv
#
#  One executable is produced per converter, named after the file stem
#  (edm4eic_trk_hits.cxx -> ./build/edm4eic_trk_hits).
# -----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.18)
project(csv_convert LANGUAGES CXX)

# ----- compiler settings ------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to an optimized build when the user/IDE didn't pick one.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# -----------------------------------------------------------------------------#
#  Dependencies (provided by the eic_xl container)                             #
# -----------------------------------------------------------------------------#
find_package(ROOT REQUIRED COMPONENTS Core RIO Tree Hist Gpad Graf Physics)
find_package(podio CONFIG REQUIRED)
find_package(EDM4HEP CONFIG REQUIRED)
find_package(EDM4EIC CONFIG REQUIRED)
find_package(fmt REQUIRED)

# -----------------------------------------------------------------------------#
#  Build one executable per converter                                          #
#                                                                              #
#  CONFIGURE_DEPENDS re-globs on each build, so dropping a new <name>.cxx in   #
#  this directory and rebuilding creates a matching <name> target — no need to #
#  edit this file when adding a converter.                                     #
# -----------------------------------------------------------------------------#
file(GLOB CONVERTER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx")

foreach(src ${CONVERTER_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name}
            PRIVATE
            ROOT::Core ROOT::RIO ROOT::Tree ROOT::Hist ROOT::Gpad ROOT::Graf ROOT::Physics
            podio::podio podio::podioRootIO podio::podioDict
            EDM4HEP::edm4hep EDM4HEP::edm4hepDict
            EDM4EIC::edm4eic EDM4EIC::edm4eicDict
            fmt::fmt)
endforeach()
