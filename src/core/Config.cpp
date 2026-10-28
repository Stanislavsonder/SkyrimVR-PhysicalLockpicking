#include "Config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>

namespace vrlp
{
	namespace
	{
		std::string Trim(const std::string& s)
		{
			std::size_t b = 0;
			std::size_t e = s.size();
			while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) {
				++b;
			}
			while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
				--e;
			}
			return s.substr(b, e - b);
		}

		std::string Lower(std::string s)
		{
			for (auto& c : s) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return s;
		}

		bool ParseFloat(const std::string& s, float& out)
		{
			if (s.empty()) {
				return false;
			}
			char*       end = nullptr;
			const float v = std::strtof(s.c_str(), &end);
			if (end == s.c_str()) {
				return false;
			}
			out = v;
			return true;
		}

		bool ParseBool(const std::string& s, bool& out)
		{
			const auto l = Lower(s);
			if (l == "1" || l == "true" || l == "yes" || l == "on") {
				out = true;
				return true;
			}
			if (l == "0" || l == "false" || l == "no" || l == "off") {
				out = false;
				return true;
			}
			return false;
		}

		bool ParseVecText(const std::string& s, Vec3& out)
		{
			std::string tmp = s;
			for (auto& c : tmp) {
				if (c == ',' || c == ';') {
					c = ' ';
				}
			}
			std::istringstream in(tmp);
			Vec3               v;
			if (in >> v.x >> v.y >> v.z) {
				out = v;
				return true;
			}
			return false;
		}
	}

	// MCM Helper saves its settings file with a UTF-8 signature, which must not end up in front
	// of the first section name.
	static std::string StripBom(const std::string& a_text)
	{
		return a_text.rfind("\xEF\xBB\xBF", 0) == 0 ? a_text.substr(3) : a_text;
	}

	std::map<std::string, std::string> ParseIni(const std::string& a_text)
	{
		std::map<std::string, std::string> result;
		std::istringstream                 in(StripBom(a_text));
		std::string                        line;
		std::string                        section;
		while (std::getline(in, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') {
				continue;
			}
			if (line.front() == '[') {
				const auto close = line.find(']');
				section = Lower(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			auto key = Lower(Trim(line.substr(0, eq)));
			auto value = Trim(line.substr(eq + 1));
			// strip trailing inline comment
			for (const char marker : { ';', '#' }) {
				const auto pos = value.find(marker);
				if (pos != std::string::npos) {
					value = Trim(value.substr(0, pos));
				}
			}
			if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
				value = value.substr(1, value.size() - 2);
			}
			result[section + "." + key] = value;
		}
		return result;
	}

	std::set<std::string> McmSettingKeys(const std::string& a_configJson)
	{
		std::set<std::string> keys;
		const std::string     marker = "\"id\"";
		for (auto pos = a_configJson.find(marker); pos != std::string::npos; pos = a_configJson.find(marker, pos + 1)) {
			const auto open = a_configJson.find('"', a_configJson.find(':', pos + marker.size()) + 1);
			const auto close = open == std::string::npos ? open : a_configJson.find('"', open + 1);
			if (close == std::string::npos) {
				break;
			}
			const std::string id = a_configJson.substr(open + 1, close - open - 1);  // "bEnabled:General"
			const auto        colon = id.find(':');
			if (colon != std::string::npos) {
				keys.insert(Lower(id.substr(colon + 1)) + "." + Lower(id.substr(0, colon)));
			}
		}
		return keys;
	}

	std::string KeepIniKeys(const std::string& a_text, const std::set<std::string>& a_keys, std::vector<std::string>* a_dropped)
	{
		std::istringstream in(StripBom(a_text));
		std::string        out;
		std::string        line;
		std::string        section;
		while (std::getline(in, line)) {
			const std::string t = Trim(line);
			if (!t.empty() && t.front() == '[') {
				const auto close = t.find(']');
				section = Lower(Trim(t.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
			} else if (const auto eq = t.find('='); eq != std::string::npos && t[0] != ';' && t[0] != '#') {
				const std::string key = section + "." + Lower(Trim(t.substr(0, eq)));
				if (!a_keys.contains(key)) {
					if (a_dropped) {
						a_dropped->push_back(key);
					}
					continue;
				}
			}
			out += line;
			out += '\n';
		}
		return out;
	}

	std::string SetIniValues(const std::string& a_text, const std::vector<IniValue>& a_values)
	{
		std::vector<std::string> lines;
		{
			std::istringstream in(StripBom(a_text));
			std::string        line;
			while (std::getline(in, line)) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				lines.push_back(line);
			}
		}
		for (const auto& v : a_values) {
			const std::string wantSection = Lower(v.section);
			const std::string wantKey = Lower(v.key);
			const std::string newLine = v.key + "=" + v.value;
			std::string       section;
			bool              done = false;
			std::size_t       sectionEnd = std::string::npos;  // insert position for a new key
			for (std::size_t n = 0; n < lines.size() && !done; ++n) {
				const std::string t = Trim(lines[n]);
				if (t.size() >= 2 && t.front() == '[') {
					if (Lower(section) == wantSection && sectionEnd == std::string::npos) {
						sectionEnd = n;
					}
					section = Trim(t.substr(1, t.find(']') - 1));
					continue;
				}
				if (t.empty() || t[0] == ';' || t[0] == '#') {
					continue;
				}
				const auto eq = t.find('=');
				if (eq != std::string::npos && Lower(section) == wantSection && Lower(Trim(t.substr(0, eq))) == wantKey) {
					lines[n] = newLine;
					done = true;
				}
			}
			if (done) {
				continue;
			}
			if (Lower(section) == wantSection && sectionEnd == std::string::npos) {
				sectionEnd = lines.size();  // the wanted section is the last one
			}
			if (sectionEnd != std::string::npos) {
				// keep blank lines between sections where they were
				while (sectionEnd > 0 && Trim(lines[sectionEnd - 1]).empty()) {
					--sectionEnd;
				}
				lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(sectionEnd), newLine);
			} else {
				if (!lines.empty() && !Trim(lines.back()).empty()) {
					lines.emplace_back();
				}
				lines.push_back("[" + v.section + "]");
				lines.push_back(newLine);
			}
		}
		std::string out;
		for (const auto& l : lines) {
			out += l;
			out += "\n";
		}
		return out;
	}

	bool ParseVec(const std::string& a_text, Vec3& a_out)
	{
		return ParseVecText(a_text, a_out);
	}

	std::vector<std::string> Config::LoadFromIni(const std::string& a_text)
	{
		const auto            kv = ParseIni(a_text);
		std::set<std::string> used;
		auto                  get = [&](const char* a_key) -> const std::string* {
            const auto it = kv.find(Lower(a_key));
            if (it == kv.end()) {
                return nullptr;
            }
            used.insert(it->first);
            return &it->second;
		};
		auto b = [&](const char* k, bool& v) { if (auto s = get(k)) ParseBool(*s, v); };
		auto f = [&](const char* k, float& v) { if (auto s = get(k)) ParseFloat(*s, v); };
		auto vec = [&](const char* k, Vec3& v) { if (auto s = get(k)) ParseVecText(*s, v); };
		auto i = [&](const char* k, int& v) {
			float tmp = static_cast<float>(v);
			if (auto s = get(k)) {
				ParseFloat(*s, tmp);
				v = static_cast<int>(tmp < 0.0f ? tmp - 0.5f : tmp + 0.5f);
			}
		};

		b("General.bEnabled", enabled);
		b("General.bLeftHandedPick", leftHandedPick);
		b("General.bActivateContainerAfterUnlock", activateContainerAfterUnlock);
		b("General.bCrimeCheck", crimeCheck);
		b("General.bDebugLogging", debugLogging);
		b("General.bFollowLeftHandedMode", followLeftHandedMode);
		b("General.bFullPause", fullPause);

		f("Placement.fLockSideOffset", lockSideOffset);
		f("Placement.fCancelDistance", cancelDistance);
		b("Placement.bLockOnObject", lockOnObject);
		b("Placement.bMoveWhilePicking", moveWhilePicking);

		f("Controls.fToolLength", toolLength);
		f("Controls.fInsertRadius", insertRadius);
		f("Controls.fWrenchInsertRadius", wrenchInsertRadius);
		f("Controls.fPickPointOffset", pickPointOffset);
		f("Controls.fWrenchPointOffset", wrenchPointOffset);
		f("Controls.fFullTurnTwist", fullTurnTwist);
		if (auto s = get("Controls.bAlternativeMode")) {
			// alternative mode = turn the wrist to search, instead of steering in the air
			bool alternative = !leverControls;
			ParseBool(*s, alternative);
			leverControls = !alternative;
		}
		f("Controls.fLeverMinRadius", leverMinRadius);
		f("Controls.fInsertFront", insertFront);
		f("Controls.fMaxDepth", maxDepth);
		f("Controls.fPickEngageMargin", pickEngageMargin);
		f("Controls.fWrenchStartAngle", wrenchStartAngle);
		f("Controls.fWrenchStartTolerance", wrenchStartTolerance);
		f("Controls.fToolMinTilt", toolMinTilt);
		f("Controls.fToolMaxTilt", toolMaxTilt);
		f("Controls.fPickFilterMinCutoff", pickFilterMinCutoff);
		f("Controls.fPickFilterBeta", pickFilterBeta);
		vec("Controls.vHandPointing", handPointing);
		vec("Controls.vHandPalm", handPalm);
		vec("Controls.vHandGripPos", handGripPos);
		vec("Controls.vGripRotation", gripRotation);
		vec("Controls.vPickTipOffset", pickTipOffset);
		vec("Controls.vTensionGripOffset", tensionGripOffset);

		f("Difficulty.fSweetSpotMult", sweetSpotMult);
		f("Difficulty.fPartialZone", partialZone);
		f("Difficulty.fBreakTimeMult", breakTimeMult);
		f("Difficulty.fForceSlack", forceSlack);
		f("Difficulty.fLockTurnSpeed", lockTurnSpeed);
		f("Difficulty.fLockReturnSpeed", lockReturnSpeed);

		f("Haptics.fStrength", hapticStrength);
		f("Haptics.fTickDegrees", hapticTickDegrees);
		f("Haptics.fPulseFarStrength", pulseFarStrength);
		f("Haptics.fPulseNearStrength", pulseNearStrength);
		f("Haptics.fPulseSweetStrength", pulseSweetStrength);
		f("Haptics.fStrainInterval", strainInterval);
		f("Haptics.fStrainStrength", strainStrength);
		f("Haptics.fPulseLength", pulseLength);
		f("Haptics.fPulseSlowInterval", pulseSlowInterval);
		f("Haptics.fPulseNearInterval", pulseNearInterval);
		f("Haptics.fPulseSweetInterval", pulseSweetInterval);
		f("Haptics.fPulseNearDegrees", pulseNearDegrees);

		b("Visuals.bShowMeshes", showMeshes);
		b("Visuals.bShowPickCount", showPickCount);
		b("Visuals.bDebugView", debugView);
		f("Visuals.fLockMeshRadius", lockMeshRadius);
		f("Visuals.fToolMeshLength", toolMeshLength);
		b("Visuals.bShowTargets", showTargets);
		i("Visuals.iAssistRing", assistRing);
		b("Visuals.bLockVariations", lockVariations);
		b("Visuals.bMarkupMode", markupMode);

		// sanity
		holdRadius = std::max({ holdRadius, insertRadius, wrenchInsertRadius });
		holdBack = std::max(holdBack, 0.0f);
		holdDepth = std::max(holdDepth, 0.0f);
		if (fullTurnTwist < 5.0f) {
			fullTurnTwist = 5.0f;
		}

		std::vector<std::string> unknown;
		for (const auto& [key, value] : kv) {
			if (!used.contains(key)) {
				unknown.push_back(key);
			}
		}
		return unknown;
	}
}
