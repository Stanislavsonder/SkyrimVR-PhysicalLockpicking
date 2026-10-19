# Debug-model generator (not part of the plugin build).
#   cmake -S tools/nifgen -B build-nifgen -DNIFLY_DIR=<nifly checkout> && cmake --build build-nifgen
#   build-nifgen/nifgen package/assets
cmake_minimum_required(VERSION 3.20)
project(nifgen CXX)
set(CMAKE_CXX_STANDARD 17)
set(NIFLY_DIR "" CACHE PATH "nifly source checkout (https://github.com/ousnius/nifly)")
add_subdirectory(${NIFLY_DIR} nifly EXCLUDE_FROM_ALL)
add_executable(nifgen nifgen.cpp)
target_link_libraries(nifgen PRIVATE nifly)
