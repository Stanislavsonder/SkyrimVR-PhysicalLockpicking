#include "AssistRing.h"

#include "AddressCheck.h"

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

		// point on the lock face at a_deg (0 = up, + = clockwise as seen by the player), just in
		// front of it
		Vec3 OnFace(float a_deg, float a_radius, float a_side)
		{
			const float a = a_deg * kDegToRad;
			return { a_side * std::sin(a) * a_radius, -0.4f, std::cos(a) * a_radius };
		}
	}

	RE::NiAVObject* AssistRing::Add(const char* a_model)
	{
		RE::NiPointer<RE::NiNode>         model;
		RE::BSModelDB::DBTraits::ArgsType args{};
		const std::string                 path = std::string("VRLockpicking\\") + a_model + ".nif";
		if (RE::BSModelDB::Demand(path.c_str(), model, args) != RE::BSResource::ErrorCode::kNone || !model) {
			logger::warn("Assist ring: could not load '{}'", path);
			return nullptr;
		}
		RE::NiObject* cloneObj = model->Clone();
		RE::NiNode*   clone = cloneObj ? cloneObj->AsNode() : nullptr;
		if (clone) {
			_face->AttachChild(clone, true);
		}
		return clone;
	}

	bool AssistRing::Create(const Config& a_cfg, RE::TESObjectREFR* a_target, bool a_showSweetSpot)
	{
		Destroy();
		_cfg = a_cfg;
		_showSweetSpot = a_showSweetSpot;
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
		_root.reset(RE::NiNode::Create(1));
		_root->name = "VRLockpickingAssistRing";
		_face.reset(RE::NiNode::Create(4 + kSweetBeads));
		_root->AttachChild(_face.get(), true);

		const float s = _cfg.lockMeshRadius * std::clamp(_cfg.visualScale, 0.1f, 10.0f) / 8.0f;
		// a thin see-through arc along the pick's range (right half, top to bottom), radius 6
		_arc.reset(Add("ring_arc"));
		Place(_arc.get(), Mat3{}, { 0.0f, -0.4f, 0.0f }, s);
		_pick.reset(Add("ring_bead_cyan"));
		if (_showSweetSpot) {
			for (int i = 0; i < kSweetBeads; ++i) {
				_sweetBeads.emplace_back(Add("ring_bead_lime"));
			}
			_sweetLo.reset(Add("ring_bead_yellow"));
			_sweetHi.reset(Add("ring_bead_yellow"));
		}
		_parent->AttachChild(_root.get(), true);
		logger::info("Assist ring on{}", _showSweetSpot ? " (with the sweet spot)" : "");
		return true;
	}

	void AssistRing::Destroy()
	{
		if (_root && _parent) {
			_parent->DetachChild(_root.get());
		}
		_arc.reset();
		_sweetBeads.clear();
		_pick.reset();
		_sweetLo.reset();
		_sweetHi.reset();
		_face.reset();
		_root.reset();
		_parent.reset();
	}

	void AssistRing::Update(const LockSession& a_session, float a_shake, const Pose& a_anchor)
	{
		if (!_root || !_parent) {
			return;
		}
		_root->local = _parent->world.Invert();
		const LockFrame&  f = a_session.Frame();
		const LockParams& p = a_session.Params();
		const float       s = _cfg.lockMeshRadius * std::clamp(_cfg.visualScale, 0.1f, 10.0f) / 8.0f;
		const Mat3        shake = AxisAngle(f.forward, a_shake * kDegToRad);
		const float       side = a_session.Side();
		Place(_face.get(), a_anchor.rot * shake * f.Basis(), a_anchor.rot * f.keyhole + a_anchor.pos, 1.0f);

		// the arc along the right half; mirrored: the left half
		Place(_arc.get(), side < 0.0f ? AxisAngle({ 0, 0, 1 }, kPi) : Mat3{}, { 0.0f, -0.4f, 0.0f }, s);
		// the simulation's -90..90 is the handle going round the right side: top 0 .. bottom 180
		Show(_pick.get(), a_session.PickInserted());
		Place(_pick.get(), Mat3{}, OnFace(a_session.PickAngle() + 90.0f, 6.0f * s, side), 0.32f * s);
		if (_showSweetSpot) {
			// beads along the sweet spot just outside the arc, its edges in yellow
			const float from = p.sweetCenter + 90.0f - p.sweetHalfWidth;
			const float span = 2.0f * p.sweetHalfWidth;
			const int   n = std::clamp(static_cast<int>(span / 2.0f) + 1, 1, kSweetBeads);
			for (int i = 0; i < kSweetBeads; ++i) {
				auto* bead = _sweetBeads[i].get();
				Show(bead, i < n);
				const float a = n > 1 ? from + span * static_cast<float>(i) / static_cast<float>(n - 1) : from + span * 0.5f;
				Place(bead, Mat3{}, OnFace(a, 6.9f * s, side), 0.14f * s);
			}
			Place(_sweetLo.get(), Mat3{}, OnFace(from, 6.9f * s, side), 0.2f * s);
			Place(_sweetHi.get(), Mat3{}, OnFace(from + span, 6.9f * s, side), 0.2f * s);
		}

		RE::NiUpdateData ud{};
		_root->Update(ud);
	}
}
