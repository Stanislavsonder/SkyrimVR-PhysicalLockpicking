#include "LockProfile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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
