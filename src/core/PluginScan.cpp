#include "PluginScan.h"

#include <cstring>
#include <functional>

namespace vrlp
{
	namespace
	{
		constexpr std::uint32_t kCompressed = 0x00040000;  // record flag: data is zlib-compressed
		constexpr std::size_t   kHeader = 24;              // record and group headers

		std::uint32_t U32(const std::uint8_t* a_p) { return a_p[0] | (a_p[1] << 8) | (a_p[2] << 16) | (static_cast<std::uint32_t>(a_p[3]) << 24); }
		std::uint16_t U16(const std::uint8_t* a_p) { return static_cast<std::uint16_t>(a_p[0] | (a_p[1] << 8)); }
		bool          Is(const std::uint8_t* a_p, const char* a_tag) { return std::memcmp(a_p, a_tag, 4) == 0; }

		// Calls a_fn(type, data, size) for each field of a record's data. A field longer than
		// 65535 bytes is announced by an XXXX field holding its size.
		void Fields(const std::vector<std::uint8_t>& a_data, const std::function<void(const std::uint8_t*, const std::uint8_t*, std::uint32_t)>& a_fn)
		{
			std::size_t   pos = 0;
			std::uint32_t bigSize = 0;
			while (pos + 6 <= a_data.size()) {
				const std::uint8_t* type = &a_data[pos];
				std::uint32_t       size = U16(&a_data[pos + 4]);
				pos += 6;
				if (bigSize) {
					size = bigSize;
					bigSize = 0;
				}
				if (pos + size > a_data.size()) {
					return;
				}
				if (Is(type, "XXXX") && size == 4) {
					bigSize = U32(&a_data[pos]);
				} else {
					a_fn(type, &a_data[pos], size);
				}
				pos += size;
			}
		}

		bool Read(std::istream& a_in, std::uint8_t* a_out, std::size_t a_size)
		{
			a_in.read(reinterpret_cast<char*>(a_out), static_cast<std::streamsize>(a_size));
			return static_cast<std::size_t>(a_in.gcount()) == a_size;
		}
	}

	LockedRefScan ScanLockedReferences(std::istream& a_in)
	{
		LockedRefScan out;
		std::uint8_t  h[kHeader];
		if (!Read(a_in, h, kHeader) || !Is(h, "TES4")) {
			return out;
		}
		std::vector<std::uint8_t> data(U32(h + 4));
		if (!Read(a_in, data.data(), data.size())) {
			return out;
		}
		Fields(data, [&](const std::uint8_t* a_type, const std::uint8_t* a_d, std::uint32_t a_size) {
			if (Is(a_type, "MAST") && a_size > 0) {
				out.masters.emplace_back(reinterpret_cast<const char*>(a_d), strnlen(reinterpret_cast<const char*>(a_d), a_size));
			}
		});
		out.ok = true;

		// Placed references live in the cell groups (interiors under CELL, exteriors under WRLD);
		// every other top group is skipped whole.
		std::vector<std::uint64_t> ends;  // where each open group ends
		std::uint64_t              pos = kHeader + data.size();
		while (true) {
			while (!ends.empty() && pos >= ends.back()) {
				ends.pop_back();
			}
			a_in.seekg(static_cast<std::streamoff>(pos));
			if (!Read(a_in, h, kHeader)) {
				break;
			}
			const std::uint32_t size = U32(h + 4);
			if (Is(h, "GRUP")) {
				if (size < kHeader) {
					break;  // damaged
				}
				const bool top = ends.empty();
				if (top && !Is(h + 8, "CELL") && !Is(h + 8, "WRLD")) {
					pos += size;
					continue;
				}
				ends.push_back(pos + size);
				pos += kHeader;
				continue;
			}
			pos += kHeader + size;
			if (!Is(h, "REFR") || ends.empty()) {
				continue;
			}
			++out.references;
			if (U32(h + 8) & kCompressed) {
				++out.compressedSkipped;
				continue;
			}
			data.resize(size);
			if (!Read(a_in, data.data(), size)) {
				break;
			}
			std::uint32_t base = 0;
			bool          locked = false;
			Fields(data, [&](const std::uint8_t* a_type, const std::uint8_t* a_d, std::uint32_t a_size) {
				if (Is(a_type, "NAME") && a_size >= 4) {
					base = U32(a_d);
				} else if (Is(a_type, "XLOC") && a_size >= 1) {
					// its level: 255 means only its key opens it, it can't be picked
					locked = a_d[0] != 0xFF;
				}
			});
			if (locked && base) {
				++out.bases[base];
			}
		}
		return out;
	}

	std::string FormFile(std::uint32_t a_rawID, const LockedRefScan& a_scan, const std::string& a_self)
	{
		const std::size_t index = a_rawID >> 24;
		return index < a_scan.masters.size() ? a_scan.masters[index] : a_self;
	}
}
