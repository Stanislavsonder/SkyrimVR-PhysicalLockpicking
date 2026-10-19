#pragma once

#include "PCH.h"

namespace vrlp
{
	// The VR Address Library on Nexus can be older than the IDs CommonLib uses. Resolving a
	// missing ID is fatal (CommonLib shows a message box that is invisible in the headset,
	// so the game appears frozen). We read the installed CSV ourselves and only call engine
	// functions whose IDs are present.
	namespace AddressCheck
	{
		void Load();

		// True if the installed address library contains every given ID (or the CSV could not
		// be inspected, in which case we cannot do better than trusting it).
		bool Has(std::initializer_list<std::uint64_t> a_ids);

		const std::string& LibraryVersion();
	}

	// Engine features and the address-library IDs they need (SE IDs, as used by CommonLib).
	namespace Ids
	{
		// Needed to replace the menu at all.
		inline constexpr std::initializer_list<std::uint64_t> kCore{
			519716,                  // LockpickingMenu target reference
			517014,                  // PlayerCharacter singleton
			514178,                  // UI singleton
			514285, 13530,           // UIMessageQueue singleton / AddMessage
			19818, 12272,            // GetLock / REFR_LOCK::GetLockLevel
			514351, 514360,          // form map + lock (LookupByID)
			514622,                  // GameSettingCollection
			19275,                   // GetItemCount
			39413,                   // AddSkillExperience
			52054, 52050,            // PlaySound / ShowHUDMessage
			66976, 66982,            // BSReadWriteLock
			67819, 67847,            // BSFixedString
			514315,                  // SkyrimVM
			11045, 66859, 66861      // MemoryManager (used by allocation helpers)
		};
		inline constexpr std::initializer_list<std::uint64_t> kMeshes{ 74040, 68835, 68936, 68900, 98893 };
		inline constexpr std::initializer_list<std::uint64_t> kEntryPoints{ 675707, 23073 };
		inline constexpr std::initializer_list<std::uint64_t> kCrime{ 19789, 19805, 12204, 514167, 40394, 36432 };
		inline constexpr std::initializer_list<std::uint64_t> kLocksPickedStat{ 51115 };
		inline constexpr std::initializer_list<std::uint64_t> kAddLockChange{ 19110 };
		inline constexpr std::initializer_list<std::uint64_t> kActivate{ 19369 };
		inline constexpr std::initializer_list<std::uint64_t> kDefaultObjects{ 10878 };  // the Skeleton Key
		inline constexpr std::initializer_list<std::uint64_t> kControlMap{ 514705 };
		// debug test locks: data handler, CreateReferenceAtLocation, ExtraDataList::Add, handle get,
		// Papyrus native call, Object::DecRef. (97538, ObjectTypeInfo::ReleaseData, missing in the VR
		// library, is linked through Variable cleanup but never reached: the natives take no objects.)
		inline constexpr std::initializer_list<std::uint64_t> kTestLocks{ 514141, 13625, 12176, 12204, 97923, 97469 };
		inline constexpr std::initializer_list<std::uint64_t> kAddLock{ 19816 };  // TESObjectREFR::AddLock
		inline constexpr std::initializer_list<std::uint64_t> kIniSettings{ 524557 };  // INISettingCollection
		// Lock Variations conditions: data handler, EditorID map + lock, water system, current location
		inline constexpr std::initializer_list<std::uint64_t> kDataHandler{ 514141 };
		inline constexpr std::initializer_list<std::uint64_t> kEditorIDs{ 514352, 514361 };
		inline constexpr std::initializer_list<std::uint64_t> kWaterSystem{ 514290 };
		inline constexpr std::initializer_list<std::uint64_t> kCurrentLocation{ 19385 };
		// calling a Papyrus function (Game.IncrementStat): VM, MemoryManager, ScrapHeap
		inline constexpr std::initializer_list<std::uint64_t> kPapyrusCall{ 514315, 66841, 66884, 66885 };
	}
}
