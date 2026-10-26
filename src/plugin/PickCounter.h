#pragma once

#include "PCH.h"

#include "Config.h"
#include "LockSession.h"

namespace vrlp
{
	// How many lockpicks are left, over the lock: a small lockpick and the number in seven-segment
	// digits, on the lock's face so it turns and shakes with it. Hidden while picks can't break
	// (the Skeleton Key, Unbreakable).
	class PickCounter
	{
	public:
		bool Create(const Config& a_cfg, RE::TESObjectREFR* a_target);
		void Destroy();
		void SetCount(int a_picks);  // < 0: hidden
		void Update(const LockSession& a_session, float a_shake, const Pose& a_anchor);

	private:
		static constexpr int kDigits = 3;
		RE::NiAVObject*      Add(const char* a_path);

		Config                                     _cfg;
		int                                        _count{ -1 };
		RE::NiPointer<RE::NiNode>                  _parent;
		RE::NiPointer<RE::NiNode>                  _root;
		RE::NiPointer<RE::NiNode>                  _face;
		RE::NiPointer<RE::NiAVObject>              _icon;
		std::vector<RE::NiPointer<RE::NiAVObject>> _segments;  // kDigits x 7
	};
}
