#include "Markup.h"

#include "AddressCheck.h"
#include "LockProfile.h"
#include "Text.h"

namespace vrlp
{
	namespace
	{
		RE::NiMatrix3 ToNi(const Mat3& m)
		{
			RE::NiMatrix3 r;
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					r.entry[i][j] = m.m[i][j];
				}
			}
			return r;
		}

		void Place(RE::NiAVObject* a_obj, const Mat3& a_rot, const Vec3& a_pos, float a_scale)
		{
			if (!a_obj) {
				return;
			}
			a_obj->local.rotate = ToNi(a_rot);
			a_obj->local.translate = { a_pos.x, a_pos.y, a_pos.z };
			a_obj->local.scale = a_scale;
		}

		void Show(RE::NiAVObject* a_obj, bool a_show)
		{
			if (a_obj) {
				a_obj->SetAppCulled(!a_show);
			}
		}
	}

	RE::NiAVObject* Markup::Add(const char* a_model)
	{
		RE::NiPointer<RE::NiNode>         model;
		RE::BSModelDB::DBTraits::ArgsType args{};
		const std::string                 path = std::string("VRLockpicking\\") + a_model + ".nif";
		if (RE::BSModelDB::Demand(path.c_str(), model, args) != RE::BSResource::ErrorCode::kNone || !model) {
			logger::warn("Markup: could not load '{}'", path);
			return nullptr;
		}
		RE::NiObject* cloneObj = model->Clone();
		RE::NiNode*   clone = cloneObj ? cloneObj->AsNode() : nullptr;
		if (clone) {
			_root->AttachChild(clone, true);
		}
		return clone;
	}

	bool Markup::Create(RE::TESObjectREFR* a_target, bool a_pickRightHand)
	{
		Destroy();
		if (!AddressCheck::Has(Ids::kMeshes)) {
			return false;
		}
		RE::NiNode* parent = nullptr;
		if (a_target) {
			if (auto* obj = a_target->Get3D()) {
				parent = obj->AsNode();
			}
		}
		if (!parent) {
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* vrNodes = player ? player->GetVRNodeData() : nullptr;
			parent = vrNodes ? vrNodes->PlayerWorldNode.get() : nullptr;
		}
		if (!parent) {
			return false;
		}
		_parent.reset(parent);
		_root.reset(RE::NiNode::Create(2));
		_root->name = "VRLockpickingMarkup";
		_centre.reset(Add("dbg_cyan"));  // not green: the target is green
		_top.reset(Add("dbg_yellow"));
		_parent->AttachChild(_root.get(), true);
		_part = Part::kLock;
		_step = 0;
		_pickRight = a_pickRightHand;
		Prompt();
		logger::info("Markup mode on");
		return true;
	}

	void Markup::Destroy()
	{
		if (_root && _parent) {
			_parent->DetachChild(_root.get());
		}
		_centre.reset();
		_top.reset();
		_root.reset();
		_parent.reset();
	}

	void Markup::NextPart()
	{
		_part = _part == Part::kLock ? Part::kPick : (_part == Part::kPick ? Part::kWrench : Part::kLock);
		_step = 0;
		Prompt();
	}

	void Markup::Prompt() const
	{
		static constexpr const char* kLock[] = { "$VRLP_Msg_MarkupLock1", "$VRLP_Msg_MarkupLock2", "$VRLP_Msg_MarkupLock3" };
		if (_part == Part::kLock) {
			Text::Show(kLock[std::clamp(_step, 0, 2)]);
			return;
		}
		const bool pick = _part == Part::kPick;
		Text::Show(pick ? "$VRLP_Msg_MarkupPick" : "$VRLP_Msg_MarkupWrench",
			Text::Get(pick == _pickRight ? "$VRLP_Msg_RightHand" : "$VRLP_Msg_LeftHand"));
	}

	std::optional<Markup::Result> Markup::Update(const LockSession& a_session, const Pose& a_anchor, const Vec3* a_tip, bool a_markPressed, bool a_backPressed)
	{
		if (!_root || !_parent) {
			return std::nullopt;
		}
		_root->local = _parent->world.Invert();
		const bool lockPart = _part == Part::kLock;
		const LockFrame& f = a_session.Frame();
		auto             toLock = [&f](const Vec3& a_p) {
            const Vec3 d = a_p - f.keyhole;
            return Vec3{ Dot(d, f.right), Dot(d, f.forward), Dot(d, f.up) };
		};
		auto fromLock = [&f](const Vec3& a_l) { return f.keyhole + f.right * a_l.x + f.forward * a_l.y + f.up * a_l.z; };

		std::optional<Result> result;
		if (lockPart && a_backPressed && _step > 0) {
			--_step;
			logger::info("Markup lock: back to step {}", _step + 1);
			Prompt();
		} else if (lockPart && a_markPressed) {
			if (!a_tip) {
				logger::info("Markup lock: left trigger at step {}, but the pick hand is not tracked", _step + 1);
				Text::Show("$VRLP_Msg_MarkupNoPickHand");
			} else if (_step < 2) {
				_marks[_step] = toLock(*a_tip);
				logger::info("Markup lock: {} at ({:+.2f}, {:+.2f}, {:+.2f}) in the lock frame (x right, y into the lock, z up)",
					_step == 0 ? "centre" : "top", _marks[_step].x, _marks[_step].y, _marks[_step].z);
				++_step;
				Prompt();
			} else {
				logger::info("Markup lock: saving");
				result = Result{ _marks[0], _marks[1] };
				_step = 0;
			}
		}

		// what has been marked so far (in the lock frame, so it follows the lock)
		auto toWorld = [&a_anchor](const Vec3& a_pos, RE::NiAVObject* a_obj, float a_scale) {
			Place(a_obj, a_anchor.rot, a_anchor.rot * a_pos + a_anchor.pos, a_scale);
		};
		Show(_centre.get(), lockPart && _step >= 1);
		Show(_top.get(), lockPart && _step >= 2);
		if (lockPart && _step >= 1) {
			toWorld(fromLock(_marks[0]), _centre.get(), 0.3f);
		}
		if (lockPart && _step >= 2) {
			toWorld(fromLock(_marks[1]), _top.get(), 0.3f);
		}
		RE::NiUpdateData ud{};
		_root->Update(ud);
		return result;
	}
}
