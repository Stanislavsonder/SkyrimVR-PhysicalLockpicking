#include "AddressCheck.h"

#include <unordered_set>

namespace vrlp::AddressCheck
{
	namespace
	{
		std::unordered_set<std::uint64_t> g_ids;
		bool                              g_loaded{ false };
		std::string                       g_version{ "unknown" };

		bool IsHexOffset(std::string_view a_s)
		{
			if (a_s.starts_with("0x") || a_s.starts_with("0X")) {
				a_s.remove_prefix(2);
			}
			if (a_s.empty()) {
				return false;
			}
			for (char c : a_s) {
				if (!std::isxdigit(static_cast<unsigned char>(c))) {
					return false;
				}
			}
			return true;
		}
	}

	void Load()
	{
		// SKSEVR's runtime is 1.4.15; CommonLib loads exactly this file.
		constexpr const char* kPath = "Data\\SKSE\\Plugins\\version-1-4-15-0.csv";
		std::ifstream         file(kPath);
		if (!file) {
			logger::warn("Could not open {} to verify address IDs; assuming they are all present", kPath);
			return;
		}

		std::string line;
		int         row = 0;
		while (std::getline(file, line)) {
			++row;
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			const auto comma = line.find(',');
			if (comma == std::string::npos) {
				continue;
			}
			const std::string first = line.substr(0, comma);
			std::string       second = line.substr(comma + 1);
			if (const auto c2 = second.find(','); c2 != std::string::npos) {
				second.resize(c2);
			}
			if (row == 2) {
				g_version = second;  // meta row: <count>,<library version>
				continue;
			}
			if (!IsHexOffset(second)) {
				continue;
			}
			char*      end = nullptr;
			const auto id = std::strtoull(first.c_str(), &end, 10);
			if (end != first.c_str()) {
				g_ids.insert(id);
			}
		}
		g_loaded = !g_ids.empty();
		logger::info("VR Address Library {}: {} IDs", g_version, g_ids.size());

		struct Group
		{
			const char*                           name;
			std::initializer_list<std::uint64_t> ids;
			const char*                           fallback;  // what is used instead, or nullptr: disabled
		};
		const Group groups[] = {
			{ "core", Ids::kCore, nullptr },
			{ "3D meshes", Ids::kMeshes, nullptr },
			{ "perk entry points", Ids::kEntryPoints, nullptr },
			{ "crime", Ids::kCrime, nullptr },
			{ "Locks Picked stat", Ids::kLocksPickedStat, "counted through Papyrus instead" },
			{ "AddLockChange", Ids::kAddLockChange, "called at its known Skyrim VR address instead" },
			{ "activate container", Ids::kActivate, nullptr },
			{ "Skeleton Key", Ids::kDefaultObjects, "only recognised when you have no lockpicks" },
		};
		for (const auto& [name, ids, fallback] : groups) {
			for (const auto id : ids) {
				if (g_loaded && !g_ids.contains(id)) {
					if (fallback) {
						logger::info("Address library is missing ID {} ({}): {}.", id, name, fallback);
					} else {
						logger::warn("Address library is missing ID {} ({}): that feature is disabled. Updating VR Address Library may enable it.", id, name);
					}
				}
			}
		}
	}

	bool Has(std::initializer_list<std::uint64_t> a_ids)
	{
		if (!g_loaded) {
			return true;
		}
		for (const auto id : a_ids) {
			if (!g_ids.contains(id)) {
				return false;
			}
		}
		return true;
	}

	const std::string& LibraryVersion() { return g_version; }
}
