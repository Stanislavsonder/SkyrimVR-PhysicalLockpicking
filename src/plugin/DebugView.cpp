#include "DebugView.h"

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

		RE::NiPoint3 ToNi(const Vec3& v) { return { v.x, v.y, v.z }; }

		void Place(RE::NiAVObject* a_obj, const Mat3& a_rot, const Vec3& a_pos, float a_scale)
		{
			if (!a_obj) {
				return;
			}
			a_obj->local.rotate = ToNi(a_rot);
			a_obj->local.translate = ToNi(a_pos);
			a_obj->local.scale = a_scale;
		}

		void Show(RE::NiAVObject* a_obj, bool a_show)
		{
			if (a_obj) {
				a_obj->SetAppCulled(!a_show);
			}
		}

		// point on the lock face at a_deg (0 = up, + = clockwise as seen by the player; a_side -1:
		// mirrored, see LockSession::Side)
		Vec3 OnFace(float a_deg, float a_radius, float a_depth, float a_side = 1.0f)
		{
			const float a = a_deg * kDegToRad;
			return { a_side * std::sin(a) * a_radius, a_depth, std::cos(a) * a_radius };
		}
	}

	RE::NiAVObject* DebugView::Add(RE::NiNode* a_parent, const char* a_model)
	{
		RE::NiPointer<RE::NiNode>         model;
		RE::BSModelDB::DBTraits::ArgsType args{};
		const std::string                 path = std::string("VRLockpicking\\") + a_model + ".nif";
		auto                              err = RE::BSModelDB::Demand(path.c_str(), model, args);
		if (err != RE::BSResource::ErrorCode::kNone || !model) {
			if (!_loadFailed) {
				logger::warn("Debug view: could not load '{}' (error {}); using gold coins as markers", path, static_cast<int>(err));
			}
			_loadFailed = true;
			// markers can be anything small: fall back to the gold coin
			if (_coinModel.empty()) {
				auto* gold = RE::TESForm::LookupByID<RE::TESObjectMISC>(0x0000000F);
				const char* m = gold ? static_cast<RE::TESModel*>(gold)->GetModel() : nullptr;
				_coinModel = m && *m ? m : "Clutter\\Coin01.nif";
			}
			model.reset();
			err = RE::BSModelDB::Demand(_coinModel.c_str(), model, args);
			if (err != RE::BSResource::ErrorCode::kNone || !model) {
				return nullptr;
			}
		}
		RE::NiObject* cloneObj = model->Clone();
		RE::NiNode*   clone = cloneObj ? cloneObj->AsNode() : nullptr;
		if (!clone) {
			return nullptr;
		}
		a_parent->AttachChild(clone, true);
		return clone;
	}

	bool DebugView::Create(const Config& a_cfg, RE::TESObjectREFR* a_target, bool a_overlay)
	{
		Destroy();
		_cfg = a_cfg;
		_overlay = a_overlay;
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
		_root.reset(RE::NiNode::Create(8));
		_root->name = "VRLockpickingDebug";
		_lock.reset(RE::NiNode::Create(32));
		_cylinder.reset(RE::NiNode::Create(8));
		_root->AttachChild(_lock.get(), true);
		_lock->AttachChild(_cylinder.get(), true);

		const float vs = std::clamp(_cfg.visualScale, 0.1f, 10.0f);
		const float s = _cfg.lockMeshRadius * vs / 8.0f;  // the models are made for a face half-size of 8
		if (!_overlay) {
			Place(Add(_lock.get(), "dbg_lock"), Mat3{}, {}, s);
			Place(Add(_cylinder.get(), "dbg_cylinder"), Mat3{}, {}, s);
			Place(Add(_cylinder.get(), "dbg_keyhole"), Mat3{}, {}, s);
			// top of the cylinder: white, green once both tools are in
			_topWhite.reset(Add(_cylinder.get(), "dbg_white"));
			_topGreen.reset(Add(_cylinder.get(), "dbg_green"));
			Place(_topWhite.get(), Mat3{}, Vec3{ 0, -0.1f, 2.6f } * s, 0.7f * s);
			Place(_topGreen.get(), Mat3{}, Vec3{ 0, -0.1f, 2.6f } * s, 0.7f * s);
		}
		// where a tool tip goes in: a see-through green cylinder around the spot, fInsertRadius wide,
		// from the keyhole into the lock (its model is radius 1, 0.6 deep = fMaxDepth at the default
		// radius); plus a dot on the keyhole centre
		Place(Add(_lock.get(), "dbg_white"), Mat3{}, { 0, -0.3f, 0 }, 0.2f * vs);
		_pickSpot.reset(Add(_lock.get(), "dbg_zone"));
		Place(_pickSpot.get(), Mat3{}, { 0, 0, _cfg.pickPointOffset * vs }, _cfg.insertRadius);
		if (std::fabs(_cfg.pickPointOffset + _cfg.wrenchPointOffset) > 0.01f) {
			// the tools go in at different spots: the wrench's as a magenta cube
			_wrenchSpot.reset(Add(_cylinder.get(), "dbg_magenta"));
			Place(_wrenchSpot.get(), Mat3{}, { 0, -0.3f, -_cfg.wrenchPointOffset * vs }, 0.45f * vs);
		}

		// the pick's range on the face: the right half, top to bottom, every 15 degrees
		for (int a = 0; a <= 180; a += 15) {
			Place(Add(_lock.get(), "dbg_gray"), Mat3{}, OnFace(static_cast<float>(a), 5.6f * s, 0.2f * s), 0.35f * s);
		}
		_pickMarker.reset(Add(_lock.get(), "dbg_cyan"));
		_sweet.reset(Add(_lock.get(), "dbg_lime"));
		_sweetLo.reset(Add(_lock.get(), "dbg_yellow"));
		_sweetHi.reset(Add(_lock.get(), "dbg_yellow"));

		// gauges above the lock: tracks of gray dots from x = -6 to +6
		for (int i = 0; i <= 10; ++i) {
			const float x = (-6.0f + 1.2f * i) * s;
			Place(Add(_lock.get(), "dbg_gray"), Mat3{}, { x, 0.0f, 10.0f * s }, 0.25f * s);
			Place(Add(_lock.get(), "dbg_gray"), Mat3{}, { x, 0.0f, 11.6f * s }, 0.25f * s);
		}
		_gLock.reset(Add(_lock.get(), "dbg_white"));
		_gAllowed.reset(Add(_lock.get(), "dbg_yellow"));
		_gTurn.reset(Add(_lock.get(), "dbg_red"));
		_gStrain.reset(Add(_lock.get(), "dbg_magenta"));

		if (!_overlay) {
			_pick.reset(Add(_root.get(), "dbg_pick"));
			_wrench.reset(Add(_root.get(), "dbg_wrench"));
		}
		_pickTipIn.reset(Add(_root.get(), "dbg_green"));
		_pickTipDeep.reset(Add(_root.get(), "dbg_orange"));
		_wrenchTipDeep.reset(Add(_root.get(), "dbg_orange"));
		_pickTipOut.reset(Add(_root.get(), "dbg_red"));
		_wrenchTipIn.reset(Add(_root.get(), "dbg_green"));
		_wrenchTipOut.reset(Add(_root.get(), "dbg_red"));

		if (_loadFailed && !_overlay) {
			logger::warn("Debug view: models missing (Meshes\\VRLockpicking) - is the mod fully installed? Showing the targets over the real models instead");
			Destroy();
			return false;
		}
		_parent->AttachChild(_root.get(), true);
		_textTimer = 0.0f;
		logger::info("Debug view on ({})", _overlay ? "targets over the models" : "coloured blocks");
		return true;
	}

	void DebugView::Destroy()
	{
		if (_root && _parent) {
			_parent->DetachChild(_root.get());
		}
		_pickSpot.reset();
		_wrenchSpot.reset();
		_sweet.reset();
		_sweetLo.reset();
		_sweetHi.reset();
		_gLock.reset();
		_gAllowed.reset();
		_gTurn.reset();
		_gStrain.reset();
		_pick.reset();
		_wrench.reset();
		_pickTipIn.reset();
		_pickTipDeep.reset();
		_wrenchTipDeep.reset();
		_pickMarker.reset();
		_topWhite.reset();
		_topGreen.reset();
		_pickTipOut.reset();
		_wrenchTipIn.reset();
		_wrenchTipOut.reset();
		_cylinder.reset();
		_lock.reset();
		_root.reset();
		_parent.reset();
		_loadFailed = false;
		_overlay = false;
	}

	void DebugView::Update(const LockSession& a_session, const Pose* a_pickHand, const Pose* a_wrenchHand, float a_shake, const Pose& a_anchor, float a_dt)
	{
		if (!_root || !_parent) {
			return;
		}
		_root->local = _parent->world.Invert();
		auto toWorld = [&a_anchor](const Mat3& a_rot, const Vec3& a_pos, RE::NiAVObject* a_obj, float a_scale) {
			Place(a_obj, a_anchor.rot * a_rot, a_anchor.rot * a_pos + a_anchor.pos, a_scale);
		};

		const Config&    sc = a_session.GetConfig();
		const LockFrame& f = a_session.Frame();
		const LockParams& p = a_session.Params();
		const float      vs = std::clamp(_cfg.visualScale, 0.1f, 10.0f);
		const float      s = _cfg.lockMeshRadius * vs / 8.0f;
		const Mat3       shake = AxisAngle(f.forward, a_shake * kDegToRad);
		toWorld(shake * f.Basis(), f.keyhole, _lock.get(), 1.0f);
		Place(_cylinder.get(), AxisAngle({ 0, 1, 0 }, a_session.Side() * a_session.LockAngle() * 90.0f * kDegToRad), {}, 1.0f);

		// sweet spot and its edges (where the lock starts turning partly)
		// (the simulation's -90..90 is the handle going round the right side: top 0 .. bottom 180)
		Place(_sweet.get(), Mat3{}, OnFace(p.sweetCenter + 90.0f, 6.6f * s, 0.2f * s, a_session.Side()), 0.5f * s);
		Place(_sweetLo.get(), Mat3{}, OnFace(p.sweetCenter + 90.0f - p.sweetHalfWidth, 6.6f * s, 0.2f * s, a_session.Side()), 0.45f * s);
		Place(_sweetHi.get(), Mat3{}, OnFace(p.sweetCenter + 90.0f + p.sweetHalfWidth, 6.6f * s, 0.2f * s, a_session.Side()), 0.45f * s);
		Show(_pickMarker.get(), a_session.PickInserted());
		Place(_pickMarker.get(), Mat3{}, OnFace(a_session.PickAngle() + 90.0f, 5.6f * s, 0.0f, a_session.Side()), 0.55f * s);
		const bool bothIn = a_session.PickInserted() && a_session.WrenchInserted();
		Show(_topWhite.get(), !bothIn);
		Show(_topGreen.get(), bothIn);

		auto gauge = [s](float a_value, float a_z) { return Vec3{ (-6.0f + 12.0f * Clamp01(a_value)) * s, -0.2f * s, a_z * s }; };
		Place(_gLock.get(), Mat3{}, gauge(a_session.LockAngle(), 10.0f), 0.55f * s);
		Place(_gAllowed.get(), Mat3{}, gauge(a_session.AllowedTurn(), 10.0f), 0.45f * s);
		Place(_gTurn.get(), Mat3{}, gauge(a_session.TurnInput(), 10.0f), 0.35f * s);
		const float strain = p.unbreakable ? 0.0f : Clamp01(a_session.PickDamage() / std::max(p.breakSeconds, 0.01f));
		Place(_gStrain.get(), Mat3{}, gauge(strain, 11.6f), 0.5f * s);

		// tools (same poses as the real models) and where their tips really are:
		// green = in place, red = out, orange = pushed through
		const float toolScale = _cfg.toolMeshLength * vs / 16.0f;
		auto        tipMarker = [&](const Vec3& a_tip, ToolPlace a_place, RE::NiAVObject* a_in, RE::NiAVObject* a_out, RE::NiAVObject* a_deep) {
            RE::NiAVObject* marker = a_place == ToolPlace::kIn ? a_in : (a_place == ToolPlace::kTooDeep ? a_deep : a_out);
            Show(a_in, marker == a_in);
            Show(a_out, marker == a_out);
            Show(a_deep, marker == a_deep);
            toWorld(Mat3{}, a_tip, marker, 0.6f * vs);
		};
		Show(_pick.get(), a_pickHand != nullptr);
		if (a_pickHand) {
			const Pose tool = a_session.PickVisual(*a_pickHand);
			toWorld(a_session.PickInserted() ? shake * tool.rot : tool.rot, tool.pos, _pick.get(), toolScale);
			const Vec3 tip = a_session.PickTool(*a_pickHand).pos;
			tipMarker(tip, a_session.PickPlace(), _pickTipIn.get(), _pickTipOut.get(), _pickTipDeep.get());
		}
		Show(_wrench.get(), a_wrenchHand != nullptr);
		if (a_wrenchHand) {
			const Pose tool = a_session.WrenchVisual(*a_wrenchHand);
			toWorld(a_session.WrenchInserted() ? shake * tool.rot : tool.rot, tool.pos, _wrench.get(), toolScale);
			const Vec3 tip = a_session.WrenchTool(*a_wrenchHand).pos;
			tipMarker(tip, a_session.WrenchPlace(), _wrenchTipIn.get(), _wrenchTipOut.get(), _wrenchTipDeep.get());
		}

		RE::NiUpdateData ud{};
		_root->Update(ud);

		// numbers, every few seconds (notifications queue up if sent faster)
		_textTimer -= a_dt;
		if (_textTimer <= 0.0f && !_quiet) {
			_textTimer = 3.0f;
			// "in", or why not (how near a tool is, the tip cubes show)
			auto place = [](ToolPlace a_p, bool a_engaged, const char* a_blocked) {
				if (a_engaged) {
					return std::string("in");
				}
				return std::string(a_p == ToolPlace::kTooDeep ? "too deep" : (a_p == ToolPlace::kIn ? a_blocked : "out"));
			};
			const std::string picks = _picksLeft < 0 ? std::string("unbreakable") : std::format("{}", _picksLeft);
			const char* pickBlocked = a_session.PickNeedsReinsert() ? "take it out for a new pick" : "handle on the left";
			const char* wrenchBlocked = "turn to start (back of hand left)";
			const std::string text = std::format(
				"pick {:.0f} (sweet {:.0f}+-{:.0f}) | turn {:.0f}% lock {:.0f}% max {:.0f}% | strain {:.0f}% | pick {} | blade {}{} | target {:.1f} hold {:.1f} depth {:.1f} | lockpicks {}",
				a_session.PickAngle() + 90.0f, p.sweetCenter + 90.0f, p.sweetHalfWidth, a_session.TurnInput() * 100.0f,
				a_session.LockAngle() * 100.0f, a_session.AllowedTurn() * 100.0f, strain * 100.0f,
				place(a_session.PickPlace(), a_session.PickInserted(), pickBlocked),
				place(a_session.WrenchPlace(), a_session.WrenchInserted(), wrenchBlocked),
				a_session.WrenchNeedsReset() ? " (turn back to start)" : "", sc.insertRadius, sc.holdRadius, sc.maxDepth, picks);
			RE::SendHUDMessage::ShowHUDMessage(text.c_str());
			logger::info("debug: {}", text);
		}
	}
}
