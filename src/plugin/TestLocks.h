#pragma once

#include "PCH.h"

namespace vrlp
{
	// Debug: MCM buttons that put a set of locked chests and doors around the player, to try many
	// locks without looking for them. The chests and doors are the loaded game's own (vanilla and
	// mods), one per distinct model, with the lock levels shared out between them.
	namespace TestLocks
	{
		// Registers the Papyrus functions the MCM buttons call (VRLockpicking_MCM.SpawnTestLocks /
		// RemoveTestLocks / ResetMarkup).
		void RegisterPapyrus();

		void Spawn();   // main thread; replaces the previous set
		void Remove();  // main thread
		void Forget();  // a game is loading: the placed references are not ours to remove any more
	}
}
