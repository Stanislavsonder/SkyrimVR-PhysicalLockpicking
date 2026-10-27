#pragma once

#include "PCH.h"

#include "VecMath.h"

namespace vrlp
{
	// The markup tool's fine adjustment on the keyboard's number pad (Num Lock on). Each press is
	// one step; held, it repeats. Read from the keyboard directly, so it also works while the
	// game is paused.
	//   4 / 6   left / right          7 / 9   turn left / right
	//   8 / 2   up / down             1 / 3   roll left / right
	//   + / -   in / out              / / *   tilt up / down
	//   0 / .   smaller / bigger      5       step size (fine, normal, coarse)
	namespace Numpad
	{
		struct Input
		{
			Vec3  move;          // game units: x right, y in (forward), z up
			Vec3  turnDeg;       // degrees about x (tilt), y (roll), z (turn)
			float scale{ 1.0f };  // size multiplier
			bool  any{ false };
		};

		// Once per frame while the markup tool runs.
		Input Read(float a_dt);
		void  Reset();  // forget held keys (a session starts)
	}
}
