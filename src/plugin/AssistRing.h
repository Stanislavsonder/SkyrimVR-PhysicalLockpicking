#pragma once

#include "PCH.h"

#include "Config.h"
#include "LockSession.h"

namespace vrlp
{
	// Optional assist ring on the lock face (iAssistRing), like the one in Thief VR: a thin
	// see-through arc along the pick's range (the right half of the keyhole, top to bottom), a
	// cyan bead where the pick is, and - for easy locks with iAssistRing = 2 - the sweet spot as a
	// row of lime beads between yellow edges.
	class AssistRing
	{
	public:
		// a_showSweetSpot: also mark the sweet spot (the caller decides by the lock's level).
		bool Create(const Config& a_cfg, RE::TESObjectREFR* a_target, bool a_showSweetSpot);
		void Destroy();
		void Update(const LockSession& a_session, float a_shake, const Pose& a_anchor);
		bool Active() const { return _root != nullptr; }

	private:
		RE::NiAVObject* Add(const char* a_model);

		Config                                     _cfg;
		RE::NiPointer<RE::NiNode>                  _parent;
		RE::NiPointer<RE::NiNode>                  _root;
		RE::NiPointer<RE::NiNode>                  _face;  // lock frame
		static constexpr int                       kSweetBeads = 12;
		RE::NiPointer<RE::NiAVObject>              _arc, _pick, _sweetLo, _sweetHi;
		std::vector<RE::NiPointer<RE::NiAVObject>> _sweetBeads;
		bool                                       _showSweetSpot{ false };
	};
}
