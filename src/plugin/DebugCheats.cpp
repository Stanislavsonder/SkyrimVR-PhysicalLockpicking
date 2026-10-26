#include "DebugCheats.h"

#include "AddressCheck.h"
#include "Text.h"

#undef GetObject  // wingdi.h's, over BGSDefaultObjectManager::GetObject

namespace vrlp::DebugCheats
{
	namespace
	{
		using EntryPoint = RE::BGSEntryPoint::ENTRY_POINT;

		template <class... Args>
		void Say(const char* a_key, const Args&... a_args)
		{
			const std::string text = Text::Format(a_key, a_args...);
			RE::SendHUDMessage::ShowHUDMessage(text.c_str());
			logger::info("Debug: {}", text);
		}

		bool UsesEntryPoint(const RE::BGSPerk* a_perk, std::initializer_list<EntryPoint> a_points)
		{
			for (const auto* entry : a_perk->perkEntries) {
				if (entry && entry->GetType() == RE::PERK_ENTRY_TYPE::kEntryPoint) {
					const auto* ep = static_cast<const RE::BGSEntryPointPerkEntry*>(entry);
					for (const auto point : a_points) {
						if (ep->IsEntryPoint(point)) {
							return true;
						}
					}
				}
			}
			return false;
		}

		// Every perk (vanilla or a mod's) that uses one of these entry points: added, or with
		// a_add false removed.
		void Perks(std::initializer_list<EntryPoint> a_points, bool a_add)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* data = RE::TESDataHandler::GetSingleton();
			if (!player || !data) {
				return;
			}
			std::vector<std::string> names;
			for (auto* perk : data->GetFormArray<RE::BGSPerk>()) {
				if (!perk || !UsesEntryPoint(perk, a_points)) {
					continue;
				}
				if (a_add) {
					player->AddPerk(perk);
				} else {
					player->RemovePerk(perk);
				}
				const char* name = perk->GetName();
				names.push_back(std::format("{} {:08X}", name && *name ? name : "?", perk->GetFormID()));
			}
			if (names.empty()) {
				Say("$VRLP_Msg_NoPerkFound");
				return;
			}
			std::string list;
			for (const auto& n : names) {
				list += (list.empty() ? "" : ", ") + n;
			}
			Say(a_add ? "$VRLP_Msg_PerksAdded" : "$VRLP_Msg_PerksRemoved", list);
		}

		void SetSkill(float a_value)
		{
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				player->AsActorValueOwner()->SetBaseActorValue(RE::ActorValue::kLockpicking, a_value);
				Say("$VRLP_Msg_SkillSet", std::format("{:.0f}", a_value));
			}
		}

		void GiveLockpicks()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* pick = RE::TESForm::LookupByID<RE::TESObjectMISC>(0x0000000A);
			if (player && pick) {
				player->AddObjectToContainer(pick, nullptr, 50, nullptr);
				Say("$VRLP_Msg_LockpicksAdded", player->GetItemCount(pick));
			}
		}

		void ToggleSkeletonKey()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* objects = AddressCheck::Has(Ids::kDefaultObjects) ? RE::BGSDefaultObjectManager::GetSingleton() : nullptr;
			auto* key = objects ? objects->GetObject<RE::TESBoundObject>(static_cast<std::size_t>(RE::DefaultObjectID::kSkeletonKey)) : nullptr;
			if (!player || !key) {
				Say("$VRLP_Msg_SkeletonKeyUnavailable");
				return;
			}
			if (const auto count = player->GetItemCount(key); count > 0) {
				player->RemoveItem(key, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				Say("$VRLP_Msg_SkeletonKeyRemoved");
			} else {
				player->AddObjectToContainer(key, nullptr, 1, nullptr);
				Say("$VRLP_Msg_SkeletonKeyAdded");
			}
		}

		// The buttons only queue the work for the main thread, so they need not wait for the frame.
		template <void (*F)()>
		void Native(RE::StaticFunctionTag*)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([] { F(); });
			}
		}

		void LockPerks() { Perks({ EntryPoint::kModLockpickSweetSpot }, true); }
		void Locksmith() { Perks({ EntryPoint::kSetLockpickStartingArc }, true); }
		void Unbreakable() { Perks({ EntryPoint::kMakeLockpicksUnbreakable }, true); }
		void WaxKey() { Perks({ EntryPoint::kModLockpickingKeyRewardChance }, true); }
		void QuickHands() { Perks({ EntryPoint::kModLockpickingCrimeChance }, true); }
		void RemovePerks()
		{
			Perks({ EntryPoint::kModLockpickSweetSpot, EntryPoint::kSetLockpickStartingArc, EntryPoint::kMakeLockpicksUnbreakable,
					  EntryPoint::kModLockpickingKeyRewardChance, EntryPoint::kModLockpickingCrimeChance },
				false);
		}
		void Skill10() { SetSkill(10.0f); }
		void Skill25() { SetSkill(25.0f); }
		void Skill50() { SetSkill(50.0f); }
		void Skill75() { SetSkill(75.0f); }
		void Skill100() { SetSkill(100.0f); }

		bool Register(RE::BSScript::IVirtualMachine* a_vm)
		{
			constexpr const char* kScript = "VRLockpicking_MCM";
			a_vm->RegisterFunction("DebugGiveLockpicks", kScript, Native<GiveLockpicks>, true);
			a_vm->RegisterFunction("DebugToggleSkeletonKey", kScript, Native<ToggleSkeletonKey>, true);
			a_vm->RegisterFunction("DebugAddLockPerks", kScript, Native<LockPerks>, true);
			a_vm->RegisterFunction("DebugAddLocksmith", kScript, Native<Locksmith>, true);
			a_vm->RegisterFunction("DebugAddUnbreakable", kScript, Native<Unbreakable>, true);
			a_vm->RegisterFunction("DebugAddWaxKey", kScript, Native<WaxKey>, true);
			a_vm->RegisterFunction("DebugAddQuickHands", kScript, Native<QuickHands>, true);
			a_vm->RegisterFunction("DebugRemovePerks", kScript, Native<RemovePerks>, true);
			a_vm->RegisterFunction("DebugSkill10", kScript, Native<Skill10>, true);
			a_vm->RegisterFunction("DebugSkill25", kScript, Native<Skill25>, true);
			a_vm->RegisterFunction("DebugSkill50", kScript, Native<Skill50>, true);
			a_vm->RegisterFunction("DebugSkill75", kScript, Native<Skill75>, true);
			a_vm->RegisterFunction("DebugSkill100", kScript, Native<Skill100>, true);
			return true;
		}
	}

	void RegisterPapyrus()
	{
		if (auto* papyrus = SKSE::GetPapyrusInterface()) {
			papyrus->Register(Register);
		}
	}
}
