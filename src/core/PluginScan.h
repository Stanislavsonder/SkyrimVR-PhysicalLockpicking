#pragma once

// Reads a plugin file (.esm / .esp / .esl, Skyrim SE / VR format) for the placed references
// that have a lock that can be picked (not "requires a key"), and the base objects they place.
// Only those doors and containers are ever picked in the game, so only those get the lock on
// the object itself, and only those are offered as test locks.

#include <cstdint>
#include <istream>
#include <map>
#include <string>
#include <vector>

namespace vrlp
{
	struct LockedRefScan
	{
		std::vector<std::string>        masters;   // the file's masters, in order
		std::map<std::uint32_t, int>    bases;     // base form ID as the file writes it -> its locked references
		int                             references{ 0 };         // all placed references read
		int                             compressedSkipped{ 0 };  // references stored compressed (not read)
		bool                            ok{ false };             // a plugin file
	};

	LockedRefScan ScanLockedReferences(std::istream& a_in);

	// The file a form ID of the scan belongs to: one of its masters (by the ID's top byte), else
	// the file itself (a_self).
	std::string FormFile(std::uint32_t a_rawID, const LockedRefScan& a_scan, const std::string& a_self);
}
