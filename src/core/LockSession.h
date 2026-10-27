#pragma once

// Game-independent physical lockpicking simulation. The SKSE layer feeds it hand poses
// and button states every frame and reacts to the events it emits. Everything here is
// plain C++ so it can be unit tested off-Windows.

#include <cstdint>
#include <vector>

#include "Config.h"
#include "VecMath.h"

namespace vrlp
{
	enum class LockLevel : int
	{
		kNovice = 0,
		kApprentice = 1,
		kAdept = 2,
		kExpert = 3,
		kMaster = 4
	};

	// Inputs gathered from the game when the session starts.
	struct LockParams
	{
		float sweetCenter{ 0.0f };     // degrees, -90 (left) .. +90 (right), 0 = straight up
		float sweetHalfWidth{ 10.0f }; // degrees
		float partialZone{ 30.0f };    // degrees beyond the sweet spot with partial rotation
		float breakSeconds{ 1.0f };    // seconds of forcing until the pick snaps
		bool  unbreakable{ false };
		// Locksmith (the perk's starting arc, degrees; 0 = none): where the pick first goes in, the
		// sweet spot is moved to within this of it, as the vanilla menu starts the pick close to it.
		float startArc{ 0.0f };
		float startRandom01{ 0.5f };   // where in that arc (0 .. 1)
	};

	// Where a tool tip is relative to its spot in the keyhole.
	enum class ToolPlace
	{
		kOut,      // not in the target (outside the circle or in front of the keyhole)
		kIn,       // in the keyhole
		kTooDeep   // pushed through: no control, no feedback
	};

	struct HandInput
	{
		bool  valid{ false };
		Pose  pose;
		float trigger{ 0.0f };  // analog 0..1
		bool  grip{ false };
		bool  cancel{ false };  // B / Y (application menu)
	};

	struct FrameInput
	{
		float     dt{ 0.0f };
		Pose      hmd;
		HandInput pickHand;
		HandInput tensionHand;
	};

	enum class SessionEvent
	{
		kPickInserted,
		kPickRemoved,
		kTensionGrabbed,
		kTensionReleased,
		kPickTick,        // pick moved a few degrees inside the lock
		kLockStartTurn,
		kLockStop,        // cylinder hit its limit for this pick angle
		kForcing,         // pick under strain (throttled)
		kPickBroken,
		kUnlocked,
		kCancelled
	};

	struct HapticOutput
	{
		float pickHand{ 0.0f };     // 0..1 strength for this frame
		float tensionHand{ 0.0f };
	};

	// Frame-of-reference of the virtual lock. forward points from the player into the lock.
	struct LockFrame
	{
		Vec3 keyhole;
		Vec3 forward{ 0, 1, 0 };
		Vec3 up{ 0, 0, 1 };
		Vec3 right{ 1, 0, 0 };

		Mat3 Basis() const { return Mat3::FromColumns(right, forward, up); }

		// Where each tool's tip sits when inserted: pick at the top of the keyhole, tension
		// wrench at the bottom (like a real lock).
		// The offsets grow with the model (fVisualScale) so the spots stay on the visible keyhole.
		Vec3 PickPoint(const Config& a_cfg) const { return keyhole + up * (a_cfg.pickPointOffset * a_cfg.visualScale); }
		Vec3 WrenchPoint(const Config& a_cfg) const { return keyhole - up * (a_cfg.wrenchPointOffset * a_cfg.visualScale); }
	};

	// Builds the lock frame in front of the headset (horizontal facing, world up).
	LockFrame MakeLockFrame(const Pose& a_hmd, const Config& a_cfg);

	// Vanilla-like numbers derived from game settings. Pass the raw GMST values.
	struct VanillaTuning
	{
		float sweetSpotDeg[5]{ 35.0f, 17.0f, 8.0f, 5.0f, 3.0f };   // fSweetSpot<Level>
		float breakSeconds[5]{ 2.0f, 1.0f, 0.75f, 0.5f, 0.25f };   // fLockpickBreak<Level>
		float sweetSkillBase{ 0.82f };                             // fLockpickSkillSweetSpotBase
		float sweetSkillMult{ 0.012f };                            // fLockpickSkillSweetSpotMult
		float breakSkillMult{ 0.005f };                            // fLockpickBreakSkillMult
	};

	// a_perkSweetSpotMult: result of the ModLockpickSweetSpot perk entry point (1 = no perk).
	// a_random01: uniform random number used to place the sweet spot.
	LockParams ComputeLockParams(LockLevel a_level, float a_skill, float a_perkSweetSpotMult, bool a_unbreakable,
		const VanillaTuning& a_tuning, const Config& a_cfg, float a_random01);

	class LockSession
	{
	public:
		// a_pickHandRef / a_wrenchHandRef: orientation of each hand when lockpicking started. A tool
		// held in that hand points straight into the lock at that moment and from then on rotates
		// 1:1 with the hand, so no controller axis convention is assumed anywhere.
		LockSession(const LockParams& a_params, const LockFrame& a_frame, const Config& a_cfg,
			const Mat3& a_pickHandRef, const Mat3& a_wrenchHandRef);

		// Applies changed tunables (in-game editor). How the tools are attached (handGrip,
		// swapGripMirror) stays as it was when the session started.
		void Retune(const Config& a_cfg);
		[[nodiscard]] const Config& GetConfig() const { return _cfg; }

		// Advances the simulation. Returns events emitted this frame (in order).
		const std::vector<SessionEvent>& Update(const FrameInput& a_in);

		[[nodiscard]] bool  Finished() const { return _finished; }
		[[nodiscard]] bool  PickInserted() const { return _pickInserted; }
		[[nodiscard]] bool  TensionEngaged() const { return _tensionEngaged; }
		[[nodiscard]] bool  WrenchInserted() const { return _tensionEngaged; }
		[[nodiscard]] float PickAngle() const { return _pickAngle; }    // degrees
		[[nodiscard]] float LockAngle() const { return _lockAngle; }    // 0..1 (1 = open)
		[[nodiscard]] float TurnInput() const { return _turnInput; }    // 0..1
		[[nodiscard]] float AllowedTurn() const { return _allowed; }    // 0..1
		[[nodiscard]] float PickDamage() const { return _pickDamage; }  // seconds of forcing so far
		[[nodiscard]] const LockParams&   Params() const { return _params; }
		[[nodiscard]] const LockFrame&    Frame() const { return _frame; }
		// The lock moved (lined up on its door or chest with the markup tool).
		void SetFrame(const LockFrame& a_frame) { _frame = a_frame; }
		[[nodiscard]] const HapticOutput& Haptics() const { return _haptics; }
		// Pose of the tool tip (pos) and tool orientation (rot; tool axis = rot * +Y, pointing
		// into the lock) for a hand holding the pick / the wrench.
		[[nodiscard]] Pose PickTool(const Pose& a_hand) const { return ToolPose(a_hand, _pickRef, _cfg.pickTipOffset, PickRightHand()); }
		[[nodiscard]] Pose WrenchTool(const Pose& a_hand) const { return ToolPose(a_hand, _wrenchRef, _cfg.tensionGripOffset, !PickRightHand()); }
		[[nodiscard]] Vec3 PickTip(const Pose& a_hand) const { return PickTool(a_hand).pos; }

		// Where each tool should be drawn: held in the hand, or (inserted) with its tip in its keyhole
		// spot and its handle pointing at the hand that holds it.
		[[nodiscard]] Pose PickVisual(const Pose& a_hand) const;
		[[nodiscard]] Pose WrenchVisual(const Pose& a_hand) const;
		// Tilt (deg) of the handle out of the lock face for a hand, clamped to the configured range.
		[[nodiscard]] float ShownTilt(const Vec3& a_grip, const Vec3& a_spot) const;
		// Angle (deg, 0 = up, + = clockwise as seen by the player) of a point around the keyhole
		// axis through a_spot; false if it is too close to the axis to tell.
		[[nodiscard]] bool OrbitAngle(const Vec3& a_point, const Vec3& a_spot, float& a_deg) const;
		// Pose of a tool pivoting in a_spot with its handle at a_angleDeg, tilted a_tiltDeg out of the face.
		[[nodiscard]] Pose LeverPose(const Vec3& a_spot, float a_angleDeg, float a_tiltDeg) const;
		// Lever controls with the pick in the left hand (bLeftHandedPick) are the mirror image: the
		// pick searches along the left half, the blade starts with the back of the hand to the right
		// and the lock turns anticlockwise. Every angle round the keyhole is measured that way
		// (-1), and the lock's turn is drawn that way.
		[[nodiscard]] float Side() const { return _cfg.leverControls && _cfg.leftHandedPick ? -1.0f : 1.0f; }
		// a_tool turned the least so its axis runs from a_grip into a_spot (never flatter than
		// kMinAimTilt to the lock face), its tip moved to a_spot.
		[[nodiscard]] Pose AimedAt(const Pose& a_tool, const Vec3& a_grip, const Vec3& a_spot) const;
		[[nodiscard]] Vec3 PickGrip(const Pose& a_hand) const { return GripPoint(a_hand, _cfg.pickTipOffset, PickRightHand()); }
		[[nodiscard]] Vec3 WrenchGrip(const Pose& a_hand) const { return GripPoint(a_hand, _cfg.tensionGripOffset, !PickRightHand()); }
		[[nodiscard]] float WrenchStartAngle() const { return _wrenchRefAngle; }
		[[nodiscard]] ToolPlace PickPlace() const { return _pickPlace; }
		[[nodiscard]] ToolPlace WrenchPlace() const { return _wrenchPlace; }
		[[nodiscard]] bool      PickNeedsReinsert() const { return _pickNeedsReinsert; }
		[[nodiscard]] bool      WrenchNeedsReset() const { return _wrenchNeedsReset; }
		[[nodiscard]] ToolPlace Classify(const Vec3& a_tip, const Vec3& a_spot, float a_radius, bool a_inserted) const;
		// Lever controls: where the back of the wrench hand points around the keyhole (deg, 0 = up,
		// 90 = right, -90 = left).
		[[nodiscard]] float BladeAngle(const Pose& a_hand) const;

		// 0..1 closeness of the current pick angle to the sweet spot (1 = inside).
		[[nodiscard]] float Proximity(float a_pickAngle) const;
		// the searching pulse at this pick angle (interval, strength)
		struct Pulse
		{
			float interval{ 0.5f };
			float strength{ 0.0f };
		};
		[[nodiscard]] Pulse SearchPulse(float a_pickAngle) const;

		// Max lock rotation (0..1) for a given pick angle.
		[[nodiscard]] float MaxTurnFor(float a_pickAngle) const;

		// Pick angle (deg) implied by a hand orientation (roll relative to the start pose).
		[[nodiscard]] float MeasurePickAngle(const Mat3& a_handRot) const;

	private:
		void Emit(SessionEvent a_event) { _events.push_back(a_event); }
		void UpdatePick(const FrameInput& a_in);
		void UpdateTension(const FrameInput& a_in);
		void UpdateLock(const FrameInput& a_in);
		Pose ToolPose(const Pose& a_hand, const Mat3& a_ref, const Vec3& a_localOffset, bool a_rightHand) const;
		Vec3 GripPoint(const Pose& a_hand, const Vec3& a_localOffset, bool a_rightHand) const;
		bool PickRightHand() const { return _cfg.leftHandedPick == _cfg.swapGripMirror; }

		LockParams _params;
		LockFrame  _frame;
		Config     _cfg;

		std::vector<SessionEvent> _events;
		HapticOutput              _haptics;

		bool  _finished{ false };
		bool  _pickInserted{ false };
		bool  _pickAngleValid{ false };
		bool  _startPlaced{ false };    // Locksmith: the sweet spot was moved to the first pick
		float _pickAngle{ 0.0f };
		OneEuroFilter _pickFilter;
		float _lastTickAngle{ 0.0f };
		Mat3  _pickRef;
		Mat3  _wrenchRef;
		float _pickPulseTimer{ 0.0f };
		float _wrenchPulseTimer{ 0.0f };
		float _strainPulseTimer{ 0.0f };

		// A short vibration burst (a "pulse") lasting a few frames, so it is felt clearly.
		struct Buzz
		{
			float time{ 0.0f };
			float strength{ 0.0f };
			void  Start(float a_seconds, float a_strength)
			{
				strength = time > 0.0f && strength > a_strength ? strength : a_strength;
				time = time > a_seconds ? time : a_seconds;
			}
			void Force(float a_seconds, float a_strength)  // replaces whatever plays
			{
				strength = a_strength;
				time = a_seconds;
			}
			float Tick(float a_dt)
			{
				if (time <= 0.0f) {
					strength = 0.0f;
					return 0.0f;
				}
				time -= a_dt;
				return strength;
			}
		};
		bool _straining{ false };  // the strain rattle plays: no searching pulses
		Buzz _pickBuzz;
		Buzz _wrenchBuzz;

		bool  _tensionEngaged{ false };
		Mat3  _tensionRefRot;
		float _wrenchRefAngle{ -90.0f };  // lever controls: blade angle the turn is measured from
		ToolPlace _pickPlace{ ToolPlace::kOut };
		bool      _pickNeedsReinsert{ false };  // after a break: the pick must leave the keyhole first
		bool      _wrenchNeedsReset{ false };   // after a break: the blade must go back to its start
		ToolPlace _wrenchPlace{ ToolPlace::kOut };
		float _turnInput{ 0.0f };

		float _lockAngle{ 0.0f };
		float _allowed{ 0.0f };
		bool  _lockMoving{ false };
		bool  _atStop{ false };
		float _pickDamage{ 0.0f };
		float _forceEventTimer{ 0.0f };
		float _breakCooldown{ 0.0f };

		bool _prevCancel{ true };  // require a fresh press (button may still be held from activation)
	};
}
