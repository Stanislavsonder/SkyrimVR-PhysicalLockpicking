#include "TestLocks.h"

#include "AddressCheck.h"
#include "LockpickManager.h"
#include "Text.h"

namespace vrlp::TestLocks
{
	namespace
	{
		constexpr std::size_t kMaxChests = 12;
		constexpr std::size_t kMaxDoors = 6;

		// Raw lock levels as stored on a reference: Novice ... Master, then "requires a key".
		constexpr std::int8_t kLevels[] = { 1, 25, 50, 75, 100, -1 };
		constexpr const char* kLevelNames[] = { "Novice", "Apprentice", "Adept", "Expert", "Master", "Key" };

		std::vector<RE::ObjectRefHandle> g_spawned;

		std::string Lower(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		bool Contains(const std::string& a_s, std::initializer_list<const char*> a_words)
		{
			return std::ranges::any_of(a_words, [&](const char* w) { return a_s.find(w) != std::string::npos; });
		}

		struct Pick
		{
			RE::TESBoundObject* form;
			std::string         model;
		};

		// One form per distinct model (a_byFolder: per model folder, for more variety), in load order.
		template <class T>
		std::vector<Pick> Collect(std::initializer_list<const char*> a_words, std::initializer_list<const char*> a_skip, bool a_byFolder, std::size_t a_max)
		{
			std::vector<Pick>     out;
			std::set<std::string> seen;
			for (auto* form : RE::TESDataHandler::GetSingleton()->GetFormArray<T>()) {
				if (!form || out.size() >= a_max) {
					continue;
				}
				const char* path = form->GetModel();
				if (!path || !*path) {
					continue;
				}
				const std::string model = Lower(path);
				if (!Contains(model, a_words) || Contains(model, a_skip)) {
					continue;
				}
				const auto        slash = model.find_last_of('\\');
				const std::string key = a_byFolder && slash != std::string::npos ? model.substr(0, slash) : model;
				if (seen.insert(key).second) {
					out.push_back({ form, path });
				}
			}
			return out;
		}

		// A key the player doesn't carry, for a lock that has one (to try the Wax Key perk).
		RE::TESKey* SpareKey()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			for (auto* key : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESKey>()) {
				const char* name = key ? key->GetName() : nullptr;
				if (name && *name && player && player->GetItemCount(key) <= 0 && !Contains(Lower(name), { "skeleton" })) {
					return key;
				}
			}
			return nullptr;
		}

		bool SetLock(RE::TESObjectREFR* a_ref, std::int8_t a_level, RE::TESKey* a_key = nullptr)
		{
			RE::REFR_LOCK* lock = a_ref->GetLock();
			if (!lock && AddressCheck::Has(Ids::kAddLock)) {
				lock = a_ref->AddLock();  // the engine's own (creates the lock and its extra data)
			}
			if (!lock) {
				lock = RE::malloc<RE::REFR_LOCK>();
				auto* extra = static_cast<RE::ExtraLock*>(RE::BSExtraData::Create(sizeof(RE::ExtraLock), RE::VTABLE_ExtraLock[0].address()));
				if (!lock || !extra) {
					return false;
				}
				std::memset(lock, 0, sizeof(RE::REFR_LOCK));
				extra->lock = lock;
				a_ref->extraList.Add(extra);
			}
			lock->baseLevel = a_level;
			lock->key = a_key;
			lock->flags.reset(RE::REFR_LOCK::Flag::kLeveled);
			lock->SetLocked(true);
			lock->numTries = 0;
			return true;
		}

		void SpawnTask()
		{
			if (!AddressCheck::Has(Ids::kTestLocks)) {
				Text::Show("$VRLP_Msg_TestLocksNeedLibrary");
				return;
			}
			Spawn();
		}

		void SpawnNative(RE::StaticFunctionTag*)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask(SpawnTask);
			}
		}

		void RemoveNative(RE::StaticFunctionTag*)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask(Remove);
			}
		}

		void ResetMarkupNative(RE::StaticFunctionTag*)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([] { LockpickManager::Get().ResetMarkup(); });
			}
		}

		bool Register(RE::BSScript::IVirtualMachine* a_vm)
		{
			// they only queue a task, so they need not wait for the frame
			a_vm->RegisterFunction("SpawnTestLocks", "VRLockpicking_MCM", SpawnNative, true);
			a_vm->RegisterFunction("RemoveTestLocks", "VRLockpicking_MCM", RemoveNative, true);
			a_vm->RegisterFunction("ResetMarkup", "VRLockpicking_MCM", ResetMarkupNative, true);
			return true;
		}
	}

	void RegisterPapyrus()
	{
		if (auto* papyrus = SKSE::GetPapyrusInterface()) {
			papyrus->Register(Register);
		}
	}

	void Forget()
	{
		g_spawned.clear();
	}

	void Remove()
	{
		int count = 0;
		for (auto& handle : g_spawned) {
			if (auto ref = std::as_const(handle).get()) {
				ref->Disable();
				ref->SetDelete(true);
				++count;
			}
		}
		g_spawned.clear();
		if (count > 0) {
			logger::info("Test locks: removed {}", count);
		}
	}

	void Spawn()
	{
		Remove();
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell) {
			return;
		}

		const auto chests = Collect<RE::TESObjectCONT>(
			{ "chest", "strongbox", "coffer", "lockbox", "footlocker", "safe", "trunk", "box" },
			{ "static", "corpse", "skeleton", "sack", "barrel", "urn" }, false, kMaxChests);
		const auto doors = Collect<RE::TESObjectDOOR>({ "door" }, { "trapdoor", "secret", "load" }, true, kMaxDoors);

		// facing: where the headset looks, else the body
		float heading = player->GetAngleZ();
		if (auto* vrNodes = player->GetVRNodeData(); vrNodes && vrNodes->HmdNode) {
			const auto& m = vrNodes->HmdNode->world.rotate;
			if (std::hypot(m.entry[0][1], m.entry[1][1]) > 0.1f) {
				heading = std::atan2(m.entry[0][1], m.entry[1][1]);
			}
		}
		const RE::NiPoint3 origin = player->GetPosition();

		// Chests on two arcs in front, doors on a wider arc behind them. Each object's -Y (the
		// usual front of a model) faces the player.
		auto place = [&](const Pick& a_pick, float a_radius, float a_offsetDeg, std::size_t a_levelIndex, RE::TESKey* a_key = nullptr) {
			const float        a = heading + a_offsetDeg * RE::NI_PI / 180.0f;
			const RE::NiPoint3 pos{ origin.x + std::sin(a) * a_radius, origin.y + std::cos(a) * a_radius, origin.z };
			const RE::NiPoint3 rot{ 0.0f, 0.0f, a };
			auto               handle = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(a_pick.form, pos, rot, cell,
                player->GetWorldspace(), nullptr, nullptr, RE::ObjectRefHandle(), false, true);
			auto ref = std::as_const(handle).get();
			if (!ref) {
				logger::warn("Test locks: could not place {:08X} ({})", a_pick.form->GetFormID(), a_pick.model);
				return;
			}
			const std::size_t level = a_levelIndex % std::size(kLevels);
			SetLock(ref.get(), kLevels[level], a_key);
			g_spawned.push_back(handle);
			logger::info("Test locks: {} {:08X} '{}' {} ({}), {:.0f} deg{}", ref->GetFormType() == RE::FormType::Door ? "door" : "chest",
				a_pick.form->GetFormID(), a_pick.form->GetName(), a_pick.model, kLevelNames[level], a_offsetDeg,
				a_key ? std::format(", has a key: '{}' (picking it gives a copy with Wax Key)", a_key->GetName()) : std::string());
		};

		const std::size_t front = std::min<std::size_t>(chests.size(), 6);
		for (std::size_t i = 0; i < chests.size(); ++i) {
			const bool        row1 = i < front;
			const std::size_t n = row1 ? front : chests.size() - front;
			const std::size_t k = row1 ? i : i - front;
			const float       span = n > 1 ? 150.0f : 0.0f;
			place(chests[i], row1 ? 160.0f : 270.0f, -span / 2 + span * k / std::max<std::size_t>(n - 1, 1), i);
		}
		for (std::size_t i = 0; i < doors.size(); ++i) {
			const float span = doors.size() > 1 ? 120.0f : 0.0f;
			place(doors[i], 430.0f, -span / 2 + span * i / std::max<std::size_t>(doors.size() - 1, 1), i + 2);
		}
		// one more door, a Novice lock that has a key (for the Wax Key), straight ahead behind the others
		if (!doors.empty()) {
			if (auto* key = SpareKey()) {
				place(doors.front(), 560.0f, 0.0f, 0, key);
			}
		}

		Text::Show("$VRLP_Msg_TestLocksPlaced", chests.size(), doors.size());
		logger::info("{} test chests and {} doors placed", chests.size(), doors.size());
	}
}
