#include "LockProfile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <set>
#include <vector>

#include "Config.h"

namespace vrlp
{
	std::string NormalizeModelPath(std::string a_path)
	{
		std::transform(a_path.begin(), a_path.end(), a_path.begin(), [](unsigned char c) {
			return c == '/' ? '\\' : static_cast<char>(std::tolower(c));
		});
		for (const char* prefix : { "data\\meshes\\", "meshes\\" }) {
			if (a_path.rfind(prefix, 0) == 0) {
				a_path = a_path.substr(std::char_traits<char>::length(prefix));
			}
		}
		return a_path;
	}

	std::map<std::string, LockProfile> ParseLockProfiles(const std::string& a_text)
	{
		std::map<std::string, LockProfile> out;
		for (const auto& [key, value] : ParseIni(a_text)) {
			const auto dot = key.rfind('.');
			if (dot == std::string::npos) {
				continue;
			}
			const std::string model = NormalizeModelPath(key.substr(0, dot));
			const std::string name = key.substr(dot + 1);
			LockProfile&      p = out[model];
			if (name == "vkeyhole") {
				p.valid = ParseVec(value, p.keyhole);
			} else if (name == "fradius") {
				p.radius = std::strtof(value.c_str(), nullptr);
			} else if (name == "froll") {
				p.roll = std::strtof(value.c_str(), nullptr);
			}
		}
		for (auto it = out.begin(); it != out.end();) {
			it = it->second.valid ? std::next(it) : out.erase(it);
		}
		return out;
	}

	std::string WriteLockProfile(const std::string& a_text, const std::string& a_modelPath, const LockProfile& a_p)
	{
		const std::string section = NormalizeModelPath(a_modelPath);
		return SetIniValues(a_text, {
										{ section, "vKeyhole", std::format("{:.3f}, {:.3f}, {:.3f}", a_p.keyhole.x, a_p.keyhole.y, a_p.keyhole.z) },
										{ section, "fRadius", std::format("{:.3f}", a_p.radius) },
										{ section, "fRoll", std::format("{:.2f}", a_p.roll) },
									});
	}

	LockFit ApplyLockProfile(const LockFit& a_fit, const LockProfile& a_profile)
	{
		LockFit fit = a_fit;
		if (!a_profile.valid || !fit.place.valid) {
			return fit;
		}
		fit.place.rot = AxisAngle({ 0, 1, 0 }, a_profile.roll * kDegToRad) * a_fit.place.rot;
		fit.place.offset = -(fit.place.rot * (a_profile.keyhole * fit.place.scale));
		fit.keyhole = a_profile.keyhole;
		return fit;
	}

	LockProfile ProfileFromMarks(const Vec3& a_centre, const Vec3& a_top, const MeshPlacement& a_place, float a_currentRoll)
	{
		LockProfile p;
		if (!a_place.valid || a_place.scale <= 0.0f) {
			return p;
		}
		const Mat3 inv = a_place.rot.Transposed();
		p.keyhole = (inv * (a_centre - a_place.offset)) / a_place.scale;
		// the marked top is at this angle (clockwise from up); turn the model back by as much
		const float dx = a_top.x - a_centre.x, dz = a_top.z - a_centre.z;
		const float topAngle = (dx * dx + dz * dz > 1e-6f) ? std::atan2(dx, dz) * kRadToDeg : 0.0f;  // no top marked: keep the turn
		p.roll = a_currentRoll - topAngle;
		while (p.roll > 180.0f) {
			p.roll -= 360.0f;
		}
		while (p.roll <= -180.0f) {
			p.roll += 360.0f;
		}
		p.valid = true;
		return p;
	}

	namespace
	{
		Vec3 VecOf(const std::string& a_value)
		{
			Vec3 v;
			vrlp::ParseVec(a_value, v);
			return v;
		}

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}
	}

	namespace
	{
		std::string Capital(std::string a_s)
		{
			if (!a_s.empty()) {
				a_s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(a_s[0])));
			}
			return a_s;
		}

		std::string VecText(const Vec3& a_v) { return std::format("{:.4f}, {:.4f}, {:.4f}", a_v.x, a_v.y, a_v.z); }

		std::vector<IniValue> ToolValues(const std::string& a_section, const std::string& a_prefix, const ToolProfile& a_p)
		{
			const std::string n = Capital(a_prefix);
			return {
				{ a_section, "v" + n + "HoldX", VecText(a_p.rot * Vec3{ 1, 0, 0 }) },
				{ a_section, "v" + n + "HoldY", VecText(a_p.rot * Vec3{ 0, 1, 0 }) },
				{ a_section, "v" + n + "HoldOffset", VecText(a_p.offset) },
				{ a_section, "f" + n + "HoldScale", std::format("{:.4f}", a_p.scale) },
			};
		}
	}

	std::map<std::string, ToolProfile> ParseToolProfiles(const std::string& a_text, const std::string& a_prefix)
	{
		const std::string n = Lower(a_prefix);
		struct Raw
		{
			Vec3  x{ 1, 0, 0 }, y{ 0, 1, 0 }, offset;
			float scale{ 1.0f };
			bool  any{ false };
		};
		std::map<std::string, Raw> raw;
		for (const auto& [key, value] : ParseIni(a_text)) {
			const auto dot = key.rfind('.');
			if (dot == std::string::npos) {
				continue;
			}
			const std::string model = NormalizeModelPath(key.substr(0, dot));
			const std::string name = key.substr(dot + 1);
			Raw&              r = raw[model];
			if (name == "v" + n + "holdx") {
				r.x = VecOf(value);
				r.any = true;
			} else if (name == "v" + n + "holdy") {
				r.y = VecOf(value);
				r.any = true;
			} else if (name == "v" + n + "holdoffset") {
				r.offset = VecOf(value);
				r.any = true;
			} else if (name == "f" + n + "holdscale") {
				r.scale = std::strtof(value.c_str(), nullptr);
				r.any = true;
			}
		}
		std::map<std::string, ToolProfile> out;
		for (const auto& [model, r] : raw) {
			if (!r.any) {
				continue;
			}
			// rebuild a clean rotation from the two saved axes
			const Vec3 y = Normalize(r.y, { 0, 1, 0 });
			Vec3       x = r.x - y * Dot(r.x, y);
			x = Normalize(x, Cross(y, Vec3{ 0, 0, 1 }));
			ToolProfile p;
			p.rot = Mat3::FromColumns(x, y, Cross(x, y));
			p.offset = r.offset;
			p.scale = r.scale > 0.01f ? r.scale : 1.0f;
			p.valid = true;
			out[model] = p;
		}
		return out;
	}

	const std::map<std::string, ToolProfile>& DefaultToolProfiles(const std::string& a_prefix)
	{
		// Measured in game with the markup tool (the "Hold" lines of the log), with the default
		// grip, reach and model length at the default size.
		static const std::string kText = R"([Clutter\Common\Lockpick.nif]
vPickHoldX = 0.1135, 0.0445, 0.9925
vPickHoldY = -0.1407, 0.9896, -0.0282
vPickHoldOffset = -0.0811, -1.7329, 0.0200
fPickHoldScale = 1.0000
[Clutter\SkeletonKey\SkeletonKey.nif]
vPickHoldX = -0.0528, -0.0241, -0.9983
vPickHoldY = 0.0472, 0.9985, -0.0266
vPickHoldOffset = 0.1727, 1.2313, -0.0985
fPickHoldScale = 1.0000
[Interface\Lockpicking\LockPickShiv01.nif]
vShivHoldX = 0.0708, -0.3068, -0.9492
vShivHoldY = -0.1464, 0.9380, -0.3141
vShivHoldOffset = -1.1958, 0.0641, -4.1449
fShivHoldScale = 1.0000
)";
		static const std::map<std::string, ToolProfile> kPicks = ParseToolProfiles(kText, "pick");
		static const std::map<std::string, ToolProfile> kShivs = ParseToolProfiles(kText, "shiv");
		return Lower(a_prefix) == "shiv" ? kShivs : kPicks;
	}

	std::string WriteToolProfile(const std::string& a_text, const std::string& a_modelPath, const std::string& a_prefix, const ToolProfile& a_p)
	{
		return SetIniValues(a_text, ToolValues(NormalizeModelPath(a_modelPath), a_prefix, a_p));
	}

	std::string ToolProfileText(const std::string& a_prefix, const ToolProfile& a_p)
	{
		std::string out;
		for (const auto& v : ToolValues("", a_prefix, a_p)) {
			out += std::format("{}{} = {}", out.empty() ? "" : " | ", v.key, v.value);
		}
		return out;
	}

	ToolProfile Regrip(const ToolProfile& a_p, const Pose& a_from, const Pose& a_to)
	{
		// room = to.rot * (a'.rot * x + a'.offset) + to.pos  must equal  from.rot * (a.rot * x + a.offset) + from.pos
		const Mat3  back = a_to.rot.Transposed();
		ToolProfile r = a_p;
		r.rot = back * a_from.rot * a_p.rot;
		r.offset = back * (a_from.rot * a_p.offset + a_from.pos - a_to.pos);
		r.valid = true;
		return r;
	}

	MeshPlacement ApplyToolProfile(const MeshPlacement& a_place, const ToolProfile& a_p)
	{
		if (!a_p.valid || !a_place.valid) {
			return a_place;
		}
		MeshPlacement m = a_place;
		m.rot = a_p.rot * a_place.rot;
		m.scale = a_place.scale * a_p.scale;
		m.offset = a_p.rot * (a_place.offset * a_p.scale) + a_p.offset;
		return m;
	}
}

namespace vrlp
{
	namespace
	{
		float& Axis(Vec3& a_v, int a_i) { return a_i == 0 ? a_v.x : (a_i == 1 ? a_v.y : a_v.z); }
		float  Axis(const Vec3& a_v, int a_i) { return a_i == 0 ? a_v.x : (a_i == 1 ? a_v.y : a_v.z); }

		// a clean rotation from the two axes that matter (into the lock, then up)
		Mat3 LockAxes(const Vec3& a_into, const Vec3& a_up)
		{
			const Vec3 into = Normalize(a_into, { 0, 1, 0 });
			Vec3       up = a_up - into * Dot(a_up, into);
			up = Normalize(up, std::fabs(into.z) < 0.9f ? Vec3{ 0, 0, 1 } : Vec3{ 0, -1, 0 });
			return Mat3::FromColumns(Cross(into, up), into, up);
		}

		std::string AnchorVecText(const Vec3& a_v) { return std::format("{:.3f}, {:.3f}, {:.3f}", a_v.x, a_v.y, a_v.z); }

		constexpr const char* kAnchorKeys[] = { "vObjectKeyhole", "vObjectInto", "vObjectUp", "sObjectLock", "vObjectKeyhole2", "vObjectInto2", "vObjectUp2", "sObjectLock2" };

		std::vector<IniValue> AnchorValues(const std::string& a_section, const ObjectAnchor& a_a)
		{
			if (a_a.shown == LockShown::kNone) {
				return { { a_section, "sObjectLock", LockShownName(a_a.shown) } };
			}
			std::vector<IniValue> out = {
				{ a_section, "vObjectKeyhole", AnchorVecText(a_a.keyhole) },
				{ a_section, "vObjectInto", AnchorVecText(a_a.rot.Column(1)) },
				{ a_section, "vObjectUp", AnchorVecText(a_a.rot.Column(2)) },
				{ a_section, "sObjectLock", LockShownName(a_a.shown) },
			};
			if (a_a.back) {
				const auto& b = *a_a.back;
				out.push_back({ a_section, "vObjectKeyhole2", AnchorVecText(b.keyhole) });
				out.push_back({ a_section, "vObjectInto2", AnchorVecText(b.rot.Column(1)) });
				out.push_back({ a_section, "vObjectUp2", AnchorVecText(b.rot.Column(2)) });
				out.push_back({ a_section, "sObjectLock2", LockShownName(b.shown) });
			}
			return out;
		}
	}

	const char* LockShownName(LockShown a_shown)
	{
		switch (a_shown) {
		case LockShown::kRing:
			return "ring";
		case LockShown::kKeyhole:
			return "keyhole";
		case LockShown::kNone:
			return "none";
		default:
			return "whole";
		}
	}

	bool ParseLockShown(const std::string& a_text, LockShown& a_out)
	{
		std::string v = a_text;
		std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		for (const LockShown s : { LockShown::kWhole, LockShown::kRing, LockShown::kKeyhole, LockShown::kNone }) {
			if (v == LockShownName(s)) {
				a_out = s;
				return true;
			}
		}
		return false;
	}

	std::vector<bool> KeyholeShapes(const std::vector<float>& a_radii)
	{
		float smallest = 0.0f;
		for (const float r : a_radii) {
			if (r > 0.0f && (smallest <= 0.0f || r < smallest)) {
				smallest = r;
			}
		}
		std::vector<bool> out;
		for (const float r : a_radii) {
			out.push_back(smallest > 0.0f && r > 0.0f && r <= smallest * 1.15f);
		}
		return out;
	}

	ObjectAnchor GuessObjectAnchor(const Vec3& a_min, const Vec3& a_max, bool a_door)
	{
		ObjectAnchor a;
		const Vec3   size = a_max - a_min;
		const Vec3   mid = (a_min + a_max) * 0.5f;
		if (!a_door) {
			// the lock just under the lid, a little in front of the face so it isn't inside it
			a.keyhole = { mid.x, a_min.y - 0.5f, a_min.z + size.z * 0.6f };
			a.rot = LockAxes({ 0, 1, 0 }, { 0, 0, 1 });
			a.valid = true;
			return a;
		}
		// a trapdoor or a floor grate lies flat: its lock is on top, near the edge away from the hinge,
		// read by a player standing past that edge, so the lock's up points to the hinge
		if (size.z < 0.5f * std::min(size.x, size.y)) {
			const float inset = std::clamp(size.y * 0.12f, 4.0f, 14.0f);
			const bool  awayEdge = std::fabs(a_max.y) >= std::fabs(a_min.y);
			a.keyhole = { mid.x, awayEdge ? a_max.y - inset : a_min.y + inset, a_max.z + 0.5f };
			a.rot = LockAxes({ 0, 0, -1 }, { 0, awayEdge ? -1.0f : 1.0f, 0 });
			a.valid = true;
			return a;
		}
		// the door's thickness is its thinner horizontal side; its lock is on that face
		const int   through = size.y <= size.x ? 1 : 0;
		const int   along = 1 - through;
		const float width = Axis(size, along);
		const float inset = std::clamp(width * 0.12f, 4.0f, 14.0f);
		Vec3        into;
		Axis(into, through) = 1.0f;
		Axis(a.keyhole, through) = Axis(a_min, through) - 0.5f;
		// the edge away from the hinge, taken to be the model's origin
		const bool awayEdge = std::fabs(Axis(a_max, along)) >= std::fabs(Axis(a_min, along));
		Axis(a.keyhole, along) = awayEdge ? Axis(a_max, along) - inset : Axis(a_min, along) + inset;
		a.keyhole.z = a_min.z + std::min(64.0f, size.z * 0.45f);
		a.rot = LockAxes(into, { 0, 0, 1 });
		a.valid = true;
		return a;
	}

	ObjectAnchor OtherSide(const ObjectAnchor& a_anchor)
	{
		if (!a_anchor.back) {
			return a_anchor;
		}
		ObjectAnchor a = a_anchor;
		a.keyhole = a_anchor.back->keyhole;
		a.rot = a_anchor.back->rot;
		a.shown = a_anchor.back->shown;
		a.back = ObjectAnchor::Side{ a_anchor.keyhole, a_anchor.rot, a_anchor.shown };
		return a;
	}

	bool UseOtherSide(const ObjectAnchor& a_anchor, const Pose& a_object, float a_scale, const Vec3& a_head)
	{
		if (!a_anchor.back) {
			return false;
		}
		const Pose front = PlaceOnObject(a_anchor, a_object, a_scale);
		const Pose back = PlaceOnObject(OtherSide(a_anchor), a_object, a_scale);
		// you face into the lock: its "into the lock" points away from you
		const bool atFront = Dot(front.pos - a_head, front.rot.Column(1)) > 0.0f;
		const bool atBack = Dot(back.pos - a_head, back.rot.Column(1)) > 0.0f;
		if (atFront != atBack) {
			return atBack;
		}
		return Distance(back.pos, a_head) < Distance(front.pos, a_head);
	}

	Mat3 RefRotation(const Vec3& a_angles)
	{
		// the engine's NiMatrix3::SetEulerAnglesXYZ (CommonLib): +Y of a model turned by z alone
		// points at (sin z, cos z), as an actor's heading
		const float sx = std::sin(a_angles.x), cx = std::cos(a_angles.x);
		const float sy = std::sin(a_angles.y), cy = std::cos(a_angles.y);
		const float sz = std::sin(a_angles.z), cz = std::cos(a_angles.z);
		Mat3        r;
		r.m[0][0] = cy * cz;
		r.m[0][1] = cy * sz;
		r.m[0][2] = -sy;
		r.m[1][0] = sx * sy * cz - cx * sz;
		r.m[1][1] = sx * sy * sz + cx * cz;
		r.m[1][2] = sx * cy;
		r.m[2][0] = cx * sy * cz + sx * sz;
		r.m[2][1] = cx * sy * sz - sx * cz;
		r.m[2][2] = cx * cy;
		return r;
	}

	Vec3 RefAngles(const Mat3& a_rot)
	{
		const float sy = std::clamp(-a_rot.m[0][2], -1.0f, 1.0f);
		const float y = std::asin(sy);
		if (std::fabs(sy) > 0.9999f) {
			// straight up or down: x and z turn the same way, all of it in z
			return { 0.0f, y, std::atan2(-a_rot.m[1][0], a_rot.m[1][1]) };
		}
		return { std::atan2(a_rot.m[1][2], a_rot.m[2][2]), y, std::atan2(a_rot.m[0][1], a_rot.m[0][0]) };
	}

	namespace
	{
		std::array<Vec3, 8> Corners(const Vec3& a_min, const Vec3& a_max)
		{
			std::array<Vec3, 8> out;
			for (int i = 0; i < 8; ++i) {
				out[i] = { (i & 1) ? a_max.x : a_min.x, (i & 2) ? a_max.y : a_min.y, (i & 4) ? a_max.z : a_min.z };
			}
			return out;
		}
	}

	StandPose StandFacing(const ObjectAnchor& a_anchor, const Vec3& a_min, const Vec3& a_max, const Vec3& a_angles, const Vec3& a_away)
	{
		const Mat3 tilt = RefRotation(a_angles);
		const Vec3 away = Normalize({ a_away.x, a_away.y, 0.0f });
		auto       flat = [](const Vec3& a_v) { return Vec3{ a_v.x, a_v.y, 0.0f }; };
		// the turn about the vertical that points the lock away from you: into the lock on a door
		// or a chest, its up on a trapdoor (whose lock looks down into it)
		Mat3  best = tilt;
		float bestScore = -1e9f;
		for (int deg = 0; deg < 360; ++deg) {
			const Mat3  r = RefRotation({ 0.0f, 0.0f, static_cast<float>(deg) * kDegToRad }) * tilt;
			const float score = Dot(flat(r * a_anchor.rot.Column(1)), away) + Dot(flat(r * a_anchor.rot.Column(2)), away);
			if (score > bestScore) {
				bestScore = score;
				best = r;
			}
		}
		StandPose out;
		out.angles = RefAngles(best);
		const Vec3 middle = best * ((a_min + a_max) * 0.5f);
		float      bottom = 1e9f;
		for (const Vec3& c : Corners(a_min, a_max)) {
			bottom = std::min(bottom, (best * c).z);
		}
		out.offset = { -middle.x, -middle.y, -bottom };
		return out;
	}

	float StandWidth(const Vec3& a_min, const Vec3& a_max, const Vec3& a_angles)
	{
		const Mat3 tilt = RefRotation(a_angles);
		const Vec3 middle = tilt * ((a_min + a_max) * 0.5f);
		float      reach = 0.0f;
		for (const Vec3& c : Corners(a_min, a_max)) {
			const Vec3 d = tilt * c - middle;
			reach = std::max(reach, std::hypot(d.x, d.y));
		}
		return 2.0f * reach;
	}

	std::map<std::string, ObjectAnchor> ParseObjectAnchors(const std::string& a_text)
	{
		struct Raw
		{
			Vec3 keyhole, into{ 0, 1, 0 }, up{ 0, 0, 1 };
			bool      valid{ false };
			bool      hasShown{ false };
			LockShown shown{ LockShown::kWhole };
		};
		// [0] the marked side, [1] a door's other side (the keys ending in 2)
		std::map<std::string, std::array<Raw, 2>> raw;
		for (const auto& [key, value] : ParseIni(a_text)) {
			const auto dot = key.rfind('.');
			if (dot == std::string::npos) {
				continue;
			}
			std::string name = key.substr(dot + 1);
			const int   side = !name.empty() && name.back() == '2' ? 1 : 0;
			if (side == 1) {
				name.pop_back();
			}
			if (name != "vobjectkeyhole" && name != "vobjectinto" && name != "vobjectup" && name != "sobjectlock") {
				continue;
			}
			Raw& r = raw[NormalizeModelPath(key.substr(0, dot))][side];
			if (name == "vobjectkeyhole") {
				r.valid = ParseVec(value, r.keyhole);
			} else if (name == "sobjectlock") {
				r.hasShown = ParseLockShown(value, r.shown);
			} else if (name == "vobjectinto") {
				ParseVec(value, r.into);
			} else {
				ParseVec(value, r.up);
			}
		}
		std::map<std::string, ObjectAnchor> out;
		for (const auto& [model, sides] : raw) {
			const Raw& r = sides[0];
			// "none" needs no place
			const bool none = r.hasShown && r.shown == LockShown::kNone;
			if (!r.valid && !none) {
				continue;
			}
			ObjectAnchor a{ r.keyhole, LockAxes(r.into, r.up), true, r.shown };
			if (const Raw& b = sides[1]; b.valid && !none) {
				a.back = ObjectAnchor::Side{ b.keyhole, LockAxes(b.into, b.up), b.shown == LockShown::kNone ? LockShown::kWhole : b.shown };
			}
			out[model] = a;
		}
		return out;
	}

	std::string WriteObjectAnchor(const std::string& a_text, const std::string& a_modelPath, const ObjectAnchor& a_anchor)
	{
		const std::string     key = NormalizeModelPath(a_modelPath);
		std::vector<IniValue> values = AnchorValues(key, a_anchor);
		// keys saved before that this place no longer has (a back side gone, or no lock on it now)
		// are emptied, so an old place isn't left behind
		std::set<std::string> saved;
		for (const auto& [name, value] : ParseIni(a_text)) {
			if (const auto dot = name.rfind('.'); dot != std::string::npos && NormalizeModelPath(name.substr(0, dot)) == key) {
				saved.insert(name.substr(dot + 1));
			}
		}
		for (const char* k : kAnchorKeys) {
			std::string lower = k;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			const bool written = std::any_of(values.begin(), values.end(), [&](const IniValue& v) { return v.key == k; });
			if (!written && saved.contains(lower)) {
				values.push_back({ key, k, "" });
			}
		}
		return SetIniValues(a_text, values);
	}

	std::string ObjectAnchorText(const ObjectAnchor& a_anchor)
	{
		std::string out;
		for (const auto& v : AnchorValues("", a_anchor)) {
			out += std::format("{}{} = {}", out.empty() ? "" : " | ", v.key, v.value);
		}
		return out;
	}

	const std::map<std::string, ObjectAnchor>& DefaultObjectAnchors()
	{
		// Lined up in NifSkope with tools/lockplacer (or in game with the markup tool, the "Lock on"
		// lines of the log). "none": locked in the game's plugins, but there is nothing to pick there
		// (the Night Mother's coffin, Apocrypha's urns, a trap's trigger) or no reaching it (a trapdoor
		// in the ceiling): the lock stays in front of you. In pieces: MSVC takes no string literal
		// longer than 16 KB.
		static const std::string                         kText = R"([architecture\farmhouse\farmhousedoor01.nif]
vObjectKeyhole = -40.000, -4.400, 83.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = -40.000, 4.400, 83.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[architecture\farmhouse\farmhouseldoor01.nif]
vObjectKeyhole = -40.000, -4.400, 83.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
[architecture\farmhouse\interior\basement\farmbtrapdoor02.nif]
vObjectKeyhole = 0.000, 23.000, 14.400
vObjectInto = 0.000, 0.000, -1.000
vObjectUp = -1.000, 0.000, 0.000
sObjectLock = ring
[architecture\farmhouse\interior\basement\farmhousejaildoor01.nif]
vObjectKeyhole = -38.700, -1.400, 86.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = -38.800, 1.400, 86.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[architecture\orclonghouse\orcdoor01.nif]
vObjectKeyhole = 42.000, 203.000, 62.000
vObjectInto = 0.000, -1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = 42.000, 198.000, 62.000
vObjectInto2 = 0.000, 1.000, -0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = whole
[architecture\orclonghouse\orcdoorload01.nif]
vObjectKeyhole = -41.920, -2.000, 62.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[architecture\riften\riftendoor01.nif]
vObjectKeyhole = 9.500, 4.300, 77.850
vObjectInto = 0.000, -1.000, 0.000
vObjectUp = -0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = 9.500, -4.200, 77.850
vObjectInto2 = -0.000, 1.000, -0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[architecture\riften\riftendoor02.nif]
vObjectKeyhole = -2.500, -4.500, 88.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = ring
vObjectKeyhole2 = -2.500, 4.500, 88.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = ring
[architecture\riften\rtirongate01.nif]
vObjectKeyhole = -52.300, -6.300, 82.500
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = -52.300, 6.300, 82.500
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[architecture\riften\rtirongate02.nif]
vObjectKeyhole = -52.400, -6.300, 82.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = -52.400, 6.300, 82.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[architecture\riften\rtmarketstalldoor01.nif]
vObjectKeyhole = -43.500, -39.600, 29.300
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
[architecture\solitude\doors\sdoorjail01.nif]
vObjectKeyhole = 2.850, 77.500, 3.100
vObjectInto = 0.000, 0.000, -1.000
vObjectUp = -0.000, 1.000, 0.000
sObjectLock = keyhole
vObjectKeyhole2 = 2.850, 77.500, -3.400
vObjectInto2 = 0.000, -0.000, 1.000
vObjectUp2 = -0.000, 1.000, 0.000
sObjectLock2 = keyhole
[architecture\solitude\doors\shousedoor01.nif]
vObjectKeyhole = 1.700, 9.560, -63.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[architecture\solitude\doors\shousedoor02.nif]
vObjectKeyhole = -10.600, -2.300, -4.850
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = -10.750, 3.000, -4.850
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[architecture\solitude\doors\shousedoor03.nif]
vObjectKeyhole = 9.300, -29.300, 85.500
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = 9.300, -22.800, 85.500
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[architecture\whiterun\wrdoor02.nif]
vObjectKeyhole = -7.600, -6.300, 54.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = 3.700, -6.300, 54.000
vObjectInto2 = -1.000, 0.000, -0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[architecture\whiterun\wrdoormaingate01.nif]
vObjectKeyhole = -15.300, -9.000, 36.000
vObjectInto = -1.000, 0.000, -0.000
vObjectUp = -0.000, 0.000, 1.000
sObjectLock = whole
[architecture\whiterun\wrdragondoor01.nif]
vObjectKeyhole = -6.800, 15.000, 19.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[architecture\whiterun\wrinteriors\wrdungeon\wrprisoncellfloorgrate01door.nif]
vObjectKeyhole = -3.000, 44.280, 3.500
vObjectInto = 0.000, -0.000, -1.000
vObjectUp = 1.000, 0.000, 0.000
sObjectLock = keyhole
[architecture\whiterun\wrinteriors\wrjaildoor01.nif]
vObjectKeyhole = 58.500, -2.800, -18.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = 58.500, 2.800, -18.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[architecture\whiterun\wrshackdoor01.nif]
vObjectKeyhole = -5.500, -50.000, 13.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[architecture\windhelm\doors\whdoor01.nif]
vObjectKeyhole = -43.500, 20.000, 71.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[architecture\windhelm\doors\whgrayquarterdoor01.nif]
vObjectKeyhole = 0.000, 77.000, -9.000
vObjectInto = 0.970, 0.242, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[clutter\common\metalcagedoor01.nif]
vObjectKeyhole = -74.000, 11.300, -17.500
vObjectInto = 0.000, -0.999, -0.052
vObjectUp = 0.000, -0.052, 0.999
sObjectLock = keyhole
[clutter\common\metalcagelonggate01.nif]
vObjectKeyhole = 91.000, 130.500, 95.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[clutter\common\strongbox01.nif]
vObjectKeyhole = -0.100, 8.600, 9.500
vObjectInto = 0.000, -0.985, -0.174
vObjectUp = 0.000, -0.174, 0.985
sObjectLock = keyhole
[clutter\containers\chest01\chest01.nif]
vObjectKeyhole = 0.000, 24.500, 33.800
vObjectInto = 0.000, -1.000, 0.000
vObjectUp = -0.000, 0.000, 1.000
sObjectLock = whole
[clutter\containers\safeandlock01.nif]
vObjectKeyhole = 23.210, -11.200, 31.800
vObjectInto = -1.000, -0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
[clutter\displaycases\displaycaselgangled01.nif]
vObjectKeyhole = 0.000, -35.100, 61.250
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
[clutter\displaycases\displaycasesmangled01.nif]
vObjectKeyhole = -0.160, -13.000, 6.900
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
[clutter\displaycases\displaycasesmflat01.nif]
vObjectKeyhole = -0.160, -13.000, 8.200
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
[clutter\dwemer\dwechest01.nif]
vObjectKeyhole = 32.000, -10.000, 21.800
vObjectInto = -0.000, -0.000, -1.000
vObjectUp = 0.000, 1.000, -0.000
sObjectLock = keyhole
[clutter\dwemer\dwecontainer01.nif]
vObjectKeyhole = 0.000, 0.000, 53.600
vObjectInto = -0.000, -0.000, -1.000
vObjectUp = 0.000, 1.000, -0.000
sObjectLock = keyhole
[clutter\dwemer\dwecontainter02.nif]
vObjectKeyhole = 0.000, 0.000, 40.300
vObjectInto = -0.000, -0.000, -1.000
vObjectUp = 0.000, 1.000, -0.000
sObjectLock = keyhole
[clutter\falmer\falmerbigfencegate01.nif]
vObjectKeyhole = 0.000, -1.000, 60.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = ring
[clutter\falmer\falmercontainer.nif]
vObjectKeyhole = 32.000, 0.000, 31.800
vObjectInto = -1.000, -0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = ring
[clutter\falmer\falmercontainer02.nif]
vObjectKeyhole = 0.000, -34.500, 33.800
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = ring
)"
			R"([clutter\nightmother\nmcoffin01.nif]
sObjectLock = none
[clutter\ruins\ruins_largechest.nif]
vObjectKeyhole = 1.000, -34.500, 58.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[clutter\ruins\ruins_smallchest.nif]
vObjectKeyhole = 0.000, -26.200, 38.800
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = ring
[clutter\stockade\stockadegate01.nif]
vObjectKeyhole = -5.000, -66.500, 68.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = ring
[clutter\upperclass\upperchest01.nif]
vObjectKeyhole = 0.000, 27.500, 34.400
vObjectInto = 0.000, -1.000, 0.000
vObjectUp = -0.000, 0.000, 1.000
sObjectLock = whole
[dlc01\clutter\dlc01snowelfchest.nif]
vObjectKeyhole = 0.100, -26.500, 29.400
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
[dlc01\dungeons\castle\lghalls\casexfreesmdoor01.nif]
vObjectKeyhole = 34.500, -4.500, 65.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = 58.500, 4.300, 65.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[dlc01\dungeons\castle\lghalls\casexfreesmdoor02.nif]
vObjectKeyhole = 46.500, -4.400, 65.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = 46.500, 4.300, 65.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[dlc02\architecture\ravenrock\bulwark\dlc2rrjailgrate01.nif]
vObjectKeyhole = 0.000, 244.000, 16.500
vObjectInto = 0.000, -0.000, -1.000
vObjectUp = 1.000, 0.000, 0.000
sObjectLock = keyhole
[dlc02\architecture\ravenrock\dlc2rrdoorload01.nif]
vObjectKeyhole = -40.520, -6.500, 66.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
[dlc02\architecture\ravenrock\redoran\redoranadoor01.nif]
vObjectKeyhole = -20.000, -5.000, 74.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = 20.000, 5.000, 74.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = whole
[dlc02\architecture\ravenrock\redoran\redoranldoor01.nif]
vObjectKeyhole = -20.000, -4.400, 74.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[dlc02\architecture\telvannitower\dlc2telmithryndoor01.nif]
vObjectKeyhole = -8.000, -8.000, 63.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
[dlc02\dungeons\apocrypha\animated\apourn\apourn01.nif]
sObjectLock = none
[dlc02\dungeons\dwemer\animated\dlc2dwemerwatergate\dlc2dwemerwatergate.nif]
vObjectKeyhole = 0.000, -53.500, 113.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
[dlc02\dungeons\icecastle\dlc02icedoor.nif]
vObjectKeyhole = 11.000, 42.000, 115.000
vObjectInto = 0.000, -1.000, 0.000
vObjectUp = -0.000, 0.000, 1.000
sObjectLock = keyhole
[dlc02\dungeons\icecastle\dlc02icedoorloadext.nif]
vObjectKeyhole = 12.000, 42.000, 115.000
vObjectInto = 0.000, -1.000, 0.000
vObjectUp = -0.000, 0.000, 1.000
sObjectLock = keyhole
[dungeons\dwemer\door\dwemerlargedoor01.nif]
vObjectKeyhole = -0.500, -5.000, 91.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = -0.500, 10.000, 91.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[dungeons\dwemer\door\dwemerlargedoorload01.nif]
vObjectKeyhole = -0.900, -22.500, 105.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
[dungeons\dwemer\door\dwemersmalldoor01.nif]
vObjectKeyhole = -66.000, -1.000, 80.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = -66.000, 1.000, 80.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = whole
[dungeons\dwemer\door\dwemersmalldoorload01.nif]
vObjectKeyhole = -65.000, 1.500, 91.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[dungeons\dwemer\partitions\dweptndoor01.nif]
vObjectKeyhole = 0.000, -5.200, 95.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = 0.000, 5.200, 95.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[dungeons\imperial\door\impwallcage01.nif]
vObjectKeyhole = 40.000, -37.500, -0.600
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[dungeons\imperial\door\impwooddoordouble01.nif]
vObjectKeyhole = 2.000, 260.500, 77.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = ring
vObjectKeyhole2 = 2.000, 275.500, 77.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = ring
[dungeons\imperial\door\impwooddoordoubleload01.nif]
vObjectKeyhole = 0.000, 248.500, 72.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = ring
[dungeons\imperial\door\impwooddoorholedoorload.nif]
vObjectKeyhole = -44.000, -53.500, 61.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[dungeons\imperial\door\impwooddoorsingle01.nif]
vObjectKeyhole = -52.000, -3.500, 76.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = -52.000, 5.500, 76.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = whole
[dungeons\imperial\door\impwooddoorsingleload01.nif]
vObjectKeyhole = -52.000, -3.500, 77.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[dungeons\imperial\door\trapdoorladder01.nif]
sObjectLock = none
[dungeons\imperial\jail\impjaildoor01.nif]
vObjectKeyhole = 170.000, -2.800, 73.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = 170.000, 3.000, 73.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[dungeons\imperial\jail\impjaildoor02.nif]
vObjectKeyhole = 86.000, -2.800, 73.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = 86.000, 2.900, 73.000
vObjectInto2 = 0.000, -1.000, 0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[dungeons\nordic\doors\animated\cagedoor\norbridgecageexsm01.nif]
vObjectKeyhole = -4.000, 0.000, 80.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = 3.500, 0.000, 80.000
vObjectInto2 = -1.000, 0.000, -0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[dungeons\nordic\doors\animated\mediumdoor\ruinsmediumdoor01.nif]
vObjectKeyhole = -139.300, 0.000, 112.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = -116.500, 0.000, 112.000
vObjectInto2 = -1.000, 0.000, -0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[dungeons\nordic\doors\animated\smdoor01\nordoorsm01.nif]
vObjectKeyhole = -1.500, 0.000, 63.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = 4.000, 0.000, 63.000
vObjectInto2 = -1.000, 0.000, -0.000
vObjectUp2 = -0.000, -0.000, 1.000
sObjectLock2 = whole
[dungeons\nordic\doors\animated\smdoor01\nordoorsmload01.nif]
vObjectKeyhole = -1.500, 0.000, 62.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[dungeons\nordic\doors\animated\smdoor02\nordoorsm02.nif]
vObjectKeyhole = -4.500, 0.000, 53.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = 3.000, 0.000, 53.000
vObjectInto2 = -1.000, 0.000, -0.000
vObjectUp2 = -0.000, -0.000, 1.000
sObjectLock2 = whole
)"
			R"([dungeons\nordic\doors\animated\smdoor02\nordoorsm02flipped.nif]
vObjectKeyhole = -4.500, 0.000, 54.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = 3.000, 0.000, 54.000
vObjectInto2 = -1.000, 0.000, -0.000
vObjectUp2 = -0.000, -0.000, 1.000
sObjectLock2 = whole
[dungeons\nordic\doors\animated\trapdoor\ruins_trapdoor01.nif]
vObjectKeyhole = -4.000, 193.000, 8.500
vObjectInto = 0.000, -0.000, -1.000
vObjectUp = -1.000, -0.000, 0.000
sObjectLock = ring
[dungeons\nordic\doors\animated\wooddoor\ruinswooddoor01.nif]
vObjectKeyhole = -7.700, -22.500, 77.500
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = 8.100, -22.500, 77.500
vObjectInto2 = -1.000, 0.000, -0.000
vObjectUp2 = -0.000, 0.000, 1.000
sObjectLock2 = keyhole
[dungeons\nordic\doors\animated\wooddoor\ruinswooddoorload01.nif]
vObjectKeyhole = -7.700, -22.700, 77.600
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
[dungeons\riften\ratwayhall\riftenrwdoor01.nif]
vObjectKeyhole = -29.000, -0.500, 77.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = -31.500, 6.200, 77.000
vObjectInto2 = 0.000, -1.000, -0.000
vObjectUp2 = -0.000, -0.000, 1.000
sObjectLock2 = whole
[dungeons\riften\ratwayhall\riftenrwdoorjail01.nif]
vObjectKeyhole = 42.000, -3.000, 72.500
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = keyhole
vObjectKeyhole2 = 42.000, 2.900, 72.500
vObjectInto2 = 0.000, -1.000, -0.000
vObjectUp2 = -0.000, -0.000, 1.000
sObjectLock2 = keyhole
[dungeons\riften\ratwayhall\riftenrwdoorload01.nif]
vObjectKeyhole = -28.000, -0.500, 76.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = -31.000, 6.200, 77.000
vObjectInto2 = 0.000, -1.000, -0.000
vObjectUp2 = -0.000, -0.000, 1.000
sObjectLock2 = whole
[dungeons\ship\katariah\shipkatariahdoor01.nif]
vObjectKeyhole = -33.000, -0.500, 91.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = -37.000, 6.500, 91.000
vObjectInto2 = 0.000, -1.000, -0.000
vObjectUp2 = -0.000, -0.000, 1.000
sObjectLock2 = whole
[dungeons\ship\katariah\shipkatariahdoor02.nif]
vObjectKeyhole = -34.000, -0.500, 90.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = -37.000, 6.500, 90.500
vObjectInto2 = 0.000, -1.000, -0.000
vObjectUp2 = -0.000, -0.000, 1.000
sObjectLock2 = whole
[dungeons\ship\katariah\shipkatariahdoorload01.nif]
vObjectKeyhole = -34.000, -0.500, 91.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = -37.000, 6.500, 91.000
vObjectInto2 = 0.000, -1.000, -0.000
vObjectUp2 = -0.000, -0.000, 1.000
sObjectLock2 = whole
[dungeons\ship\shipdoor01.nif]
vObjectKeyhole = -33.000, -0.500, 77.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = -32.000, 6.500, 77.000
vObjectInto2 = 0.000, -1.000, -0.000
vObjectUp2 = -0.000, -0.000, 1.000
sObjectLock2 = whole
[dungeons\ship\shipdoor02.nif]
vObjectKeyhole = -32.000, -0.500, 77.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
vObjectKeyhole2 = -32.000, 6.500, 77.000
vObjectInto2 = 0.000, -1.000, -0.000
vObjectUp2 = -0.000, -0.000, 1.000
sObjectLock2 = whole
[dungeons\ship\shiptrapdoor01.nif]
vObjectKeyhole = -31.680, 6.500, 4.800
vObjectInto = -0.000, -1.000, -0.000
vObjectUp = 0.000, 0.000, 1.000
sObjectLock = whole
[furniture\noble\noblechest01.nif]
vObjectKeyhole = 0.000, -21.000, 18.200
vObjectInto = 0.000, 0.985, -0.174
vObjectUp = 0.000, 0.174, 0.985
sObjectLock = ring
[traps\hingetrap\traphingetrigger01.nif]
sObjectLock = none
)";
		static const std::map<std::string, ObjectAnchor> kAnchors = ParseObjectAnchors(kText);
		return kAnchors;
	}

	Pose PlaceOnObject(const ObjectAnchor& a_anchor, const Pose& a_object, float a_scale)
	{
		Pose p;
		p.pos = a_object.rot * (a_anchor.keyhole * a_scale) + a_object.pos;
		p.rot = a_object.rot * a_anchor.rot;
		return p;
	}

	ObjectAnchor ObjectAnchorFromPose(const Pose& a_lock, const Pose& a_object, float a_scale)
	{
		const Mat3   back = a_object.rot.Transposed();
		const Mat3   local = back * a_lock.rot;
		ObjectAnchor a;
		a.keyhole = (back * (a_lock.pos - a_object.pos)) / (a_scale > 1e-4f ? a_scale : 1.0f);
		a.rot = LockAxes(local.Column(1), local.Column(2));
		a.valid = true;
		return a;
	}

	std::string WriteLockableModels(const std::vector<LockableModel>& a_models)
	{
		std::string out = "; VR Lockpicking: the door and chest models your game locks somewhere (for tools/lockplacer)\n"
		                  "; kind\tmodel\tbounds min\tbounds max\tlocked references\tname\n";
		for (const auto& m : a_models) {
			out += std::format("{}\t{}\t{:.0f} {:.0f} {:.0f}\t{:.0f} {:.0f} {:.0f}\t{}\t{}\n", m.door ? "door" : "container", m.model, m.boundMin.x,
				m.boundMin.y, m.boundMin.z, m.boundMax.x, m.boundMax.y, m.boundMax.z, m.locked, m.name);
		}
		return out;
	}

	std::vector<LockableModel> ParseLockableModels(const std::string& a_text)
	{
		std::vector<LockableModel> out;
		std::size_t                start = 0;
		while (start < a_text.size()) {
			std::size_t end = a_text.find('\n', start);
			if (end == std::string::npos) {
				end = a_text.size();
			}
			std::string line = a_text.substr(start, end - start);
			start = end + 1;
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.empty() || line[0] == ';') {
				continue;
			}
			std::vector<std::string> f;
			std::size_t              p = 0;
			while (true) {
				const std::size_t tab = line.find('\t', p);
				f.push_back(line.substr(p, tab == std::string::npos ? std::string::npos : tab - p));
				if (tab == std::string::npos) {
					break;
				}
				p = tab + 1;
			}
			if (f.size() < 4 || (f[0] != "door" && f[0] != "container") || f[1].empty()) {
				continue;
			}
			LockableModel m;
			m.door = f[0] == "door";
			m.model = f[1];
			ParseVec(f[2], m.boundMin);
			ParseVec(f[3], m.boundMax);
			m.locked = f.size() > 4 ? std::atoi(f[4].c_str()) : 0;
			m.name = f.size() > 5 ? f[5] : std::string();
			out.push_back(m);
		}
		return out;
	}
}
