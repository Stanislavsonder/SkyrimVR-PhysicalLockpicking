#pragma once

#include "PCH.h"

#include "Config.h"
#include "LockSession.h"

namespace vrlp
{
	// Debug view (bDebugView): the lock and tools as plain coloured blocks, plus gauges, so it is
	// easy to see where things are and how the simulation reads your hands.
	//
	//   slate plate   = lock body            yellow disc = the turning cylinder (white mark = its top)
	//   red slot      = keyhole              see-through green cylinder = where the tools go in
	//   gray dots     = the pick's range (right half, top to bottom, every 15 deg)   cyan = the pick now
	//   lime          = sweet spot, yellow = its edges            white / green top mark = both tools in
	//   cyan rod      = pick, orange bar = wrench
	//   small cubes at the tool tips = where the tips really are (green = in place, red = out,
	//                                  orange = pushed through)
	//   gauges above the lock: top row  white = lock turned, yellow = how far it can turn, red = your turn
	//                          next row magenta = pick strain until it breaks
	class DebugView
	{
	public:
		// a_overlay: only the targets, tips and gauges, drawn over the real models (bShowTargets).
		bool Create(const Config& a_cfg, RE::TESObjectREFR* a_target, bool a_overlay);
		void Destroy();
		void Update(const LockSession& a_session, const Pose* a_pickHand, const Pose* a_wrenchHand, float a_shake, const Pose& a_anchor, float a_dt);
		bool Active() const { return _root != nullptr; }
		bool Overlay() const { return _overlay; }
		void SetPicksLeft(int a_count) { _picksLeft = a_count; }  // -1 = unbreakable
		void SetQuiet(bool a_quiet) { _quiet = a_quiet; }          // no numbers on screen (markup tool)

	private:
		RE::NiAVObject* Add(RE::NiNode* a_parent, const char* a_model);

		Config                       _cfg;
		RE::NiPointer<RE::NiNode>    _parent;
		RE::NiPointer<RE::NiNode>    _root;
		RE::NiPointer<RE::NiNode>    _lock;      // lock frame (unscaled)
		RE::NiPointer<RE::NiNode>    _cylinder;  // turns with the lock
		RE::NiPointer<RE::NiAVObject> _pickSpot, _wrenchSpot, _sweet, _sweetLo, _sweetHi;
		RE::NiPointer<RE::NiAVObject> _gLock, _gAllowed, _gTurn, _gStrain;
		RE::NiPointer<RE::NiAVObject> _pick, _wrench;
		RE::NiPointer<RE::NiAVObject> _pickTipIn, _pickTipOut, _wrenchTipIn, _wrenchTipOut, _pickTipDeep, _wrenchTipDeep;
		RE::NiPointer<RE::NiAVObject> _pickMarker, _topWhite, _topGreen;
		int                           _picksLeft{ -1 };
		float                         _textTimer{ 0.0f };
		bool                          _loadFailed{ false };
		bool                          _overlay{ false };
		bool                          _quiet{ false };
		std::string                   _coinModel;   // stand-in for markers if the debug models are missing
	};
}
