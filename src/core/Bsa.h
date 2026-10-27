#pragma once

// Reads files out of the game's archives (.bsa, Skyrim SE / VR format, version 105; the older
// version 104 only when its files aren't compressed). Used by tools/lockplacer to take door and
// chest models straight from the game's Data folder, with nothing to extract first.

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vrlp
{
	class BsaArchive
	{
	public:
		// False when the file isn't an archive this reads.
		bool Open(const std::string& a_path);

		// A file by its path inside the archive ("meshes\clutter\chest01.nif"), whatever its case
		// and slashes; nothing when it isn't there or can't be read.
		std::optional<std::vector<std::uint8_t>> Read(const std::string& a_path);

		bool        Contains(const std::string& a_path) const;
		std::size_t Size() const { return _files.size(); }

	private:
		struct Entry
		{
			std::uint64_t offset{ 0 };
			std::uint32_t size{ 0 };
			bool          compressed{ false };
		};

		std::ifstream                          _in;
		std::uint32_t                          _version{ 0 };
		bool                                   _embedNames{ false };
		std::unordered_map<std::string, Entry> _files;
	};

	// Decodes an LZ4 frame (what version 105 archives compress with). Nothing when it's broken.
	std::optional<std::vector<std::uint8_t>> DecodeLz4Frame(const std::uint8_t* a_data, std::size_t a_size);

	// The key BsaArchive files go by: lower case, backslashes, no leading slash.
	std::string BsaKey(const std::string& a_path);
}
