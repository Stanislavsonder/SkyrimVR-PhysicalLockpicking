# Lines up the lock on door / chest models outside the game (not part of the plugin build).
#   cmake -S tools/lockplacer -B build-lockplacer -DNIFLY_DIR=<nifly checkout> && cmake --build build-lockplacer
cmake_minimum_required(VERSION 3.20)
project(lockplacer CXX)
set(CMAKE_CXX_STANDARD 20)
set(NIFLY_DIR "" CACHE PATH "nifly source checkout (https://github.com/ousnius/nifly)")
# one exe, no runtime DLLs to install
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_subdirectory(${NIFLY_DIR} nifly EXCLUDE_FROM_ALL)
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/core)
add_executable(lockplacer lockplacer.cpp ${CORE}/LockProfile.cpp ${CORE}/Config.cpp ${CORE}/MeshFit.cpp)
target_include_directories(lockplacer PRIVATE ${CORE})
target_link_libraries(lockplacer PRIVATE nifly)
if(MSVC)
	target_compile_options(lockplacer PRIVATE /utf-8)
endif()
