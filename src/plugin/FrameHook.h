#pragma once

#include "PCH.h"

namespace vrlp
{
	// Runs a callback on the main thread right after the game has moved the VR wand / hand nodes
	// for this frame (a call in Main::Draw, the spot HIGGS uses), so whatever follows the hands
	// is placed with this frame's poses instead of the last one's.
	namespace FrameHook
	{
		using Callback = void (*)();

		// Installs the hook; false (and logged) if the call site doesn't look as expected.
		bool Install(Callback a_callback);

		// True while the hook keeps firing (it can be overwritten by another mod later).
		bool Alive();
	}
}
