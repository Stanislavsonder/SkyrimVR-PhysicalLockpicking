#include "LockableObjects.h"

#include "PluginScan.h"

namespace vrlp::LockableObjects
{
	namespace
	{
		std::atomic<bool>                      g_ready{ false };
		std::unordered_map<RE::FormID, int>    g_counts;  // written before g_ready, read after

		void Run(std::vector<std::string> a_files)
		{
			auto* data = RE::TESDataHandler::GetSingleton();
			const auto start = std::chrono::steady_clock::now();
			std::unordered_map<RE::FormID, int> counts;
			int  refs = 0, compressed = 0, unresolved = 0;
			auto buffer = std::make_unique<char[]>(1 << 20);
			for (const auto& name : a_files) {
				std::ifstream in;
				in.rdbuf()->pubsetbuf(buffer.get(), 1 << 20);
				in.open("Data\\" + name, std::ios::binary);
				if (!in) {
					logger::info("Lockable objects: could not open {}", name);
					continue;
				}
				const LockedRefScan scan = ScanLockedReferences(in);
				if (!scan.ok) {
					logger::info("Lockable objects: {} is not a plugin file", name);
					continue;
				}
				int locked = 0;
				for (const auto& [raw, n] : scan.bases) {
					const std::string file = FormFile(raw, scan, name);
					const RE::FormID  id = data ? data->LookupFormID(raw & 0xFFFFFF, file) : 0;
					auto*             form = id ? RE::TESForm::LookupByID(id) : nullptr;
					if (!form || !(form->Is(RE::FormType::Door) || form->Is(RE::FormType::Container))) {
						++unresolved;  // not loaded, or a lock on something else (an activator...)
						continue;
					}
					counts[id] += n;
					locked += n;
				}
				refs += scan.references;
				compressed += scan.compressedSkipped;
				if (locked > 0 || scan.compressedSkipped > 0) {
					logger::info("Lockable objects: {}: {} locked doors / containers{}", name, locked,
						scan.compressedSkipped ? std::format(" ({} references stored compressed, not read)", scan.compressedSkipped) : std::string());
				}
			}
			g_counts = std::move(counts);
			g_ready.store(true, std::memory_order_release);
			logger::info("Lockable objects: {} doors / containers are locked somewhere, from {} references in {} plugins ({:.1f} s; {} compressed, {} not doors / containers)",
				g_counts.size(), refs, a_files.size(), std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count(), compressed,
				unresolved);
		}
	}

	void Scan()
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		if (!data || g_ready.load()) {
			return;
		}
		std::vector<std::string> files;
		for (auto* file : data->files) {
			if (file && file->compileIndex != 0xFF && file->fileName[0]) {
				files.emplace_back(file->fileName);
			}
		}
		std::thread(Run, std::move(files)).detach();
	}

	bool Ready()
	{
		return g_ready.load(std::memory_order_acquire);
	}

	bool IsLockable(RE::FormID a_base)
	{
		return LockedReferences(a_base) > 0;
	}

	int LockedReferences(RE::FormID a_base)
	{
		if (!Ready()) {
			return 0;
		}
		const auto it = g_counts.find(a_base);
		return it != g_counts.end() ? it->second : 0;
	}
}
