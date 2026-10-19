#pragma once

#include "PCH.h"

namespace vrlp
{
	// Debug: MCM buttons that set the player up for trying the lockpicking rules without the
	// console - lockpicks, the Skeleton Key, the lockpicking perks (found by the entry points they
	// use, so mods' perks count too) and the Lockpicking skill.
	namespace DebugCheats
	{
		// Registers the Papyrus functions the MCM buttons call (VRLockpicking_MCM.Debug*).
		void RegisterPapyrus();
	}
}
