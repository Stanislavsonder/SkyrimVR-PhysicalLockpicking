#pragma once

// Reads the *_LID.ini files of the "Lock Variations" mod, so the same lock / lockpick models and
// sounds show up in VR as they would in the flat lockpicking menu. Written from a description of
// the file format, not from that mod's code. Everything that needs the game (forms, locations,
// textures) is left to the caller as predicates over the strings written in the files.
//
// Format, in short:
//   [<model path substring>]  or  [<model path substring>|<location>]
//   Chest = Interface\Lockpicking\MyLock.nif            ; lock model for containers
//   Door|0x1234~Foo.esp,MyDoorEDID = ...               ; for doors, only when an id matches
//   Chest||underwater = ...                             ; only when the player is underwater
//   Lockpick = ...                                      ; lockpick model
//   CylinderSqueakA / CylinderSqueakB / CylinderStop / CylinderTurn / PickMovement /
//   LockpickingUnlock = <sound descriptor EditorID>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace vrlp
{
	struct LockVariationSounds
	{
		std::string squeakA{ "UILockpickingCylinderSqueakA" };
		std::string squeakB{ "UILockpickingCylinderSqueakB" };
		std::string stop{ "UILockpickingCylinderStop" };
		std::string turn{ "UILockpickingCylinderTurn" };
		std::string pickMovement{ "UILockpickingPickMovement" };
		std::string unlock{ "UILockpickingUnlock" };
	};

	class LockVariations
	{
	public:
		// a_fileName is only kept for the log (Choice::lockSource / pickSource). Files should be
		// added in load order: a section that is already known gets the new file's models after
		// its own, and the new file's sounds replace its sounds.
		void AddFile(const std::string& a_text, const std::string& a_fileName);

		struct Query
		{
			std::string modelPath;  // the locked door's / container's base object model, as the game has it
			bool        isDoor{ false };
			bool        underwater{ false };
			// "Does this location string (FormID or EditorID as written) name the reference's
			// location or one of its parents?" Leave empty when the reference has no location:
			// sections with a location then still match, as in Lock Variations.
			std::function<bool(const std::string&)> locationMatches;
			// "Does this condition id (FormID / EditorID of a TextureSet, Door or Container, or a
			// diffuse texture path substring) match the reference?" Empty: no condition matches.
			std::function<bool(const std::string&)> conditionMatches;
		};

		struct Choice
		{
			std::string         lockModel;  // as written in the file (trimmed); empty = keep the vanilla one
			std::string         pickModel;
			LockVariationSounds sounds;     // from the section the lock model came from (else the pick's), else vanilla
			std::string         lockSource;  // "[section] in file", for the log
			std::string         pickSource;

			bool any() const { return !lockModel.empty() || !pickModel.empty(); }
		};

		// Lock and lockpick are chosen independently: the first section in priority order that
		// matches and yields a model of that kind wins. Priority: sections with a model substring
		// before the catch-all empty one; longer (more specific) substrings first, ties in
		// lexicographic order; for the same substring, sections with a location first; otherwise
		// in the order they were first read.
		Choice Choose(const Query& a_query) const;

		std::size_t SectionCount() const { return _sections.size(); }

		// "[section] (file): N chest, N door, N lockpick models", in priority order, for the log.
		std::vector<std::string> Describe() const;

		// Every id written in key conditions and section locations (deduplicated, as written), so
		// the plugin can resolve them to forms once instead of per lock.
		std::vector<std::string> ConditionIds() const;

	private:
		enum class Kind
		{
			kChest,
			kDoor,
			kLockpick,
		};

		struct Model
		{
			Kind                     kind;
			std::string              path;
			std::vector<std::string> ids;
			bool                     underwater{ false };
			bool                     hasFlags{ false };
		};

		struct Section
		{
			std::string         name;       // as written, for the log
			std::string         substring;  // normalised: lower case, backslashes, no "meshes\"
			std::string         location;   // as written; empty = anywhere
			std::string         file;
			std::vector<Model>  models;
			LockVariationSounds sounds;
			std::size_t         order{ 0 };
		};

		Section&             FindOrAdd(const std::string& a_header, const std::string& a_file);
		void                 Sort();
		const Model*         Pick(const Section& a_section, bool a_lockpick, const Query& a_query) const;

		std::vector<Section> _sections;  // kept in priority order
	};
}
