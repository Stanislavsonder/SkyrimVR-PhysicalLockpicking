#include "Visuals.h"

#include "AddressCheck.h"

namespace vrlp
{
	namespace
	{
		constexpr const char* kMenuPickMesh = "Interface\\Lockpicking\\LockPick01.nif";
		constexpr float       kPickSink = 2.5f;  // how far the pick model reaches past its tip point
		constexpr const char* kItemPickKey = "clutter\\common\\lockpick.nif";  // the lockpick item's model, normalised
		constexpr RE::FormID  kLockpickID = 0x0000000A;

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
		Vec3         FromNi(const RE::NiPoint3& v) { return { v.x, v.y, v.z }; }

		void SetLocal(RE::NiAVObject* a_obj, const Mat3& a_rot, const Vec3& a_pos)
		{
			a_obj->local.rotate = ToNi(a_rot);
			a_obj->local.translate = ToNi(a_pos);
		}

		RE::NiTransform MakeTransform(const Mat3& a_rot, float a_scale, const Vec3& a_pos)
		{
			RE::NiTransform t;
			t.rotate = ToNi(a_rot);
			t.scale = a_scale;
			t.translate = ToNi(a_pos);
			return t;
		}

		std::string Lower(std::string_view a_s)
		{
			std::string r(a_s);
			std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return r;
		}

		bool NameHas(const RE::NiAVObject* a_obj, std::string_view a_needleLower)
		{
			const char* n = a_obj ? a_obj->name.c_str() : nullptr;
			return n && !a_needleLower.empty() && Lower(n).find(a_needleLower) != std::string::npos;
		}

		bool IsCylinderName(const RE::NiAVObject* a_obj)
		{
			for (const char* n : { "tumble", "cylinder", "keyhole", "inner", "plug" }) {
				if (NameHas(a_obj, n)) {
					return true;
				}
			}
			return false;
		}

		// A lock model's nodes in their rest pose (as in the file), by node name.
		struct NodePose
		{
			const char*     name;
			RE::NiTransform local;
		};

		RE::NiTransform RestLocal(float tx, float ty, float tz, std::initializer_list<float> a_rot)
		{
			RE::NiTransform t;
			t.translate = { tx, ty, tz };
			auto r = a_rot.begin();
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					t.rotate.entry[i][j] = *r++;
				}
			}
			return t;
		}

		using NodePoses = std::vector<std::pair<std::string, RE::NiTransform>>;

		// Rest poses: the vanilla lock (read from the game with the lockpicking menu never opened),
		// and every other lock model as first loaded before the vanilla menu ran.
		std::map<std::string, NodePoses>& RestPoses()
		{
			static std::map<std::string, NodePoses> poses = [] {
				const NodePose vanilla[] = {
					{ "LockPickLock01", RestLocal(0.000f, -4.983f, 0.000f, { -1, 0, 0, 0, 1, 0, 0, 0, -1 }) },
					{ "LockPickLockTumble", RestLocal(-14.521f, 2.782f, -1.482f, { 1, 0, 0, 0, 1, 0, 0, 0, 1 }) },
					{ "LockPickLockTumble:2", RestLocal(-33.497f, 1.190f, -55.454f, { 1, 0, 0, 0, 1, 0, 0, 0, 1 }) },
					{ "LockPickLockTumble:3", RestLocal(-33.497f, 1.190f, -55.454f, { 1, 0, 0, 0, 1, 0, 0, 0, 1 }) },
					{ "LockPickLock01:2", RestLocal(-48.018f, 3.972f, -56.936f, { 1, 0, 0, 0, 1, 0, 0, 0, 1 }) },
					{ "LockPickLock01:4", RestLocal(-48.018f, 3.972f, -56.936f, { 1, 0, 0, 0, 1, 0, 0, 0, 1 }) },
					{ "Shiv", RestLocal(56.979f, -74.381f, -74.377f, { -0.0059f, -0.4223f, 0.9064f, 0.1384f, 0.8974f, 0.4190f, -0.9904f, 0.1279f, 0.0532f }) },
					{ "Shiv:0", RestLocal(0.079f, -128.889f, 0.492f, { 1, 0, 0, 0, 1, 0, 0, 0, 1 }) },
					{ "Shiv:1", RestLocal(0.079f, -128.889f, 0.492f, { 1, 0, 0, 0, 1, 0, 0, 0, 1 }) },
				};
				std::map<std::string, NodePoses> out;
				auto& list = out[NormalizeModelPath("Interface\\Lockpicking\\LockPickShiv01.nif")];
				for (const auto& n : vanilla) {
					list.emplace_back(n.name, n.local);
				}
				return out;
			}();
			return poses;
		}

		bool g_vanillaMenuRan = false;

		void CollectPose(RE::NiAVObject* a_obj, NodePoses& a_out, int a_depth = 0)
		{
			if (!a_obj || a_depth > 32) {
				return;
			}
			if (a_depth > 0 && a_obj->name.c_str() && *a_obj->name.c_str()) {
				a_out.emplace_back(a_obj->name.c_str(), a_obj->local);
			}
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) {
					CollectPose(child.get(), a_out, a_depth + 1);
				}
			}
		}

		int ApplyPose(RE::NiAVObject* a_obj, const NodePoses& a_pose, int a_depth = 0)
		{
			if (!a_obj || a_depth > 32) {
				return 0;
			}
			int set = 0;
			if (a_depth > 0 && a_obj->name.c_str()) {
				for (const auto& [name, local] : a_pose) {
					if (name == a_obj->name.c_str()) {
						a_obj->local = local;
						++set;
						break;
					}
				}
			}
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) {
					set += ApplyPose(child.get(), a_pose, a_depth + 1);
				}
			}
			return set;
		}

		// Cloned collision would only be dead weight, and must never reach the physics world: the
		// clone shares the rigid body of the model's collision, so updating the clone moves it to
		// where that body is - a lockpick lying somewhere in the world (seen in game: thousands of
		// units off). Removed before the clone is first updated.
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

		struct WalkState
		{
			const Config*         cfg;
			bool                  isLock;
			bool                  shivOnly{ false };  // keep only the screwdriver (the tension wrench model)
			std::vector<MeshPart> parts;
			RE::NiAVObject*       cylinderNode{ nullptr };
			Vec3                  shivSum;
			int                   shivCount{ 0 };
			bool                  unhide{ false };
			int                   unhidden{ 0 };
			std::string           log;
		};

		// a_parent: the parent's transform in mesh space. Worked out here rather than read from the
		// nodes' world transforms and bounds, which a model shared with the world (the lockpick item)
		// can carry over from where the game last drew it.
		void Walk(RE::NiAVObject* a_obj, const RE::NiTransform& a_parent, int a_depth, bool a_shiv, bool a_cyl, WalkState& a_st)
		{
			if (!a_obj || a_depth > 32) {
				return;
			}
			// a tool model shared with the world (the lockpick item) can be hidden there, which the
			// clone copies: shown here (a lock's own hidden nodes stay hidden)
			if (a_st.unhide && a_obj->GetAppCulled()) {
				a_obj->SetAppCulled(false);
				++a_st.unhidden;
			}
			const RE::NiTransform xf = a_parent * a_obj->local;

			const bool shiv = a_shiv || NameHas(a_obj, "shiv") || NameHas(a_obj, "screwdriver");
			bool       cyl = a_cyl;
			if (a_st.isLock && !cyl && a_depth > 0 && a_obj->AsNode() && IsCylinderName(a_obj)) {
				cyl = true;
				if (!a_st.cylinderNode) {
					a_st.cylinderNode = a_obj;
				}
			}
			const auto* rtti = a_obj->GetRTTI();
			a_st.log += std::format("\n    {:>{}}{} [{}]", "", a_depth * 2, a_obj->name.c_str() ? a_obj->name.c_str() : "",
				rtti && rtti->GetName() ? rtti->GetName() : "?");
			if (a_st.isLock) {
				// the node's own transform: an animated model (the lockpicking menu's) shows the pose
				// the menu left it in
				const auto& t = a_obj->local;
				a_st.log += std::format(" local t=({:.3f}, {:.3f}, {:.3f}) r=[{:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f}] s={:.4f}",
					t.translate.x, t.translate.y, t.translate.z, t.rotate.entry[0][0], t.rotate.entry[0][1], t.rotate.entry[0][2],
					t.rotate.entry[1][0], t.rotate.entry[1][1], t.rotate.entry[1][2], t.rotate.entry[2][0], t.rotate.entry[2][1],
					t.rotate.entry[2][2], t.scale);
			}

			if (auto* geom = a_obj->AsGeometry()) {
				MeshPart       part;
				const RE::NiBound& mb = geom->GetModelData().modelBound;
				part.centre = FromNi(xf * mb.center);
				part.radius = mb.radius * xf.scale;
				part.cylinder = cyl;
				std::string box = "no box";
				if (auto* bb = geom->GetVRModelBoundBox()) {
					const Vec3 c = FromNi(bb->center);
					const Vec3 h = FromNi(bb->halfExtents);
					if (std::isfinite(h.x + h.y + h.z + c.x + c.y + c.z) && (h.x > 1e-3f || h.y > 1e-3f || h.z > 1e-3f) &&
						Length(h) < 1e5f) {
						for (int i = 0; i < 8; ++i) {
							const Vec3 local{ c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y), c.z + ((i & 4) ? h.z : -h.z) };
							part.corners.push_back(FromNi(xf * ToNi(local)));
						}
						// must agree with the bounding sphere, or it is not what we think it is
						const float slack = part.radius * 0.25f + 0.5f;
						bool        ok = true;
						for (const auto& q : part.corners) {
							ok = ok && Distance(q, part.centre) <= part.radius * 1.75f + slack;
						}
						ok = ok && Distance(FromNi(xf * ToNi(c)), part.centre) <= part.radius + slack;
						box = std::format("box c=({:.1f}, {:.1f}, {:.1f}) half=({:.1f}, {:.1f}, {:.1f}){}", c.x, c.y, c.z, h.x, h.y, h.z,
							ok ? "" : " (ignored: disagrees with the bound)");
						if (!ok) {
							part.corners.clear();
						}
					}
				}
				a_st.log += std::format(" bound r={:.1f} c=({:.1f}, {:.1f}, {:.1f}) {}{}{}", part.radius, part.centre.x, part.centre.y,
					part.centre.z, box, cyl ? " [turns]" : "", shiv && a_st.isLock ? " [screwdriver]" : "");
				if (a_st.isLock && shiv) {
					a_st.shivSum += part.centre;
					++a_st.shivCount;
					a_obj->SetAppCulled(true);  // the screwdriver is drawn in the hand
				} else if (a_st.shivOnly && !shiv) {
					a_obj->SetAppCulled(true);
				} else if (part.radius > 0.0f) {
					a_st.parts.push_back(std::move(part));
				}
			}
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) {
					Walk(child.get(), xf, a_depth + 1, shiv, cyl, a_st);
				}
			}
		}

		std::string LockpickItemModel()
		{
			auto* misc = RE::TESForm::LookupByID<RE::TESObjectMISC>(kLockpickID);
			const char* model = misc ? static_cast<RE::TESModel*>(misc)->GetModel() : nullptr;
			return model && *model ? model : kMenuPickMesh;
		}
	}

	bool Visuals::LoadModel(Model& a_out, Kind a_kind, const std::string& a_path)
	{
		RE::NiPointer<RE::NiNode>         model;
		RE::BSModelDB::DBTraits::ArgsType args{};
		const auto                        err = RE::BSModelDB::Demand(a_path.c_str(), model, args);
		if (err != RE::BSResource::ErrorCode::kNone || !model) {
			logger::warn("Could not load mesh '{}' (error {})", a_path, static_cast<int>(err));
			return false;
		}

		// Never attach the shared cached model itself - clone it.
		RE::NiObject* cloneObj = model->Clone();
		RE::NiNode*   clone = cloneObj ? cloneObj->AsNode() : nullptr;
		if (!clone) {
			logger::warn("Could not clone mesh '{}'", a_path);
			return false;
		}

		RE::NiNode* holder = RE::NiNode::Create(1);
		holder->AttachChild(clone, true);
		a_out = {};
		a_out.holder.reset(holder);
		a_out.clone.reset(clone);
		a_out.kind = a_kind;
		a_out.path = a_path;

		if (a_kind == Kind::kLock || a_kind == Kind::kShiv) {
			// the vanilla menu animates lock models (the turning cylinder, the screwdriver sliding
			// in) and leaves them in its last pose: put them back in their rest pose
			auto&      poses = RestPoses();
			const auto key = NormalizeModelPath(a_path);
			if (const auto it = poses.find(key); it != poses.end()) {
				ApplyPose(clone, it->second);
			} else if (!g_vanillaMenuRan) {
				CollectPose(clone, poses[key]);
			} else {
				logger::info("Mesh '{}': first loaded after the vanilla lockpicking menu ran; it may show that menu's pose", a_path);
			}
		}

		// Evaluate the model with an identity root so every bound is in "mesh space".
		StripCollision(clone);
		clone->local = RE::NiTransform();
		holder->local = RE::NiTransform();
		RE::NiUpdateData ud{};
		holder->Update(ud);
		a_out.bound = clone->worldBound;

		WalkState st{ &_cfg, a_kind == Kind::kLock, a_kind == Kind::kShiv };
		st.unhide = a_kind == Kind::kTool;
		Walk(clone, RE::NiTransform(), 0, false, false, st);
		if (a_kind == Kind::kShiv && st.parts.empty()) {
			logger::info("Mesh '{}' has no screwdriver part", a_path);
			a_out = {};
			return false;
		}
		a_out.parts = std::move(st.parts);
		// The vanilla lockpick is not measured at all: its shape is known (one part along X, 21.4
		// long, from the file), so it is drawn the same whatever else happens in the game.
		if (a_kind == Kind::kTool && NormalizeModelPath(a_path) == kItemPickKey) {
			MeshPart part;
			part.radius = 10.7f;
			const Vec3 h{ 10.7f, 0.5f, 0.8f };
			for (int i = 0; i < 8; ++i) {
				part.corners.push_back({ (i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z });
			}
			a_out.parts = { part };
			// ...and its one shape sits at the model's origin, as in the file
			for (auto& child : clone->GetChildren()) {
				if (child) {
					child->local = RE::NiTransform();
				}
			}
		}
		if (!a_out.parts.empty()) {
			// the whole model's bound from its parts, for the same reason as in Walk
			Vec3 lo = a_out.parts.front().centre, hi = lo;
			for (const auto& part : a_out.parts) {
				const Vec3 r{ part.radius, part.radius, part.radius };
				lo = { std::min(lo.x, part.centre.x - r.x), std::min(lo.y, part.centre.y - r.y), std::min(lo.z, part.centre.z - r.z) };
				hi = { std::max(hi.x, part.centre.x + r.x), std::max(hi.y, part.centre.y + r.y), std::max(hi.z, part.centre.z + r.z) };
			}
			const Vec3 c = (lo + hi) * 0.5f;
			float      radius = 0.0f;
			for (const auto& part : a_out.parts) {
				radius = std::max(radius, Distance(c, part.centre) + part.radius);
			}
			a_out.bound.center = { c.x, c.y, c.z };
			a_out.bound.radius = radius;
		}
		// The screwdriver starts out in front of the lock (it slides in during the menu's intro),
		// so it tells which way the lock faces. Without one: the menu camera looks along +Y.
		a_out.frontHint = st.shivCount > 0 ? st.shivSum / static_cast<float>(st.shivCount) : Vec3{ 0.0f, -1000.0f, 0.0f };
		static std::set<std::string> logged;
		if (logged.insert(a_path).second || _cfg.debugLogging) {
			logger::info("Mesh '{}' nodes:{}", a_path, st.log);
		}
		// ...and faded out there
		float fade = 1.0f;
		if (auto* fadeNode = clone->AsFadeNode(); fadeNode && a_kind == Kind::kTool) {
			fade = fadeNode->GetRuntimeData().currentFade;
			fadeNode->GetRuntimeData().currentFade = 1.0f;
		}
		if (st.unhidden > 0) {
			logger::info("Mesh '{}': {} node(s) hidden in the game's copy (fade {:.2f}); shown", a_path, st.unhidden, fade);
		}

		if (a_kind == Kind::kLock) {
			_cylinder.reset();
			if (st.cylinderNode && st.cylinderNode->parent) {
				_cylinderWorld0 = st.cylinderNode->world;
				_cylinderParent0 = st.cylinderNode->parent->world;
				_cylinder.reset(st.cylinderNode);
				logger::info("Lock: turning part '{}'", st.cylinderNode->name.c_str());
			} else {
				logger::info("Lock: no turning part found - the wrench shows the turn");
			}
		}
		ApplyFit(a_out);
		return true;
	}

	void Visuals::ApplyFit(Model& a_model, bool a_log)
	{
		if (!a_model.clone) {
			return;
		}
		const float   scale = std::clamp(_cfg.visualScale, 0.1f, 10.0f);
		MeshPlacement fit;
		if (a_model.kind == Kind::kLock) {
			LockFit lf = FitLockParts(a_model.parts, a_model.frontHint, _cfg.lockMeshRadius * scale);
			if (_profile && lf.place.valid) {
				lf = ApplyLockProfile(lf, *_profile);  // measured with the markup tool
				_keyholeRadius = _profile->radius * lf.place.scale;
				if (a_log) {
					logger::info("Lock profile for '{}': keyhole ({:.1f}, {:.1f}, {:.1f}), circle {:.2f}, roll {:.1f}", a_model.path,
						_profile->keyhole.x, _profile->keyhole.y, _profile->keyhole.z, _keyholeRadius, _profile->roll);
				}
			}
			fit = lf.place;
			_cylinderPivot = lf.keyhole;
			_cylinderAxis = lf.forward;
			if (fit.valid && a_log) {
				logger::info("Lock fit: keyhole ({:.1f}, {:.1f}, {:.1f}), into the lock ({:.0f}, {:.0f}, {:.0f}), scale {:.3f}{}", lf.keyhole.x,
					lf.keyhole.y, lf.keyhole.z, lf.forward.x, lf.forward.y, lf.forward.z, fit.scale,
					lf.fromCylinder ? ", keyhole on the turning part" : "");
			}
		} else {
			// Menu models (pick, screwdriver) start out in front of the lock, which sits at the
			// menu's origin, so their tip is the end nearer it. The lockpick item model
			// (Clutter\Common\Lockpick.nif) lies along X with its tip at -X (seen in game).
			const Vec3  lockInMenu{};
			const bool  menuModel = a_model.path.find("Interface") != std::string::npos;
			const bool  shiv = a_model.kind == Kind::kShiv;
			const bool  itemPick = !menuModel && NormalizeModelPath(a_model.path) == kItemPickKey;
			const bool  flip = (!shiv && _cfg.flipPickMesh) != itemPick;
			fit = FitToolParts(a_model.parts, _cfg.toolMeshLength * scale, flip, menuModel ? &lockInMenu : nullptr);
			const bool  measured = ToolProfileFor(a_model).valid;
			if (fit.valid && a_log) {
				const Vec3 tip = fit.rot.Transposed() * (-fit.offset) / fit.scale;
				const Vec3 dir = fit.rot.Transposed() * Vec3{ 0, 1, 0 };
				logger::info("Tool fit '{}'{}: {} parts, tip ({:.1f}, {:.1f}, {:.1f}) pointing ({:.2f}, {:.2f}, {:.2f}), scale {:.3f}{}",
					a_model.path, shiv ? " (screwdriver)" : "", a_model.parts.size(), tip.x, tip.y, tip.z, dir.x, dir.y, dir.z, fit.scale,
					measured ? ", aligned (markup tool or built in)" : "");
				for (const auto& part : a_model.parts) {
					Vec3 lo = part.corners.empty() ? part.centre : part.corners.front(), hi = lo;
					for (const auto& c : part.corners) {
						lo = { std::min(lo.x, c.x), std::min(lo.y, c.y), std::min(lo.z, c.z) };
						hi = { std::max(hi.x, c.x), std::max(hi.y, c.y), std::max(hi.z, c.z) };
					}
					logger::info("    part: centre ({:.1f}, {:.1f}, {:.1f}) radius {:.1f}, box ({:.1f}, {:.1f}, {:.1f})..({:.1f}, {:.1f}, {:.1f})",
						part.centre.x, part.centre.y, part.centre.z, part.radius, lo.x, lo.y, lo.z, hi.x, hi.y, hi.z);
				}
			}
		}
		if (!fit.valid) {
			// Fallback: centre the bounding sphere on the holder origin and scale it.
			const float radius = a_model.bound.radius;
			const Vec3  centre = FromNi(a_model.bound.center);
			const float target = (a_model.kind == Kind::kLock ? _cfg.lockMeshRadius : _cfg.toolMeshLength * 0.5f) * scale;
			fit.rot = Mat3{};
			fit.scale = (target > 0.0f && radius > 0.001f) ? target / radius : 1.0f;
			fit.offset = -(centre * fit.scale);
			if (a_model.kind == Kind::kLock) {
				_cylinderPivot = centre;
				_cylinderAxis = { 0, 1, 0 };
			}
		}

		const bool    lock = a_model.kind == Kind::kLock;
		MeshPlacement total = fit;
		total.valid = true;
		if (!lock) {
			total = ApplyToolProfile(total, ToolProfileFor(a_model));  // aligned in the hand (markup tool)
		}
		if (&a_model == &_pick) {
			// the pick's model reaches a little past the tip point, so it is seen going into the
			// keyhole rather than stopping at its rim (the tool's +Y points into the lock)
			total.offset += Vec3{ 0.0f, kPickSink * scale, 0.0f };
		}
		a_model.clone->local = MakeTransform(total.rot, total.scale, total.offset);
		total.valid = fit.valid;
		if (lock) {
			_lockTotal = total;
		}
	}

	void Visuals::SetToolProfiles(std::map<std::string, ToolProfile> a_picks, std::map<std::string, ToolProfile> a_shivs)
	{
		_pickProfiles = std::move(a_picks);
		_shivProfiles = std::move(a_shivs);
	}

	ToolProfile Visuals::ToolProfileFor(const Model& a_model) const
	{
		const bool  shiv = a_model.kind == Kind::kShiv;
		const auto  key = NormalizeModelPath(a_model.path);
		const auto& own = shiv ? _shivProfiles : _pickProfiles;
		const auto& builtIn = DefaultToolProfiles(shiv ? "shiv" : "pick");
		// a Lock Variations lock carries a copy of the vanilla screwdriver: unless it was aligned
		// itself, it takes the vanilla screwdriver's alignment
		static const std::string vanillaShiv = NormalizeModelPath(Config{}.lockMesh);
		for (const auto& k : { key, shiv ? vanillaShiv : key }) {
			if (const auto it = own.find(k); it != own.end()) {
				return it->second;
			}
			if (const auto it = builtIn.find(k); it != builtIn.end()) {
				return it->second;
			}
		}
		return ToolProfile{};
	}

	ToolProfile Visuals::ToolAlignment(bool a_wrench) const
	{
		ToolProfile p = ToolProfileFor(a_wrench ? _wrench : _pick);
		p.valid = true;  // identity when there is none yet
		return p;
	}

	void Visuals::SetToolProfile(bool a_wrench, const ToolProfile& a_profile, bool a_log)
	{
		Model&     model = a_wrench ? _wrench : _pick;
		const bool shiv = model.kind == Kind::kShiv;
		const auto key = NormalizeModelPath(model.path);
		if (a_profile.valid) {
			(shiv ? _shivProfiles : _pickProfiles)[key] = a_profile;
		} else {
			(shiv ? _shivProfiles : _pickProfiles).erase(key);
		}
		// the same model may be both tools (a lock without a screwdriver)
		ApplyFit(_pick, a_log);
		ApplyFit(_wrench, a_log);
	}

	void Visuals::SetLockProfile(const LockProfile& a_profile)
	{
		_profile = a_profile;
		ApplyFit(_lock);
	}

	void Visuals::TurnCylinder(float a_angleRad)
	{
		if (!_cylinder) {
			return;
		}
		const Mat3      r = AxisAngle(_cylinderAxis, a_angleRad);
		RE::NiTransform turn;
		turn.rotate = ToNi(r);
		turn.translate = ToNi(_cylinderPivot - r * _cylinderPivot);
		_cylinder->local = _cylinderParent0.Invert() * (turn * _cylinderWorld0);
	}

	bool Visuals::Create(const Config& a_cfg, RE::TESObjectREFR* a_target, const LockProfile* a_profile)
	{
		Destroy();
		_cfg = a_cfg;
		_profile.reset();
		_keyholeRadius = 0.0f;
		if (a_profile && a_profile->valid) {
			_profile = *a_profile;
		}
		if (!_cfg.showMeshes) {
			return false;
		}
		if (!AddressCheck::Has(Ids::kMeshes)) {
			logger::warn("Visuals disabled: address library lacks mesh loading IDs");
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
			logger::warn("Visuals: no parent node");
			return false;
		}
		_parent.reset(parent);
		_root.reset(RE::NiNode::Create(3));
		_root->name = "VRLockpickingRoot";

		if (LoadModel(_lock, Kind::kLock, _cfg.lockMesh)) {
			_root->AttachChild(_lock.holder.get(), true);
		}
		// Tools: the lockpick item's own model (or a Lock Variations pick, or the Skeleton Key), the
		// lockpicking menu's pick as a fallback. The tension wrench is the menu's screwdriver (out
		// of the lock model), like in the vanilla minigame, or a second pick if the lock has none.
		std::string toolPath = !_cfg.pickMesh.empty() ? _cfg.pickMesh : LockpickItemModel();
		if (!LoadModel(_pick, Kind::kTool, toolPath) && toolPath != kMenuPickMesh) {
			toolPath = kMenuPickMesh;
			LoadModel(_pick, Kind::kTool, toolPath);
		}
		if (_pick.holder) {
			_root->AttachChild(_pick.holder.get(), true);
		}
		if (LoadModel(_wrench, Kind::kShiv, _cfg.lockMesh) || LoadModel(_wrench, Kind::kTool, toolPath)) {
			_root->AttachChild(_wrench.holder.get(), true);
		}

		if (!_lock.holder && !_pick.holder) {
			_root.reset();
			_parent.reset();
			return false;
		}

		_parent->AttachChild(_root.get(), true);
		return true;
	}

	// When a tool goes into the keyhole (or comes out) it jumps between the hand and the keyhole;
	// instead it glides there in a short, eased move (as HIGGS moves things into the hand).
	Pose Visuals::Glide(ToolGlide& a_g, bool a_inserted, const Pose& a_target)
	{
		constexpr float kSeconds = 0.15f;
		const auto      now = std::chrono::steady_clock::now();
		if (a_g.hasLast && a_inserted != a_g.inserted) {
			a_g.from = a_g.last;
			a_g.start = now;
			a_g.active = true;
		}
		a_g.inserted = a_inserted;
		Pose out = a_target;
		if (a_g.active) {
			const float t = std::chrono::duration<float>(now - a_g.start).count() / kSeconds;
			if (t >= 1.0f) {
				a_g.active = false;
			} else {
				const float s = t * t * (3.0f - 2.0f * t);
				out.pos = a_g.from.pos + (a_target.pos - a_g.from.pos) * s;
				out.rot = RotationBlend(a_g.from.rot, a_target.rot, s);
			}
		}
		a_g.last = out;
		a_g.hasLast = true;
		return out;
	}

	void Visuals::NoteVanillaMenu()
	{
		g_vanillaMenuRan = true;
	}

	void Visuals::Retune(const Config& a_cfg)
	{
		_cfg = a_cfg;
		ApplyFit(_lock, false);
		ApplyFit(_pick, false);
		ApplyFit(_wrench, false);
	}

	void Visuals::Destroy()
	{
		if (_root && _parent) {
			_parent->DetachChild(_root.get());
		}
		_cylinder.reset();
		_lock = {};
		_pick = {};
		_pickGlide = {};
		_wrenchGlide = {};
		_wrench = {};
		_root.reset();
		_parent.reset();
	}

	void Visuals::Update(const LockSession& a_session, const Pose* a_pickHand, const Pose* a_wrenchHand, float a_shake, const Pose& a_anchor)
	{
		if (!_root || !_parent) {
			return;
		}

		// Root compensates the parent so children can be placed in world coordinates.
		_root->local = _parent->world.Invert();

		// The session works in room space; a_anchor takes room space to the world.
		auto toWorld = [&a_anchor](const Mat3& a_rot, const Vec3& a_pos, RE::NiAVObject* a_obj) {
			SetLocal(a_obj, a_anchor.rot * a_rot, a_anchor.rot * a_pos + a_anchor.pos);
		};

		const LockFrame& f = a_session.Frame();
		const Mat3       basis = f.Basis();
		const Mat3       shake = AxisAngle(f.forward, a_shake * kDegToRad);
		const float      turn = a_session.Side() * a_session.LockAngle() * 90.0f * kDegToRad;

		if (_lock.holder) {
			toWorld(shake * basis, f.keyhole, _lock.holder.get());
			TurnCylinder(turn);
		}

		// Tools: held in the hands; once in the keyhole their tip stays in it, the handle towards the hand.
		if (_pick.holder) {
			_pick.holder->SetAppCulled(!a_pickHand);
			if (a_pickHand) {
				Pose tool = a_session.PickVisual(*a_pickHand);
				if (a_session.PickInserted()) {
					tool.rot = shake * tool.rot;
				}
				tool = Glide(_pickGlide, a_session.PickInserted(), tool);
				toWorld(tool.rot, tool.pos, _pick.holder.get());
			} else {
				_pickGlide.hasLast = false;
			}
		}
		if (_wrench.holder) {
			_wrench.holder->SetAppCulled(!a_wrenchHand);
			if (a_wrenchHand) {
				Pose tool = a_session.WrenchVisual(*a_wrenchHand);
				if (a_session.WrenchInserted()) {
					tool.rot = shake * tool.rot;
				}
				tool = Glide(_wrenchGlide, a_session.WrenchInserted(), tool);
				toWorld(tool.rot, tool.pos, _wrench.holder.get());
			} else {
				_wrenchGlide.hasLast = false;
			}
		}

		RE::NiUpdateData ud{};
		_root->Update(ud);
	}
}
