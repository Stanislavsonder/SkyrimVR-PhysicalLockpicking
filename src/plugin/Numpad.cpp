#include "Numpad.h"

#include "Text.h"

// the keyboard as it is now, whichever window has the focus (user32)
REX_W32_IMPORT(std::int16_t, GetAsyncKeyState, std::int32_t);

namespace vrlp::Numpad
{
	namespace
	{
		constexpr float kDelay = 0.35f;   // held this long, a key starts repeating...
		constexpr float kRepeat = 0.05f;  // ...one step every this often

		struct Step
		{
			float units;
			float degrees;
			float size;  // fraction
		};
		constexpr Step kSteps[] = { { 0.1f, 0.5f, 0.01f }, { 0.5f, 2.0f, 0.03f }, { 2.0f, 10.0f, 0.10f } };
		int            g_step = 1;

		enum Key
		{
			kLeft,
			kRight,
			kUp,
			kDown,
			kIn,
			kOut,
			kTurnLeft,
			kTurnRight,
			kRollLeft,
			kRollRight,
			kTiltUp,
			kTiltDown,
			kSmaller,
			kBigger,
			kStepSize,
			kCount
		};
		// virtual-key codes
		constexpr std::int32_t kCodes[kCount] = {
			0x64, 0x66,  // Num4, Num6
			0x68, 0x62,  // Num8, Num2
			0x6B, 0x6D,  // Num+, Num-
			0x67, 0x69,  // Num7, Num9
			0x61, 0x63,  // Num1, Num3
			0x6F, 0x6A,  // Num/, Num*
			0x60, 0x6E,  // Num0, Num.
			0x65         // Num5
		};

		struct Held
		{
			bool  down{ false };
			float time{ 0.0f };
			float next{ 0.0f };
		};
		Held g_held[kCount];

		// steps this key gives this frame
		int Steps(Key a_key, float a_dt)
		{
			Held&      h = g_held[a_key];
			const bool down = (W32_IMPL_GetAsyncKeyState(kCodes[a_key]) & 0x8000) != 0;
			if (!down) {
				h = {};
				return 0;
			}
			if (!h.down) {
				h.down = true;
				h.time = 0.0f;
				h.next = kDelay;
				return 1;
			}
			h.time += a_dt;
			int n = 0;
			while (h.time >= h.next && n < 10) {
				h.next += kRepeat;
				++n;
			}
			return n;
		}
	}

	void Reset()
	{
		for (auto& h : g_held) {
			h = {};
		}
	}

	Input Read(float a_dt)
	{
		Input in;
		int   n[kCount];
		for (int k = 0; k < kCount; ++k) {
			n[k] = Steps(static_cast<Key>(k), a_dt);
		}
		if (n[kStepSize] > 0 && g_held[kStepSize].time == 0.0f) {  // a press, not a repeat
			g_step = (g_step + 1) % static_cast<int>(std::size(kSteps));
			const Step& s = kSteps[g_step];
			Text::Show("$VRLP_Msg_MarkupStep", std::format("{:.1f}", s.units), std::format("{:.1f}", s.degrees));
		}
		const Step& s = kSteps[g_step];
		in.move = Vec3{ static_cast<float>(n[kRight] - n[kLeft]), static_cast<float>(n[kIn] - n[kOut]), static_cast<float>(n[kUp] - n[kDown]) } * s.units;
		// turning left is anticlockwise seen from above (+ about up); rolling right is clockwise
		// as the player sees it (+ about the axis into the lock); tilting up brings the top towards you
		in.turnDeg = Vec3{ static_cast<float>(n[kTiltUp] - n[kTiltDown]), static_cast<float>(n[kRollRight] - n[kRollLeft]),
						 static_cast<float>(n[kTurnLeft] - n[kTurnRight]) } *
		             s.degrees;
		in.scale = std::pow(1.0f + s.size, static_cast<float>(n[kBigger] - n[kSmaller]));
		in.any = Length(in.move) > 0.0f || Length(in.turnDeg) > 0.0f || in.scale != 1.0f;
		return in;
	}
}
