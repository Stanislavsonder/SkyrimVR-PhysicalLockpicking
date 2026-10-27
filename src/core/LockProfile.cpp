#include "LockProfile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
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

		// a_clearBack: a back side saved before is gone (its keys are emptied)
		std::vector<IniValue> AnchorValues(const std::string& a_section, const ObjectAnchor& a_a, bool a_clearBack = false)
		{
			std::vector<IniValue> out = {
				{ a_section, "vObjectKeyhole", AnchorVecText(a_a.keyhole) },
				{ a_section, "vObjectInto", AnchorVecText(a_a.rot.Column(1)) },
				{ a_section, "vObjectUp", AnchorVecText(a_a.rot.Column(2)) },
				{ a_section, "bObjectKeyholeOnly", a_a.keyholeOnly ? "1" : "0" },
			};
			if (a_a.back) {
				const auto& b = *a_a.back;
				out.push_back({ a_section, "vObjectKeyhole2", AnchorVecText(b.keyhole) });
				out.push_back({ a_section, "vObjectInto2", AnchorVecText(b.rot.Column(1)) });
				out.push_back({ a_section, "vObjectUp2", AnchorVecText(b.rot.Column(2)) });
				out.push_back({ a_section, "bObjectKeyholeOnly2", b.keyholeOnly ? "1" : "0" });
			} else if (a_clearBack) {
				for (const char* key : { "vObjectKeyhole2", "vObjectInto2", "vObjectUp2", "bObjectKeyholeOnly2" }) {
					out.push_back({ a_section, key, "" });
				}
			}
			return out;
		}
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

	ObjectAnchor OtherSide(const ObjectAnchor& a_anchor, const Vec3& a_min, const Vec3& a_max)
	{
		if (a_anchor.back) {
			ObjectAnchor a = a_anchor;
			a.keyhole = a_anchor.back->keyhole;
			a.rot = a_anchor.back->rot;
			a.keyholeOnly = a_anchor.back->keyholeOnly;
			a.back = ObjectAnchor::Side{ a_anchor.keyhole, a_anchor.rot, a_anchor.keyholeOnly };
			return a;
		}
		ObjectAnchor a = a_anchor;
		const Vec3   into = a_anchor.rot.Column(1);
		const int    i = std::fabs(into.x) >= std::fabs(into.y) ? (std::fabs(into.x) >= std::fabs(into.z) ? 0 : 2) :
		                                                          (std::fabs(into.y) >= std::fabs(into.z) ? 1 : 2);
		Axis(a.keyhole, i) = Axis(a_min, i) + Axis(a_max, i) - Axis(a_anchor.keyhole, i);
		// turned about the up axis: right and into the lock reversed
		a.rot = Mat3::FromColumns(-a_anchor.rot.Column(0), -into, a_anchor.rot.Column(2));
		return a;
	}

	bool UseOtherSide(const ObjectAnchor& a_anchor, const Vec3& a_min, const Vec3& a_max, const Pose& a_object, float a_scale, const Vec3& a_head)
	{
		const Pose front = PlaceOnObject(a_anchor, a_object, a_scale);
		const Pose back = PlaceOnObject(OtherSide(a_anchor, a_min, a_max), a_object, a_scale);
		// you face into the lock: its "into the lock" points away from you
		const bool atFront = Dot(front.pos - a_head, front.rot.Column(1)) > 0.0f;
		const bool atBack = Dot(back.pos - a_head, back.rot.Column(1)) > 0.0f;
		if (atFront != atBack) {
			return atBack;
		}
		return Distance(back.pos, a_head) < Distance(front.pos, a_head);
	}

	std::map<std::string, ObjectAnchor> ParseObjectAnchors(const std::string& a_text)
	{
		struct Raw
		{
			Vec3 keyhole, into{ 0, 1, 0 }, up{ 0, 0, 1 };
			bool valid{ false };
			bool keyholeOnly{ false };
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
			if (name != "vobjectkeyhole" && name != "vobjectinto" && name != "vobjectup" && name != "bobjectkeyholeonly") {
				continue;
			}
			Raw& r = raw[NormalizeModelPath(key.substr(0, dot))][side];
			if (name == "vobjectkeyhole") {
				r.valid = ParseVec(value, r.keyhole);
			} else if (name == "bobjectkeyholeonly") {
				std::string v = value;
				std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				r.keyholeOnly = v == "1" || v == "true" || v == "yes" || v == "on";
			} else if (name == "vobjectinto") {
				ParseVec(value, r.into);
			} else {
				ParseVec(value, r.up);
			}
		}
		std::map<std::string, ObjectAnchor> out;
		for (const auto& [model, sides] : raw) {
			const Raw& r = sides[0];
			if (!r.valid) {
				continue;
			}
			ObjectAnchor a{ r.keyhole, LockAxes(r.into, r.up), true, r.keyholeOnly };
			if (const Raw& b = sides[1]; b.valid) {
				a.back = ObjectAnchor::Side{ b.keyhole, LockAxes(b.into, b.up), b.keyholeOnly };
			}
			out[model] = a;
		}
		return out;
	}

	std::string WriteObjectAnchor(const std::string& a_text, const std::string& a_modelPath, const ObjectAnchor& a_anchor)
	{
		const std::string key = NormalizeModelPath(a_modelPath);
		const auto        before = ParseObjectAnchors(a_text);
		const auto        old = before.find(key);
		const bool        clearBack = old != before.end() && old->second.back && !a_anchor.back;
		return SetIniValues(a_text, AnchorValues(key, a_anchor, clearBack));
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
		// lines of the log).
		static const std::string                         kText = R"([architecture\farmhouse\farmhousedoor01.nif]
vObjectKeyhole = -40.000, -4.200, 83.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 1
[architecture\farmhouse\farmhouseldoor01.nif]
vObjectKeyhole = -40.000, -4.500, 83.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 1
[architecture\farmhouse\interior\basement\farmbtrapdoor02.nif]
vObjectKeyhole = 0.000, 32.000, 16.300
vObjectInto = 0.000, 0.000, -1.000
vObjectUp = -1.000, 0.000, 0.000
bObjectKeyholeOnly = 0
[architecture\farmhouse\interior\basement\farmhousejaildoor01.nif]
vObjectKeyhole = -38.500, -1.200, 86.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 1
[architecture\orclonghouse\orcdoor01.nif]
vObjectKeyhole = 42.000, 203.000, 62.000
vObjectInto = 0.000, -1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 0
[architecture\orclonghouse\orcdoorload01.nif]
vObjectKeyhole = -41.920, -2.000, 62.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 0
[architecture\riften\riftendoor01.nif]
vObjectKeyhole = 10.000, 5.000, 76.000
vObjectInto = 0.000, -1.000, 0.000
vObjectUp = -0.000, 0.000, 1.000
bObjectKeyholeOnly = 0
[architecture\riften\riftendoor02.nif]
vObjectKeyhole = -6.000, -4.300, 88.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 0
[architecture\riften\rtirongate01.nif]
vObjectKeyhole = -52.300, -6.200, 82.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 1
[architecture\riften\rtirongate02.nif]
vObjectKeyhole = -52.400, -6.000, 82.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 1
[architecture\riften\rtmarketstalldoor01.nif]
vObjectKeyhole = -43.500, -39.500, 29.300
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 1
[architecture\solitude\doors\sdoorjail01.nif]
vObjectKeyhole = 2.700, 77.000, 3.100
vObjectInto = 0.000, 0.000, -1.000
vObjectUp = -0.000, 1.000, 0.000
bObjectKeyholeOnly = 1
[architecture\solitude\doors\shousedoor01.nif]
vObjectKeyhole = 1.700, 9.560, -63.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 0
[architecture\solitude\doors\shousedoor02.nif]
vObjectKeyhole = -10.560, -2.300, -5.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 1
[architecture\solitude\doors\shousedoor03.nif]
vObjectKeyhole = 9.300, -29.300, 85.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 1
[architecture\whiterun\wrdoor02.nif]
vObjectKeyhole = -7.500, -6.280, 54.000
vObjectInto = 1.000, 0.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 1
[architecture\whiterun\wrinteriors\wrdungeon\wrprisoncellfloorgrate01door.nif]
vObjectKeyhole = -3.000, 44.280, 3.500
vObjectInto = 0.000, -0.000, -1.000
vObjectUp = 1.000, 0.000, 0.000
bObjectKeyholeOnly = 0
[architecture\whiterun\wrinteriors\wrjaildoor01.nif]
vObjectKeyhole = 58.500, -2.800, -18.000
vObjectInto = 0.000, 1.000, 0.000
vObjectUp = 0.000, 0.000, 1.000
bObjectKeyholeOnly = 0
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
