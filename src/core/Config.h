#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "VecMath.h"

namespace vrlp
{
	// All tunables. Distances are Skyrim units (1 unit ~= 1.43 cm), angles are degrees.
	struct Config
	{
		// [General]
		bool enabled{ true };
		bool leftHandedPick{ false };            // pick in left hand, tension tool in right
		bool activateContainerAfterUnlock{ true };
		bool crimeCheck{ true };
		bool debugLogging{ false };
		bool followLeftHandedMode{ true };       // swap wand nodes when Skyrim VR's bLeftHandedMode is on
		bool fullPause{ true };                  // pause the game the way menus do (off: the game goes on)

		// [Placement] - where the virtual lock appears, relative to the headset
		float lockDistance{ 28.0f };       // forward from HMD (horizontal; not in the INI)
		float lockHeightOffset{ -25.0f };  // vertical offset from HMD (not in the INI)
		float lockSideOffset{ 0.0f };      // + = to the right
		float cancelDistance{ 90.0f };     // walk/lean this far from the lock to give up

		// [Controls]
		float toolLength{ 15.0f };           // pick / wrench length from the fist to the tip
		float insertRadius{ 3.0f };          // a pick tip goes in when it comes this close to its spot...
		float wrenchInsertRadius{ 3.0f };    // ...a wrench tip this close...
		float holdRadius{ 12.0f };           // ...and either stays in until it strays further than this sideways (hand jitter; not in the INI)...
		float holdBack{ 4.0f };              // ...or this much further back out than it went in...
		float holdDepth{ 8.0f };             // ...or this much deeper (not in the INI)
		float pickPointOffset{ 0.0f };       // where the pick goes in: this far above the keyhole centre...
		float wrenchPointOffset{ 0.0f };     // ...and the wrench this far below it (0 = both into the centre)
		float fullTurnTwist{ 90.0f };        // turn of the wrench (deg) that equals "full" turn
		// Lever controls (like the vanilla minigame): once a tool is in the keyhole its tip stays
		// there and it pivots around it. The pick's angle is where its handle points (left, up,
		// right); the wrench turns the lock when its handle is swung clockwise around the keyhole.
		// Off (the default, "alternative mode" in the menu): the pick angle is the roll of the wrist
		// and the wrench is twisted like a key.
		bool  leverControls{ false };
		float leverMinRadius{ 1.5f };        // handle closer than this to the keyhole axis: angle holds
		float insertFront{ 0.0f };           // a tip counts as in from this far in front of the keyhole (0 = the keyhole plane)...
		float maxDepth{ 3.0f };              // ...and down to this depth; deeper = pushed through, no control
		float pickEngageMargin{ 15.0f };     // the pick goes in with its handle on the right half (+- this)
		float wrenchStartAngle{ -90.0f };    // lever: back of the wrench hand to the left = start (deg, 0 = up)
		float wrenchStartTolerance{ 25.0f }; // the wrench only goes in this close to its start position
		float toolMinTilt{ 10.0f };          // shown tilt of an inserted tool out of the lock face (deg):
		float toolMaxTilt{ 90.0f };          // within these, the handle points at the hand
		float pickFilterMinCutoff{ 1.0f };   // pick angle smoothing (1-euro filter): Hz when the hand is still (0 = off)...
		float pickFilterBeta{ 0.05f };       // ...and how fast the cutoff rises with the angle's speed (per deg/s)
		// Tools are held in the visible hand: shaft along the fingers, out of the palm.
		// Hand-node space of the right hand (left mirrors X), values from HIGGS. handGrip is set
		// by the plugin: off when there are no hand nodes (the controllers then hold the tools).
		bool  handGrip{ true };
		Vec3  handPointing{ 0.0f, 0.0f, 1.0f };
		Vec3  handPalm{ -0.018f, -0.965f, 0.261f };
		Vec3  handGripPos{ 0.0f, -2.4f, 6.0f };
		Vec3  gripRotation{ 0.0f, 0.0f, 0.0f };  // Euler XYZ (deg) of the tool in the fist (pitch, roll, yaw)
		// Set by the plugin (not the INI): the skeleton hands are swapped (Skyrim VR left-handed mode),
		// so the pick sits in the skeleton's left hand when the player holds it in the right.
		bool  swapGripMirror{ false };
		Vec3  pickTipOffset{ 0.0f, 0.0f, 0.0f };      // controller-local position of the pick tip
		Vec3  tensionGripOffset{ 0.0f, 0.0f, 0.0f };  // controller-local grab point

		// [Difficulty]
		float sweetSpotMult{ 1.0f };    // scales the vanilla-derived sweet spot
		float partialZone{ 30.0f };     // extra degrees beyond sweet spot where the lock turns partially
		float breakTimeMult{ 1.0f };    // scales vanilla pick break time
		float forceSlack{ 0.10f };      // how much you may over-twist before the pick takes damage
		float lockTurnSpeed{ 1.6f };    // 0..1 lock rotation per second
		float lockReturnSpeed{ 3.0f };

		// [Haptics]
		float hapticStrength{ 1.0f };
		float hapticTickDegrees{ 0.0f };   // optional click every N degrees of pick movement
		float pulseLength{ 0.025f };       // seconds each pulse lasts
		// Searching pulses (once both tools are in): far from the sweet spot one interval and
		// strength; a sharp step to the "near" ones within pulseNearDegrees of its edge; from there
		// a smooth change to the "sweet" ones at its edge; those inside it.
		float pulseSlowInterval{ 0.50f };   // seconds between pulses far from the sweet spot
		float pulseFarStrength{ 0.45f };
		float pulseNearInterval{ 0.40f };
		float pulseNearStrength{ 0.45f };
		float pulseSweetInterval{ 0.30f };
		float pulseSweetStrength{ 0.45f };
		float pulseNearDegrees{ 10.0f };    // how close (deg, beyond the sweet spot's edge) counts as near
		// Forcing a pick that is about to break: a steady rattle that replaces every other pulse.
		float strainInterval{ 0.08f };
		float strainStrength{ 0.9f };

		// [Visuals]
		bool        showMeshes{ true };
		// set by the plugin (Lock Variations, the Skeleton Key), not the INI:
		std::string lockMesh{ "Interface\\Lockpicking\\LockPickShiv01.nif" };  // the lock (and its screwdriver)
		std::string pickMesh;                 // the pick; empty = the lockpick item's model
		bool        debugView{ false };       // solid-colour shapes and gauges instead of the models
		bool        showTargets{ false };     // keyhole target circle, insertion spots and gauges over the models
		bool        lockVariations{ true };   // use Lock Variations' *_LID.ini models and sounds
		int         assistRing{ 0 };          // ring on the lock face: 0 off, 1 the pick's angle, 2 also the sweet spot on easy locks
		bool        markupMode{ false };      // model markup tool: line up the lock's keyhole and the tools in the hands
		float       visualScale{ 1.1f };      // scales the lock model, the tool models and the keyhole spots (not in the INI)
		float       lockMeshRadius{ 8.0f };   // lock face half-size (bound radius without auto-align)
		float       toolMeshLength{ 16.0f };  // tool model length (tip to end of handle)
		bool        flipPickMesh{ false };    // the pick model's tip is the other end from the one the fit guesses

		// Parses INI text; malformed values keep their current value. Returns the keys it did not
		// recognise ("section.key", lower case).
		std::vector<std::string> LoadFromIni(const std::string& a_text);

		bool operator==(const Config&) const = default;
	};

	// Minimal INI reader: [section] / key = value / ; or # comments. Keys are case-insensitive
	// and stored as "section.key".
	std::map<std::string, std::string> ParseIni(const std::string& a_text);

	// The settings an MCM Helper config.json offers ("id": "bEnabled:General" -> "general.benabled").
	std::set<std::string> McmSettingKeys(const std::string& a_configJson);

	// "x, y, z" (commas, semicolons or spaces); false (a_out unchanged) if malformed.
	bool ParseVec(const std::string& a_text, Vec3& a_out);

	// INI text with only the given keys ("section.key", lower case); the others are left out (and
	// listed in a_dropped). Comments and section lines stay.
	std::string KeepIniKeys(const std::string& a_text, const std::set<std::string>& a_keys, std::vector<std::string>* a_dropped = nullptr);

	struct IniValue
	{
		std::string section;
		std::string key;
		std::string value;
	};

	// Sets keys in INI text, keeping everything else (comments, other keys, order). Existing keys
	// are replaced in place (case-insensitive), new ones appended to their section (created at
	// the end if missing).
	std::string SetIniValues(const std::string& a_text, const std::vector<IniValue>& a_values);
}
