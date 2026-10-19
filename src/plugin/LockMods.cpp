#include "LockMods.h"

#include "AddressCheck.h"
#include "LockProfile.h"

namespace vrlp::LockMods
{
	namespace
	{
		LockVariations g_variations;

		std::string Lower(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		// "0x1234~Plugin.esp" (an id in a plugin), "0x00012345" (a full FormID) or an EditorID.
		RE::TESForm* Resolve(const std::string& a_id)
		{
			if (a_id.empty()) {
				return nullptr;
			}
			try {
				if (const auto tilde = a_id.find('~'); tilde != std::string::npos) {
					auto* data = AddressCheck::Has(Ids::kDataHandler) ? RE::TESDataHandler::GetSingleton() : nullptr;
					const auto local = static_cast<RE::FormID>(std::stoul(a_id.substr(0, tilde), nullptr, 16));
					return data ? data->LookupForm(local, a_id.substr(tilde + 1)) : nullptr;
				}
				if (a_id.size() > 2 && a_id[0] == '0' && (a_id[1] == 'x' || a_id[1] == 'X')) {
					return RE::TESForm::LookupByID(static_cast<RE::FormID>(std::stoul(a_id, nullptr, 16)));
				}
			} catch (const std::exception&) {
				return nullptr;
			}
			// EditorIDs are only known for some forms unless a mod keeps them all (po3's Tweaks)
			return AddressCheck::Has(Ids::kEditorIDs) ? RE::TESForm::LookupByEditorID(a_id) : nullptr;
		}
	}

	void Load()
	{
		g_variations = {};
		std::vector<std::filesystem::path> files;
		std::error_code                    ec;
		for (const auto& entry : std::filesystem::directory_iterator("Data", ec)) {
			const std::string name = Lower(entry.path().filename().string());
			if (entry.is_regular_file(ec) && name.size() > 8 && name.ends_with("_lid.ini")) {
				files.push_back(entry.path());
			}
		}
		std::ranges::sort(files);
		for (const auto& path : files) {
			std::ifstream     in(path);
			std::stringstream ss;
			ss << in.rdbuf();
			g_variations.AddFile(ss.str(), path.filename().string());
		}
		if (!files.empty()) {
			logger::info("Lock Variations: {} file(s), {} lock type(s)", files.size(), g_variations.SectionCount());
			static std::size_t logged = 0;  // the list once (settings load at every journal close)
			if (logged != g_variations.SectionCount()) {
				for (const auto& line : g_variations.Describe()) {
					logger::info("    {}", line);
				}
				logged = g_variations.SectionCount();
			}
		}
	}

	Result For(RE::TESObjectREFR* a_target)
	{
		Result result;
		auto*  base = a_target ? a_target->GetBaseObject() : nullptr;
		auto*  model = base ? base->As<RE::TESModel>() : nullptr;
		if (!model || g_variations.SectionCount() == 0) {
			return result;
		}

		LockVariations::Query q;
		q.modelPath = model->GetModel() ? model->GetModel() : "";
		q.isDoor = base->Is(RE::FormType::Door);
		if (AddressCheck::Has(Ids::kWaterSystem)) {
			if (auto* water = RE::TESWaterSystem::GetSingleton()) {
				q.underwater = water->playerUnderwater;
			}
		}
		RE::BGSLocation* here = AddressCheck::Has(Ids::kCurrentLocation) ? a_target->GetCurrentLocation() : nullptr;
		if (here) {
			q.locationMatches = [here](const std::string& a_id) {
				auto* wanted = Resolve(a_id);
				for (auto* loc = here; loc && wanted; loc = loc->parentLoc) {
					if (loc == wanted) {
						return true;
					}
				}
				return false;
			};
		}
		// the base object's texture swaps (the diffuse path of each), for texture conditions
		std::vector<std::pair<RE::BGSTextureSet*, std::string>> textures;
		if (auto* swap = base->As<RE::TESModelTextureSwap>(); swap && swap->alternateTextures) {
			for (std::uint32_t i = 0; i < swap->numAlternateTextures; ++i) {
				auto* set = swap->alternateTextures[i].textureSet;
				const char* diffuse = set ? set->textures[0].textureName.c_str() : nullptr;
				textures.emplace_back(set, Lower(diffuse ? diffuse : ""));
			}
		}
		q.conditionMatches = [base, &textures](const std::string& a_id) {
			if (auto* form = Resolve(a_id)) {
				if (form->Is(RE::FormType::TextureSet)) {
					return std::ranges::any_of(textures, [form](const auto& t) { return t.first == form; });
				}
				return form == base;  // a door or container base
			}
			// not a form: a diffuse texture path substring
			const std::string needle = Lower(a_id);
			return std::ranges::any_of(textures, [&needle](const auto& t) { return !needle.empty() && t.second.find(needle) != std::string::npos; });
		};

		const auto choice = g_variations.Choose(q);
		result.lockModel = choice.lockModel;
		result.pickModel = choice.pickModel;
		result.sounds = choice.sounds;
		if (choice.any()) {
			logger::info("Lock Variations for '{}': lock '{}' ({}), pick '{}' ({})", q.modelPath, choice.lockModel, choice.lockSource,
				choice.pickModel, choice.pickSource);
		} else {
			logger::info("Lock Variations: no section for '{}' ({}), vanilla lock", q.modelPath, q.isDoor ? "door" : "container");
		}
		return result;
	}
}
