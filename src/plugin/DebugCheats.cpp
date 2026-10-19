#include "DebugCheats.h"

#include "AddressCheck.h"

#undef GetObject  // wingdi.h's, over BGSDefaultObjectManager::GetObject

namespace vrlp::DebugCheats
{
	namespace
	{
		using EntryPoint = RE::BGSEntryPoint::ENTRY_POINT;

		void Say(const std::string& a_text)
		{
			RE::SendHUDMessage::ShowHUDMessage(a_text.c_str());
			logger::info("Debug: {}", a_text);
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
		void Perks(std::initializer_list<EntryPoint> a_points, bool a_add, const char* a_what)
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
				Say(std::format("No perk found for {}", a_what));
				return;
			}
			std::string list;
			for (const auto& n : names) {
				list += (list.empty() ? "" : ", ") + n;
			}
			Say(std::format("{} {}: {}", a_add ? "Added" : "Removed", a_what, list));
		}

		void SetSkill(float a_value)
		{
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				player->AsActorValueOwner()->SetBaseActorValue(RE::ActorValue::kLockpicking, a_value);
				Say(std::format("Lockpicking set to {:.0f}", a_value));
			}
		}

		void GiveLockpicks()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* pick = RE::TESForm::LookupByID<RE::TESObjectMISC>(0x0000000A);
			if (player && pick) {
				player->AddObjectToContainer(pick, nullptr, 50, nullptr);
				Say(std::format("50 lockpicks added ({} now)", player->GetItemCount(pick)));
			}
		}

		void ToggleSkeletonKey()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* objects = AddressCheck::Has(Ids::kDefaultObjects) ? RE::BGSDefaultObjectManager::GetSingleton() : nullptr;
			auto* key = objects ? objects->GetObject<RE::TESBoundObject>(static_cast<std::size_t>(RE::DefaultObjectID::kSkeletonKey)) : nullptr;
			if (!player || !key) {
				Say("The Skeleton Key is not available (VR Address Library)");
				return;
			}
			if (const auto count = player->GetItemCount(key); count > 0) {
				player->RemoveItem(key, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				Say("Skeleton Key removed");
			} else {
				player->AddObjectToContainer(key, nullptr, 1, nullptr);
				Say("Skeleton Key added");
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

		void LockPerks() { Perks({ EntryPoint::kModLockpickSweetSpot }, true, "the sweet spot perks"); }
		void Locksmith() { Perks({ EntryPoint::kSetLockpickStartingArc }, true, "Locksmith"); }
		void Unbreakable() { Perks({ EntryPoint::kMakeLockpicksUnbreakable }, true, "Unbreakable"); }
		void WaxKey() { Perks({ EntryPoint::kModLockpickingKeyRewardChance }, true, "Wax Key"); }
		void QuickHands() { Perks({ EntryPoint::kModLockpickingCrimeChance }, true, "Quick Hands"); }
		void RemovePerks()
		{
			Perks({ EntryPoint::kModLockpickSweetSpot, EntryPoint::kSetLockpickStartingArc, EntryPoint::kMakeLockpicksUnbreakable,
					  EntryPoint::kModLockpickingKeyRewardChance, EntryPoint::kModLockpickingCrimeChance },
				false, "the lockpicking perks");
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
