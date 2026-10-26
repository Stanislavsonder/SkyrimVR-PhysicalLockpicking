#include "Text.h"

#include "AddressCheck.h"
#include "Translations.h"

namespace vrlp::Text
{
	namespace
	{
		std::map<std::string, std::string> g_text;     // the game's language
		std::map<std::string, std::string> g_english;  // fallback

		std::map<std::string, std::string> Read(const std::string& a_language)
		{
			const std::string path = std::format("Data\\Interface\\Translations\\VRLockpicking_{}.txt", a_language);
			std::ifstream     in(path, std::ios::binary);
			if (!in) {
				return {};
			}
			std::stringstream ss;
			ss << in.rdbuf();
			return ParseTranslations(ss.str());
		}
	}

	void Load()
	{
		std::string language = "ENGLISH";
		auto*       inis = AddressCheck::Has(Ids::kIniSettings) ? RE::INISettingCollection::GetSingleton() : nullptr;
		if (auto* setting = inis ? inis->GetSetting("sLanguage:General") : nullptr) {
			if (const char* s = setting->GetString(); s && *s) {
				language = s;
				std::ranges::transform(language, language.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			}
		}
		g_english = Read("ENGLISH");
		g_text = language == "ENGLISH" ? g_english : Read(language);
		logger::info("Messages: language {} ({} texts, {} in English)", language, g_text.size(), g_english.size());
	}

	std::string Get(const char* a_key, const std::vector<std::string>& a_args)
	{
		for (const auto* table : { &g_text, &g_english }) {
			if (const auto it = table->find(a_key); it != table->end()) {
				return FormatText(it->second, a_args);
			}
		}
		return FormatText(a_key, a_args);  // no translation file at all: at least say which message
	}
}
