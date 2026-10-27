#pragma once

#include "PCH.h"

namespace vrlp
{
	// Debug: MCM buttons that put locked chests and doors around the player, to try many locks
	// (and line up the lock on each model) without looking for them. They are the loaded game's
	// own (vanilla and mods), every chest and door model once, in batches: each press places the
	// next batch on an arc around the player, with the lock levels shared out between them.
	namespace TestLocks
	{
		// Registers the Papyrus functions the MCM buttons call (VRLockpicking_MCM.SpawnTestLocks /
		// RemoveTestLocks / ResetMarkup).
		void RegisterPapyrus();

		void Spawn();   // main thread; replaces the previous batch with the next one
		void Remove();  // main thread
		void Forget();  // a game is loading: the placed references are not ours to remove any more
	}
}
