#include "LockSession.h"

#include <cmath>

namespace vrlp
{
	LockFrame MakeLockFrame(const Pose& a_hmd, const Config& a_cfg)
	{
		LockFrame frame;
		Vec3      fwd = a_hmd.rot * Vec3{ 0.0f, 1.0f, 0.0f };
		fwd.z = 0.0f;
		frame.forward = Normalize(fwd, { 0.0f, 1.0f, 0.0f });
		frame.up = { 0.0f, 0.0f, 1.0f };
		frame.right = Normalize(Cross(frame.forward, frame.up), { 1.0f, 0.0f, 0.0f });
		frame.keyhole = a_hmd.pos + frame.forward * a_cfg.lockDistance + frame.right * a_cfg.lockSideOffset +
		                frame.up * a_cfg.lockHeightOffset;
		return frame;
	}

	LockParams ComputeLockParams(LockLevel a_level, float a_skill, float a_perkSweetSpotMult, bool a_unbreakable,
		const VanillaTuning& a_tuning, const Config& a_cfg, float a_random01)
	{
		const int idx = std::clamp(static_cast<int>(a_level), 0, 4);

		LockParams p;
		const float skillFactor = std::max(0.3f, a_tuning.sweetSkillBase + a_tuning.sweetSkillMult * a_skill);
		const float perk = a_perkSweetSpotMult > 0.0f ? a_perkSweetSpotMult : 1.0f;
		const float width = a_tuning.sweetSpotDeg[idx] * skillFactor * perk * a_cfg.sweetSpotMult;
		p.sweetHalfWidth = std::clamp(width * 0.5f, 1.5f, 85.0f);
		p.partialZone = std::max(0.0f, a_cfg.partialZone);

		const float breakSkill = std::max(0.1f, a_skill * a_tuning.breakSkillMult + 1.0f);
		p.breakSeconds = std::max(0.05f, a_tuning.breakSeconds[idx] * breakSkill * a_cfg.breakTimeMult);
		p.unbreakable = a_unbreakable;

		const float limit = std::max(0.0f, 88.0f - p.sweetHalfWidth);
		p.sweetCenter = -limit + 2.0f * limit * Clamp01(a_random01);
		return p;
	}

	LockSession::LockSession(const LockParams& a_params, const LockFrame& a_frame, const Config& a_cfg,
		const Mat3& a_pickHandRef, const Mat3& a_wrenchHandRef) :
		_params(a_params),
		_frame(a_frame),
		_cfg(a_cfg),
		_pickRef(a_pickHandRef),
		_wrenchRef(a_wrenchHandRef)
	{
		_events.reserve(8);
	}

	Pose LockSession::ToolPose(const Pose& a_hand, const Mat3& a_ref, const Vec3& a_localOffset, bool a_rightHand) const
	{
		Pose tool;
		if (_cfg.handGrip) {
			// Held in the fist: shaft along the fingers, flat side against the palm.
			const float mirror = a_rightHand ? 1.0f : -1.0f;
			const Vec3  pointing = Normalize({ _cfg.handPointing.x * mirror, _cfg.handPointing.y, _cfg.handPointing.z }, { 0, 0, 1 });
			Vec3        palm{ _cfg.handPalm.x * mirror, _cfg.handPalm.y, _cfg.handPalm.z };
			Vec3        toolUp = -(palm - pointing * Dot(palm, pointing));
			toolUp = Normalize(toolUp, Normalize(Cross(pointing, Vec3{ 1, 0, 0 }), { 0, 1, 0 }));
			const Vec3 toolRight = Cross(pointing, toolUp);
			// player correction, in tool space (x = pitch, y = roll, z = yaw); mirrored for the left hand
			const Vec3 adj = a_rightHand ? _cfg.gripRotation : Vec3{ _cfg.gripRotation.x, -_cfg.gripRotation.y, -_cfg.gripRotation.z };
			const Mat3 grip = Mat3::FromColumns(toolRight, pointing, toolUp) * EulerDegXYZ(adj);  // tool frame in hand space
			const Vec3 gripPos{ _cfg.handGripPos.x * mirror, _cfg.handGripPos.y, _cfg.handGripPos.z };
			tool.rot = a_hand.rot * grip;
			tool.pos = a_hand.TransformPoint(gripPos + a_localOffset) + tool.rot * Vec3{ 0.0f, _cfg.toolLength, 0.0f };
		} else {
			tool.rot = a_hand.rot * a_ref.Transposed() * _frame.Basis();
			tool.pos = a_hand.TransformPoint(a_localOffset) + tool.rot * Vec3{ 0.0f, _cfg.toolLength, 0.0f };
		}
		return tool;
	}

	Vec3 LockSession::GripPoint(const Pose& a_hand, const Vec3& a_localOffset, bool a_rightHand) const
	{
		if (_cfg.handGrip) {
			const float mirror = a_rightHand ? 1.0f : -1.0f;
			const Vec3  gripPos{ _cfg.handGripPos.x * mirror, _cfg.handGripPos.y, _cfg.handGripPos.z };
			return a_hand.TransformPoint(gripPos + a_localOffset);
		}
		return a_hand.TransformPoint(a_localOffset);
	}

	bool LockSession::OrbitAngle(const Vec3& a_point, const Vec3& a_spot, float& a_deg) const
	{
		const Vec3  h = a_point - a_spot;
		const float x = Dot(h, _frame.right) * Side();
		const float y = Dot(h, _frame.up);
		if (std::sqrt(x * x + y * y) < std::max(_cfg.leverMinRadius, 0.01f)) {
			return false;
		}
		a_deg = std::atan2(x, y) * kRadToDeg;
		return true;
	}

	float LockSession::ShownTilt(const Vec3& a_grip, const Vec3& a_spot) const
	{
		const Vec3  h = a_grip - a_spot;
		const float x = Dot(h, _frame.right);
		const float y = Dot(h, _frame.up);
		const float out = -Dot(h, _frame.forward);
		const float tilt = std::atan2(out, std::sqrt(x * x + y * y)) * kRadToDeg;
		return std::clamp(tilt, std::min(_cfg.toolMinTilt, _cfg.toolMaxTilt), std::max(_cfg.toolMinTilt, _cfg.toolMaxTilt));
	}

	Pose LockSession::LeverPose(const Vec3& a_spot, float a_angleDeg, float a_tiltDeg) const
	{
		const float a = a_angleDeg * kDegToRad;
		const float t = a_tiltDeg * kDegToRad;
		const Vec3  inPlane = _frame.right * (Side() * std::sin(a)) + _frame.up * std::cos(a);
		const Vec3  out = -_frame.forward;
		const Vec3  handle = inPlane * std::cos(t) + out * std::sin(t);  // from the tip towards the hand
		const Vec3  y = -handle;                                         // tool axis: towards the tip
		const Vec3  z = Normalize(out - y * Dot(out, y), inPlane);       // flat side faces the player
		Pose        p;
		p.rot = Mat3::FromColumns(Cross(y, z), y, z);
		p.pos = a_spot;
		return p;
	}

	Pose LockSession::AimedAt(const Pose& a_tool, const Vec3& a_grip, const Vec3& a_spot) const
	{
		constexpr float kMinAimTilt = 10.0f;  // deg
		Vec3            dir = Normalize(a_spot - a_grip, _frame.forward);
		const float     minIn = std::sin(kMinAimTilt * kDegToRad);
		if (Dot(dir, _frame.forward) < minIn) {
			const Vec3 side = Normalize(dir - _frame.forward * Dot(dir, _frame.forward), _frame.up);
			dir = side * std::cos(kMinAimTilt * kDegToRad) + _frame.forward * minIn;
		}
		const Vec3  axis = a_tool.rot * Vec3{ 0, 1, 0 };
		const Vec3  c = Cross(axis, dir);
		const float s = Length(c);
		Pose        p = a_tool;
		if (s > 1e-5f) {
			p.rot = AxisAngle(c, std::atan2(s, Dot(axis, dir))) * a_tool.rot;
		} else if (Dot(axis, dir) < 0.0f) {
			p.rot = AxisAngle(_frame.up, kPi) * a_tool.rot;
		}
		p.pos = a_spot;
		return p;
	}

	Pose LockSession::PickVisual(const Pose& a_hand) const
	{
		if (!_pickInserted) {
			return PickTool(a_hand);
		}
		if (!_cfg.leverControls) {
			return AimedAt(PickTool(a_hand), PickGrip(a_hand), _frame.PickPoint(_cfg));
		}
		// the handle round the keyhole towards the hand, also while the lock turns (the simulation
		// holds its angle then); with the hand on the keyhole axis, at the simulation's angle
		// (-90..90: top = 0 .. bottom = 180), carried round by the cylinder
		const Vec3 spot = _frame.PickPoint(_cfg);
		const Vec3 grip = PickGrip(a_hand);
		float      shown = 0.0f;
		if (!OrbitAngle(grip, spot, shown)) {
			shown = _pickAngle + 90.0f + _lockAngle * 90.0f;
		}
		return LeverPose(spot, shown, ShownTilt(grip, spot));
	}

	Pose LockSession::WrenchVisual(const Pose& a_hand) const
	{
		if (!_tensionEngaged) {
			return WrenchTool(a_hand);
		}
		const Vec3 spot = _frame.WrenchPoint(_cfg);
		// in from the hand that holds it...
		Pose p = AimedAt(WrenchTool(a_hand), WrenchGrip(a_hand), spot);
		if (!_cfg.leverControls) {
			return p;
		}
		// ...the blade's flat side facing where the cylinder has turned it (it stops and shakes
		// where the lock stops)
		const float a = (_wrenchRefAngle + _lockAngle * 90.0f) * kDegToRad;
		const Vec3  y = p.rot * Vec3{ 0, 1, 0 };
		const Vec3  blade = _frame.right * (Side() * std::sin(a)) + _frame.up * std::cos(a);
		const Vec3  z = Normalize(blade - y * Dot(blade, y), _frame.up);
		p.rot = Mat3::FromColumns(Cross(y, z), y, z);
		return p;
	}

	void LockSession::Retune(const Config& a_cfg)
	{
		const bool handGrip = _cfg.handGrip;
		const bool swap = _cfg.swapGripMirror;
		_cfg = a_cfg;
		_cfg.handGrip = handGrip;
		_cfg.swapGripMirror = swap;
	}

	LockSession::Pulse LockSession::SearchPulse(float a_pickAngle) const
	{
		// far: one setting; a sharp step to "near" within fPulseNearDegrees of the sweet spot's
		// edge, changing smoothly from there to "sweet" at the edge; "sweet" inside it
		const float d = std::fabs(a_pickAngle - _params.sweetCenter);
		const float edge = _params.sweetHalfWidth;
		const float near = std::max(_cfg.pulseNearDegrees, 0.01f);
		Pulse       p;
		if (d <= edge) {
			p = { _cfg.pulseSweetInterval, _cfg.pulseSweetStrength };
		} else if (d <= edge + near) {
			const float t = (d - edge) / near;  // 0 at the sweet spot's edge .. 1 at the near zone's edge
			p.interval = _cfg.pulseSweetInterval + (_cfg.pulseNearInterval - _cfg.pulseSweetInterval) * t;
			p.strength = _cfg.pulseSweetStrength + (_cfg.pulseNearStrength - _cfg.pulseSweetStrength) * t;
		} else {
			p = { _cfg.pulseSlowInterval, _cfg.pulseFarStrength };
		}
		p.interval = std::max(p.interval, 0.05f);
		p.strength = Clamp01(p.strength);
		return p;
	}

	float LockSession::Proximity(float a_pickAngle) const
	{
		const float d = std::fabs(a_pickAngle - _params.sweetCenter);
		if (d <= _params.sweetHalfWidth) {
			return 1.0f;
		}
		const float range = std::max(_params.partialZone, 1.0f) * 2.0f;  // feel it a bit before the lock would turn
		return Clamp01(1.0f - (d - _params.sweetHalfWidth) / range);
	}

	float LockSession::MaxTurnFor(float a_pickAngle) const
	{
		constexpr float kJiggle = 0.04f;  // even a wrong angle lets the cylinder wiggle a bit
		const float     d = std::fabs(a_pickAngle - _params.sweetCenter);
		if (d <= _params.sweetHalfWidth) {
			return 1.0f;
		}
		if (_params.partialZone <= 0.0f) {
			return kJiggle;
		}
		const float t = 1.0f - (d - _params.sweetHalfWidth) / _params.partialZone;
		return std::max(kJiggle, t);
	}

	float LockSession::MeasurePickAngle(const Mat3& a_handRot) const
	{
		// world-space rotation since the start pose; its twist around the keyhole axis is the
		// wrist roll (+ = clockwise as seen by the player).
		const Mat3 delta = a_handRot * _pickRef.Transposed();
		return TwistAngle(delta, _frame.forward) * kRadToDeg;
	}

	const std::vector<SessionEvent>& LockSession::Update(const FrameInput& a_in)
	{
		_events.clear();
		_haptics = {};
		if (_finished) {
			return _events;
		}

		const bool cancel = (a_in.pickHand.valid && a_in.pickHand.cancel) ||
		                    (a_in.tensionHand.valid && a_in.tensionHand.cancel);
		if (cancel && !_prevCancel) {
			_prevCancel = cancel;
			_finished = true;
			Emit(SessionEvent::kCancelled);
			return _events;
		}
		_prevCancel = cancel;

		Vec3 away = a_in.hmd.pos - _frame.keyhole;
		away.z = 0.0f;
		if (Length(away) > _cfg.cancelDistance) {
			_finished = true;
			Emit(SessionEvent::kCancelled);
			return _events;
		}

		if (_breakCooldown > 0.0f) {
			_breakCooldown -= a_in.dt;
		}

		UpdatePick(a_in);
		UpdateTension(a_in);
		UpdateLock(a_in);
		_haptics.pickHand = std::max(_haptics.pickHand, _pickBuzz.Tick(a_in.dt));
		_haptics.tensionHand = std::max(_haptics.tensionHand, _wrenchBuzz.Tick(a_in.dt));

		_haptics.pickHand = Clamp01(_haptics.pickHand * _cfg.hapticStrength);
		_haptics.tensionHand = Clamp01(_haptics.tensionHand * _cfg.hapticStrength);
		return _events;
	}

	ToolPlace LockSession::Classify(const Vec3& a_tip, const Vec3& a_spot, float a_radius, bool a_inserted) const
	{
		// A short cylinder around the keyhole axis: inside the target circle sideways, and in
		// depth between a little in front of the keyhole and the maximum depth. Once in, a tool
		// may stray further (holdRadius sideways, holdDepth back out and further in), so hand jitter
		// doesn't pull it out. Sideways is the wider one: the hand can't see where in the keyhole the
		// tip is, but pulling straight back still takes the tool out.
		const Vec3  d = a_tip - a_spot;
		const float depth = Dot(d, _frame.forward);
		const float side = Length(d - _frame.forward * depth);
		const float radius = a_inserted ? std::max(_cfg.holdRadius, a_radius) : a_radius;
		// ...and pushed further in (the drawn tool stays in the keyhole, so the hand can't see it)
		const float slack = a_inserted ? _cfg.holdDepth : 0.0f;
		if (side > radius || depth < -(_cfg.insertFront + slack)) {
			return ToolPlace::kOut;
		}
		return depth > _cfg.maxDepth + slack ? ToolPlace::kTooDeep : ToolPlace::kIn;
	}

	float LockSession::BladeAngle(const Pose& a_hand) const
	{
		// direction the back of the wrench hand points, around the keyhole (0 = up, 90 = right,
		// -90 = left): the tool's +Z is the back of the hand (fist grip) or "up" at the start pose
		const Vec3  back = WrenchTool(a_hand).rot * Vec3{ 0, 0, 1 };
		const float x = Dot(back, _frame.right) * Side();
		const float y = Dot(back, _frame.up);
		return std::atan2(x, y) * kRadToDeg;
	}

	void LockSession::UpdatePick(const FrameInput& a_in)
	{
		if (!a_in.pickHand.valid) {
			_pickPlace = ToolPlace::kOut;
			return;
		}

		const Vec3 spot = _frame.PickPoint(_cfg);
		const Pose tool = PickTool(a_in.pickHand.pose);
		_pickPlace = Classify(tool.pos, spot, _cfg.insertRadius, _pickInserted);
		if (_pickPlace != ToolPlace::kIn) {
			_pickNeedsReinsert = false;  // taken out: a new pick can go in
		}
		float orbit = 0.0f;
		const bool orbitValid = OrbitAngle(PickGrip(a_in.pickHand.pose), spot, orbit);
		if (orbit < -90.0f) {
			orbit += 360.0f;  // -90..270, so straight down (+-180) is one end of the right half
		}

		if (!_pickInserted) {
			// lever controls: the handle must be on the right half (the pick's range) to go in
			const bool rightHalf = !_cfg.leverControls || !orbitValid ||
			                       (orbit >= -_cfg.pickEngageMargin && orbit <= 180.0f + _cfg.pickEngageMargin);
			if (_breakCooldown <= 0.0f && !_pickNeedsReinsert && _pickPlace == ToolPlace::kIn && rightHalf) {
				_pickInserted = true;
				_pickAngleValid = false;
				Emit(SessionEvent::kPickInserted);
				_haptics.pickHand = std::max(_haptics.pickHand, 0.5f);
			}
		} else if (_pickPlace != ToolPlace::kIn) {
			_pickInserted = false;
			Emit(SessionEvent::kPickRemoved);
			_haptics.pickHand = std::max(_haptics.pickHand, 0.25f);
			return;
		}

		if (!_pickInserted) {
			return;
		}

		float raw = _pickAngleValid ? _pickAngle : -90.0f;
		if (_cfg.leverControls) {
			// The handle goes round the right side, top (0) to bottom (180): the simulation's
			// -90..90. Held by the cylinder while it is under tension.
			if (!(_pickAngleValid && _turnInput > 0.05f) && orbitValid) {
				raw = std::clamp(orbit, -_cfg.pickEngageMargin, 180.0f + _cfg.pickEngageMargin) - 90.0f;
			}
		} else {
			raw = MeasurePickAngle(a_in.pickHand.pose.rot);
		}
		const float clamped = std::clamp(raw, -90.0f, 90.0f);
		if (!_pickAngleValid) {
			_pickFilter.Reset();
			_pickAngle = _pickFilter.Filter(clamped, a_in.dt, 0.0f, 0.0f);
			_lastTickAngle = clamped;
			_pickAngleValid = true;
			if (!_startPlaced && _params.startArc > 0.0f) {
				// Locksmith: the sweet spot within the starting arc of where the pick went in (on
				// either side, never right under it), kept inside the pick's range
				_startPlaced = true;
				const float arc = std::min(_params.startArc, 90.0f);
				const float u = Clamp01(_params.startRandom01) * 2.0f - 1.0f;  // -1 .. 1
				const float off = (0.25f + 0.75f * std::fabs(u)) * arc * (u < 0.0f ? -1.0f : 1.0f);
				const float limit = std::max(0.0f, 88.0f - _params.sweetHalfWidth);
				float centre = clamped + off;
				if (centre > limit || centre < -limit) {
					centre = clamped - off;  // the other side when that one is off the range
				}
				_params.sweetCenter = std::clamp(centre, -limit, limit);
			}
		} else {
			// shaky hands held still are smoothed hard, a sweep barely lags
			_pickAngle = _pickFilter.Filter(clamped, a_in.dt, _cfg.pickFilterMinCutoff, _cfg.pickFilterBeta);
		}

		if (std::fabs(_pickAngle - _lastTickAngle) >= std::max(_cfg.hapticTickDegrees, 2.0f)) {
			_lastTickAngle = _pickAngle;
			Emit(SessionEvent::kPickTick);  // scrape sound
			if (_cfg.hapticTickDegrees > 0.0f && _tensionEngaged) {
				_haptics.pickHand = std::max(_haptics.pickHand, _cfg.pulseFarStrength * 0.5f);
			}
		}

		// "searching" pulses, only once both tools are in, and not while the strain rattle plays
		if (!_tensionEngaged || _straining) {
			_pickPulseTimer = 0.0f;
			return;
		}
		_pickPulseTimer -= a_in.dt;
		if (_pickPulseTimer <= 0.0f) {
			const Pulse p = SearchPulse(_pickAngle);
			_pickPulseTimer = p.interval;
			_pickBuzz.Start(_cfg.pulseLength, p.strength);
		}
	}

	void LockSession::UpdateTension(const FrameInput& a_in)
	{
		const HandInput& hand = a_in.tensionHand;
		if (!hand.valid) {
			_wrenchPlace = ToolPlace::kOut;
			if (_tensionEngaged) {
				_tensionEngaged = false;
				Emit(SessionEvent::kTensionReleased);
			}
			_turnInput = 0.0f;
			return;
		}

		const Vec3 spot = _frame.WrenchPoint(_cfg);
		_wrenchPlace = Classify(WrenchTool(hand.pose).pos, spot, _cfg.wrenchInsertRadius, _tensionEngaged);
		// lever controls: the blade turns like a key; back of the hand left = 0, up = full turn
		float blade = 0.0f;
		if (_cfg.leverControls) {
			blade = BladeAngle(hand.pose) - _cfg.wrenchStartAngle;
			while (blade > 180.0f) {
				blade -= 360.0f;
			}
			while (blade <= -180.0f) {
				blade += 360.0f;
			}
		}

		if (!_tensionEngaged) {
			// lever controls: only at the start position, so the lock can't jump to the hand's angle
			const bool atStart = !_cfg.leverControls || std::fabs(blade) <= _cfg.wrenchStartTolerance;
			if (_wrenchPlace == ToolPlace::kIn && atStart) {
				_tensionEngaged = true;
				_tensionRefRot = hand.pose.rot;
				_wrenchRefAngle = _cfg.wrenchStartAngle;
				Emit(SessionEvent::kTensionGrabbed);
				_haptics.tensionHand = std::max(_haptics.tensionHand, 0.4f);
			}
		} else if (_wrenchPlace != ToolPlace::kIn) {
			_tensionEngaged = false;
			Emit(SessionEvent::kTensionReleased);
		}

		if (!_tensionEngaged) {
			_turnInput = 0.0f;
			return;
		}

		float turn = 0.0f;
		if (_cfg.leverControls) {
			turn = std::clamp(blade / _cfg.fullTurnTwist, 0.0f, 1.0f + _cfg.forceSlack * 2.0f);
		} else {
			const Mat3  delta = hand.pose.rot * _tensionRefRot.Transposed();
			const float twist = TwistAngle(delta, _frame.forward) * kRadToDeg;  // + = clockwise as seen by player
			turn = Clamp01(twist / _cfg.fullTurnTwist);
		}
		// after a broken pick the blade turns nothing until it is back at its start
		if (_wrenchNeedsReset) {
			if (turn > 0.05f) {
				turn = 0.0f;
			} else {
				_wrenchNeedsReset = false;
			}
		}
		_turnInput = turn;
	}

	void LockSession::UpdateLock(const FrameInput& a_in)
	{
		// nothing turns (or strains) until both tools are in the keyhole
		if (!(_pickInserted && _tensionEngaged)) {
			_turnInput = 0.0f;
		}
		_allowed = _pickInserted ? MaxTurnFor(_pickAngle) : 0.0f;
		const float target = std::min(_turnInput, _allowed);
		const float prev = _lockAngle;
		const float speed = target > _lockAngle ? _cfg.lockTurnSpeed : _cfg.lockReturnSpeed;
		_lockAngle = Approach(_lockAngle, target, speed * a_in.dt);

		const bool moving = _lockAngle > prev + 1e-4f;
		if (moving && !_lockMoving) {
			Emit(SessionEvent::kLockStartTurn);
		}
		_lockMoving = moving;
		if (moving) {
			_haptics.tensionHand = std::max(_haptics.tensionHand, 0.10f);
		}

		const bool atStop = _pickInserted && _turnInput > _allowed + 0.02f && _lockAngle >= _allowed - 0.01f;
		if (atStop && !_atStop) {
			Emit(SessionEvent::kLockStop);
			_haptics.tensionHand = std::max(_haptics.tensionHand, 0.6f);
		}
		_atStop = atStop;

		const bool forcing = _pickInserted && _allowed < 0.999f && _turnInput > _allowed + _cfg.forceSlack &&
		                     _lockAngle >= _allowed - 0.01f;
		if (forcing) {
			_pickDamage += a_in.dt;
			// strain: a steady rattle (fStrainInterval, fStrainStrength) that replaces every
			// other pulse, so you can ease off before the pick snaps
			_straining = true;
			_strainPulseTimer -= a_in.dt;
			if (_strainPulseTimer <= 0.0f) {
				_strainPulseTimer = std::max(_cfg.strainInterval, 0.02f);
				const float strength = Clamp01(_cfg.strainStrength);
				_pickBuzz.Force(_cfg.pulseLength * 1.5f, strength);
				_wrenchBuzz.Force(_cfg.pulseLength * 1.5f, strength);
			}
			_forceEventTimer -= a_in.dt;
			if (_forceEventTimer <= 0.0f) {
				Emit(SessionEvent::kForcing);
				_forceEventTimer = 0.45f;
			}
			if (!_params.unbreakable && _pickDamage >= _params.breakSeconds) {
				Emit(SessionEvent::kPickBroken);
				_haptics.pickHand = 1.0f;
				_haptics.tensionHand = std::max(_haptics.tensionHand, 0.5f);
				_pickInserted = false;
				_pickDamage = 0.0f;
				_lockAngle = 0.0f;
				_lockMoving = false;
				_atStop = false;
				_breakCooldown = 0.6f;
				_forceEventTimer = 0.0f;
				// safeguard: a new pick has to be put in (taken out of the keyhole first), and the
				// blade has to go back to its start before it can turn again
				_pickNeedsReinsert = true;
				_wrenchNeedsReset = true;
				_straining = false;
				_turnInput = 0.0f;
				return;
			}
		} else {
			_forceEventTimer = 0.0f;
			_strainPulseTimer = 0.0f;
			_straining = false;
		}

		if (_allowed >= 0.999f && _lockAngle >= 0.999f) {
			_finished = true;
			Emit(SessionEvent::kUnlocked);
			_haptics.pickHand = std::max(_haptics.pickHand, 0.8f);
			_haptics.tensionHand = std::max(_haptics.tensionHand, 0.8f);
		}
	}
}
