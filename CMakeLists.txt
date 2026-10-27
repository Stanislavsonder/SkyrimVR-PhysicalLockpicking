cmake_minimum_required(VERSION 3.24)

project(VRLockpicking VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VRLP_BUILD_PLUGIN "Build the SKSE VR plugin DLL (Windows / clang-cl cross only)" ${WIN32})
option(VRLP_BUILD_TESTS "Build native unit tests for the lockpicking core" ON)

# ---------------------------------------------------------------------------
# Game independent core (also used by the tests)
# ---------------------------------------------------------------------------
add_library(vrlp_core STATIC
	src/core/Config.cpp
	src/core/LockSession.cpp
	src/core/MeshFit.cpp
	src/core/LockProfile.cpp
	src/core/LockVariations.cpp
	src/core/Translations.cpp
)
target_include_directories(vrlp_core PUBLIC src/core)
target_compile_features(vrlp_core PUBLIC cxx_std_20)

if(VRLP_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
	enable_testing()
	add_executable(vrlp_tests tests/test_core.cpp)
	target_link_libraries(vrlp_tests PRIVATE vrlp_core)
	target_compile_definitions(vrlp_tests PRIVATE VRLP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
	add_test(NAME vrlp_tests COMMAND vrlp_tests)
endif()

if(NOT VRLP_BUILD_PLUGIN)
	return()
endif()

# ---------------------------------------------------------------------------
# SKSE VR plugin
# ---------------------------------------------------------------------------
set(ENABLE_SKYRIM_SE OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COMMONLIB_ENABLE_IPO OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibVR EXCLUDE_FROM_ALL)

add_library(VRLockpicking SHARED
	src/plugin/main.cpp
	src/plugin/AddressCheck.cpp
	src/plugin/LockpickManager.cpp
	src/plugin/VRInput.cpp
	src/plugin/Visuals.cpp
	src/plugin/DebugView.cpp
	src/plugin/Markup.cpp
	src/plugin/TestLocks.cpp
	src/plugin/DebugCheats.cpp
	src/plugin/FrameHook.cpp
	src/plugin/AssistRing.cpp
	src/plugin/PickCounter.cpp
	src/plugin/LockMods.cpp
	src/plugin/Text.cpp
	src/plugin/version.rc
)
target_include_directories(VRLockpicking PRIVATE src/plugin)
target_link_libraries(VRLockpicking PRIVATE vrlp_core CommonLibSSE::CommonLibSSE)
target_precompile_headers(VRLockpicking PRIVATE src/plugin/PCH.h)
set_target_properties(VRLockpicking PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
set_target_properties(vrlp_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

if(MSVC)
	target_compile_options(VRLockpicking PRIVATE /utf-8 /permissive- /Zc:preprocessor)
	target_link_options(VRLockpicking PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
endif()
