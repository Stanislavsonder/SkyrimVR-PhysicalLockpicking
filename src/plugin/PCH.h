#pragma once

#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <openvr.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <set>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>


// <windows.h> may sneak in through third-party headers
#undef PlaySound
#undef min
#undef max

namespace logger = SKSE::log;

using namespace std::literals;
