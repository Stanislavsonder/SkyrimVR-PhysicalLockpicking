#pragma once

// Small self-contained vector math used by the lockpicking core.
// Conventions match Skyrim / NetImmerse:
//   * world space is right handed, X = east, Y = north (forward), Z = up
//   * Mat3 is row major and transforms column vectors: v_world = M * v_local,
//     so the columns of M are the local axes expressed in world space.

#include <algorithm>
#include <cmath>

namespace vrlp
{
	inline constexpr float kPi = 3.14159265358979323846f;
	inline constexpr float kDegToRad = kPi / 180.0f;
	inline constexpr float kRadToDeg = 180.0f / kPi;

	struct Vec3
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };

		constexpr Vec3() = default;
		constexpr Vec3(float a_x, float a_y, float a_z) :
			x(a_x), y(a_y), z(a_z) {}

		constexpr bool operator==(const Vec3&) const = default;
		constexpr Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
		constexpr Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
		constexpr Vec3 operator-() const { return { -x, -y, -z }; }
		constexpr Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
		constexpr Vec3 operator/(float s) const { return { x / s, y / s, z / s }; }
		Vec3&          operator+=(const Vec3& o)
		{
			x += o.x;
			y += o.y;
			z += o.z;
			return *this;
		}
	};

	constexpr float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	constexpr Vec3  Cross(const Vec3& a, const Vec3& b)
	{
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}
	inline float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }
	inline float Distance(const Vec3& a, const Vec3& b) { return Length(a - b); }
	inline Vec3  Normalize(const Vec3& v, const Vec3& a_fallback = { 0.0f, 1.0f, 0.0f })
	{
		const float len = Length(v);
		return len > 1e-6f ? v / len : a_fallback;
	}

	struct Mat3
	{
		float m[3][3]{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

		static Mat3 FromColumns(const Vec3& c0, const Vec3& c1, const Vec3& c2)
		{
			Mat3 r;
			r.m[0][0] = c0.x;
			r.m[1][0] = c0.y;
			r.m[2][0] = c0.z;
			r.m[0][1] = c1.x;
			r.m[1][1] = c1.y;
			r.m[2][1] = c1.z;
			r.m[0][2] = c2.x;
			r.m[1][2] = c2.y;
			r.m[2][2] = c2.z;
			return r;
		}

		Vec3 Column(int c) const { return { m[0][c], m[1][c], m[2][c] }; }

		Vec3 operator*(const Vec3& v) const
		{
			return {
				m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
				m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
				m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z
			};
		}

		Mat3 operator*(const Mat3& o) const
		{
			Mat3 r;
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
				}
			}
			return r;
		}

		Mat3 Transposed() const
		{
			Mat3 r;
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					r.m[i][j] = m[j][i];
				}
			}
			return r;
		}
	};

	// Rotation of a_angleRad around the unit axis a_axis (right-hand rule).
	inline Mat3 AxisAngle(const Vec3& a_axis, float a_angleRad)
	{
		const Vec3  a = Normalize(a_axis);
		const float c = std::cos(a_angleRad);
		const float s = std::sin(a_angleRad);
		const float t = 1.0f - c;
		Mat3        r;
		r.m[0][0] = t * a.x * a.x + c;
		r.m[0][1] = t * a.x * a.y - s * a.z;
		r.m[0][2] = t * a.x * a.z + s * a.y;
		r.m[1][0] = t * a.x * a.y + s * a.z;
		r.m[1][1] = t * a.y * a.y + c;
		r.m[1][2] = t * a.y * a.z - s * a.x;
		r.m[2][0] = t * a.x * a.z - s * a.y;
		r.m[2][1] = t * a.y * a.z + s * a.x;
		r.m[2][2] = t * a.z * a.z + c;
		return r;
	}

	// Intrinsic X, then Y, then Z rotation (degrees) - used for mesh correction offsets.
	inline Mat3 EulerDegXYZ(const Vec3& a_deg)
	{
		return AxisAngle({ 0, 0, 1 }, a_deg.z * kDegToRad) *
		       AxisAngle({ 0, 1, 0 }, a_deg.y * kDegToRad) *
		       AxisAngle({ 1, 0, 0 }, a_deg.x * kDegToRad);
	}

	struct Quat
	{
		float w{ 1.0f };
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
	};

	inline Quat ToQuat(const Mat3& r)
	{
		Quat        q;
		const float trace = r.m[0][0] + r.m[1][1] + r.m[2][2];
		if (trace > 0.0f) {
			const float s = std::sqrt(trace + 1.0f) * 2.0f;
			q.w = 0.25f * s;
			q.x = (r.m[2][1] - r.m[1][2]) / s;
			q.y = (r.m[0][2] - r.m[2][0]) / s;
			q.z = (r.m[1][0] - r.m[0][1]) / s;
		} else if (r.m[0][0] > r.m[1][1] && r.m[0][0] > r.m[2][2]) {
			const float s = std::sqrt(1.0f + r.m[0][0] - r.m[1][1] - r.m[2][2]) * 2.0f;
			q.w = (r.m[2][1] - r.m[1][2]) / s;
			q.x = 0.25f * s;
			q.y = (r.m[0][1] + r.m[1][0]) / s;
			q.z = (r.m[0][2] + r.m[2][0]) / s;
		} else if (r.m[1][1] > r.m[2][2]) {
			const float s = std::sqrt(1.0f + r.m[1][1] - r.m[0][0] - r.m[2][2]) * 2.0f;
			q.w = (r.m[0][2] - r.m[2][0]) / s;
			q.x = (r.m[0][1] + r.m[1][0]) / s;
			q.y = 0.25f * s;
			q.z = (r.m[1][2] + r.m[2][1]) / s;
		} else {
			const float s = std::sqrt(1.0f + r.m[2][2] - r.m[0][0] - r.m[1][1]) * 2.0f;
			q.w = (r.m[1][0] - r.m[0][1]) / s;
			q.x = (r.m[0][2] + r.m[2][0]) / s;
			q.y = (r.m[1][2] + r.m[2][1]) / s;
			q.z = 0.25f * s;
		}
		return q;
	}

	// Swing-twist decomposition: returns the signed twist angle (radians, in (-pi, pi])
	// of rotation a_rot around unit axis a_axis. Robust to whatever the controller's own
	// local axes are, which is why it is used to measure wrist roll.
	inline float TwistAngle(const Mat3& a_rot, const Vec3& a_axis)
	{
		const Quat  q = ToQuat(a_rot);
		const Vec3  axis = Normalize(a_axis);
		const float proj = q.x * axis.x + q.y * axis.y + q.z * axis.z;
		float       angle = 2.0f * std::atan2(proj, q.w);
		if (angle > kPi) {
			angle -= 2.0f * kPi;
		} else if (angle <= -kPi) {
			angle += 2.0f * kPi;
		}
		return angle;
	}

	inline float WrapDeg(float a_deg)
	{
		while (a_deg > 180.0f) {
			a_deg -= 360.0f;
		}
		while (a_deg <= -180.0f) {
			a_deg += 360.0f;
		}
		return a_deg;
	}

	inline float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

	inline float Approach(float a_current, float a_target, float a_maxStep)
	{
		if (a_current < a_target) {
			return (std::min)(a_current + a_maxStep, a_target);
		}
		return (std::max)(a_current - a_maxStep, a_target);
	}

	// The rotation a_t of the way from a_a to a_b (about the axis between them).
	inline Mat3 RotationBlend(const Mat3& a_a, const Mat3& a_b, float a_t)
	{
		const Mat3  rel = a_a.Transposed() * a_b;
		const float angle = std::acos(std::clamp((rel.m[0][0] + rel.m[1][1] + rel.m[2][2] - 1.0f) * 0.5f, -1.0f, 1.0f));
		const Vec3  axis{ rel.m[2][1] - rel.m[1][2], rel.m[0][2] - rel.m[2][0], rel.m[1][0] - rel.m[0][1] };
		if (angle < 1e-4f || a_t >= 1.0f) {
			return a_b;
		}
		if (Length(axis) < 1e-5f) {
			return a_t < 0.5f ? a_a : a_b;  // half a turn: any axis would do
		}
		return a_a * AxisAngle(axis, angle * a_t);
	}

	// 1-euro filter (Casiez, Roussel, Vogel, CHI 2012): a low-pass whose cutoff rises with the
	// speed of the signal, so a hand held still is smoothed hard and a fast move barely lags.
	class OneEuroFilter
	{
	public:
		void Reset() { _valid = false; }

		// a_minCutoff (Hz) <= 0 passes the value through.
		float Filter(float a_x, float a_dt, float a_minCutoff, float a_beta, float a_dCutoff = 1.0f)
		{
			if (!_valid || a_minCutoff <= 0.0f || a_dt <= 0.0f) {
				_x = a_x;
				_dx = 0.0f;
				_valid = true;
				return _x;
			}
			_dx += Alpha(a_dt, a_dCutoff) * ((a_x - _x) / a_dt - _dx);
			const float cutoff = a_minCutoff + a_beta * std::fabs(_dx);
			_x += Alpha(a_dt, cutoff) * (a_x - _x);
			return _x;
		}

	private:
		static float Alpha(float a_dt, float a_cutoff)
		{
			const float tau = 1.0f / (2.0f * kPi * a_cutoff);
			return 1.0f / (1.0f + tau / a_dt);
		}

		float _x{ 0.0f };
		float _dx{ 0.0f };
		bool  _valid{ false };
	};

	struct Pose
	{
		Vec3 pos;
		Mat3 rot;

		Vec3 TransformPoint(const Vec3& a_local) const { return pos + rot * a_local; }
	};
}
