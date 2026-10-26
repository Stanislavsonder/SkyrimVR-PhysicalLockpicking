#include "Translations.h"

#include <algorithm>

namespace vrlp
{
	namespace
	{
		void AppendUtf8(std::string& a_out, char32_t a_c)
		{
			if (a_c < 0x80) {
				a_out += static_cast<char>(a_c);
			} else if (a_c < 0x800) {
				a_out += static_cast<char>(0xC0 | (a_c >> 6));
				a_out += static_cast<char>(0x80 | (a_c & 0x3F));
			} else if (a_c < 0x10000) {
				a_out += static_cast<char>(0xE0 | (a_c >> 12));
				a_out += static_cast<char>(0x80 | ((a_c >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_c & 0x3F));
			} else {
				a_out += static_cast<char>(0xF0 | (a_c >> 18));
				a_out += static_cast<char>(0x80 | ((a_c >> 12) & 0x3F));
				a_out += static_cast<char>(0x80 | ((a_c >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_c & 0x3F));
			}
		}

		// UTF-16 LE (after its byte order mark) to UTF-8; an unpaired surrogate becomes U+FFFD.
		std::string Utf16ToUtf8(const std::string& a_bytes, std::size_t a_start)
		{
			std::string out;
			auto        unit = [&a_bytes](std::size_t i) {
                return static_cast<char16_t>(static_cast<unsigned char>(a_bytes[i]) | (static_cast<unsigned char>(a_bytes[i + 1]) << 8));
			};
			for (std::size_t i = a_start; i + 1 < a_bytes.size(); i += 2) {
				const char16_t u = unit(i);
				if (u >= 0xD800 && u < 0xDC00 && i + 3 < a_bytes.size()) {
					const char16_t low = unit(i + 2);
					if (low >= 0xDC00 && low < 0xE000) {
						AppendUtf8(out, 0x10000 + ((static_cast<char32_t>(u) - 0xD800) << 10) + (low - 0xDC00));
						i += 2;
						continue;
					}
				}
				AppendUtf8(out, (u >= 0xD800 && u < 0xE000) ? char32_t{ 0xFFFD } : static_cast<char32_t>(u));
			}
			return out;
		}
	}

	std::map<std::string, std::string> ParseTranslations(const std::string& a_bytes)
	{
		std::string text;
		if (a_bytes.size() >= 2 && static_cast<unsigned char>(a_bytes[0]) == 0xFF && static_cast<unsigned char>(a_bytes[1]) == 0xFE) {
			text = Utf16ToUtf8(a_bytes, 2);
		} else if (a_bytes.starts_with("\xEF\xBB\xBF")) {
			text = a_bytes.substr(3);
		} else {
			text = a_bytes;
		}

		std::map<std::string, std::string> out;
		std::size_t                        pos = 0;
		while (pos < text.size()) {
			std::size_t end = text.find('\n', pos);
			if (end == std::string::npos) {
				end = text.size();
			}
			std::string line = text.substr(pos, end - pos);
			pos = end + 1;
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			const std::size_t tab = line.find('\t');
			if (line.starts_with('$') && tab != std::string::npos) {
				out[line.substr(0, tab)] = line.substr(tab + 1);
			}
		}
		return out;
	}

	std::string FormatText(std::string_view a_text, const std::vector<std::string>& a_args)
	{
		std::string out;
		std::size_t next = 0;
		std::size_t pos = 0;
		while (pos < a_text.size()) {
			const std::size_t brace = a_text.find("{}", pos);
			if (brace == std::string_view::npos) {
				break;
			}
			out += a_text.substr(pos, brace - pos);
			out += next < a_args.size() ? a_args[next++] : std::string("{}");
			pos = brace + 2;
		}
		out += a_text.substr(std::min(pos, a_text.size()));
		return out;
	}
}
