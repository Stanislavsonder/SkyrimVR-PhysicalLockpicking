#pragma once

#include "PCH.h"

#include "Config.h"
#include "LockSession.h"
#include "LockProfile.h"
#include "MeshFit.h"

namespace vrlp
{
	// In-world 3D lock + lockpick meshes.
	class Visuals
	{
	public:
		// Meshes are parented to the target's 3D (always rendered), falling back to the player's VR world node.
		// a_profile: the measured keyhole of this lock model (markup tool), if there is one.
		bool Create(const Config& a_cfg, RE::TESObjectREFR* a_target, const LockProfile* a_profile = nullptr);
		void Destroy();

		// Re-applies size / orientation settings (in-game editor) without reloading the models.
		void Retune(const Config& a_cfg);

		// The vanilla lockpicking menu ran: it animates the lock models it shows (the shared copies
		// this mod clones too) and leaves them in its last pose. Lock models first loaded after this
		// cannot be trusted to be in their rest pose.
		static void NoteVanillaMenu();

		// World poses of the hands holding the pick / the tension wrench (null if untracked).
		// Poses are in the session's room space; a_anchor is the room's world pose.
		void Update(const LockSession& a_session, const Pose* a_pickHand, const Pose* a_wrenchHand, float a_shake, const Pose& a_anchor);

		bool Active() const { return _root != nullptr; }

		// Lock model placement right now (mesh -> lock frame) and the roll it includes, for the markup tool.
		const MeshPlacement& LockPlacement() const { return _lockTotal; }
		float                LockRoll() const { return _profile ? _profile->roll : 0.0f; }
		const std::string&   LockModelPath() const { return _lock.path; }
		// Target circle radius from the profile, in game units (0 = no profile).
		float KeyholeRadius() const { return _keyholeRadius; }
		void  SetLockProfile(const LockProfile& a_profile);

		// Tool profiles (markup tool), keyed by normalized model path; set before Create.
		void SetToolProfiles(std::map<std::string, ToolProfile> a_picks, std::map<std::string, ToolProfile> a_shivs);
		// The pick / wrench model now: path, and its profile key prefix ("pick" for a pick model,
		// "shiv" for the screwdriver of a lock model).
		const std::string&   ToolModelPath(bool a_wrench) const { return a_wrench ? _wrench.path : _pick.path; }
		const char*          ToolPrefix(bool a_wrench) const { return (a_wrench ? _wrench.kind : _pick.kind) == Kind::kShiv ? "shiv" : "pick"; }
		// Alignment of the pick / wrench model in the hand now (identity if none); setting an
		// invalid one goes back to the automatic fit.
		ToolProfile ToolAlignment(bool a_wrench) const;
		void        SetToolProfile(bool a_wrench, const ToolProfile& a_profile, bool a_log = true);
		bool        HasTool(bool a_wrench) const { return (a_wrench ? _wrench : _pick).clone != nullptr; }

	private:
		enum class Kind
		{
			kLock,
			kTool,
			kShiv  // the screwdriver out of the lock model
		};

		struct Model
		{
			RE::NiPointer<RE::NiNode> holder;  // we drive its transform (lock frame / tool frame)
			RE::NiPointer<RE::NiNode> clone;   // carries the static mesh correction (fit)
			std::vector<MeshPart>     parts;   // model-space bounds of the visible shapes
			RE::NiBound               bound;   // whole model, model space
			Vec3                      frontHint;  // lock: a point in front of it, model space
			Kind                      kind{ Kind::kTool };
			std::string               path;
		};

		bool LoadModel(Model& a_out, Kind a_kind, const std::string& a_path);
		void ApplyFit(Model& a_model, bool a_log = true);
		void TurnCylinder(float a_angleRad);

		struct ToolGlide
		{
			Pose                                  from;
			Pose                                  last;
			std::chrono::steady_clock::time_point start;
			bool                                  inserted{ false };
			bool                                  active{ false };
			bool                                  hasLast{ false };
		};
		static Pose Glide(ToolGlide& a_g, bool a_inserted, const Pose& a_target);
		ToolGlide   _pickGlide;
		ToolGlide   _wrenchGlide;
		ToolProfile ToolProfileFor(const Model& a_model) const;

		Config                     _cfg;
		std::optional<LockProfile> _profile;
		MeshPlacement              _lockTotal;
		std::map<std::string, ToolProfile> _pickProfiles;
		std::map<std::string, ToolProfile> _shivProfiles;
		float                      _keyholeRadius{ 0.0f };
		RE::NiPointer<RE::NiNode> _parent;
		RE::NiPointer<RE::NiNode> _root;
		Model                     _lock;
		Model                     _pick;
		Model                     _wrench;

		// Part of the lock that turns (the tumbler), instead of the whole lock.
		RE::NiPointer<RE::NiAVObject> _cylinder;
		RE::NiTransform               _cylinderWorld0;   // in mesh space, unturned
		RE::NiTransform               _cylinderParent0;  // parent's transform in mesh space
		Vec3                          _cylinderPivot;    // keyhole centre in mesh space
		Vec3                          _cylinderAxis;     // keyhole axis (into the lock) in mesh space
	};
}
