#pragma once

#include "PCH.h"

#include "LockVariations.h"

namespace vrlp
{
	// Lock Variations support: its Data\*_LID.ini files choose, per lock, the lock and lockpick
	// models and the lockpicking sounds. Its own code only runs inside the vanilla menu, which we
	// skip, so we read the same files ourselves (read-only) and apply them.
	namespace LockMods
	{
		// (Re)reads every Data\*_LID.ini.
		void Load();

		struct Result
		{
			std::string         lockModel;  // empty = keep ours
			std::string         pickModel;
			LockVariationSounds sounds;
		};

		// What the files say for this lock.
		Result For(RE::TESObjectREFR* a_target);
	}
}
