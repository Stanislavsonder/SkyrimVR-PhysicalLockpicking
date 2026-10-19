#pragma once

#include "PCH.h"

namespace vrlp
{
	enum class Hand : int
	{
		kLeft = 0,
		kRight = 1
	};

	struct ButtonSnapshot
	{
		bool          connected{ false };
		std::uint64_t pressed{ 0 };
		float         trigger{ 0.0f };
		float         gripAxis{ 0.0f };
		bool          grip{ false };
		bool          cancel{ false };  // B / Y (k_EButton_ApplicationMenu)
		float         stickX{ 0.0f };   // thumbstick / trackpad
		float         stickY{ 0.0f };
		bool          stickClick{ false };
		bool          buttonA{ false };  // A / X
	};

	// Wraps the SKSEVR controller-state hook (raw OpenVR button states, with the
	// ability to hide them from the game) plus OpenVR haptics.
	class VRInput
	{
	public:
		static VRInput& Get()
		{
			static VRInput instance;
			return instance;
		}

		// Must be called from SKSEPlugin_Load.
		bool Install(const SKSE::LoadInterface* a_skse);

		ButtonSnapshot Snapshot(Hand a_hand) const;

		// While blocking, the game sees no buttons/axes at all, except the thumbsticks with
		// a_passSticks (so the player can still walk and turn).
		void SetBlocking(bool a_block, bool a_passSticks = false);
		bool IsBlocking() const { return _blocking.load(); }

		// Strength 0..1 for this frame (call at most once per frame per hand).
		void Haptic(Hand a_hand, float a_strength);

		// Called once per controller per frame by the hook; used to drive per-frame work.
		void SetFrameCallback(void (*a_callback)()) { _frameCallback = a_callback; }

		vr::IVRSystem* System() const { return _system; }

	private:
		static void OnControllerState(std::uint32_t a_deviceIndex, vr::VRControllerState001_t* a_state, std::uint32_t a_size, bool& a_keep);

		vr::IVRSystem*             AcquireSystem();
		vr::ETrackedControllerRole RoleForDevice(std::uint32_t a_deviceIndex);

		mutable std::mutex _lock;
		ButtonSnapshot     _hands[2];
		std::uint64_t      _postBlockMask[2]{ 0, 0 };
		bool               _postBlockAxes[2]{ false, false };
		std::atomic<bool>  _blocking{ false };
		std::atomic<bool>  _passSticks{ false };
		bool               _stickPassed[2]{ false, false };
		vr::IVRSystem*     _system{ nullptr };
		void (*_frameCallback)(){ nullptr };
		// Haptics: Haptic() sets what each hand should feel for the next ~frame; a worker thread
		// sends a pulse every 5 ms (the most OpenVR's legacy pulses allow) while it lasts, so the
		// feel doesn't depend on the frame rate.
		void HapticLoop();
		void Pulse(Hand a_hand, float a_strength);

		std::atomic<float>        _hapticStrength[2]{ 0.0f, 0.0f };
		std::atomic<std::int64_t> _hapticUntil[2]{ 0, 0 };  // steady clock, microseconds
		std::once_flag            _hapticThread;
	};
}
