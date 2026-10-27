#pragma once

#include "PCH.h"

#include "Config.h"
#include "LockSession.h"
#include "VRInput.h"
#include "AssistRing.h"
#include "PickCounter.h"
#include "DebugView.h"
#include "LockProfile.h"
#include "LockVariations.h"
#include "Markup.h"
#include "Numpad.h"
#include "Visuals.h"

namespace vrlp
{
	class LockpickManager : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static LockpickManager& Get()
		{
			static LockpickManager instance;
			return instance;
		}

		void LoadConfig();
		void RegisterEvents();
		// Forgets every model markup (the lock keyholes and tool grips the player saved): the
		// built-in alignments and the automatic fit are used again. Main thread.
		void ResetMarkup();
		const Config& GetConfig() const { return _cfg; }

		// Called (from any thread) once per controller poll; schedules the main-thread update.
		static void OnFrame();
		static void OnPostWandUpdate();  // FrameHook: right after the hands moved this frame
		void        RefreshVisuals();

		// Ends any running session without side effects (load game, etc.).
		void Abort(const char* a_reason);

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

	private:
		void Start(RE::TESObjectREFRPtr a_target);
		void Update();
		void End();

		void HandleEvent(SessionEvent a_event, RE::TESObjectREFR* a_target);
		void OnUnlocked(RE::TESObjectREFR* a_target);
		bool OnPickBroken(RE::TESObjectREFR* a_target);  // returns false if the session must end
		bool HandleCrime(RE::TESObjectREFR* a_target);   // returns true if caught
		RE::NiNode* HandNodeFor(RE::VR_NODE_DATA* a_nodes, Hand a_physicalHand) const;
		void PauseWorld(bool a_pause);
		bool OtherPause() const;
		void HideControllers(RE::VR_NODE_DATA* a_nodes, bool a_hide);
		void HideHands(RE::VR_NODE_DATA* a_nodes, bool a_hide);
		void CheckHandsWhilePaused(RE::VR_NODE_DATA* a_nodes, float a_dt);
		void MuteGameRumble(bool a_mute);
		void LockControls(bool a_lock);
		void RecordGripSample(RE::VR_NODE_DATA* a_nodes);
		void UpdateMarkup(const FrameInput& a_in, float a_dt);
		void UpdateAlignment(bool a_wrench, const Pose* a_tool, const ButtonSnapshot& a_left, const ButtonSnapshot& a_right, bool a_reset,
			const Numpad::Input& a_pad, float a_dt);
		void UpdateObjectPlacement(const HandInput& a_leftHand, const ButtonSnapshot& a_left, bool a_reset, const Numpad::Input& a_pad, float a_dt);
		void NudgeKeyhole(const Numpad::Input& a_pad, float a_dt);
		bool LockOnObject(RE::TESObjectREFR* a_target, const Vec3& a_head);
		Pose ObjectLockWorld() const;
		ObjectAnchor ObjectSideShown() const;  // the door's side you stand on, as a lock on the object
		void SaveObjectPlacement();
		void EndGrab(bool a_save);
		void SaveAlignment();
		void SaveLockProfile(const LockProfile& a_profile, const std::string& a_modelPath);
		bool SaveProfilesText(const std::string& a_text);

		Config                     _cfg;
		std::optional<LockSession> _session;
		Visuals                    _visuals;
		DebugView                  _debug;
		AssistRing                 _assist;
		PickCounter                _counter;
		LockVariationSounds        _sounds;  // this lock's (Lock Variations), else vanilla
		Markup                     _markup;
		std::map<std::string, LockProfile> _profiles;  // measured keyholes, by lock model path
		std::map<std::string, ToolProfile> _pickProfiles;  // measured tools, by model path
		std::map<std::string, ToolProfile> _shivProfiles;
		std::map<std::string, ObjectAnchor> _objectAnchors;  // where the lock sits, by door / chest model path
		// bLockOnObject: the door or chest the lock sits on this session
		struct OnObject
		{
			std::string  model;
			ObjectAnchor anchor;  // as saved (a door: the side it was marked on)
			Vec3         boundMin, boundMax;
			Pose         object;  // its world pose (it doesn't move while you pick)
			float        scale{ 1.0f };
			bool         door{ false };
			bool         otherSide{ false };  // you stand on the door's other side
		};
		std::optional<OnObject>    _onObject;
		struct LockGrab
		{
			Pose hand;  // room space, when the trigger went down
			Pose lock;
		};
		std::optional<LockGrab>    _lockGrab;  // markup: the lock carried with the left hand
		bool                       _objectDirty{ false };
		float                      _objectIdle{ 0.0f };
		bool                       _keyholeDirty{ false };
		float                      _keyholeIdle{ 0.0f };
		float                      _cancelDistance{ 90.0f };
		bool                       _moving{ false };  // the player may walk while picking (lock on the object)
		bool                       _nextHeld{ true };
		bool                       _resetHeld{ true };
		bool                       _alignDirty{ false };
		struct Grab
		{
			Pose        tool;     // tool frame (room space) when the trigger went down
			ToolProfile profile;  // the alignment then
		};
		std::optional<Grab>        _grab;  // markup: the tool model held still while the hand moves
		float                      _alignIdle{ 0.0f };
		bool                       _markHeld{ true };
		RE::TESObjectREFRPtr       _target;
		LockLevel                  _level{ LockLevel::kNovice };
		std::atomic<bool>          _active{ false };
		std::atomic<bool>          _updatePending{ false };
		float                      _shake{ 0.0f };  // lock shake shown this frame (deg)
		std::chrono::steady_clock::time_point _lastUpdate{};
		std::mt19937               _rng{ std::random_device{}() };
		float                      _debugTimer{ 0.0f };
		float                      _shakeTime{ 0.0f };
		bool                       _useHandNodes{ false };  // track the visible hands, not the controllers
		bool                       _controlsLocked{ false };
		bool                       _gripTriggerHeld{ true };
		int                        _gripSamples{ 0 };
		std::uint32_t              _lockedControls{ 0 };
		Pose                       _anchor;                 // room space -> world
		bool                       _rumbleMuted{ false };
		bool                       _fullPaused{ false };
		bool                       _controllersHidden{ false };
		bool                       _handsHidden{ false };
		Vec3                       _keyholeWorld;  // where the lock was put, in the world
		struct HandCheck
		{
			float timer{ 0.0f };
			Vec3  hand, wand;
			bool  valid{ false };
			bool  reported{ false };
		} _handCheck;
		float                      _savedRumble{ 0.0f };
		Config                     _iniCfg;                 // main INI only (what "reset" goes back to)
	};
}
