#include "VRInput.h"

namespace vrlp
{
	namespace
	{
		// Layout copied from SKSEVR's PluginAPI.h (kInterface_VR = 0x10).
		constexpr std::uint32_t kInterface_VR = 0x10;

		struct SKSEVRInterface
		{
			std::uint32_t interfaceVersion;
			std::uint32_t openvrSourceVersion;
			std::uint32_t openvrTargetVersion;

			using ControllerStateCallback = void (*)(std::uint32_t, vr::VRControllerState001_t*, std::uint32_t, bool&);
			using WaitGetPosesCallback = bool (*)(vr::TrackedDevicePose_t*, std::uint32_t, vr::TrackedDevicePose_t*, std::uint32_t);

			bool (*IsActionsEnabled)();
			void (*RegisterForControllerState)(SKSE::PluginHandle, std::int32_t, ControllerStateCallback);
			void (*RegisterForPoses)(SKSE::PluginHandle, std::int32_t, WaitGetPosesCallback);
		};

		constexpr std::uint64_t Mask(vr::EVRButtonId a_id) { return 1ull << static_cast<std::uint64_t>(a_id); }

		constexpr std::uint64_t kGripMask = Mask(vr::k_EButton_Grip);
		constexpr std::uint64_t kTriggerMask = Mask(vr::k_EButton_SteamVR_Trigger);
		constexpr std::uint64_t kCancelMask = Mask(vr::k_EButton_ApplicationMenu);
		constexpr std::uint64_t kButtonAMask = Mask(vr::k_EButton_A);
		constexpr std::uint64_t kStickMask = Mask(vr::k_EButton_SteamVR_Touchpad);  // thumbstick / trackpad click
	}

	vr::IVRSystem* VRInput::AcquireSystem()
	{
		if (_system) {
			return _system;
		}
		using GetGenericInterface_t = void* (*)(const char*, vr::EVRInitError*);
		auto module = REX::W32::GetModuleHandleA("openvr_api.dll");
		if (!module) {
			return nullptr;
		}
		auto fn = reinterpret_cast<GetGenericInterface_t>(REX::W32::GetProcAddress(module, "VR_GetGenericInterface"));
		if (!fn) {
			return nullptr;
		}
		vr::EVRInitError err = vr::VRInitError_None;
		auto*            sys = static_cast<vr::IVRSystem*>(fn(vr::IVRSystem_Version, &err));
		if (err != vr::VRInitError_None || !sys) {
			logger::warn("VR_GetGenericInterface({}) failed: {}", vr::IVRSystem_Version, static_cast<int>(err));
			return nullptr;
		}
		_system = sys;
		logger::info("Acquired {}", vr::IVRSystem_Version);
		return _system;
	}

	bool VRInput::Install(const SKSE::LoadInterface* a_skse)
	{
		auto* vrInterface = static_cast<SKSEVRInterface*>(a_skse->QueryInterface(kInterface_VR));
		if (!vrInterface || !vrInterface->RegisterForControllerState) {
			logger::critical("SKSEVR VR interface not available - is this Skyrim VR with SKSEVR 2.0.12+?");
			return false;
		}
		logger::info("SKSEVR interface v{} (openvr {:08X} -> {:08X}), actions enabled: {}",
			vrInterface->interfaceVersion, vrInterface->openvrSourceVersion, vrInterface->openvrTargetVersion,
			vrInterface->IsActionsEnabled ? vrInterface->IsActionsEnabled() : false);
		// Run late so that other plugins (HIGGS, VRIK...) have already seen the raw input
		// before we hide it from the game.
		vrInterface->RegisterForControllerState(a_skse->GetPluginHandle(), 1000, &VRInput::OnControllerState);
		return true;
	}

	vr::ETrackedControllerRole VRInput::RoleForDevice(std::uint32_t a_deviceIndex)
	{
		// Prefer the game's own OpenVR wrapper: it works with whatever runtime/interface
		// version the game negotiated (SteamVR, OpenComposite, ...).
		if (auto* bsvr = RE::BSOpenVR::GetSingleton()) {
			return bsvr->GetControllerRoleForTrackedDeviceIndex(a_deviceIndex);
		}
		if (auto* sys = AcquireSystem()) {
			return sys->GetControllerRoleForTrackedDeviceIndex(a_deviceIndex);
		}
		return vr::TrackedControllerRole_Invalid;
	}

	void VRInput::OnControllerState(std::uint32_t a_deviceIndex, vr::VRControllerState001_t* a_state, std::uint32_t, bool&)
	{
		auto& self = Get();
		if (!a_state) {
			return;
		}

		const auto role = self.RoleForDevice(a_deviceIndex);
		int        hand;
		if (role == vr::TrackedControllerRole_LeftHand) {
			hand = 0;
		} else if (role == vr::TrackedControllerRole_RightHand) {
			hand = 1;
		} else {
			return;
		}

		{
			std::scoped_lock lock(self._lock);
			auto&            snap = self._hands[hand];
			snap.connected = true;
			snap.pressed = a_state->ulButtonPressed;
			snap.trigger = a_state->rAxis[1].x;
			snap.gripAxis = a_state->rAxis[2].x;
			snap.grip = (a_state->ulButtonPressed & kGripMask) != 0 || snap.gripAxis > 0.6f;
			snap.cancel = (a_state->ulButtonPressed & kCancelMask) != 0;
			snap.stickX = a_state->rAxis[0].x;
			snap.stickY = a_state->rAxis[0].y;
			snap.stickClick = (a_state->ulButtonPressed & kStickMask) != 0;
			snap.buttonA = (a_state->ulButtonPressed & kButtonAMask) != 0;

			if (self._blocking.load()) {
				// Remember what is held so it stays hidden after the session ends,
				// until the player lets go (no surprise attack when releasing the grip).
				self._postBlockMask[hand] = a_state->ulButtonPressed;
				self._postBlockAxes[hand] = true;
				a_state->ulButtonPressed = 0;
				a_state->ulButtonTouched = 0;
				const auto stick = a_state->rAxis[0];
				for (auto& axis : a_state->rAxis) {
					axis.x = 0.0f;
					axis.y = 0.0f;
				}
				// walking / turning stays possible unless the player is held in place
				self._stickPassed[hand] = self._passSticks.load();
				if (self._stickPassed[hand]) {
					a_state->rAxis[0] = stick;
				}
			} else if (self._postBlockMask[hand] != 0 || self._postBlockAxes[hand]) {
				const std::uint64_t stillHeld = self._postBlockMask[hand] & a_state->ulButtonPressed;
				self._postBlockMask[hand] = stillHeld;
				a_state->ulButtonPressed &= ~stillHeld;
				a_state->ulButtonTouched &= ~stillHeld;
				if (stillHeld & kTriggerMask) {
					a_state->rAxis[1].x = 0.0f;
				}
				if (stillHeld & kGripMask) {
					a_state->rAxis[2].x = 0.0f;
				}
				// analog inputs: hide until released / centred so the player doesn't lurch or
				// half-pull an attack right after the session
				if (self._postBlockAxes[hand]) {
					auto&      stick = a_state->rAxis[0];
					const bool stickIdle = self._stickPassed[hand] || (std::abs(stick.x) < 0.2f && std::abs(stick.y) < 0.2f);
					const bool triggerIdle = a_state->rAxis[1].x < 0.1f;
					const bool gripIdle = a_state->rAxis[2].x < 0.1f;
					if (stickIdle && triggerIdle && gripIdle) {
						self._postBlockAxes[hand] = false;
					} else {
						if (!self._stickPassed[hand]) {
							stick.x = stick.y = 0.0f;
						}
						a_state->rAxis[1].x = 0.0f;
						a_state->rAxis[2].x = 0.0f;
					}
				}
			}
		}

		if (self._frameCallback) {
			self._frameCallback();
		}
	}

	ButtonSnapshot VRInput::Snapshot(Hand a_hand) const
	{
		std::scoped_lock lock(_lock);
		return _hands[static_cast<int>(a_hand)];
	}

	void VRInput::SetBlocking(bool a_block, bool a_passSticks)
	{
		_passSticks.store(a_passSticks);
		_blocking.store(a_block);
	}

	namespace
	{
		std::int64_t NowMicros()
		{
			return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}
	}

	void VRInput::Haptic(Hand a_hand, float a_strength)
	{
		std::call_once(_hapticThread, [this] { std::thread([this] { HapticLoop(); }).detach(); });
		const int h = static_cast<int>(a_hand);
		if (a_strength <= 0.01f) {
			_hapticUntil[h].store(0);
			return;
		}
		// lasts a little over a frame (90 Hz), renewed every frame while it should go on
		_hapticStrength[h].store(std::clamp(a_strength, 0.0f, 1.0f));
		_hapticUntil[h].store(NowMicros() + 15000);
	}

	void VRInput::HapticLoop()
	{
		for (;;) {
			const std::int64_t now = NowMicros();
			for (int h = 0; h < 2; ++h) {
				if (now < _hapticUntil[h].load()) {
					Pulse(static_cast<Hand>(h), _hapticStrength[h].load());
				}
			}
			std::this_thread::sleep_for(5ms);
		}
	}

	void VRInput::Pulse(Hand a_hand, float a_strength)
	{
		if (auto* bsvr = RE::BSOpenVR::GetSingleton()) {
			// Same call HIGGS uses; strength 0..1 of the maximum pulse.
			bsvr->TriggerHapticPulse(a_hand == Hand::kRight, a_strength);
			return;
		}
		auto* sys = AcquireSystem();
		if (!sys) {
			return;
		}
		const auto role = a_hand == Hand::kLeft ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand;
		const auto device = sys->GetTrackedDeviceIndexForControllerRole(role);
		if (device == vr::k_unTrackedDeviceIndexInvalid) {
			return;
		}
		const auto micros = static_cast<unsigned short>(a_strength * 3999.0f);
		sys->TriggerHapticPulse(device, 0, micros);
	}
}
