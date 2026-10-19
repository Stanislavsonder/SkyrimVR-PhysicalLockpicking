#include "MeshFit.h"

#include <algorithm>
#include <cmath>

namespace vrlp
{
	namespace
	{
		// Rotation taking orthonormal right-handed frame (a0,a1,a2) onto (b0,b1,b2).
		Mat3 FrameToFrame(const Vec3& a0, const Vec3& a1, const Vec3& a2, const Vec3& b0, const Vec3& b1, const Vec3& b2)
		{
			const Mat3 A = Mat3::FromColumns(a0, a1, a2);
			const Mat3 B = Mat3::FromColumns(b0, b1, b2);
			return B * A.Transposed();
		}

		// Box corners when known, otherwise the sphere's extreme points along each axis.
		void PartPoints(const MeshPart& a_part, std::vector<Vec3>& a_out)
		{
			if (!a_part.corners.empty()) {
				a_out.insert(a_out.end(), a_part.corners.begin(), a_part.corners.end());
				return;
			}
			const float r = a_part.radius;
			for (const Vec3& d : { Vec3{ r, 0, 0 }, Vec3{ -r, 0, 0 }, Vec3{ 0, r, 0 }, Vec3{ 0, -r, 0 }, Vec3{ 0, 0, r }, Vec3{ 0, 0, -r } }) {
				a_out.push_back(a_part.centre + d);
			}
		}

		float Comp(const Vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

		Vec3 Axis(int i) { return i == 0 ? Vec3{ 1, 0, 0 } : (i == 1 ? Vec3{ 0, 1, 0 } : Vec3{ 0, 0, 1 }); }
	}

	Vec3 SnapAxis(const Vec3& a_dir)
	{
		const float ax = std::fabs(a_dir.x), ay = std::fabs(a_dir.y), az = std::fabs(a_dir.z);
		if (ax >= ay && ax >= az) {
			return { a_dir.x >= 0.0f ? 1.0f : -1.0f, 0, 0 };
		}
		if (ay >= az) {
			return { 0, a_dir.y >= 0.0f ? 1.0f : -1.0f, 0 };
		}
		return { 0, 0, a_dir.z >= 0.0f ? 1.0f : -1.0f };
	}

	LockFit FitLockParts(const std::vector<MeshPart>& a_parts, const Vec3& a_frontHint, float a_radius)
	{
		LockFit fit;
		const MeshPart* body = nullptr;
		Vec3            cylCentre;
		int             cylCount = 0;
		for (const auto& p : a_parts) {
			if (p.cylinder) {
				cylCentre += p.centre;
				++cylCount;
			} else if (!body || p.radius > body->radius) {
				body = &p;
			}
		}
		if (!body && cylCount == 0) {
			return fit;
		}
		if (cylCount > 0) {
			cylCentre = cylCentre / static_cast<float>(cylCount);
		}
		const Vec3 bodyCentre = body ? body->centre : cylCentre;

		// keyhole axis, pointing out of the front (towards whatever looks at / comes into the lock)
		Vec3 out{ 0, -1, 0 };
		if (Length(a_frontHint - bodyCentre) > 1e-3f) {
			out = SnapAxis(a_frontHint - bodyCentre);
		}
		fit.fromCylinder = cylCount > 0;

		// the front face: the front of the cylinder, or of the whole lock without one
		std::vector<Vec3> front;
		std::vector<Vec3> all;
		for (const auto& p : a_parts) {
			if (p.cylinder || cylCount == 0) {
				PartPoints(p, front);
			}
			PartPoints(p, all);
		}
		float frontDist = -1e30f;
		for (const auto& q : front) {
			frontDist = std::max(frontDist, Dot(q, out));
		}
		const Vec3 lateralRef = cylCount > 0 ? cylCentre : bodyCentre;
		const Vec3 keyhole = lateralRef - out * Dot(lateralRef, out) + out * frontDist;

		Vec3 up = Vec3{ 0, 0, 1 } - out * out.z;
		if (Length(up) < 0.5f) {
			up = Vec3{ 0, 1, 0 } - out * out.y;
		}
		up = Normalize(up);
		const Vec3 fwd = -out;
		const Vec3 right = Normalize(Cross(fwd, up));

		float halfFace = 0.0f;
		for (const auto& q : all) {
			const Vec3 d = q - keyhole;
			halfFace = std::max({ halfFace, std::fabs(Dot(d, right)), std::fabs(Dot(d, up)) });
		}

		fit.place.rot = FrameToFrame(right, fwd, up, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 });
		fit.place.scale = halfFace > 1e-4f ? a_radius / halfFace : 1.0f;
		fit.place.offset = -(fit.place.rot * (keyhole * fit.place.scale));
		fit.place.valid = true;
		fit.keyhole = keyhole;
		fit.forward = fwd;
		return fit;
	}

	MeshPlacement FitToolParts(const std::vector<MeshPart>& a_parts, float a_length, bool a_flip, const Vec3* a_tipHint)
	{
		MeshPlacement     m;
		std::vector<Vec3> pts;
		for (const auto& p : a_parts) {
			PartPoints(p, pts);
		}
		if (pts.empty()) {
			return m;
		}
		Vec3 lo = pts.front(), hi = pts.front();
		for (const auto& q : pts) {
			lo = { std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z) };
			hi = { std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z) };
		}
		int order[3]{ 0, 1, 2 };
		std::sort(order, order + 3, [&](int a, int b) { return Comp(hi - lo, a) > Comp(hi - lo, b); });
		const int   k = order[0];
		const float span = Comp(hi - lo, k);
		if (span < 1e-4f) {
			return m;
		}
		const Vec3 centre = (lo + hi) * 0.5f;
		bool plusEnd = true;
		if (a_tipHint) {
			plusEnd = Comp(*a_tipHint - centre, k) >= 0.0f;
		}
		if (a_flip) {
			plusEnd = !plusEnd;
		}
		const Vec3 axis = Axis(k) * (plusEnd ? 1.0f : -1.0f);  // centre -> tip
		const Vec3 tip = centre + axis * (span * 0.5f);

		// tip direction -> +Y (handle along -Y), second axis -> X
		const Vec3 a1 = Axis(order[1]);
		m.rot = FrameToFrame(a1, axis, Cross(a1, axis), { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 });
		m.scale = a_length / span;
		m.offset = -(m.rot * (tip * m.scale));
		m.valid = true;
		return m;
	}
}
