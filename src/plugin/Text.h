#pragma once

#include "PCH.h"

namespace vrlp
{
	// The plugin's own messages in the game's language. They live in the same translation files
	// as the settings menu (Interface\Translations\VRLockpicking_<LANGUAGE>.txt); a language
	// without its own file, or a key missing from it, falls back to English.
	namespace Text
	{
		// Reads the game's language (sLanguage) and its file; after the game has loaded its data.
		void Load();

		// The text for a "$VRLP_..." key, with "{}" replaced by the arguments in order.
		std::string Get(const char* a_key, const std::vector<std::string>& a_args = {});

		template <class... Args>
		std::string Format(const char* a_key, const Args&... a_args)
		{
			return Get(a_key, { std::format("{}", a_args)... });
		}

		// Get() shown as a HUD message.
		template <class... Args>
		void Show(const char* a_key, const Args&... a_args)
		{
			RE::SendHUDMessage::ShowHUDMessage(Format(a_key, a_args...).c_str());
		}
	}
}
