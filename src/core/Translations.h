#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace vrlp
{
	// The game's translation files (Interface\Translations\<plugin>_<LANGUAGE>.txt): one
	// "$KEY<tab>text" per line, UTF-16 LE with a byte order mark (UTF-8 is read too). The settings
	// menu shows them through the game; the plugin's own messages are looked up here. Texts come
	// back as UTF-8.
	std::map<std::string, std::string> ParseTranslations(const std::string& a_bytes);

	// "{}" in a text replaced by the arguments in order (left as "{}" when they run out).
	std::string FormatText(std::string_view a_text, const std::vector<std::string>& a_args);
}
