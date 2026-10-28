#include "TestLocks.h"

#include "AddressCheck.h"
#include "LockableObjects.h"
#include "LockProfile.h"
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
			Vec3                min, max;  // its bounds (OBND), model space
			Vec3                angles;    // how the game's own reference of it stands
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
					{ static_cast<float>(b.boundMax.x), static_cast<float>(b.boundMax.y), static_cast<float>(b.boundMax.z) },
					LockableObjects::GameAngles(form->GetFormID()) });
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
			return std::max(StandWidth(a_p.min, a_p.max, a_p.angles), 20.0f);
		}

		// The turn the game gave the copies placed last time, against the one they were placed
		// for (RefRotation): logged when they differ, so a wrong guess of the game's turn shows.
		void CheckTurns()
		{
			int checked = 0, off = 0;
			for (auto& handle : g_spawned) {
				auto  ref = std::as_const(handle).get();
				auto* obj = ref ? ref->Get3D() : nullptr;
				if (!obj) {
					continue;
				}
				const auto& a = ref->data.angle;
				const Mat3  want = RefRotation({ a.x, a.y, a.z });
				const auto& m = obj->world.rotate;
				float       worst = 1.0f;
				for (int c = 0; c < 3; ++c) {
					worst = std::min(worst, Dot(want.Column(c), Normalize({ m.entry[0][c], m.entry[1][c], m.entry[2][c] })));
				}
				++checked;
				if (worst < 0.99f) {
					++off;
					const char* path = ref->GetBaseObject() && ref->GetBaseObject()->As<RE::TESModel>() ? ref->GetBaseObject()->As<RE::TESModel>()->GetModel() : "";
					logger::warn("Test locks: {} stands turned otherwise than placed (angles {:.2f} {:.2f} {:.2f}; columns {:.2f} {:.2f} {:.2f} / {:.2f} {:.2f} {:.2f} / {:.2f} {:.2f} {:.2f})",
						path, a.x, a.y, a.z, m.entry[0][0], m.entry[1][0], m.entry[2][0], m.entry[0][1], m.entry[1][1], m.entry[2][1], m.entry[0][2], m.entry[1][2], m.entry[2][2]);
				}
			}
			if (checked > 0) {
				logger::info("Test locks: {} of the {} placed last time stand as placed", checked - off, checked);
			}
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
		CheckTurns();
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

		// Along the arc, centred in front of you. Each stands as the game stands it (some models
		// lie on their side in their own file), turned so the side its lock is on faces you, its
		// middle on the arc and its bottom on the floor.
		auto&       manager = LockpickManager::Get();
		float       along = -arc / 2.0f;
		std::size_t chests = 0, doors = 0;
		for (std::size_t k = 0; k < batch.size(); ++k) {
			const Pick& p = *batch[k];
			const float w = Width(p) + kGap;
			const float a = heading + (along + w / 2.0f) / kRadius;
			along += w;
			ObjectAnchor anchor = manager.AnchorFor(p.model, p.min, p.max, p.door);
			if (anchor.shown == LockShown::kNone) {
				anchor = GuessObjectAnchor(p.min, p.max, p.door);  // no lock on it: its likely front
			}
			const Vec3         away{ std::sin(a), std::cos(a), 0.0f };
			const StandPose    stand = StandFacing(anchor, p.min, p.max, p.angles, away);
			const RE::NiPoint3 pos{ origin.x + away.x * kRadius + stand.offset.x, origin.y + away.y * kRadius + stand.offset.y, origin.z + stand.offset.z };
			const RE::NiPoint3 rot{ stand.angles.x, stand.angles.y, stand.angles.z };
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
			logger::info("Test locks: {} {:08X} '{}' {} ({}; locked {} times in the game; the game stands it at {:.0f} {:.0f} {:.0f} deg, placed at {:.0f} {:.0f} {:.0f}){}",
				p.door ? "door" : "chest", p.form->GetFormID(), p.form->GetName(), p.model, kLevelNames[level], LockableObjects::LockedReferences(p.form->GetFormID()),
				p.angles.x * kRadToDeg, p.angles.y * kRadToDeg, p.angles.z * kRadToDeg, stand.angles.x * kRadToDeg, stand.angles.y * kRadToDeg, stand.angles.z * kRadToDeg,
				withKey ? std::format(", has a key: '{}'", withKey->GetName()) : std::string());
		}
		g_next += batch.size();

		Text::Show("$VRLP_Msg_TestLocksPlaced", batchNo, batches, chests, doors);
		logger::info("Test locks: batch {} of {}: {} chests and {} doors placed", batchNo, batches, chests, doors);
	}
}
