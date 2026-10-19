#pragma once

#include "PCH.h"

#include "Config.h"
#include "LockSession.h"

namespace vrlp
{
	// Model markup tool (bMarkupMode). Three parts, one after another (A / X goes to the next):
	//   lock:   the pick's tip (its tip cube) marks the keyhole on the lock model: put it on the
	//           keyhole's centre and press the left trigger, then on the keyhole's top (which way
	//           is up), then press once more to save; a stick click goes a step back.
	//   pick / wrench: the model is lined up in the hand (hold the left trigger to keep it still
	//           while the hand moves; sticks for fine turns and size), done by LockpickManager;
	//           this class only says what to do.
	class Markup
	{
	public:
		enum class Part
		{
			kLock,
			kPick,
			kWrench
		};

		struct Result
		{
			Vec3 centre;  // in the lock frame (x right, y into the lock, z up, keyhole at the origin)
			Vec3 top;
		};

		bool Create(RE::TESObjectREFR* a_target, bool a_pickRightHand);
		void Destroy();
		bool Active() const { return _root != nullptr; }
		Part CurrentPart() const { return _part; }
		void NextPart();
		void Prompt() const;

		// Lock part: a_tip is the pick's tip (room space), if tracked. Returns the marks once the
		// user confirms. Other parts: hides the marks.
		std::optional<Result> Update(const LockSession& a_session, const Pose& a_anchor, const Vec3* a_tip, bool a_markPressed, bool a_backPressed);

	private:
		RE::NiAVObject* Add(const char* a_model);

		RE::NiPointer<RE::NiNode>     _parent;
		RE::NiPointer<RE::NiNode>     _root;
		RE::NiPointer<RE::NiAVObject> _centre, _top;
		Part _part{ Part::kLock };
		int  _step{ 0 };  // lock: 0 centre, 1 top, 2 confirm
		bool _pickRight{ true };
		Vec3 _marks[2];
	};
}
