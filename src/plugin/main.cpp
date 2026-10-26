#include "PCH.h"

#include "AddressCheck.h"
#include "FrameHook.h"
#include "LockpickManager.h"
#include "DebugCheats.h"
#include "TestLocks.h"
#include "Text.h"
#include "VRInput.h"
#include "Version.h"

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			vrlp::Text::Load();
			vrlp::LockpickManager::Get().RegisterEvents();
			// after every plugin has loaded (and hooked), so it can chain after HIGGS
			vrlp::FrameHook::Install(&vrlp::LockpickManager::OnPostWandUpdate);
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			vrlp::LockpickManager::Get().Abort("game loading");
			vrlp::TestLocks::Forget();
			break;
		default:
			break;
		}
	}
}

SKSEPluginInfo(
	.Version = REL::Version{ VRLP_VERSION_MAJOR, VRLP_VERSION_MINOR, VRLP_VERSION_PATCH, 0 },
	.Name = "VRLockpicking"sv,
	.Author = "VRLockpicking contributors"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::InitInfo info{};
	info.log = true;
	info.logName = "VRLockpicking";
	info.logPattern = "[%H:%M:%S.%e] [%l] %v";
	info.trampoline = true;
	info.trampolineSize = 256;
	SKSE::Init(a_skse, info);
	spdlog::flush_on(spdlog::level::info);
	logger::info("VRLockpicking {}.{}.{} loading", VRLP_VERSION_MAJOR, VRLP_VERSION_MINOR, VRLP_VERSION_PATCH);

	if (!REL::Module::IsVR()) {
		logger::critical("This plugin only supports Skyrim VR");
		return false;
	}

	vrlp::AddressCheck::Load();
	vrlp::LockpickManager::Get().LoadConfig();

	if (!vrlp::VRInput::Get().Install(a_skse)) {
		return false;
	}
	vrlp::VRInput::Get().SetFrameCallback(&vrlp::LockpickManager::OnFrame);

	vrlp::TestLocks::RegisterPapyrus();
	vrlp::DebugCheats::RegisterPapyrus();

	if (auto* messaging = SKSE::GetMessagingInterface()) {
		messaging->RegisterListener(OnMessage);
	}

	logger::info("VRLockpicking loaded");
	return true;
}
