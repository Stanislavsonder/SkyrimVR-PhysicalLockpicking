#pragma once

// Works out how to place the lock and lockpick models in the world from the bounds of their
// parts. The vanilla lockpicking-menu models are modelled in the menu's own space (and the
// animated parts start far away from the lock), so without this the visible pick and lock
// don't line up with where the simulation thinks the tip and keyhole are.

#include <vector>

#include "VecMath.h"

namespace vrlp
{
	// One shape of a model, in model space: bounding sphere plus (when the engine provides one)
	// the 8 corners of its bounding box.
	struct MeshPart
	{
		Vec3              centre;
		float             radius{ 0.0f };
		std::vector<Vec3> corners;
		bool              cylinder{ false };  // the part of a lock that turns (holds the keyhole)
	};

	// Snaps a direction to the nearest coordinate axis (models are authored axis aligned).
	Vec3 SnapAxis(const Vec3& a_dir);

	// Transform applied to a mesh (mesh space -> holder space): p' = rot * (scale * p) + offset.
	struct MeshPlacement
	{
		Mat3  rot;
		float scale{ 1.0f };
		Vec3  offset;
		bool  valid{ false };

		Vec3 Apply(const Vec3& a_p) const { return rot * (a_p * scale) + offset; }
	};

	struct LockFit
	{
		MeshPlacement place;
		Vec3          keyhole;           // mesh space: centre of the keyhole on the front face
		Vec3          forward;           // mesh space: keyhole axis, pointing into the lock
		bool          fromCylinder{ false };  // keyhole placed on the turning part
	};

	// Lock: the keyhole axis is the model axis pointing from the lock body towards a_frontHint
	// (where the menu camera / the screwdriver comes from). The keyhole is on the front face of
	// the turning part (cylinder), or of the whole lock without one. Places the keyhole at the
	// origin, the axis along +Y (into the lock), mesh +Z as up, scaled so the lock's face
	// half-size equals a_radius.
	LockFit FitLockParts(const std::vector<MeshPart>& a_parts, const Vec3& a_frontHint, float a_radius);

	// Tool (pick / wrench): the long axis of all parts' bounds. Places one end (the tip) at the
	// origin with the shaft running back along -Y, the second longest axis along X, scaled so the
	// tool is a_length long. The tip is the end nearer a_tipHint (the + end without one); a_flip
	// swaps ends.
	MeshPlacement FitToolParts(const std::vector<MeshPart>& a_parts, float a_length, bool a_flip, const Vec3* a_tipHint = nullptr);
}
