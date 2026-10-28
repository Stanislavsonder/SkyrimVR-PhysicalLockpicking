#pragma once

// Lock profiles: where the keyhole is on a particular lock model, measured with the markup tool
// and saved per model (by its .nif path), so lock-model mods can be supported model by model.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "MeshFit.h"
#include "VecMath.h"

namespace vrlp
{
	struct LockProfile
	{
		Vec3  keyhole;           // keyhole centre in the model's own space
		float radius{ 0.0f };    // unused (the target size is fixed); kept so older files still read
		float roll{ 0.0f };      // extra turn (deg, clockwise as seen by the player) so the keyhole's top is up
		bool  valid{ false };
	};

	// "Meshes\Interface/Foo.NIF" -> "interface\foo.nif"
	std::string NormalizeModelPath(std::string a_path);

	// INI text: one section per model path, keys vKeyhole, fRadius, fRoll.
	std::map<std::string, LockProfile> ParseLockProfiles(const std::string& a_text);
	std::string                        WriteLockProfile(const std::string& a_text, const std::string& a_modelPath, const LockProfile& a_profile);

	// Moves an automatic lock fit onto a profile: the profile's keyhole goes to the origin, and the
	// model is turned by the profile's roll around the keyhole axis. Scale stays.
	LockFit ApplyLockProfile(const LockFit& a_fit, const LockProfile& a_profile);

	// The markup tool's result. Marks are in the lock frame as shown now (x right, y into the
	// lock, z up, keyhole at the origin): the keyhole's centre and its top. a_place is the model's
	// current placement (mesh -> lock frame) and a_currentRoll the roll it already includes. The
	// target size stays fixed (radius 0 = the configured one).
	LockProfile ProfileFromMarks(const Vec3& a_centre, const Vec3& a_top, const MeshPlacement& a_place, float a_currentRoll);

	// How a tool model is turned / moved / sized in the hand, set with the markup tool's align
	// step, on top of the automatic fit. Saved in the same file under the model's section:
	// vPickHoldX / vPickHoldY / vPickHoldOffset / fPickHoldScale for a pick model, the same
	// with Shiv for the screwdriver inside a lock model.
	struct ToolProfile
	{
		Mat3  rot;           // turn about the tool's tip, in the tool frame (x side, y to the tip, z flat side)
		Vec3  offset;        // then moved, tool frame, game units
		float scale{ 1.0f };  // size about the tip
		bool  valid{ false };
	};

	// a_prefix: "pick" or "shiv"
	std::map<std::string, ToolProfile> ParseToolProfiles(const std::string& a_text, const std::string& a_prefix);
	std::string                        WriteToolProfile(const std::string& a_text, const std::string& a_modelPath, const std::string& a_prefix, const ToolProfile& a_profile);
	// Built-in alignments of the default tool models, lined up in game with the markup tool. A
	// profile of the player's own for the same model wins. Keys as ParseToolProfiles returns them.
	const std::map<std::string, ToolProfile>& DefaultToolProfiles(const std::string& a_prefix);

	// The INI lines WriteToolProfile writes, for the log.
	std::string ToolProfileText(const std::string& a_prefix, const ToolProfile& a_profile);

	// Re-gripping: the model was held still in the room while the tool frame moved from a_from to
	// a_to (room space); returns the alignment that keeps the model where it is in the new grip.
	ToolProfile Regrip(const ToolProfile& a_profile, const Pose& a_from, const Pose& a_to);

	// The placement (mesh -> tool frame) with the alignment applied on top.
	MeshPlacement ApplyToolProfile(const MeshPlacement& a_place, const ToolProfile& a_profile);

	// Where the lock sits on a door or chest (bLockOnObject), in the object's own model space
	// (before the reference's scale): the keyhole, and the lock frame's axes (right, into the
	// lock, up). Set with the markup tool's first step and saved under the object's model in the
	// same file: vObjectKeyhole, vObjectInto, vObjectUp. A door can have its other side marked too
	// (the same keys ending in 2); without it, the other side is the mirror through the door
	// (OtherSide). sObjectLock says how much of the lock is drawn (LockShown).
	enum class LockShown
	{
		kWhole,    // "whole": the whole lock
		kRing,     // "ring": its turning part, the keyhole and the ring round it (small chests, safes)
		kKeyhole,  // "keyhole": the keyhole alone (a lock built into the model)
		kNone,     // "none": no lock on this model, it stays in front of you (nothing there to pick)
	};
	const char* LockShownName(LockShown a_shown);
	// false (a_out unchanged) for anything else
	bool ParseLockShown(const std::string& a_text, LockShown& a_out);
	// The keyhole among the shapes of a lock's turning part, by their bound radii: the smallest,
	// and any within 15% of it. The others are the ring.
	std::vector<bool> KeyholeShapes(const std::vector<float>& a_radii);

	struct ObjectAnchor
	{
		struct Side
		{
			Vec3      keyhole;
			Mat3      rot;
			LockShown shown{ LockShown::kWhole };
		};
		Vec3                keyhole;
		Mat3                rot;  // columns: right, into the lock, up
		bool                valid{ false };
		LockShown           shown{ LockShown::kWhole };
		std::optional<Side> back;  // the door's other side, when it was marked
	};

	// A first guess from the model's bounds (OBND, model space). Chest: on its front (-Y) face,
	// in the middle, a little below the lid. Door: on the face of its thin side, at handle height,
	// near the edge furthest from the model's origin (doors swing about their origin).
	ObjectAnchor GuessObjectAnchor(const Vec3& a_boundMin, const Vec3& a_boundMax, bool a_door);
	// The lock on the other side of a door: the one marked there (front and back swapped), else
	// this one mirrored through the middle of the door, facing the other way. Applying it twice
	// gives the anchor back.
	ObjectAnchor OtherSide(const ObjectAnchor& a_anchor, const Vec3& a_boundMin, const Vec3& a_boundMax);
	// Which of a door's two locks a player with the head at a_head picks: the one they stand in
	// front of, else the nearer one. True for the other side.
	bool UseOtherSide(const ObjectAnchor& a_anchor, const Vec3& a_boundMin, const Vec3& a_boundMax, const Pose& a_object, float a_scale, const Vec3& a_head);

	std::map<std::string, ObjectAnchor> ParseObjectAnchors(const std::string& a_text);
	std::string                         WriteObjectAnchor(const std::string& a_text, const std::string& a_modelPath, const ObjectAnchor& a_anchor);
	// The INI lines WriteObjectAnchor writes, for the log.
	std::string ObjectAnchorText(const ObjectAnchor& a_anchor);
	// Built-in placements, lined up in game with the markup tool. A player's own for the same
	// model wins.
	const std::map<std::string, ObjectAnchor>& DefaultObjectAnchors();

	// The lock frame's pose in the world for an anchor on an object placed at a_object (its
	// world rotation and position) with scale a_scale: pos = keyhole, rot columns = right, into
	// the lock, up. ObjectAnchorFromPose is the other way round.
	Pose         PlaceOnObject(const ObjectAnchor& a_anchor, const Pose& a_object, float a_scale);
	ObjectAnchor ObjectAnchorFromPose(const Pose& a_lock, const Pose& a_object, float a_scale);

	// The door and chest models the game locks somewhere, as the plugin lists them for lining up
	// the lock outside the game (tools/lockplacer): one line each, tab-separated: "door" or
	// "container", the model path, its bounds (min and max, "x y z"), how many references lock
	// it, its name.
	struct LockableModel
	{
		std::string model;
		bool        door{ false };
		Vec3        boundMin, boundMax;
		int         locked{ 0 };
		std::string name;
	};
	std::string                WriteLockableModels(const std::vector<LockableModel>& a_models);
	std::vector<LockableModel> ParseLockableModels(const std::string& a_text);

}
