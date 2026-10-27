#include "TestLocks.h"

#include "AddressCheck.h"
#include "LockableObjects.h"
#include "LockpickManager.h"
#include "Text.h"

namespace vrlp::TestLocks
{
	namespace
	{
		// Each press of the button places the next batch: as many as fit on an arc around you.
		constexpr float       kRadius = 300.0f;   // from you to the objects' middles
		constexpr float       kArcDeg = 300.0f;   // the arc they stand on (the gap is behind you)
		constexpr float       kGap = 60.0f;       // between neighbours
		constexpr std::size_t kMaxPerBatch = 16;

		// Raw lock levels as stored on a reference: Novice ... Master.
		constexpr std::int8_t kLevels[] = { 1, 25, 50, 75, 100 };
		constexpr const char* kLevelNames[] = { "Novice", "Apprentice", "Adept", "Expert", "Master" };

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
			bool                door;
			RE::NiPoint3        min, max;  // its bounds (OBND), model space
		};

		// Every door and chest model of the loaded game and mods that is locked somewhere in it
		// (LockableObjects), one form each: containers first, then doors, each by model path.
		std::vector<Pick> g_all;
		std::size_t       g_next = 0;  // first of the next batch

		template <class T>
		void Collect(std::vector<Pick>& a_out, bool a_door)
		{
			std::set<std::string> seen;
			std::vector<Pick>     found;
			for (auto* form : RE::TESDataHandler::GetSingleton()->GetFormArray<T>()) {
				const char* path = form ? form->GetModel() : nullptr;
				if (!path || !*path || !LockableObjects::IsLockable(form->GetFormID())) {
					continue;
				}
				if (!seen.insert(Lower(path)).second) {
					continue;
				}
				const auto& b = form->boundData;
				found.push_back({ form, path, a_door, { static_cast<float>(b.boundMin.x), static_cast<float>(b.boundMin.y), static_cast<float>(b.boundMin.z) },
					{ static_cast<float>(b.boundMax.x), static_cast<float>(b.boundMax.y), static_cast<float>(b.boundMax.z) } });
			}
			std::ranges::sort(found, [](const Pick& a_l, const Pick& a_r) { return Lower(a_l.model) < Lower(a_r.model); });
			a_out.insert(a_out.end(), found.begin(), found.end());
		}

		void CollectAll()
		{
			g_all.clear();
			g_next = 0;
			Collect<RE::TESObjectCONT>(g_all, false);
			Collect<RE::TESObjectDOOR>(g_all, true);
			const auto doors = static_cast<std::size_t>(std::ranges::count_if(g_all, [](const Pick& a_p) { return a_p.door; }));
			logger::info("Test locks: {} container and {} door models in this game", g_all.size() - doors, doors);
		}

		// how wide it stands on the arc
		float Width(const Pick& a_p)
		{
			return std::max({ a_p.max.x - a_p.min.x, a_p.max.y - a_p.min.y, 20.0f });
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
		if (!LockableObjects::Ready()) {
			Text::Show("$VRLP_Msg_TestLocksScanning");
			return;
		}
		if (g_all.empty()) {
			CollectAll();
		}
		if (g_all.empty()) {
			return;
		}
		if (g_next >= g_all.size()) {
			g_next = 0;  // after the last batch, the first again
		}

		// this batch: as many as fit on the arc
		std::vector<const Pick*> batch;
		float                    arc = 0.0f;
		const float              length = kArcDeg * RE::NI_PI / 180.0f * kRadius;
		for (std::size_t i = g_next; i < g_all.size() && batch.size() < kMaxPerBatch; ++i) {
			const float w = Width(g_all[i]) + kGap;
			if (!batch.empty() && arc + w > length) {
				break;
			}
			batch.push_back(&g_all[i]);
			arc += w;
		}
		// batch numbers: count the batches the same way from the start
		std::size_t batchNo = 0, batches = 0;
		for (std::size_t i = 0; i < g_all.size(); ++batches) {
			if (i == g_next) {
				batchNo = batches + 1;
			}
			float       used = 0.0f;
			std::size_t n = 0;
			while (i < g_all.size() && n < kMaxPerBatch && (n == 0 || used + Width(g_all[i]) + kGap <= length)) {
				used += Width(g_all[i]) + kGap;
				++i;
				++n;
			}
		}

		// facing: where the headset looks, else the body
		float heading = player->GetAngleZ();
		if (auto* vrNodes = player->GetVRNodeData(); vrNodes && vrNodes->HmdNode) {
			const auto& m = vrNodes->HmdNode->world.rotate;
			if (std::hypot(m.entry[0][1], m.entry[1][1]) > 0.1f) {
				heading = std::atan2(m.entry[0][1], m.entry[1][1]);
			}
		}
		const RE::NiPoint3 origin = player->GetPosition();
		RE::TESKey*        key = SpareKey();
		bool               keyGiven = false;

		// Along the arc, centred in front of you. Each object's front faces you: -Y (the usual
		// front of a model), or -X for a door that is thinner that way.
		float       along = -arc / 2.0f;
		std::size_t chests = 0, doors = 0;
		for (std::size_t k = 0; k < batch.size(); ++k) {
			const Pick& p = *batch[k];
			const float w = Width(p) + kGap;
			const float a = heading + (along + w / 2.0f) / kRadius;
			along += w;
			const bool  sideways = p.door && (p.max.x - p.min.x) < (p.max.y - p.min.y);
			const float r = sideways ? a - RE::NI_PI / 2.0f : a;
			// the model's middle on the arc (its origin may be at an edge, as a door's hinge)
			const float  cx = (p.min.x + p.max.x) * 0.5f, cy = (p.min.y + p.max.y) * 0.5f;
			const float  offX = cx * std::cos(r) + cy * std::sin(r);
			const float  offY = -cx * std::sin(r) + cy * std::cos(r);
			RE::NiPoint3 pos{ origin.x + std::sin(a) * kRadius - offX, origin.y + std::cos(a) * kRadius - offY, origin.z };
			const RE::NiPoint3 rot{ 0.0f, 0.0f, r };
			auto               handle = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(p.form, pos, rot, cell,
                player->GetWorldspace(), nullptr, nullptr, RE::ObjectRefHandle(), false, true);
			auto ref = std::as_const(handle).get();
			if (!ref) {
				logger::warn("Test locks: could not place {:08X} ({})", p.form->GetFormID(), p.model);
				continue;
			}
			const std::size_t level = (g_next + k) % std::size(kLevels);
			// the first door has a key (picking it gives a copy with Wax Key)
			RE::TESKey* withKey = p.door && !keyGiven ? key : nullptr;
			keyGiven = keyGiven || withKey;
			SetLock(ref.get(), kLevels[level], withKey);
			g_spawned.push_back(handle);
			(p.door ? doors : chests)++;
			logger::info("Test locks: {} {:08X} '{}' {} ({}; locked {} times in the game){}", p.door ? "door" : "chest", p.form->GetFormID(),
				p.form->GetName(), p.model, kLevelNames[level], LockableObjects::LockedReferences(p.form->GetFormID()),
				withKey ? std::format(", has a key: '{}'", withKey->GetName()) : std::string());
		}
		g_next += batch.size();

		Text::Show("$VRLP_Msg_TestLocksPlaced", batchNo, batches, chests, doors);
		logger::info("Test locks: batch {} of {}: {} chests and {} doors placed", batchNo, batches, chests, doors);
	}
}
