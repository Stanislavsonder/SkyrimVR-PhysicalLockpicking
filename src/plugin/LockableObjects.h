#pragma once

#include "PCH.h"

namespace vrlp
{
	// Which doors and containers are ever locked in this game: the base objects of every placed
	// reference that has a lock in the loaded plugins (vanilla and mods). Only those get the lock
	// on the object itself (the others never have a lock in the game; one a script locks keeps it
	// in front of the player), and only those are offered as test locks. Read from the plugin
	// files in the background once the game's data has loaded.
	namespace LockableObjects
	{
		void Scan();  // main thread, after the data has loaded
		bool Ready();
		// false until the scan is done
		bool IsLockable(RE::FormID a_base);
		// how many references lock it (0 = never)
		int  LockedReferences(RE::FormID a_base);
	}
}
