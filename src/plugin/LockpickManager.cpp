#include "LockpickManager.h"

#include "AddressCheck.h"
#include "FrameHook.h"
#include "LockMods.h"
#include "VRInput.h"

#undef GetObject  // wingdi.h's, over BGSDefaultObjectManager::GetObject

namespace vrlp
{
	namespace
	{
		constexpr RE::FormID kLockpickID = 0x0000000A;
		constexpr const char* kIniPath = "Data\\SKSE\\Plugins\\VRLockpicking.ini";
		constexpr const char* kMcmSettingsPath = "Data\\MCM\\Settings\\VRLockpicking.ini";
		constexpr const char* kMcmConfigPath = "Data\\MCM\\Config\\VRLockpicking\\config.json";
		constexpr const char* kProfilesPath = "Data\\SKSE\\Plugins\\VRLockpicking_Locks.ini";

		std::string ReadText(const char* a_path)
		{
			std::ifstream     in(a_path);
			std::stringstream ss;
			if (in) {
				ss << in.rdbuf();
			}
			return ss.str();
		}

		float GetGMSTFloat(const char* a_name, float a_default)
		{
			if (auto* gmst = RE::GameSettingCollection::GetSingleton()) {
				if (auto* setting = gmst->GetSetting(a_name)) {
					return setting->GetFloat();
				}
			}
			return a_default;
		}

		void ShowGMSTMessage(const char* a_name, const char* a_fallback)
		{
			const char* text = a_fallback;
			if (auto* gmst = RE::GameSettingCollection::GetSingleton()) {
				if (auto* setting = gmst->GetSetting(a_name)) {
					if (auto* s = setting->GetString(); s && *s) {
						text = s;
					}
				}
			}
			RE::SendHUDMessage::ShowHUDMessage(text);
		}

		// Calls a float perk entry point with the number of condition arguments the engine expects.
		float EntryPointFloat(RE::BGSEntryPoint::ENTRY_POINT a_ep, RE::Actor* a_owner, RE::TESObjectREFR* a_ref, float a_value)
		{
			if (!AddressCheck::Has(Ids::kEntryPoints)) {
				return a_value;
			}
			auto* ep = RE::BGSEntryPoint::GetEntryPoint(a_ep);
			if (!ep || !a_owner) {
				return a_value;
			}
			float value = a_value;
			switch (ep->parameters.count) {
			case 1:
				RE::BGSEntryPoint::HandleEntryPoint(a_ep, a_owner, &value);
				break;
			case 2:
				RE::BGSEntryPoint::HandleEntryPoint(a_ep, a_owner, a_ref, &value);
				break;
			default:
				logger::warn("Entry point {} has {} parameters; skipped", static_cast<int>(a_ep), ep->parameters.count);
				break;
			}
			return value;
		}

		Pose ToPose(const RE::NiTransform& a_t)
		{
			Pose p;
			p.pos = { a_t.translate.x, a_t.translate.y, a_t.translate.z };
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					p.rot.m[i][j] = a_t.rotate.entry[i][j];
				}
			}
			return p;
		}

		// The session runs in the player's room space (the tracking space): locomotion, snap turns
		// or being pushed move the room, not the lock relative to you. No scale, game units.
		Pose RoomAnchor(RE::VR_NODE_DATA* a_nodes)
		{
			Pose p;
			if (auto* room = a_nodes ? a_nodes->RoomNode.get() : nullptr) {
				p = ToPose(room->world);
			}
			return p;
		}

		Pose ToRoom(const Pose& a_anchor, const Pose& a_world)
		{
			const Mat3 inv = a_anchor.rot.Transposed();
			Pose       p;
			p.rot = inv * a_world.rot;
			p.pos = inv * (a_world.pos - a_anchor.pos);
			return p;
		}

		HandInput ReadHand(RE::NiNode* a_node, Hand a_hand, const Pose& a_anchor)
		{
			HandInput in;
			if (!a_node) {
				return in;
			}
			const auto snap = VRInput::Get().Snapshot(a_hand);
			in.valid = true;
			in.pose = ToRoom(a_anchor, ToPose(a_node->world));
			in.trigger = snap.trigger;
			in.grip = snap.grip;
			in.cancel = snap.cancel;
			return in;
		}

		RE::TESObjectMISC* Lockpick()
		{
			return RE::TESForm::LookupByID<RE::TESObjectMISC>(kLockpickID);
		}

		// Skyrim VR's left-handed mode swaps which controller drives the "right" wand node.
		RE::NiNode* WandNode(RE::VR_NODE_DATA* a_nodes, Hand a_physicalHand)
		{
			bool right = a_physicalHand == Hand::kRight;
			if (LockpickManager::Get().GetConfig().followLeftHandedMode && RE::BSOpenVRControllerDevice::IsLeftHandedMode()) {
				right = !right;
			}
			return (right ? a_nodes->RightWandNode : a_nodes->LeftWandNode).get();
		}

		// Physical hand -> skeleton hand node (the visible first-person hand), same swap as the wands.
		RE::NiNode* SkeletonHandNode(RE::VR_NODE_DATA* a_nodes, Hand a_physicalHand)
		{
			bool right = a_physicalHand == Hand::kRight;
			if (LockpickManager::Get().GetConfig().followLeftHandedMode && RE::BSOpenVRControllerDevice::IsLeftHandedMode()) {
				right = !right;
			}
			return (right ? a_nodes->NPCRHnd : a_nodes->NPCLHnd).get();
		}

		// The Skeleton Key, if the player carries it.
		RE::TESBoundObject* CarriedSkeletonKey(RE::PlayerCharacter* a_player)
		{
			auto* objects = AddressCheck::Has(Ids::kDefaultObjects) ? RE::BGSDefaultObjectManager::GetSingleton() : nullptr;
			auto* key = objects ? objects->GetObject<RE::TESBoundObject>(static_cast<std::size_t>(RE::DefaultObjectID::kSkeletonKey)) : nullptr;
			return key && a_player->GetItemCount(key) > 0 ? key : nullptr;
		}

	}

	void LockpickManager::LoadConfig()
	{
		// SKSE\Plugins\VRLockpicking.ini (everything), then what the player changed in the MCM
		// (MCM Helper writes only changed keys to MCM\Settings\VRLockpicking.ini). Of the MCM file
		// only the settings the menu still offers count: values saved for options that have since
		// left the menu would otherwise apply unseen.
		Config c;
		auto   apply = [&c](const char* a_path, const std::string& a_text) {
            for (const auto& key : c.LoadFromIni(a_text)) {
                logger::warn("{}: unknown setting '{}'", a_path, key);
            }
		};
		if (std::ifstream file(kIniPath); file) {
			apply(kIniPath, ReadText(kIniPath));
		} else {
			logger::warn("{} not found, using defaults", kIniPath);
		}
		_iniCfg = c;
		if (std::ifstream file(kMcmSettingsPath); file) {
			std::string                 text = ReadText(kMcmSettingsPath);
			const std::set<std::string> offered = McmSettingKeys(ReadText(kMcmConfigPath));
			if (!offered.empty()) {
				std::vector<std::string> dropped;
				text = KeepIniKeys(text, offered, &dropped);
				std::string list;
				for (const auto& key : dropped) {
					list += (list.empty() ? "" : ", ") + key;
				}
				static std::string logged;  // once, not at every journal close
				if (!list.empty() && list != logged) {
					logger::info("Ignoring MCM values of options no longer in the menu: {}", list);
					logged = list;
				}
			}
			apply(kMcmSettingsPath, text);
			logger::info("Loaded MCM settings from {}", kMcmSettingsPath);
		}
		_cfg = c;
		const std::string profilesText = ReadText(kProfilesPath);
		_profiles = ParseLockProfiles(profilesText);
		_pickProfiles = ParseToolProfiles(profilesText, "pick");
		_shivProfiles = ParseToolProfiles(profilesText, "shiv");
		if (!_profiles.empty() || !_pickProfiles.empty() || !_shivProfiles.empty()) {
			logger::info("Model profiles ({}): {} lock(s), {} pick(s), {} screwdriver(s) measured", kProfilesPath, _profiles.size(),
				_pickProfiles.size(), _shivProfiles.size());
		}
		if (_cfg.lockVariations) {
			LockMods::Load();
		}
		logger::info("Config loaded: enabled={} leftHandedPick={} meshes={} scale={:.2f} pause={}",
			_cfg.enabled, _cfg.leftHandedPick, _cfg.showMeshes, _cfg.visualScale, _cfg.fullPause);
		spdlog::set_level(_cfg.debugLogging ? spdlog::level::debug : spdlog::level::info);
	}

	void LockpickManager::RegisterEvents()
	{
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
			logger::info("Registered for menu events");
		}
	}

	RE::BSEventNotifyControl LockpickManager::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}
		if (!a_event->opening) {
			// The MCM lives in the journal: pick up changed settings when it closes.
			if (a_event->menuName == RE::JournalMenu::MENU_NAME && !_active.load()) {
				SKSE::GetTaskInterface()->AddTask([] { Get().LoadConfig(); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}
		if (!_cfg.enabled) {
			if (a_event->menuName == RE::LockpickingMenu::MENU_NAME && a_event->opening) {
				Visuals::NoteVanillaMenu();
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		if (a_event->menuName != RE::LockpickingMenu::MENU_NAME) {
			// Any menu that pauses the game (message box, journal, loading screen...) ends the
			// session so its buttons are not hidden from the player.
			if (_active.load()) {
				std::string name(a_event->menuName.c_str());
				// The "Open / Take" prompt for whatever a hand points at: nothing can be activated
				// while picking, so don't show it either.
				if (name == "WSActivateRollover") {
					if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
						queue->AddMessage(a_event->menuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
					}
					return RE::BSEventNotifyControl::kContinue;
				}
				SKSE::GetTaskInterface()->AddTask([name] {
					auto* ui = RE::UI::GetSingleton();
					auto  menu = ui ? ui->GetMenu(name) : nullptr;
					if (menu && menu->PausesGame()) {
						Get().Abort(std::format("pausing menu '{}' opened", name).c_str());
					} else {
						logger::debug("Menu '{}' opened during lockpicking (not pausing)", name);
					}
				});
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		auto hideMenu = [] {
			if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
				queue->AddMessage(RE::LockpickingMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			}
		};

		if (_active.load()) {
			hideMenu();
			return RE::BSEventNotifyControl::kContinue;
		}

		if (!AddressCheck::Has(Ids::kCore)) {
			static bool warned = false;
			if (!warned) {
				warned = true;
				logger::error("VR Address Library {} lacks IDs this mod needs; using the vanilla lockpicking menu", AddressCheck::LibraryVersion());
				RE::SendHUDMessage::ShowHUDMessage("VR Lockpicking: please update VR Address Library for SKSEVR. Using vanilla lockpicking.");
			}
			Visuals::NoteVanillaMenu();
			return RE::BSEventNotifyControl::kContinue;
		}

		auto target = RE::LockpickingMenu::GetTargetReference();
		if (!target) {
			logger::warn("Lockpicking menu opened without a target reference; leaving vanilla menu");
			Visuals::NoteVanillaMenu();
			return RE::BSEventNotifyControl::kContinue;
		}

		logger::info("Intercepting lockpicking of {:08X} '{}'", target->GetFormID(), target->GetName());
		_active.store(true);
		hideMenu();
		SKSE::GetTaskInterface()->AddTask([target] { Get().Start(target); });
		return RE::BSEventNotifyControl::kContinue;
	}

	void LockpickManager::Start(RE::TESObjectREFRPtr a_target)
	{
		auto  ref = a_target;
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* vrNodes = player ? player->GetVRNodeData() : nullptr;
		if (!ref || !player || !vrNodes || !ref->IsLocked()) {
			logger::warn("Start aborted: target or player not available");
			_active.store(false);
			return;
		}

		const auto level = ref->GetLockLevel();
		if (level < RE::LOCK_LEVEL::kVeryEasy || level > RE::LOCK_LEVEL::kVeryHard) {
			logger::info("Lock level {} not pickable", static_cast<int>(level));
			_active.store(false);
			return;
		}

		// The vanilla menu only opens if the player may pick this lock (lockpicks or the
		// Skeleton Key), so a player without lockpicks here must be using the Skeleton Key. With
		// both, the Skeleton Key is used too, as in the vanilla menu.
		auto*      pick = Lockpick();
		const bool hasLockpicks = pick && player->GetItemCount(pick) > 0;
		auto*      skeletonKey = CarriedSkeletonKey(player);

		RE::NiNode* hmdNode = vrNodes->HmdNode ? vrNodes->HmdNode.get() : vrNodes->UprightHmdNode.get();
		if (!hmdNode) {
			logger::warn("Start aborted: no HMD node");
			_active.store(false);
			return;
		}

		_level = static_cast<LockLevel>(static_cast<int>(level));
		const float skill = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kLockpicking);

		VanillaTuning tuning;
		const char* sweetNames[5] = { "fSweetSpotVeryEasy", "fSweetSpotEasy", "fSweetSpotAverage", "fSweetSpotHard", "fSweetSpotVeryHard" };
		const char* breakNames[5] = { "fLockpickBreakNovice", "fLockpickBreakApprentice", "fLockpickBreakAdept", "fLockpickBreakExpert", "fLockpickBreakMaster" };
		for (int i = 0; i < 5; ++i) {
			tuning.sweetSpotDeg[i] = GetGMSTFloat(sweetNames[i], tuning.sweetSpotDeg[i]);
			tuning.breakSeconds[i] = GetGMSTFloat(breakNames[i], tuning.breakSeconds[i]);
		}
		tuning.sweetSkillBase = GetGMSTFloat("fLockpickSkillSweetSpotBase", tuning.sweetSkillBase);
		tuning.sweetSkillMult = GetGMSTFloat("fLockpickSkillSweetSpotMult", tuning.sweetSkillMult);
		tuning.breakSkillMult = GetGMSTFloat("fLockpickBreakSkillMult", tuning.breakSkillMult);

		const float perkMult = EntryPointFloat(RE::BGSEntryPoint::ENTRY_POINT::kModLockpickSweetSpot, player, ref.get(), 1.0f);
		// perks through their entry points, as the vanilla menu asks for them (so mods that change
		// or replace the perks count the same way)
		const bool unbreakable = !hasLockpicks || skeletonKey != nullptr ||
		                         EntryPointFloat(RE::BGSEntryPoint::ENTRY_POINT::kMakeLockpicksUnbreakable, player, ref.get(), 0.0f) != 0.0f;

		std::uniform_real_distribution<float> uni(0.0f, 1.0f);
		LockParams params = ComputeLockParams(_level, skill, perkMult, unbreakable, tuning, _cfg, uni(_rng));
		// Locksmith: the vanilla menu starts the pick this close (degrees) to the sweet spot
		params.startArc = std::max(0.0f, EntryPointFloat(RE::BGSEntryPoint::ENTRY_POINT::kSetLockpickStartingArc, player, ref.get(), 0.0f));
		params.startRandom01 = uni(_rng);
		_anchor = RoomAnchor(vrNodes);
		const LockFrame  frame = MakeLockFrame(ToRoom(_anchor, ToPose(hmdNode->world)), _cfg);

		logger::info("Session start: level {} skill {:.0f} perkMult {:.2f} -> sweet {:.1f} +/- {:.1f} deg, break {:.2f}s{}{}",
			static_cast<int>(_level), skill, perkMult, params.sweetCenter, params.sweetHalfWidth, params.breakSeconds,
			unbreakable ? " (unbreakable)" : "",
			params.startArc > 0.0f ? std::format(" (Locksmith: within {:.0f} deg of where the pick goes in)", params.startArc) : std::string());

		// Tools are held in the visible hands when the skeleton hand nodes exist (fist grip); otherwise
		// they hang off the controllers, pointing into the lock at the start pose.
		const Hand pickSide = _cfg.leftHandedPick ? Hand::kLeft : Hand::kRight;
		const Hand wrenchSide = _cfg.leftHandedPick ? Hand::kRight : Hand::kLeft;
		Config     sessionCfg = _cfg;
		_useHandNodes = _cfg.handGrip && SkeletonHandNode(vrNodes, pickSide) && SkeletonHandNode(vrNodes, wrenchSide);
		sessionCfg.handGrip = _useHandNodes;
		sessionCfg.swapGripMirror = _cfg.followLeftHandedMode && RE::BSOpenVRControllerDevice::IsLeftHandedMode();
		RE::NiNode* pickNode = HandNodeFor(vrNodes, pickSide);
		RE::NiNode* wrenchNode = HandNodeFor(vrNodes, wrenchSide);
		const Mat3  pickRef = pickNode ? ToRoom(_anchor, ToPose(pickNode->world)).rot : frame.Basis();
		const Mat3  wrenchRef = wrenchNode ? ToRoom(_anchor, ToPose(wrenchNode->world)).rot : frame.Basis();
		logger::info("Tools held by {} ({}left-handed mode)", _useHandNodes ? "the visible hands" : "the controllers",
			sessionCfg.swapGripMirror ? "" : "no ");
		_session.emplace(params, frame, sessionCfg, pickRef, wrenchRef);
		_keyholeWorld = _anchor.rot * frame.keyhole + _anchor.pos;
		_target = a_target;
		_lastUpdate = std::chrono::steady_clock::now();
		_shakeTime = 0.0f;
		// Lock Variations: this lock's own lock / lockpick models and sounds
		Config models = _cfg;
		_sounds = {};
		if (_cfg.lockVariations) {
			const LockMods::Result mods = LockMods::For(ref.get());
			if (!mods.lockModel.empty()) {
				models.lockMesh = mods.lockModel;
			}
			if (!mods.pickModel.empty()) {
				models.pickMesh = mods.pickModel;
			}
			_sounds = mods.sounds;
		}
		// the Skeleton Key is the pick whenever you carry it
		if (auto* model = skeletonKey ? skeletonKey->As<RE::TESModel>() : nullptr; model && model->GetModel() && *model->GetModel()) {
			models.pickMesh = model->GetModel();
			models.flipPickMesh = !models.flipPickMesh;  // its tip is the other end from the one the fit guesses (seen in game)
			logger::info("Picking with the Skeleton Key ('{}')", models.pickMesh);
		}
		const auto        profileIt = _profiles.find(NormalizeModelPath(models.lockMesh));
		const LockProfile* profile = profileIt != _profiles.end() ? &profileIt->second : nullptr;
		_visuals.SetToolProfiles(_pickProfiles, _shivProfiles);
		if (_cfg.debugView && !_cfg.markupMode) {
			if (!_debug.Create(_cfg, ref.get(), false)) {
				logger::info("Debug view unavailable, using the models with the targets over them");
				_visuals.Create(models, ref.get(), profile);
				_debug.Create(_cfg, ref.get(), true);
			}
		} else {
			if (!_visuals.Create(models, ref.get(), profile)) {
				logger::info("Running without 3D meshes (haptics + sound only)");
			}
			if (_cfg.showTargets || _cfg.markupMode) {
				_debug.Create(_cfg, ref.get(), true);
			}
			if (_cfg.markupMode) {
				_markup.Create(ref.get(), !_cfg.leftHandedPick);
			}
		}
		// the assist ring, unless the debug view (which shows all of it) or the markup tool is on
		if (_cfg.assistRing > 0 && !_markup.Active() && !(_debug.Active() && !_debug.Overlay())) {
			const bool easy = static_cast<int>(_level) <= static_cast<int>(LockLevel::kApprentice);
			_assist.Create(_cfg, ref.get(), _cfg.assistRing >= 2 && easy);
		}
		if (!_markup.Active()) {
			_counter.Create(_cfg, ref.get());
		}
		_markHeld = _nextHeld = _resetHeld = true;
		_alignDirty = false;
		_grab.reset();
		const bool quiet = _markup.Active();  // during markup only its own messages
		VRInput::Get().SetBlocking(true);  // the thumbsticks too: you stay where you are
		PauseWorld(true);
		MuteGameRumble(true);
		// the "Open" prompt of the chest you just activated may still be up
		if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage("WSActivateRollover", RE::UI_MESSAGE_TYPE::kHide, nullptr);
		}
		LockControls(true);
		{
			const int picks = params.unbreakable ? -1 : (pick ? player->GetItemCount(pick) : 0);
			_debug.SetPicksLeft(picks);
			_counter.SetCount(picks);
			if (_debug.Active() && !quiet) {
				RE::SendHUDMessage::ShowHUDMessage(picks < 0 ? "Lockpicks: unbreakable" : std::format("Lockpicks: {}", picks).c_str());
			}
		}

		_gripSamples = 0;
		_gripTriggerHeld = true;
		_debug.SetQuiet(quiet);
		if (_debug.Active() && !quiet) {
			RE::SendHUDMessage::ShowHUDMessage("Debug: press a trigger to record how you hold your hands (1 neutral, 2 turned in, 3 the other way).");
		}
	}

	// FrameHook: on the main thread, right after the game moved the hand nodes this frame. The
	// simulation ran earlier in the frame (Update, from the controller callback's task) with the
	// hands as they were then; the models are placed again here with this frame's hands, so they
	// don't trail them by a frame.
	void LockpickManager::OnPostWandUpdate()
	{
		auto& self = Get();
		if (!self._active.load() || !self._session) {
			return;
		}
		// While the game is fully paused the task queue may not run; then the whole frame runs here.
		if (self._fullPaused && std::chrono::steady_clock::now() - self._lastUpdate > std::chrono::milliseconds(50)) {
			self.Update();
			if (auto* player = RE::PlayerCharacter::GetSingleton(); player && self._fullPaused) {
				self.HideControllers(player->GetVRNodeData(), true);
				self.HideHands(player->GetVRNodeData(), true);
			}
			return;
		}
		self.RefreshVisuals();
	}

	void LockpickManager::RefreshVisuals()
	{
		if (OtherPause()) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* vrNodes = player ? player->GetVRNodeData() : nullptr;
		if (!vrNodes) {
			return;
		}
		const Hand      pickSide = _cfg.leftHandedPick ? Hand::kLeft : Hand::kRight;
		const Hand      tensionSide = _cfg.leftHandedPick ? Hand::kRight : Hand::kLeft;
		const Pose      anchor = RoomAnchor(vrNodes);
		const HandInput pick = ReadHand(HandNodeFor(vrNodes, pickSide), pickSide, anchor);
		const HandInput tension = ReadHand(HandNodeFor(vrNodes, tensionSide), tensionSide, anchor);
		_visuals.Update(*_session, pick.valid ? &pick.pose : nullptr, tension.valid ? &tension.pose : nullptr, _shake, anchor);
		_debug.Update(*_session, pick.valid ? &pick.pose : nullptr, tension.valid ? &tension.pose : nullptr, _shake, anchor, 0.0f);
		_assist.Update(*_session, _shake, anchor);
		_counter.Update(*_session, _shake, anchor);
		if (_fullPaused) {
			HideControllers(vrNodes, true);
		}
		HideHands(vrNodes, true);  // after the hands were read: their pose is still the tracked one
	}

	// Only the tools show while lockpicking: the hand bones (and so the fingers under them) shrink
	// to nothing, which folds the skinned hands away; their position and rotation, which the tools
	// follow, stay as tracked. Done each frame after the game (and HIGGS) posed them.
	void LockpickManager::HideHands(RE::VR_NODE_DATA* a_nodes, bool a_hide)
	{
		if (!a_nodes || (!a_hide && !_handsHidden)) {
			return;
		}
		for (auto* node : { a_nodes->NPCLHnd.get(), a_nodes->NPCRHnd.get() }) {
			if (node) {
				node->local.scale = a_hide ? 0.0001f : 1.0f;
				RE::NiUpdateData ud{};
				node->Update(ud);
			}
		}
		_handsHidden = a_hide;
	}

	// While the game is paused it shows the controller models; during a full pause they would
	// cover the hands holding the tools. Shrunk to nothing right after the game placed them (as
	// HIGGS hides its off-hand controller), and given back afterwards.
	void LockpickManager::HideControllers(RE::VR_NODE_DATA* a_nodes, bool a_hide)
	{
		if (!a_nodes || (!a_hide && !_controllersHidden)) {
			return;
		}
		for (auto* node : { a_nodes->LeftValveIndexControllerNode.get(), a_nodes->RightValveIndexControllerNode.get() }) {
			if (node) {
				node->local.scale = a_hide ? 0.0001f : 1.0f;
				RE::NiUpdateData ud{};
				node->Update(ud);
			}
		}
		_controllersHidden = a_hide;
	}

	void LockpickManager::OnFrame()
	{
		auto& self = Get();
		if (!self._active.load() || self._updatePending.exchange(true)) {
			return;
		}
		SKSE::GetTaskInterface()->AddTask([] {
			auto& mgr = Get();
			mgr._updatePending.store(false);
			mgr.Update();
		});
	}

	void LockpickManager::Update()
	{
		if (!_session) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		float      dt = std::chrono::duration<float>(now - _lastUpdate).count();
		_lastUpdate = now;
		dt = std::clamp(dt, 0.0f, 0.1f);

		if (OtherPause()) {
			// Usually the vanilla lockpicking menu still closing. Other pausing menus end the
			// session from ProcessEvent.
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* vrNodes = player ? player->GetVRNodeData() : nullptr;
		auto  ref = _target;
		if (!player || !vrNodes || player->IsDead()) {
			Abort("player unavailable");
			return;
		}
		if (!ref || !ref->IsLocked()) {
			Abort("target unloaded or already unlocked");
			return;
		}

		RE::NiNode* hmdNode = vrNodes->HmdNode ? vrNodes->HmdNode.get() : vrNodes->UprightHmdNode.get();
		if (!hmdNode) {
			return;
		}

		const Hand  pickSide = _cfg.leftHandedPick ? Hand::kLeft : Hand::kRight;
		const Hand  tensionSide = _cfg.leftHandedPick ? Hand::kRight : Hand::kLeft;
		RE::NiNode* pickNode = HandNodeFor(vrNodes, pickSide);
		RE::NiNode* tensionNode = HandNodeFor(vrNodes, tensionSide);

		FrameInput in;
		in.dt = dt;
		_anchor = RoomAnchor(vrNodes);
		in.hmd = ToRoom(_anchor, ToPose(hmdNode->world));
		// Walked away (with the thumbstick, which moves the whole room the session works in, so
		// the lock would come along): the head this far from where the keyhole was, in the world.
		{
			Vec3 away = ToPose(hmdNode->world).pos - _keyholeWorld;
			away.z = 0.0f;
			if (Length(away) > _cfg.cancelDistance) {
				Abort("walked away from the lock");
				return;
			}
		}
		in.pickHand = ReadHand(pickNode, pickSide, _anchor);
		in.tensionHand = ReadHand(tensionNode, tensionSide, _anchor);
		if (_fullPaused) {
			CheckHandsWhilePaused(vrNodes, dt);
		}

		if (_markup.Active()) {
			UpdateMarkup(in, dt);
		} else if (_debug.Active()) {
			RecordGripSample(vrNodes);
		}

		const std::vector<SessionEvent> events = _session->Update(in);
		for (const auto ev : events) {
			HandleEvent(ev, ref.get());
			if (!_session) {
				return;  // handler ended the session
			}
		}

		const auto& haptics = _session->Haptics();
		VRInput::Get().Haptic(pickSide, haptics.pickHand);
		VRInput::Get().Haptic(tensionSide, haptics.tensionHand);

		float& shake = _shake;
		shake = 0.0f;
		const bool forcing = _session->PickInserted() && _session->TurnInput() > _session->AllowedTurn() + _cfg.forceSlack;
		if (forcing) {
			_shakeTime += dt;
			const float ratio = _session->Params().unbreakable ? 0.3f : Clamp01(_session->PickDamage() / _session->Params().breakSeconds);
			shake = std::sin(_shakeTime * 70.0f) * (0.5f + 2.0f * ratio);
		}
		_visuals.Update(*_session, in.pickHand.valid ? &in.pickHand.pose : nullptr,
			in.tensionHand.valid ? &in.tensionHand.pose : nullptr, shake, _anchor);
		_assist.Update(*_session, shake, _anchor);
		_counter.Update(*_session, shake, _anchor);
		_debug.Update(*_session, in.pickHand.valid ? &in.pickHand.pose : nullptr,
			in.tensionHand.valid ? &in.tensionHand.pose : nullptr, shake, _anchor, dt);

		if (_cfg.debugLogging) {
			_debugTimer += dt;
			if (_debugTimer > 1.0f) {
				_debugTimer = 0.0f;
				const auto& r = in.pickHand.pose.rot;
				const Vec3  tip = _session->PickTip(in.pickHand.pose);
				logger::debug("pick ins={} ang={:.1f} (sweet {:.1f}) lock={:.2f} allowed={:.2f} turn={:.2f} dmg={:.2f} tipDist={:.1f} tension={} | handX=({:.2f},{:.2f},{:.2f}) handY=({:.2f},{:.2f},{:.2f}) handZ=({:.2f},{:.2f},{:.2f})",
					_session->PickInserted(), _session->PickAngle(), _session->Params().sweetCenter, _session->LockAngle(),
					_session->AllowedTurn(), _session->TurnInput(), _session->PickDamage(), Distance(tip, _session->Frame().keyhole),
					_session->TensionEngaged(),
					r.m[0][0], r.m[1][0], r.m[2][0], r.m[0][1], r.m[1][1], r.m[2][1], r.m[0][2], r.m[1][2], r.m[2][2]);
			}
		}

		if (_session && _session->Finished()) {
			End();
		}
	}

	void LockpickManager::HandleEvent(SessionEvent a_event, RE::TESObjectREFR* a_target)
	{
		switch (a_event) {
		case SessionEvent::kPickInserted:
		case SessionEvent::kPickTick:
			RE::PlaySound(_sounds.pickMovement.c_str());
			break;
		case SessionEvent::kLockStartTurn:
			RE::PlaySound(_sounds.turn.c_str());
			break;
		case SessionEvent::kLockStop:
			RE::PlaySound(_sounds.stop.c_str());
			break;
		case SessionEvent::kForcing:
			RE::PlaySound(((_rng() & 1) ? _sounds.squeakA : _sounds.squeakB).c_str());
			break;
		case SessionEvent::kPickBroken:
			if (!OnPickBroken(a_target)) {
				End();
			}
			break;
		case SessionEvent::kUnlocked:
			OnUnlocked(a_target);
			break;
		case SessionEvent::kCancelled:
			logger::info("Session cancelled");
			End();
			break;
		default:
			break;
		}
	}

	bool LockpickManager::OnPickBroken(RE::TESObjectREFR* a_target)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		RE::PlaySound("UILockpickingPickBreak");
		auto* pick = Lockpick();
		if (player && pick) {
			player->RemoveItem(pick, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			player->AddSkillExperience(RE::ActorValue::kLockpicking, GetGMSTFloat("fSkillUsageLockPickBroken", 1.0f));
		}
		logger::info("Pick broken");

		if (_cfg.crimeCheck && HandleCrime(a_target)) {
			return false;
		}
		const int left = player && pick ? player->GetItemCount(pick) : 0;
		if (left <= 0) {
			ShowGMSTMessage("sOutOfLockpicks", "You have no lockpicks left.");
			return false;
		}
		_debug.SetPicksLeft(left);
		_counter.SetCount(left);
		if (_debug.Active() && !_markup.Active()) {
			RE::SendHUDMessage::ShowHUDMessage(std::format("Lockpick broke - {} left", left).c_str());
		}
		return true;
	}

	void LockpickManager::OnUnlocked(RE::TESObjectREFR* a_target)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!a_target || !player) {
			return;
		}
		auto* lock = a_target->GetLock();
		if (!lock) {
			return;
		}
		RE::TESKey* key = lock->key;

		lock->SetLocked(false);
		// Record the change so the save keeps the lock open (as BakaAutoLockpicking does).
		if (AddressCheck::Has(Ids::kAddLockChange)) {
			a_target->AddLockChange();
		} else {
			// Not in the released VR Address Library, but Skyrim VR has a single build: SE ID
			// 19110 is at this offset (VR Address Library database, status "verified").
			using func_t = void (*)(RE::TESObjectREFR*);
			static REL::Relocation<func_t> addLockChange{ REL::Offset(0x297310) };
			addLockChange(a_target);
		}
		// "Locks Picked" statistic: the engine's event when the library has it, else Papyrus
		// Game.IncrementStat (the vanilla menu would count it; we skip the menu).
		if (AddressCheck::Has(Ids::kLocksPickedStat)) {
			if (auto* source = RE::LocksPicked::QEventSource()) {
				RE::LocksPicked::Event ev{};
				source->SendEvent(&ev);
			}
		} else if (auto* vm = AddressCheck::Has(Ids::kPapyrusCall) ? RE::BSScript::Internal::VirtualMachine::GetSingleton() : nullptr) {
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			auto* args = RE::MakeFunctionArguments(RE::BSFixedString("Locks Picked"), static_cast<std::int32_t>(1));
			if (!vm->DispatchStaticCall("Game", "IncrementStat", args, none)) {
				logger::info("Could not count the pick in the Locks Picked statistic");
			}
		}
		RE::PlaySound(_sounds.unlock.c_str());

		const char* xpNames[5] = { "fSkillUsageLockPickVeryEasy", "fSkillUsageLockPickEasy", "fSkillUsageLockPickAverage", "fSkillUsageLockPickHard", "fSkillUsageLockPickVeryHard" };
		const int   idx = std::clamp(static_cast<int>(_level), 0, 4);
		player->AddSkillExperience(RE::ActorValue::kLockpicking, GetGMSTFloat(xpNames[idx], static_cast<float>(idx + 1)));

		// Wax Key (the key reward chance entry point): a copy of the key
		bool waxKey = false;
		if (key) {
			const float chance = EntryPointFloat(RE::BGSEntryPoint::ENTRY_POINT::kModLockpickingKeyRewardChance, player, a_target, 0.0f);
			std::uniform_real_distribution<float> uni(0.0f, 100.0f);
			waxKey = chance > 0.0f && uni(_rng) < (chance <= 1.0f ? chance * 100.0f : chance);
			const bool carried = player->GetItemCount(key) > 0;
			logger::info("Lock has a key '{}': key reward chance {:.2f}{}", key->GetName(), chance,
				carried ? ", already carried" : (waxKey ? ", a copy given (Wax Key)" : ", none given"));
		}
		if (key && player->GetItemCount(key) <= 0 && waxKey) {
			player->AddObjectToContainer(key, nullptr, 1, nullptr);
			RE::PlaySound("ITMKeyUpSD");
		}

		logger::info("Unlocked {:08X}", a_target->GetFormID());

		const bool caught = _cfg.crimeCheck && HandleCrime(a_target);

		auto* base = a_target->GetBaseObject();
		if (_cfg.activateContainerAfterUnlock && !caught && base && base->Is(RE::FormType::Container) &&
			AddressCheck::Has(Ids::kActivate)) {
			RE::TESObjectREFRPtr ref{ a_target };
			// After End() so the container menu receives input.
			SKSE::GetTaskInterface()->AddTask([ref] {
				if (auto* pc = RE::PlayerCharacter::GetSingleton(); ref && pc) {
					ref->ActivateRef(pc, 0, nullptr, 0, false);
				}
			});
		}
	}

	bool LockpickManager::HandleCrime(RE::TESObjectREFR* a_target)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !a_target || !AddressCheck::Has(Ids::kCrime)) {
			return false;
		}

		RE::TESForm* owner = a_target->GetOwner();
		if (!owner && a_target->GetFormType() == RE::FormType::Reference && a_target->GetBaseObject() &&
			a_target->GetBaseObject()->Is(RE::FormType::Door)) {
			if (auto* xtra = a_target->extraList.GetByType<RE::ExtraTeleport>(); xtra && xtra->teleportData) {
				if (auto door = std::as_const(xtra->teleportData->linkedDoor).get()) {  // const get(): ID 12204, present in old address libraries
					if (auto* cell = door->GetParentCell()) {
						owner = cell->GetOwner();
					}
				}
			}
		}
		if (!owner) {
			return false;
		}
		// Player owns it (or belongs to the owning faction): no crime.
		if (a_target->IsAnOwner(player, true, false)) {
			return false;
		}

		auto* processLists = RE::ProcessLists::GetSingleton();
		if (!processLists) {
			return false;
		}
		// Returns the highest stealth-detection level; <= 0 means nobody detects the player.
		std::uint32_t losCount = 1;
		if (processLists->RequestHighestDetectionLevelAgainstActor(player, losCount) <= 0) {
			return false;
		}

		const float chance = EntryPointFloat(RE::BGSEntryPoint::ENTRY_POINT::kModLockpickingCrimeChance, player, a_target, 1.0f);
		std::uniform_real_distribution<float> uni(0.0f, 1.0f);
		if (uni(_rng) >= chance) {
			return false;
		}

		logger::info("Caught lockpicking (owner {:08X})", owner->GetFormID());
		player->TrespassAlarm(a_target, owner, -1);
		ShowGMSTMessage("sLockpickingCaught", "You have been caught lockpicking.");
		return true;
	}

	RE::NiNode* LockpickManager::HandNodeFor(RE::VR_NODE_DATA* a_nodes, Hand a_physicalHand) const
	{
		if (_useHandNodes) {
			if (auto* node = SkeletonHandNode(a_nodes, a_physicalHand)) {
				return node;
			}
		}
		return WandNode(a_nodes, a_physicalHand);
	}

	// Like the vanilla menu, the world stops while you pick: everything runs at a tiny fraction of
	// normal speed, so NPCs can't walk into you. Head and hand tracking are unaffected.
	bool LockpickManager::OtherPause() const
	{
		// a pause that isn't our own full pause (a menu, a loading screen)
		auto* ui = RE::UI::GetSingleton();
		return ui && ui->numPausesGame > (_fullPaused ? 1u : 0u);
	}

	// Full pause: the game may stop moving the visible hands while it is paused. Compare how far
	// the right hand node and the right controller moved each second; say so once if only the
	// controller does.
	void LockpickManager::CheckHandsWhilePaused(RE::VR_NODE_DATA* a_nodes, float a_dt)
	{
		auto* hand = SkeletonHandNode(a_nodes, Hand::kRight);
		auto* wand = WandNode(a_nodes, Hand::kRight);
		if (!hand || !wand || _handCheck.reported) {
			return;
		}
		const Vec3 h{ hand->world.translate.x, hand->world.translate.y, hand->world.translate.z };
		const Vec3 w{ wand->world.translate.x, wand->world.translate.y, wand->world.translate.z };
		_handCheck.timer += a_dt;
		if (!_handCheck.valid) {
			_handCheck.hand = h;
			_handCheck.wand = w;
			_handCheck.valid = true;
			_handCheck.timer = 0.0f;
			return;
		}
		if (_handCheck.timer < 1.0f) {
			return;
		}
		const float handMoved = Distance(h, _handCheck.hand);
		const float wandMoved = Distance(w, _handCheck.wand);
		logger::debug("Full pause: in the last second the right hand moved {:.1f}, its controller {:.1f}", handMoved, wandMoved);
		if (wandMoved > 3.0f && handMoved < 0.3f) {
			logger::warn("Full pause: the visible hands don't follow the controllers while the game is paused");
			RE::SendHUDMessage::ShowHUDMessage("Full pause: your hands don't follow while the game is paused - turn Full pause off in the MCM.");
			_handCheck.reported = true;
		}
		_handCheck.hand = h;
		_handCheck.wand = w;
		_handCheck.timer = 0.0f;
	}

	void LockpickManager::PauseWorld(bool a_pause)
	{
		// Full pause: the game's own pause, as a menu does it (Skyrim Souls RE
		// counts menus the same way): nothing in the world moves or reacts.
		if (a_pause && _cfg.fullPause && !_fullPaused) {
			if (auto* ui = RE::UI::GetSingleton()) {
				++ui->numPausesGame;
				_fullPaused = true;
				_handCheck = {};
				logger::info("Game paused (full pause)");
				return;
			}
		}
		if (!a_pause && _fullPaused) {
			if (auto* ui = RE::UI::GetSingleton(); ui && ui->numPausesGame > 0) {
				--ui->numPausesGame;
			}
			_fullPaused = false;
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				HideControllers(player->GetVRNodeData(), false);
			}
			logger::info("Game resumed (full pause)");
			return;
		}
	}

	// The game's own "something to activate" rumble would mix with ours while the hands are near
	// the lock: off during a session, restored after (as HIGGS does while holding things).
	void LockpickManager::MuteGameRumble(bool a_mute)
	{
		auto* inis = AddressCheck::Has(Ids::kIniSettings) ? RE::INISettingCollection::GetSingleton() : nullptr;
		auto* setting = inis ? inis->GetSetting("fActivateRumbleIntensity:VRInput") : nullptr;
		if (!setting || a_mute == _rumbleMuted) {
			return;
		}
		if (a_mute) {
			_savedRumble = setting->GetFloat();
			setting->SetFloat(0.0f);
		} else {
			setting->SetFloat(_savedRumble);
		}
		_rumbleMuted = a_mute;
	}

	// Debug: a trigger press (either hand) logs both hands and controllers relative to the lock,
	// so a more natural way of holding the tools can be worked out from real poses.
	void LockpickManager::RecordGripSample(RE::VR_NODE_DATA* a_nodes)
	{
		const auto  left = VRInput::Get().Snapshot(Hand::kLeft);
		const auto  right = VRInput::Get().Snapshot(Hand::kRight);
		const bool  pressed = left.trigger > 0.8f || right.trigger > 0.8f;
		const bool  edge = pressed && !_gripTriggerHeld;
		_gripTriggerHeld = pressed ? true : (left.trigger < 0.3f && right.trigger < 0.3f ? false : _gripTriggerHeld);
		if (!edge || !_session) {
			return;
		}
		++_gripSamples;
		static constexpr const char* kLabels[] = { "neutral", "holding (turned in)", "turned the other way" };
		const char* label = _gripSamples <= 3 ? kLabels[_gripSamples - 1] : "extra";

		// vectors in lock coordinates: (right, into the lock, up)
		const LockFrame& f = _session->Frame();
		auto lockVec = [&f](const Vec3& a_v) { return std::format("({:+.2f} {:+.2f} {:+.2f})", Dot(a_v, f.right), Dot(a_v, f.forward), Dot(a_v, f.up)); };
		auto axes = [&](RE::NiNode* a_node) {
			if (!a_node) {
				return std::string("missing");
			}
			const Pose p = ToRoom(_anchor, ToPose(a_node->world));
			return std::format("pos {} X {} Y {} Z {}", lockVec(p.pos - f.keyhole), lockVec(p.rot * Vec3{ 1, 0, 0 }), lockVec(p.rot * Vec3{ 0, 1, 0 }),
				lockVec(p.rot * Vec3{ 0, 0, 1 }));
		};
		auto handLine = [&](const char* a_name, Hand a_hand, bool a_pickHand) {
			RE::NiNode* handNode = SkeletonHandNode(a_nodes, a_hand);
			RE::NiNode* wand = WandNode(a_nodes, a_hand);
			if (!handNode && !wand) {
				logger::info("  {} hand: not tracked", a_name);
				return;
			}
			std::string extra;
			if (handNode) {
				const Pose  p = ToRoom(_anchor, ToPose(handNode->world));
				const float mirror = a_hand == Hand::kRight ? 1.0f : -1.0f;
				const Vec3  palm = p.rot * Vec3{ _cfg.handPalm.x * mirror, _cfg.handPalm.y, _cfg.handPalm.z };
				const Vec3  fingers = p.rot * Vec3{ _cfg.handPointing.x * mirror, _cfg.handPointing.y, _cfg.handPointing.z };
				extra = std::format(" | palm {} back {} fingers {}", lockVec(palm), lockVec(-palm), lockVec(fingers));
			}
			const Pose tool = a_pickHand ? _session->PickTool(ToRoom(_anchor, ToPose(handNode ? handNode->world : wand->world))) :
			                               _session->WrenchTool(ToRoom(_anchor, ToPose(handNode ? handNode->world : wand->world)));
			logger::info("  {} hand: {}{}\n    {} controller: {}\n    {} tool now: tip {} along {} flat side {}", a_name, axes(handNode), extra, a_name,
				axes(wand), a_name, lockVec(tool.pos - f.keyhole), lockVec(tool.rot * Vec3{ 0, 1, 0 }), lockVec(tool.rot * Vec3{ 0, 0, 1 }));
		};
		logger::info("Grip sample {} ({}), vectors in lock coordinates (right, into the lock, up):", _gripSamples, label);
		const Hand pickSide = _cfg.leftHandedPick ? Hand::kLeft : Hand::kRight;
		handLine("right", Hand::kRight, pickSide == Hand::kRight);
		handLine("left", Hand::kLeft, pickSide == Hand::kLeft);
		RE::SendHUDMessage::ShowHUDMessage(std::format("Grip sample {} recorded ({})", _gripSamples, label).c_str());
		VRInput::Get().Haptic(Hand::kLeft, 0.8f);
		VRInput::Get().Haptic(Hand::kRight, 0.8f);
	}

	// Markup tool. Lock part: the pick's tip marks the keyhole (left trigger), a stick click goes
	// back. Pick / wrench parts: the model is lined up in the hand (UpdateAlignment).
	void LockpickManager::UpdateMarkup(const FrameInput& a_in, float a_dt)
	{
		const auto left = VRInput::Get().Snapshot(Hand::kLeft);
		const auto right = VRInput::Get().Snapshot(Hand::kRight);
		auto       edge = [](float a_value, bool& a_held) {
            const bool pressed = a_value > 0.8f;
            const bool fired = pressed && !a_held;
            if (pressed) {
                a_held = true;
            } else if (a_value < 0.3f) {
                a_held = false;
            }
            return fired;
		};
		const bool mark = edge(left.trigger, _markHeld);  // lock part
		const bool next = edge((right.buttonA || left.buttonA) ? 1.0f : 0.0f, _nextHeld);
		const bool reset = edge((right.stickClick || left.stickClick) ? 1.0f : 0.0f, _resetHeld);

		if (next) {
			EndGrab(true);
			SaveAlignment();
			_markup.NextPart();
			if (_markup.CurrentPart() != Markup::Part::kLock) {
				const bool wrench = _markup.CurrentPart() == Markup::Part::kWrench;
				logger::info("Markup: lining up the {} '{}' now", wrench ? "wrench" : "pick", _visuals.ToolModelPath(wrench));
			} else {
				logger::info("Markup: marking the keyhole of '{}' now", _visuals.LockModelPath());
			}
		}

		if (_markup.CurrentPart() != Markup::Part::kLock) {
			_markup.Update(*_session, _anchor, nullptr, false, false);
			const bool wrench = _markup.CurrentPart() == Markup::Part::kWrench;
			const auto& hand = wrench ? a_in.tensionHand : a_in.pickHand;
			std::optional<Pose> tool;
			if (hand.valid) {
				tool = wrench ? _session->WrenchVisual(hand.pose) : _session->PickVisual(hand.pose);
			}
			UpdateAlignment(wrench, tool ? &*tool : nullptr, left, right, reset, a_dt);
			return;
		}

		// lock: the pick's tip (the same point its tip cube shows) marks the keyhole
		std::optional<Vec3> tip;
		if (a_in.pickHand.valid) {
			tip = _session->PickTool(a_in.pickHand.pose).pos;
		}
		if (auto result = _markup.Update(*_session, _anchor, tip ? &*tip : nullptr, mark, reset)) {
			const LockProfile p = ProfileFromMarks(result->centre, result->top, _visuals.LockPlacement(), _visuals.LockRoll());
			if (!p.valid || _visuals.LockModelPath().empty()) {
				logger::info("Markup lock: no lock model shown, nothing to save");
				RE::SendHUDMessage::ShowHUDMessage("Markup: no lock model to save it for.");
			} else {
				SaveLockProfile(p, _visuals.LockModelPath());
			}
			_markup.Prompt();
		}
	}

	// Lines the tool model up in the hand, the way you would by hand:
	//   left trigger held: the model stays where it is in the room while you move your hand (and
	//                      with it the tip cube); let go and it moves with the hand again, in the
	//                      new grip. Saved and logged on letting go.
	//   sticks (fine):     right stick swings it about its tip, left stick left / right rolls it;
	//                      right trigger held + right stick up / down sizes it.
	//   stick click:       back to the automatic fit.
	void LockpickManager::UpdateAlignment(bool a_wrench, const Pose* a_tool, const ButtonSnapshot& a_left, const ButtonSnapshot& a_right, bool a_reset, float a_dt)
	{
		if (!_visuals.HasTool(a_wrench)) {
			return;
		}
		if (a_reset) {
			EndGrab(false);
			_visuals.SetToolProfile(a_wrench, ToolProfile{}, false);
			_alignDirty = true;
			SaveAlignment();
			RE::SendHUDMessage::ShowHUDMessage("Back to the automatic fit.");
			return;
		}

		// grab: hysteresis so a half-pressed trigger does not flicker
		const bool wasGrabbing = _grab.has_value();
		const bool grabbing = a_tool && (wasGrabbing ? a_left.trigger > 0.3f : a_left.trigger > 0.8f);
		if (grabbing && !wasGrabbing) {
			_grab = Grab{ *a_tool, _visuals.ToolAlignment(a_wrench) };
		}
		if (grabbing) {
			_visuals.SetToolProfile(a_wrench, Regrip(_grab->profile, _grab->tool, *a_tool), false);
			_alignDirty = true;
			return;
		}
		if (wasGrabbing) {
			EndGrab(true);
			return;
		}

		auto dz = [](float a_v) { return std::fabs(a_v) < 0.2f ? 0.0f : (a_v - std::copysign(0.2f, a_v)) / 0.8f; };
		const float rx = dz(a_right.stickX), ry = dz(a_right.stickY), lx = dz(a_left.stickX);
		ToolProfile p = _visuals.ToolAlignment(a_wrench);
		bool        changed = false;
		if (a_right.trigger > 0.5f) {
			if (ry != 0.0f) {
				p.scale = std::clamp(p.scale * std::exp(ry * 0.4f * a_dt), 0.1f, 10.0f);
				changed = true;
			}
		} else if (rx != 0.0f || ry != 0.0f || lx != 0.0f) {
			// about the model's tip (wherever it has been moved), on the tool frame's axes
			const float rate = 30.0f * kDegToRad * a_dt;
			p.rot = AxisAngle({ 0, 0, 1 }, -rx * rate) * AxisAngle({ 1, 0, 0 }, ry * rate) * AxisAngle({ 0, 1, 0 }, lx * rate) * p.rot;
			changed = true;
		}
		if (changed) {
			p.valid = true;
			_visuals.SetToolProfile(a_wrench, p, false);
			_alignDirty = true;
			_alignIdle = 0.0f;
		} else if (_alignDirty) {
			_alignIdle += a_dt;
			if (_alignIdle >= 1.0f) {
				SaveAlignment();
			}
		}
	}

	void LockpickManager::EndGrab(bool a_save)
	{
		if (!_grab) {
			return;
		}
		_grab.reset();
		if (a_save) {
			SaveAlignment();
		}
	}

	void LockpickManager::SaveAlignment()
	{
		if (!_alignDirty || _markup.CurrentPart() == Markup::Part::kLock) {
			_alignDirty = false;
			return;
		}
		_alignDirty = false;
		const bool        wrench = _markup.CurrentPart() == Markup::Part::kWrench;
		const std::string path = _visuals.ToolModelPath(wrench);
		const std::string prefix = _visuals.ToolPrefix(wrench);
		const ToolProfile p = _visuals.ToolAlignment(wrench);
		if (path.empty() || !SaveProfilesText(WriteToolProfile(ReadText(kProfilesPath), path, prefix, p))) {
			return;
		}
		(prefix == "shiv" ? _shivProfiles : _pickProfiles)[NormalizeModelPath(path)] = p;
		// everything needed to make this the default: the model, the INI lines, and in words
		const Vec3  along = p.rot * Vec3{ 0, 1, 0 };
		const Vec3  side = p.rot * Vec3{ 1, 0, 0 };
		const float turned = std::acos(std::clamp(along.y, -1.0f, 1.0f)) * kRadToDeg;
		logger::info("Hold {} [{}] {} (model tip {:.2f} from the tip cube, its axis {:.0f} deg off the reach, side ({:+.2f}, {:+.2f}, {:+.2f}), size x{:.2f}, scale={:.2f})",
			wrench ? "wrench" : "pick", NormalizeModelPath(path), ToolProfileText(prefix, p), Length(p.offset), turned, side.x, side.y, side.z,
			p.scale, _cfg.visualScale);
		RE::SendHUDMessage::ShowHUDMessage(std::format("{} saved.", wrench ? "Wrench" : "Pick").c_str());
	}

	void LockpickManager::ResetMarkup()
	{
		if (_session) {
			RE::SendHUDMessage::ShowHUDMessage("Finish lockpicking first.");
			return;
		}
		// emptied rather than deleted: under Mod Organizer the file lives in its overwrite folder
		if (!SaveProfilesText("; VR Lockpicking model markup - reset from the menu\n")) {
			return;
		}
		_profiles.clear();
		_pickProfiles.clear();
		_shivProfiles.clear();
		logger::info("Model markup reset ({})", kProfilesPath);
		RE::SendHUDMessage::ShowHUDMessage("Model markup reset.");
	}

	bool LockpickManager::SaveProfilesText(const std::string& a_text)
	{
		std::ofstream out(kProfilesPath, std::ios::trunc);
		if (!out) {
			logger::warn("Could not write {}", kProfilesPath);
			RE::SendHUDMessage::ShowHUDMessage("Markup: could not save.");
			return false;
		}
		out << a_text;
		return true;
	}

	void LockpickManager::SaveLockProfile(const LockProfile& a_profile, const std::string& a_modelPath)
	{
		if (!SaveProfilesText(WriteLockProfile(ReadText(kProfilesPath), a_modelPath, a_profile))) {
			return;
		}
		_profiles[NormalizeModelPath(a_modelPath)] = a_profile;
		_visuals.SetLockProfile(a_profile);  // the model moves so its keyhole sits in the target
		logger::info("Markup saved for '{}': keyhole ({:.3f}, {:.3f}, {:.3f}) in model units, roll {:.1f} deg -> {}", a_modelPath,
			a_profile.keyhole.x, a_profile.keyhole.y, a_profile.keyhole.z, a_profile.roll, kProfilesPath);
		RE::SendHUDMessage::ShowHUDMessage("Markup saved for this lock model: its keyhole is now in the green target.");
	}

	// Keeps the player in place while picking: no walking, turning, sneaking, jumping or attacks.
	void LockpickManager::LockControls(bool a_lock)
	{
		auto* map = AddressCheck::Has(Ids::kControlMap) ? RE::ControlMap::GetSingleton() : nullptr;
		if (!map || a_lock == _controlsLocked) {
			return;
		}
		using UEFlag = RE::UserEvents::USER_EVENT_FLAG;
		constexpr auto kFlags = static_cast<std::uint32_t>(UEFlag::kMovement) | static_cast<std::uint32_t>(UEFlag::kLooking) |
		                        static_cast<std::uint32_t>(UEFlag::kJumping) | static_cast<std::uint32_t>(UEFlag::kSneaking) |
		                        static_cast<std::uint32_t>(UEFlag::kFighting) | static_cast<std::uint32_t>(UEFlag::kActivate) |
		                        static_cast<std::uint32_t>(UEFlag::kWheelZoom) | static_cast<std::uint32_t>(UEFlag::kPOVSwitch);
		auto& enabled = map->GetRuntimeData().enabledControls;
		if (a_lock) {
			_lockedControls = enabled.underlying() & kFlags;  // only give back what was on
			enabled.reset(static_cast<UEFlag>(_lockedControls));
			_controlsLocked = true;
		} else {
			enabled.set(static_cast<UEFlag>(_lockedControls));
			_controlsLocked = false;
		}
	}

	void LockpickManager::End()
	{
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			HideHands(player->GetVRNodeData(), false);
		}
		LockControls(false);
		PauseWorld(false);
		MuteGameRumble(false);
		_grab.reset();  // whatever it shows now is saved
		SaveAlignment();
		_visuals.Destroy();
		_debug.Destroy();
		_assist.Destroy();
		_counter.Destroy();
		_markup.Destroy();
		_session.reset();
		_target.reset();
		VRInput::Get().SetBlocking(false);
		_active.store(false);
	}

	void LockpickManager::Abort(const char* a_reason)
	{
		if (!_active.load() && !_session) {
			return;
		}
		logger::info("Session aborted: {}", a_reason);
		End();
	}
}
