#include "PickCounter.h"

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

		void StripCollision(RE::NiAVObject* a_obj, int a_depth = 0)
		{
			if (!a_obj || a_depth > 32) {
				return;
			}
			a_obj->collisionObject.reset();
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) {
					StripCollision(child.get(), a_depth + 1);
				}
			}
		}

		// Seven-segment digits: a top, b top right, c bottom right, d bottom, e bottom left,
		// f top left, g middle.
		constexpr std::uint8_t kDigitSegments[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
	}

	RE::NiAVObject* PickCounter::Add(const char* a_path)
	{
		RE::NiPointer<RE::NiNode>         model;
		RE::BSModelDB::DBTraits::ArgsType args{};
		if (RE::BSModelDB::Demand(a_path, model, args) != RE::BSResource::ErrorCode::kNone || !model) {
			logger::warn("Pick counter: could not load '{}'", a_path);
			return nullptr;
		}
		RE::NiObject* cloneObj = model->Clone();
		RE::NiNode*   clone = cloneObj ? cloneObj->AsNode() : nullptr;
		if (clone) {
			StripCollision(clone);  // a lockpick item's model shares its rigid body with the world
			for (auto& child : clone->GetChildren()) {
				if (child) {
					child->local = RE::NiTransform();
				}
			}
			clone->local = RE::NiTransform();
			_face->AttachChild(clone, true);
		}
		return clone;
	}

	bool PickCounter::Create(const Config& a_cfg, RE::TESObjectREFR* a_target)
	{
		Destroy();
		_cfg = a_cfg;
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
		_root->name = "VRLockpickingPickCounter";
		_face.reset(RE::NiNode::Create(1 + kDigits * 7));
		_root->AttachChild(_face.get(), true);
		_icon.reset(Add("Clutter\\Common\\Lockpick.nif"));
		for (int i = 0; i < kDigits * 7; ++i) {
			_segments.emplace_back(Add("VRLockpicking\\count_seg.nif"));
		}
		_parent->AttachChild(_root.get(), true);
		SetCount(_count);
		return true;
	}

	void PickCounter::Destroy()
	{
		if (_root && _parent) {
			_parent->DetachChild(_root.get());
		}
		_segments.clear();
		_icon.reset();
		_face.reset();
		_root.reset();
		_parent.reset();
	}

	void PickCounter::SetCount(int a_picks)
	{
		_count = a_picks;
		if (!_root) {
			return;
		}
		_root->SetAppCulled(a_picks < 0);
	}

	void PickCounter::Update(const LockSession& a_session, float a_shake, const Pose& a_anchor)
	{
		if (!_root || !_parent || _count < 0) {
			return;
		}
		_root->local = _parent->world.Invert();
		const LockFrame& f = a_session.Frame();
		const Mat3       shake = AxisAngle(f.forward, a_shake * kDegToRad);
		Place(_face.get(), a_anchor.rot * shake * f.Basis(), a_anchor.rot * f.keyhole + a_anchor.pos, 1.0f);

		// on the face (x right, y into the lock, z up), over the lock: the icon, then the number. The
		// vanilla lock reaches about 5.7 above the keyhole; the digits sit about 1.8 over it.
		const float s = _cfg.lockMeshRadius * std::clamp(_cfg.visualScale, 0.1f, 10.0f) / 8.0f;
		const float w = 0.8f * s, h = 1.4f * s, gap = 0.45f * s;
		const float y = -0.6f, z = 8.2f * s;
		const std::string digits = std::to_string(std::clamp(_count, 0, 999));
		const float width = 2.6f * s + static_cast<float>(digits.size()) * (w + gap);
		float       x = -width * 0.5f;

		// the lockpick lies along its model's X; 2.2 long, tilted up a little
		Place(_icon.get(), AxisAngle({ 0, 1, 0 }, -20.0f * kDegToRad), { x + 1.1f * s, y, z }, 2.2f * s / 21.4f);
		x += 2.6f * s;

		const Mat3 horizontal{};
		const Mat3 vertical = AxisAngle({ 0, 1, 0 }, 0.5f * kPi);
		for (int d = 0; d < kDigits; ++d) {
			const bool         used = d < static_cast<int>(digits.size());
			const std::uint8_t mask = used ? kDigitSegments[digits[d] - '0'] : 0;
			const float        cx = x + w * 0.5f;
			const struct
			{
				Vec3  pos;
				bool  upright;
			} seg[7] = {
				{ { cx, y, z + h * 0.5f }, false },              // a
				{ { cx + w * 0.5f, y, z + h * 0.25f }, true },   // b
				{ { cx + w * 0.5f, y, z - h * 0.25f }, true },   // c
				{ { cx, y, z - h * 0.5f }, false },              // d
				{ { cx - w * 0.5f, y, z - h * 0.25f }, true },   // e
				{ { cx - w * 0.5f, y, z + h * 0.25f }, true },   // f
				{ { cx, y, z }, false },                         // g
			};
			for (int i = 0; i < 7; ++i) {
				auto* obj = _segments[d * 7 + i].get();
				Show(obj, (mask >> i) & 1);
				Place(obj, seg[i].upright ? vertical : horizontal, seg[i].pos, seg[i].upright ? h * 0.5f : w);
			}
			if (used) {
				x += w + gap;
			}
		}

		RE::NiUpdateData ud{};
		_root->Update(ud);
	}
}
