#include "LockVariations.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <set>

#include "LockProfile.h"

namespace vrlp
{
	namespace
	{
		std::string Trim(const std::string& a_s)
		{
			const auto first = a_s.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) {
				return {};
			}
			const auto last = a_s.find_last_not_of(" \t\r\n");
			return a_s.substr(first, last - first + 1);
		}

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		bool StartsWith(const std::string& a_s, const char* a_prefix)
		{
			return a_s.rfind(a_prefix, 0) == 0;
		}

		std::vector<std::string> Split(const std::string& a_s, char a_sep)
		{
			std::vector<std::string> out;
			std::size_t              start = 0;
			while (true) {
				const auto at = a_s.find(a_sep, start);
				out.push_back(a_s.substr(start, at == std::string::npos ? std::string::npos : at - start));
				if (at == std::string::npos) {
					return out;
				}
				start = at + 1;
			}
		}

		// Comments start a line, or follow whitespace on a value line: a path never has " ;" in
		// it, but a lone ';' or '#' inside a name might.
		std::string StripComment(const std::string& a_line)
		{
			for (std::size_t i = 0; i < a_line.size(); ++i) {
				if ((a_line[i] == ';' || a_line[i] == '#') && (i == 0 || a_line[i - 1] == ' ' || a_line[i - 1] == '\t')) {
					return a_line.substr(0, i);
				}
			}
			return a_line;
		}

		const std::string& DefaultLock()
		{
			static const std::string s = NormalizeModelPath("Interface/Lockpicking/LockPickShiv01.nif");
			return s;
		}

		const std::string& DefaultPick()
		{
			static const std::string s = NormalizeModelPath("Interface/Lockpicking/LockPick01.nif");
			return s;
		}

		// Sound keys are whole names; returns null for anything else.
		std::string* SoundSlot(LockVariationSounds& a_sounds, const std::string& a_lowerName)
		{
			if (a_lowerName == "cylindersqueaka") {
				return &a_sounds.squeakA;
			}
			if (a_lowerName == "cylindersqueakb") {
				return &a_sounds.squeakB;
			}
			if (a_lowerName == "cylinderstop") {
				return &a_sounds.stop;
			}
			if (a_lowerName == "cylinderturn") {
				return &a_sounds.turn;
			}
			if (a_lowerName == "pickmovement") {
				return &a_sounds.pickMovement;
			}
			if (a_lowerName == "lockpickingunlock") {
				return &a_sounds.unlock;
			}
			return nullptr;
		}
	}

	LockVariations::Section& LockVariations::FindOrAdd(const std::string& a_header, const std::string& a_file)
	{
		// the substring is a path and never has a '|', so the first one starts the location
		const auto  bar = a_header.find('|');
		std::string substring = NormalizeModelPath(Trim(a_header.substr(0, bar)));
		std::string location = bar == std::string::npos ? std::string{} : Trim(a_header.substr(bar + 1));

		for (auto& s : _sections) {
			if (s.substring == substring && Lower(s.location) == Lower(location)) {
				if (s.file.find(a_file) == std::string::npos) {
					s.file += ", " + a_file;
				}
				return s;
			}
		}
		Section s;
		s.name = a_header;
		s.substring = std::move(substring);
		s.location = std::move(location);
		s.file = a_file;
		s.order = _sections.size();
		_sections.push_back(std::move(s));
		return _sections.back();
	}

	void LockVariations::AddFile(const std::string& a_text, const std::string& a_fileName)
	{
		Section*    current = nullptr;
		std::size_t pos = 0;
		while (pos <= a_text.size()) {
			auto end = a_text.find('\n', pos);
			if (end == std::string::npos) {
				end = a_text.size();
			}
			std::string line = a_text.substr(pos, end - pos);
			pos = end + 1;
			if (StartsWith(line, "\xEF\xBB\xBF")) {
				line.erase(0, 3);
			}
			line = Trim(StripComment(line));
			if (line.empty()) {
				continue;
			}

			if (line.front() == '[') {
				const auto close = line.rfind(']');
				current = close == std::string::npos ? nullptr : &FindOrAdd(Trim(line.substr(1, close - 1)), a_fileName);
				continue;
			}
			const auto eq = line.find('=');
			if (!current || eq == std::string::npos) {
				continue;
			}
			const std::string key = Trim(line.substr(0, eq));
			const std::string value = Trim(line.substr(eq + 1));
			if (value.empty()) {
				continue;
			}

			const auto        parts = Split(key, '|');
			const std::string name = Lower(Trim(parts[0]));

			// before the model prefixes: "LockpickingUnlock" starts with "Lockpick" but is a sound
			if (std::string* slot = SoundSlot(current->sounds, name)) {
				*slot = value;
				continue;
			}

			Model m;
			if (StartsWith(name, "chest")) {
				m.kind = Kind::kChest;
			} else if (StartsWith(name, "door")) {
				m.kind = Kind::kDoor;
			} else if (StartsWith(name, "lockpick")) {
				m.kind = Kind::kLockpick;
			} else {
				continue;
			}
			m.path = value;
			if (parts.size() > 1) {
				for (const auto& id : Split(parts[1], ',')) {
					if (auto t = Trim(id); !t.empty()) {
						m.ids.push_back(std::move(t));
					}
				}
			}
			if (parts.size() > 2) {
				for (const auto& flag : Split(parts[2], ',')) {
					const auto f = Lower(Trim(flag));
					if (f.empty()) {
						continue;
					}
					m.hasFlags = true;
					m.underwater = m.underwater || f == "underwater";
				}
			}
			// A key with a condition part that is empty ("Chest|") is still unconditional.
			current->models.push_back(std::move(m));
		}
		Sort();
	}

	void LockVariations::Sort()
	{
		std::sort(_sections.begin(), _sections.end(), [](const Section& a, const Section& b) {
			const bool ae = a.substring.empty(), be = b.substring.empty();
			if (ae != be) {
				return be;  // the catch-all goes last
			}
			// Longer first: every substring that is contained in a longer matching one matches
			// too, so the longer one is the more specific choice for the same model.
			if (a.substring.size() != b.substring.size()) {
				return a.substring.size() > b.substring.size();
			}
			if (a.substring != b.substring) {
				return a.substring < b.substring;
			}
			const bool al = !a.location.empty(), bl = !b.location.empty();
			if (al != bl) {
				return al;
			}
			return a.order < b.order;
		});
	}

	const LockVariations::Model* LockVariations::Pick(const Section& a_section, bool a_lockpick, const Query& a_query) const
	{
		const Kind want = a_lockpick ? Kind::kLockpick : (a_query.isDoor ? Kind::kDoor : Kind::kChest);

		// conditional with flags, then conditional, then plain; file order within each
		const auto rank = [](const Model& m) { return m.hasFlags ? 0 : (!m.ids.empty() ? 1 : 2); };
		std::vector<const Model*> candidates;
		for (const auto& m : a_section.models) {
			if (m.kind == want) {
				candidates.push_back(&m);
			}
		}
		std::stable_sort(candidates.begin(), candidates.end(), [&](const Model* a, const Model* b) { return rank(*a) < rank(*b); });

		const std::string& vanilla = a_lockpick ? DefaultPick() : DefaultLock();
		for (const Model* m : candidates) {
			bool valid;
			if (m->underwater) {
				valid = a_query.underwater;  // the flag alone decides; ids are not needed with it
			} else if (!m->ids.empty()) {
				valid = a_query.conditionMatches &&
				        std::any_of(m->ids.begin(), m->ids.end(), [&](const std::string& id) { return a_query.conditionMatches(id); });
			} else {
				valid = true;
			}
			if (valid && NormalizeModelPath(m->path) != vanilla) {
				return m;
			}
		}
		return nullptr;
	}

	LockVariations::Choice LockVariations::Choose(const Query& a_query) const
	{
		Choice            out;
		const std::string model = NormalizeModelPath(a_query.modelPath);
		const Section*    lockSection = nullptr;
		const Section*    pickSection = nullptr;

		for (const auto& s : _sections) {
			if (lockSection && pickSection) {
				break;
			}
			if (!s.substring.empty() && model.find(s.substring) == std::string::npos) {
				continue;
			}
			// no location known: the location condition does not apply
			if (!s.location.empty() && a_query.locationMatches && !a_query.locationMatches(s.location)) {
				continue;
			}
			if (!lockSection) {
				if (const Model* m = Pick(s, false, a_query)) {
					out.lockModel = m->path;
					out.lockSource = "[" + s.name + "] in " + s.file;
					lockSection = &s;
				}
			}
			if (!pickSection) {
				if (const Model* m = Pick(s, true, a_query)) {
					out.pickModel = m->path;
					out.pickSource = "[" + s.name + "] in " + s.file;
					pickSection = &s;
				}
			}
		}

		if (lockSection) {
			out.sounds = lockSection->sounds;
		} else if (pickSection) {
			out.sounds = pickSection->sounds;
		}
		return out;
	}

	std::vector<std::string> LockVariations::Describe() const
	{
		std::vector<std::string> out;
		for (const auto& section : _sections) {
			int chests = 0, doors = 0, picks = 0;
			for (const auto& m : section.models) {
				(m.kind == Kind::kChest ? chests : m.kind == Kind::kDoor ? doors : picks)++;
			}
			out.push_back(std::format("[{}] ({}): {} chest, {} door, {} lockpick model(s)", section.name, section.file, chests, doors, picks));
		}
		return out;
	}

	std::vector<std::string> LockVariations::ConditionIds() const
	{
		std::set<std::string> ids;
		for (const auto& s : _sections) {
			if (!s.location.empty()) {
				ids.insert(s.location);
			}
			for (const auto& m : s.models) {
				ids.insert(m.ids.begin(), m.ids.end());
			}
		}
		return { ids.begin(), ids.end() };
	}
}
