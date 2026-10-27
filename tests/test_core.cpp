// Native (Linux/Windows) unit tests for the game-independent lockpicking core.
// Build: see tests/CMakeLists.txt or run `cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests`.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "Config.h"
#include "LockSession.h"
#include "LockProfile.h"
#include "LockVariations.h"
#include "MeshFit.h"
#include "PluginScan.h"
#include "Translations.h"
#include "VecMath.h"

using namespace vrlp;

namespace
{
	int g_failures = 0;
	int g_checks = 0;

#define CHECK(cond)                                                              \
	do {                                                                         \
		++g_checks;                                                              \
		if (!(cond)) {                                                           \
			++g_failures;                                                        \
			std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
		}                                                                        \
	} while (0)

#define CHECK_NEAR(a, b, eps)                                                                        \
	do {                                                                                             \
		++g_checks;                                                                                  \
		const double _a = (a), _b = (b);                                                             \
		if (!(std::fabs(_a - _b) <= (eps))) {                                                        \
			++g_failures;                                                                            \
			std::printf("  FAILED %s:%d: %s = %f, expected %f\n", __FILE__, __LINE__, #a, _a, _b);   \
		}                                                                                            \
	} while (0)

	struct TestCase
	{
		const char*           name;
		std::function<void()> fn;
	};
	std::vector<TestCase>& Registry()
	{
		static std::vector<TestCase> r;
		return r;
	}
	struct Registrar
	{
		Registrar(const char* n, std::function<void()> f) { Registry().push_back({ n, std::move(f) }); }
	};
#define TEST(name)                                    \
	static void        name();                        \
	static Registrar   reg_##name(#name, name);       \
	static void        name()

	// Most tests drive the tools through the "start pose" attachment (tool points into the lock
	// at the start orientation) so they can place tool tips exactly; the fist grip has its own test.
	Config TestCfg()
	{
		Config c;
		c.handGrip = false;
		c.leverControls = false;  // wrist controls; lever controls have their own tests
		c.fullTurnTwist = 50.0f;
		return c;
	}

	// Player standing at origin, looking north (+Y), head 120 units up.
	Pose Hmd()
	{
		Pose p;
		p.pos = { 0, 0, 120 };
		return p;
	}

	// Controller pose whose pick tip sits exactly in the keyhole, rolled by a_rollDeg
	// (clockwise as seen by player) around the keyhole axis.
	// Hand poses whose tool tips sit in the keyhole. Sessions use f.Basis() as the start
	// orientation of both hands, so the tool orientation equals the hand orientation.
	Pose PickPoseAt(const LockFrame& f, const Config& c, float a_rollDeg, const Vec3& a_extraOffset = {})
	{
		Pose p;
		p.rot = AxisAngle(f.forward, a_rollDeg * kDegToRad) * f.Basis();
		p.pos = f.PickPoint(c) - p.rot * Vec3{ 0, c.toolLength, 0 } + a_extraOffset;
		return p;
	}

	Pose TensionPose(const LockFrame& f, const Config& c, float a_twistDeg)
	{
		Pose p;
		p.rot = AxisAngle(f.forward, a_twistDeg * kDegToRad) * f.Basis();
		p.pos = f.WrenchPoint(c) - p.rot * Vec3{ 0, c.toolLength, 0 };
		return p;
	}

	LockParams SimpleParams(float center, float half)
	{
		LockParams p;
		p.sweetCenter = center;
		p.sweetHalfWidth = half;
		p.partialZone = 30.0f;
		p.breakSeconds = 1.0f;
		return p;
	}

	bool Has(const std::vector<SessionEvent>& ev, SessionEvent e)
	{
		for (auto x : ev) {
			if (x == e) {
				return true;
			}
		}
		return false;
	}

	FrameInput MakeInput(const LockFrame& f, const Config& c, float pickRoll, float twist, bool grip, float dt = 1.0f / 90.0f)
	{
		FrameInput in;
		in.dt = dt;
		in.hmd = Hmd();
		in.pickHand.valid = true;
		in.pickHand.pose = PickPoseAt(f, c, pickRoll);
		in.tensionHand.valid = true;
		in.tensionHand.pose = TensionPose(f, c, twist);
		in.tensionHand.grip = grip;
		return in;
	}
}

TEST(TwistAngleIsolatesAxis)
{
	const Vec3 axis = Normalize({ 0.3f, 1.0f, 0.1f });
	for (float deg : { -120.0f, -45.0f, 0.0f, 10.0f, 30.0f, 89.0f, 170.0f }) {
		const Mat3 pureTwist = AxisAngle(axis, deg * kDegToRad);
		CHECK_NEAR(TwistAngle(pureTwist, axis) * kRadToDeg, deg, 0.01);
		// add a swing around a perpendicular axis - twist must be preserved
		const Vec3 perp = Normalize(Cross(axis, { 0, 0, 1 }));
		const Mat3 withSwing = AxisAngle(perp, 20.0f * kDegToRad) * pureTwist;
		CHECK_NEAR(TwistAngle(withSwing, axis) * kRadToDeg, deg, 4.0);
	}
}

TEST(QuaternionRoundTripAllBranches)
{
	// rotations near 180 deg exercise the non-trace branches
	for (const Vec3 axis : { Vec3{ 1, 0, 0 }, Vec3{ 0, 1, 0 }, Vec3{ 0, 0, 1 } }) {
		const Mat3 r = AxisAngle(axis, 179.0f * kDegToRad);
		CHECK_NEAR(std::fabs(TwistAngle(r, axis) * kRadToDeg), 179.0, 0.05);
	}
}

TEST(LockFrameFacesPlayer)
{
	Config c = TestCfg();
	Pose   h = Hmd();
	h.rot = AxisAngle({ 0, 0, 1 }, 90.0f * kDegToRad) * AxisAngle({ 1, 0, 0 }, -30.0f * kDegToRad);  // looking west, pitched down
	const LockFrame f = MakeLockFrame(h, c);
	CHECK_NEAR(f.forward.x, -1.0, 1e-4);
	CHECK_NEAR(f.forward.z, 0.0, 1e-6);
	CHECK_NEAR(f.up.z, 1.0, 1e-6);
	// right of a player facing west is north
	CHECK_NEAR(f.right.y, 1.0, 1e-4);
	CHECK_NEAR(f.keyhole.x, -c.lockDistance, 1e-3);
	CHECK_NEAR(f.keyhole.z, 120.0f + c.lockHeightOffset, 1e-3);
}

TEST(PickAngleFromRoll)
{
	Config          c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(0, 10), f, c, f.Basis(), f.Basis());
	CHECK_NEAR(s.MeasurePickAngle(f.Basis()), 0.0, 1e-3);
	CHECK_NEAR(s.MeasurePickAngle(AxisAngle(f.forward, 40.0f * kDegToRad) * f.Basis()), 40.0, 1e-2);
	CHECK_NEAR(s.MeasurePickAngle(AxisAngle(f.forward, -75.0f * kDegToRad) * f.Basis()), -75.0, 1e-2);
	// tilting the hand (pitch) barely changes the roll reading
	const Mat3 pitched = AxisAngle(f.right, 25.0f * kDegToRad) * AxisAngle(f.forward, 30.0f * kDegToRad) * f.Basis();
	CHECK_NEAR(s.MeasurePickAngle(pitched), 30.0, 3.0);
	// any controller axis convention works: reference is whatever the hand was at start
	const Mat3  weirdRef = AxisAngle(Normalize({ 0.3f, -0.5f, 0.8f }), 1.7f);
	LockSession s2(SimpleParams(0, 10), f, c, weirdRef, f.Basis());
	CHECK_NEAR(s2.MeasurePickAngle(AxisAngle(f.forward, -50.0f * kDegToRad) * weirdRef), -50.0, 1e-2);
}

TEST(MaxTurnProfile)
{
	Config          c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(20, 5), f, c, f.Basis(), f.Basis());
	CHECK_NEAR(s.MaxTurnFor(20), 1.0, 1e-6);
	CHECK_NEAR(s.MaxTurnFor(24.9f), 1.0, 1e-6);
	CHECK_NEAR(s.MaxTurnFor(40), 0.5, 1e-4);   // 15 deg into a 30 deg partial zone
	CHECK_NEAR(s.MaxTurnFor(-60), 0.04, 1e-6);  // far away: only a wiggle
}

TEST(UnlockAtSweetSpot)
{
	Config          c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(25, 6), f, c, f.Basis(), f.Basis());

	bool inserted = false, grabbed = false, unlocked = false;
	for (int i = 0; i < 400 && !s.Finished(); ++i) {
		const float twist = i < 10 ? 0.0f : 70.0f;
		auto&       ev = s.Update(MakeInput(f, c, 25.0f, twist, true));
		inserted |= Has(ev, SessionEvent::kPickInserted);
		grabbed |= Has(ev, SessionEvent::kTensionGrabbed);
		unlocked |= Has(ev, SessionEvent::kUnlocked);
		CHECK(!Has(ev, SessionEvent::kPickBroken));
	}
	CHECK(inserted);
	CHECK(grabbed);
	CHECK(unlocked);
	CHECK(s.Finished());
}

TEST(PartialAngleTurnsPartiallyWithoutBreakingInSlack)
{
	Config c = TestCfg();
	c.forceSlack = 0.5f;
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(0, 5), f, c, f.Basis(), f.Basis());
	for (int i = 0; i < 300; ++i) {
		// pick at 20 deg -> 15 deg into partial zone -> allowed 0.5; twist 0.8 of full
		auto& ev = s.Update(MakeInput(f, c, 20.0f, i < 5 ? 0.0f : 0.8f * c.fullTurnTwist, true));
		CHECK(!Has(ev, SessionEvent::kUnlocked));
		CHECK(!Has(ev, SessionEvent::kPickBroken));
	}
	CHECK_NEAR(s.LockAngle(), 0.5, 0.01);
	CHECK(s.PickDamage() == 0.0f);
}

TEST(ForcingBreaksPick)
{
	Config          c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(-60, 3), f, c, f.Basis(), f.Basis());
	int             broken = 0;
	int             frames = 0;
	for (; frames < 500 && broken == 0; ++frames) {
		auto& ev = s.Update(MakeInput(f, c, 60.0f, frames < 5 ? 0.0f : 90.0f, true));
		if (Has(ev, SessionEvent::kPickBroken)) {
			++broken;
		}
	}
	CHECK(broken == 1);
	// ~1 s of forcing at 90 fps, plus a few frames to insert / ramp up
	CHECK(frames > 85 && frames < 110);
	CHECK(!s.PickInserted());
	CHECK_NEAR(s.LockAngle(), 0.0, 1e-6);

	// safeguard: staying where you are (blade still turned) doesn't put a new pick in...
	bool reinserted = false;
	for (int i = 0; i < 120; ++i) {
		reinserted |= Has(s.Update(MakeInput(f, c, 60.0f, 90.0f, true)), SessionEvent::kPickInserted);
	}
	CHECK(!reinserted);
	CHECK(s.PickNeedsReinsert());
	// ...a new pick goes in after taking the old one out
	FrameInput out = MakeInput(f, c, 60.0f, 90.0f, true);
	out.pickHand.pose.pos = out.pickHand.pose.pos - f.forward * 5.0f;
	s.Update(out);
	for (int i = 0; i < 5; ++i) reinserted |= Has(s.Update(MakeInput(f, c, 60.0f, 90.0f, true)), SessionEvent::kPickInserted);
	CHECK(reinserted);
	// but the blade turns nothing (no strain) until it went back to its start
	for (int i = 0; i < 60; ++i) s.Update(MakeInput(f, c, 60.0f, 90.0f, true));
	CHECK(s.WrenchNeedsReset());
	CHECK(s.TurnInput() == 0.0f);
	CHECK(s.PickDamage() == 0.0f);
	for (int i = 0; i < 5; ++i) s.Update(MakeInput(f, c, 60.0f, 0.0f, true));
	CHECK(!s.WrenchNeedsReset());
	for (int i = 0; i < 30; ++i) s.Update(MakeInput(f, c, 60.0f, 90.0f, true));
	CHECK(s.PickDamage() > 0.0f);
}

TEST(StrainVibrationIsSteadyAndReplacesTheRest)
{
	Config c = TestCfg();
	c.strainStrength = 0.9f;
	c.pulseFarStrength = c.pulseNearStrength = c.pulseSweetStrength = 0.3f;
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(-60, 3), f, c, f.Basis(), f.Basis());
	float early = 0.0f, late = 0.0f, weakest = 1.0f;
	int   frames = 0;
	for (int i = 0; i < 200; ++i) {
		if (Has(s.Update(MakeInput(f, c, 60.0f, i < 5 ? 0.0f : 90.0f, true)), SessionEvent::kPickBroken)) {
			break;
		}
		const float wear = s.PickDamage() / s.Params().breakSeconds;
		const float h = s.Haptics().pickHand;
		if (wear > 0.0f && wear < 0.2f) early = std::max(early, h);
		if (wear > 0.8f) late = std::max(late, h);
		if (wear > 0.05f && h > 0.0f) {
			weakest = std::min(weakest, h);  // no weaker searching pulse in between
			++frames;
		}
	}
	CHECK(frames > 0);
	CHECK_NEAR(early, c.strainStrength * c.hapticStrength, 1e-4);
	CHECK_NEAR(late, early, 1e-4);
	CHECK(weakest >= c.strainStrength * c.hapticStrength - 1e-4f);
}

TEST(TipInFrontOfTheKeyholeIsOut)
{
	Config          c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(0, 10), f, c, f.Basis(), f.Basis());
	CHECK(s.Classify(f.PickPoint(c) - f.forward * 0.3f, f.PickPoint(c), c.insertRadius, false) == ToolPlace::kOut);
	CHECK(s.Classify(f.PickPoint(c) + f.forward * 0.3f, f.PickPoint(c), c.insertRadius, false) == ToolPlace::kIn);
	CHECK(s.Classify(f.PickPoint(c) + f.forward * (c.maxDepth + 0.5f), f.PickPoint(c), c.insertRadius, false) == ToolPlace::kTooDeep);
	CHECK(s.Classify(f.PickPoint(c) + f.forward * 0.3f + f.right * (c.insertRadius + 0.2f), f.PickPoint(c), c.insertRadius, false) == ToolPlace::kOut);
}

TEST(RotationBlendGoesPartWay)
{
	const Mat3 a = AxisAngle({ 1, 0, 0 }, 0.3f);
	const Mat3 b = a * AxisAngle(Normalize({ 0.2f, 1.0f, -0.5f }), 1.4f);
	const Mat3 half = RotationBlend(a, b, 0.5f);
	const Mat3 expected = a * AxisAngle(Normalize({ 0.2f, 1.0f, -0.5f }), 0.7f);
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			CHECK_NEAR(half.m[i][j], expected.m[i][j], 1e-4);
		}
	}
	const Mat3 end = RotationBlend(a, b, 1.0f);
	const Mat3 start = RotationBlend(a, b, 0.0f);
	CHECK_NEAR(end.m[1][2], b.m[1][2], 1e-6);
	CHECK_NEAR(start.m[1][2], a.m[1][2], 1e-4);
}

TEST(OneEuroFilterSmoothsStillHandsNotSweeps)
{
	const float dt = 1.0f / 90.0f;
	// a hand held still at 10 deg with +-2 deg of jitter
	OneEuroFilter still;
	float         maxDev = 0.0f;
	for (int i = 0; i < 270; ++i) {
		const float x = 10.0f + ((i % 2) ? 2.0f : -2.0f);
		const float y = still.Filter(x, dt, 1.0f, 0.05f);
		if (i > 90) {
			maxDev = std::max(maxDev, std::fabs(y - 10.0f));
		}
	}
	CHECK(maxDev < 0.5f);  // the jitter is mostly gone
	// a sweep at 120 deg/s: stays close behind
	OneEuroFilter sweep;
	float         lag = 0.0f;
	for (int i = 0; i < 90; ++i) {
		const float x = 120.0f * dt * static_cast<float>(i);
		lag = x - sweep.Filter(x, dt, 1.0f, 0.05f);
	}
	CHECK(lag < 8.0f);
	// off: passes straight through
	OneEuroFilter off;
	off.Filter(0.0f, dt, 0.0f, 0.0f);
	CHECK_NEAR(off.Filter(5.0f, dt, 0.0f, 0.0f), 5.0, 1e-6);
}

TEST(InsertedToolsHoldWithShakyHands)
{
	Config c = TestCfg();
	c.insertRadius = 3.0f;
	c.holdRadius = 8.0f;
	c.holdBack = 5.0f;
	c.holdDepth = 6.0f;
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(0, 10), f, c, f.Basis(), f.Basis());
	const Vec3      spot = f.PickPoint(c);
	const Vec3      in = spot + f.forward * 0.5f;
	// going in takes aiming at the small target...
	CHECK(s.Classify(in + f.right * 5.0f, spot, c.insertRadius, false) == ToolPlace::kOut);
	CHECK(s.Classify(in + f.right * 2.5f, spot, c.insertRadius, false) == ToolPlace::kIn);
	// ...once in, it stays in out to the hold radius sideways, and a little back out
	CHECK(s.Classify(in + f.right * 5.0f, spot, c.insertRadius, true) == ToolPlace::kIn);
	CHECK(s.Classify(in + f.up * -7.5f, spot, c.insertRadius, true) == ToolPlace::kIn);
	CHECK(s.Classify(spot - f.forward * 3.0f, spot, c.insertRadius, true) == ToolPlace::kIn);
	CHECK(s.Classify(in + f.right * 8.5f, spot, c.insertRadius, true) == ToolPlace::kOut);
	CHECK(s.Classify(spot - f.forward * 6.0f, spot, c.insertRadius, true) == ToolPlace::kOut);
	// ...and further in: pushing the hand towards the lock doesn't pull it out
	const float slack = c.holdDepth;
	CHECK(s.Classify(spot + f.forward * (c.maxDepth + 0.5f), spot, c.insertRadius, true) == ToolPlace::kIn);
	CHECK(s.Classify(spot + f.forward * (c.maxDepth + slack - 0.5f), spot, c.insertRadius, true) == ToolPlace::kIn);
	// pushed right through is still pushed through
	CHECK(s.Classify(spot + f.forward * (c.maxDepth + slack + 0.5f), spot, c.insertRadius, true) == ToolPlace::kTooDeep);
	// a hold radius smaller than the target is raised to it
	Config d;
	d.LoadFromIni("[Controls]\nfInsertRadius = 9\n");
	CHECK(d.holdRadius >= 9.0f);
	// sideways it holds further than back out: pulling straight back takes it out
	const Config e;
	CHECK(e.holdRadius > e.holdBack + e.insertRadius);
	CHECK(e.holdDepth > e.holdBack);
}

TEST(UnbreakableNeverBreaks)
{
	Config          c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	auto            p = SimpleParams(-60, 3);
	p.unbreakable = true;
	LockSession s(p, f, c, f.Basis(), f.Basis());
	for (int i = 0; i < 2000; ++i) {
		CHECK(!Has(s.Update(MakeInput(f, c, 60.0f, i < 5 ? 0.0f : 90.0f, true)), SessionEvent::kPickBroken));
	}
}

TEST(NoTurnWithoutPick)
{
	Config          c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(0, 40), f, c, f.Basis(), f.Basis());
	for (int i = 0; i < 200; ++i) {
		FrameInput in = MakeInput(f, c, 0.0f, 90.0f, true);
		in.pickHand.pose.pos += Vec3{ 0, -30, 0 };  // pick hand pulled back
		s.Update(in);
	}
	CHECK(!s.PickInserted());
	CHECK(s.LockAngle() == 0.0f);
}

TEST(CancelNeedsFreshPress)
{
	Config          c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(0, 10), f, c, f.Basis(), f.Basis());
	FrameInput      held = MakeInput(f, c, 0.0f, 0.0f, false);
	held.pickHand.cancel = true;
	for (int i = 0; i < 10; ++i) {
		CHECK(!Has(s.Update(held), SessionEvent::kCancelled));  // still held from activation
	}
	FrameInput released = MakeInput(f, c, 0.0f, 0.0f, false);
	s.Update(released);
	CHECK(Has(s.Update(held), SessionEvent::kCancelled));
	CHECK(s.Finished());
}

TEST(WalkingAwayCancels)
{
	Config          c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(0, 10), f, c, f.Basis(), f.Basis());
	FrameInput      in = MakeInput(f, c, 0.0f, 0.0f, false);
	in.hmd.pos = { 0, -200, 120 };
	CHECK(Has(s.Update(in), SessionEvent::kCancelled));
}

TEST(PickTicksWhileMoving)
{
	Config          c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(0, 10), f, c, f.Basis(), f.Basis());
	int             ticks = 0;
	for (int i = 0; i < 300; ++i) {
		const float roll = -60.0f + 120.0f * (i / 300.0f);
		ticks += Has(s.Update(MakeInput(f, c, roll, 0.0f, false)), SessionEvent::kPickTick) ? 1 : 0;
	}
	const float expected = 120.0f / std::max(c.hapticTickDegrees, 2.0f);  // scrape sound every few degrees
	CHECK(ticks > expected * 0.6f && ticks < expected * 1.2f);
}

TEST(ComputeParamsFromVanillaSettings)
{
	Config        c = TestCfg();
	VanillaTuning t;
	auto          novice = ComputeLockParams(LockLevel::kNovice, 15.0f, 1.0f, false, t, c, 0.5f);
	auto          master = ComputeLockParams(LockLevel::kMaster, 15.0f, 1.0f, false, t, c, 0.5f);
	auto          masterSkilled = ComputeLockParams(LockLevel::kMaster, 100.0f, 1.0f, false, t, c, 0.5f);
	CHECK_NEAR(novice.sweetHalfWidth, 35.0f * 1.0f / 2.0f, 0.01);
	CHECK(master.sweetHalfWidth < novice.sweetHalfWidth);
	CHECK(masterSkilled.sweetHalfWidth > master.sweetHalfWidth);
	CHECK_NEAR(novice.breakSeconds, 2.0f * (1.0f + 15.0f * 0.005f), 1e-4);
	CHECK(master.breakSeconds < novice.breakSeconds);
	for (float r : { 0.0f, 0.3f, 1.0f }) {
		auto p = ComputeLockParams(LockLevel::kAdept, 50.0f, 3.0f, false, t, c, r);
		CHECK(p.sweetCenter - p.sweetHalfWidth >= -90.0f);
		CHECK(p.sweetCenter + p.sweetHalfWidth <= 90.0f);
	}
}

// A "player bot" with hand tremor: sweeps the pick, probes with light tension and
// homes in on the angle where the cylinder turns furthest. Makes sure even master locks
// are solvable with realistic VR jitter and that the bot does not need dozens of picks.
TEST(BotCanSolveLocksWithJitter)
{
	Config        c = TestCfg();
	VanillaTuning t;
	std::mt19937  rng(1234);
	std::normal_distribution<float> tremor(0.0f, 0.6f);  // degrees of hand jitter
	std::uniform_real_distribution<float> uni(0.0f, 1.0f);

	for (int level = 0; level <= 4; ++level) {
		int totalBroken = 0;
		int solved = 0;
		for (int trial = 0; trial < 20; ++trial) {
			const LockFrame f = MakeLockFrame(Hmd(), c);
			LockSession     s(ComputeLockParams(static_cast<LockLevel>(level), 30.0f, 1.0f, false, t, c, uni(rng)), f, c, f.Basis(), f.Basis());

			// coarse sweep with gentle probing tension
			float best = -90.0f, bestTurn = -1.0f;
			float angle = -88.0f;
			int   frame = 0;
			int   broken = 0;
			auto  step = [&](float roll, float twistFrac) {
                FrameInput in = MakeInput(f, c, roll + tremor(rng), twistFrac * c.fullTurnTwist, true);
                auto&      ev = s.Update(in);
                ++frame;
                const bool unlocked = Has(ev, SessionEvent::kUnlocked);
                if (Has(ev, SessionEvent::kPickBroken)) {
                    ++broken;
                    // take the broken pick out and let the tension go, as a player has to
                    FrameInput out = MakeInput(f, c, roll, 0.0f, true);
                    out.pickHand.pose.pos = out.pickHand.pose.pos - f.forward * 5.0f;
                    for (int k = 0; k < 60; ++k) s.Update(out);
                }
                return unlocked;
			};
			bool done = false;
			float span = 8.0f;
			float center = 0.0f;
			for (int pass = 0; pass < 8 && !done; ++pass) {
				const float from = pass == 0 ? -88.0f : center - span;
				const float to = pass == 0 ? 88.0f : center + span;
				const float inc = pass == 0 ? 6.0f : std::max(0.6f, span / 6.0f);
				bestTurn = -1.0f;
				for (angle = from; angle <= to && !done; angle += inc) {
					// release tension, move, probe a little beyond what partial zone allows
					for (int k = 0; k < 8; ++k) step(angle, 0.0f);
					float reached = 0.0f;
					for (int k = 0; k < 25; ++k) {
						done |= step(angle, 1.0f);
						reached = s.LockAngle();
						if (s.AllowedTurn() < 0.999f && reached >= s.AllowedTurn() - 0.01f) break;  // felt the stop -> back off
					}
					if (reached > bestTurn) {
						bestTurn = reached;
						best = angle;
					}
				}
				center = best;
				span = std::max(1.0f, span * 0.5f);
			}
			// final attempt: hold the best angle and turn fully
			for (int k = 0; k < 200 && !done; ++k) done |= step(best, 1.0f);
			totalBroken += broken;
			solved += done ? 1 : 0;
		}
		std::printf("    level %d: solved %d/20, broken picks %d\n", level, solved, totalBroken);
		CHECK(solved >= 18);
		CHECK(totalBroken <= 20 * (level + 1));
	}
}

TEST(ToolsFollowHandsOneToOne)
{
	Config          c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	const Mat3      handRef = AxisAngle(Normalize({ 0.2f, -0.7f, 0.4f }), 1.1f);  // arbitrary controller axes
	LockSession     s(SimpleParams(0, 10), f, c, handRef, handRef);

	// at the start pose the tool points straight into the lock, toolLength ahead of the hand
	Pose hand;
	hand.rot = handRef;
	hand.pos = { 5, 3, 100 };
	const Pose t0 = s.PickTool(hand);
	CHECK_NEAR(Distance(t0.pos, hand.pos + f.forward * c.toolLength), 0.0, 1e-3);
	CHECK_NEAR(Dot(t0.rot * Vec3{ 0, 1, 0 }, f.forward), 1.0, 1e-4);

	// any extra hand rotation is applied to the tool exactly
	const Mat3 extra = AxisAngle(Normalize({ 1, 2, 3 }), 0.6f);
	hand.rot = extra * handRef;
	const Pose t1 = s.PickTool(hand);
	const Mat3 expected = extra * f.Basis();
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			CHECK_NEAR(t1.rot.m[i][j], expected.m[i][j], 1e-4);
		}
	}
	CHECK_NEAR(Distance(t1.pos, hand.pos + extra * f.forward * c.toolLength), 0.0, 1e-3);
}

TEST(WrenchMustBeInKeyholeToTurn)
{
	Config          c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(0, 20), f, c, f.Basis(), f.Basis());
	for (int i = 0; i < 200; ++i) {
		FrameInput in = MakeInput(f, c, 0.0f, i < 5 ? 0.0f : 80.0f, false);
		in.tensionHand.pose.pos += f.right * 15.0f;  // wrench next to the lock, not in it
		s.Update(in);
	}
	CHECK(!s.WrenchInserted());
	CHECK(s.LockAngle() == 0.0f);
	// put it in (no grip needed by default) and twist
	bool unlocked = false;
	for (int i = 0; i < 300 && !unlocked; ++i) {
		unlocked = Has(s.Update(MakeInput(f, c, 0.0f, i < 5 ? 0.0f : 80.0f, false)), SessionEvent::kUnlocked);
	}
	CHECK(unlocked);
}

TEST(PulsesFasterNearSweetSpot)
{
	Config c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(40, 5), f, c, f.Basis(), f.Basis());
	CHECK(s.Proximity(40) == 1.0f);
	CHECK(s.Proximity(0) > s.Proximity(-40));
	// hold the pick still at an angle for 3 s and count the vibration pulses (strength must not change)
	auto countPulses = [&](float roll, float& strength) {
		LockSession t(SimpleParams(40, 5), f, c, f.Basis(), f.Basis());
		int pulses = 0;
		for (int i = 0; i < 30; ++i) t.Update(MakeInput(f, c, roll, 0.0f, false));  // insert + settle
		for (int i = 0; i < 270; ++i) {
			t.Update(MakeInput(f, c, roll, 0.0f, false));
			if (t.Haptics().pickHand > 0.0f) {
				++pulses;
				strength = std::max(strength, t.Haptics().pickHand);
			}
		}
		return pulses;
	};
	// three steps (0.5 s far, 0.4 s within 10 deg of the sweet spot, 0.3 s in it): a little faster,
	// never a giveaway
	float farS = 0.0f, nearS = 0.0f, sweetS = 0.0f;
	const int farN = countPulses(-60.0f, farS);
	const int nearN = countPulses(50.0f, nearS);  // sweet spot 40 +- 5: 5 deg past its edge
	const int sweetN = countPulses(40.0f, sweetS);
	const int justOutN = countPulses(56.5f, farS);  // 11.5 deg past the edge: far again
	std::printf("    vibrating frames in 3 s: far %d, near %d, sweet %d, just outside near %d\n", farN, nearN, sweetN, justOutN);
	CHECK(farN >= 1);
	CHECK(nearN > farN);
	CHECK(sweetN > nearN);
	CHECK(sweetN < farN * 2);
	CHECK(justOutN <= farN + 2);
	CHECK_NEAR(farS, nearS, 1e-4);
	CHECK_NEAR(sweetS, nearS, 1e-4);
	// far -> near is a step, near -> sweet is smooth
	Config k = TestCfg();
	k.pulseNearStrength = 0.4f;
	k.pulseSweetStrength = 0.8f;
	k.pulseFarStrength = 0.2f;
	LockSession q(SimpleParams(40, 5), f, k, f.Basis(), f.Basis());
	CHECK_NEAR(q.SearchPulse(56.0f).strength, 0.2, 1e-5);                // 11 deg past the edge: far
	CHECK_NEAR(q.SearchPulse(54.99f).strength, 0.4, 1e-2);               // just inside near: near
	CHECK_NEAR(q.SearchPulse(50.0f).strength, 0.6, 1e-4);                // half way: half way
	CHECK_NEAR(q.SearchPulse(50.0f).interval, 0.35, 1e-4);
	CHECK_NEAR(q.SearchPulse(45.0f).strength, 0.8, 1e-4);                // at the edge: sweet
	CHECK_NEAR(q.SearchPulse(40.0f).interval, k.pulseSweetInterval, 1e-6);
}

TEST(NoFeedbackBeforeTheKeyhole)
{
	Config c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession s(SimpleParams(40, 5), f, c, f.Basis(), f.Basis());
	int pulses = 0;
	for (int i = 0; i < 180; ++i) {
		FrameInput in = MakeInput(f, c, 0.0f, 0.0f, false);
		in.pickHand.pose.pos = in.pickHand.pose.pos + f.right * (c.insertRadius + 1.0f);  // right next to it
		in.tensionHand.pose.pos = in.tensionHand.pose.pos + f.right * (c.insertRadius + 1.0f);
		s.Update(in);
		pulses += (s.Haptics().pickHand > 0.0f || s.Haptics().tensionHand > 0.0f) ? 1 : 0;
	}
	CHECK(!s.PickInserted());
	CHECK(pulses == 0);
}

TEST(ToolsCannotGoThroughTheLock)
{
	Config c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession s(SimpleParams(0, 10), f, c, f.Basis(), f.Basis());
	for (int i = 0; i < 5; ++i) s.Update(MakeInput(f, c, 0.0f, 0.0f, false));
	CHECK(s.PickInserted());
	CHECK(s.PickPlace() == ToolPlace::kIn);
	FrameInput in = MakeInput(f, c, 0.0f, 0.0f, false);
	in.pickHand.pose.pos = in.pickHand.pose.pos + f.forward * (c.maxDepth + 1.0f);  // pushed a little: stays in
	s.Update(in);
	CHECK(s.PickInserted());
	in.pickHand.pose.pos = in.pickHand.pose.pos + f.forward * (c.holdDepth + 1.0f);  // pushed through
	s.Update(in);
	CHECK(s.PickPlace() == ToolPlace::kTooDeep);
	CHECK(!s.PickInserted());
	// a tip coming in from behind the keyhole doesn't count either
	for (int i = 0; i < 5; ++i) s.Update(in);
	CHECK(!s.PickInserted());
	for (int i = 0; i < 5; ++i) s.Update(MakeInput(f, c, 0.0f, 0.0f, false));
	CHECK(s.PickInserted());
}

TEST(NothingTurnsWithOneTool)
{
	Config c = TestCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession s(SimpleParams(0, 10), f, c, f.Basis(), f.Basis());
	for (int i = 0; i < 60; ++i) {
		FrameInput in = MakeInput(f, c, 50.0f, i < 5 ? 0.0f : 80.0f, false);
		in.pickHand.pose.pos = in.pickHand.pose.pos + f.right * 20.0f;  // pick not in
		s.Update(in);
	}
	CHECK(s.WrenchInserted());
	CHECK(s.LockAngle() == 0.0f);
	CHECK(s.PickDamage() == 0.0f);
}

TEST(FistGripHoldsToolAlongFingers)
{
	Config c;  // defaults: tool held in the fist
	CHECK(c.handGrip);
	const LockFrame f = MakeLockFrame(Hmd(), c);
	const Mat3      someRef = AxisAngle(Normalize({ 0.3f, 1, -0.2f }), 0.9f);
	LockSession     s(SimpleParams(0, 10), f, c, someRef, someRef);
	Pose hand;
	hand.rot = AxisAngle(Normalize({ -0.5f, 0.2f, 1 }), 0.7f);
	hand.pos = { 3, 30, 90 };
	const Pose t = s.PickTool(hand);
	// tip = grip point + toolLength along the pointing direction, independent of the start pose
	const Vec3 expectTip = hand.TransformPoint(c.handGripPos) + hand.rot * (Normalize(c.handPointing) * c.toolLength);
	CHECK_NEAR(Distance(t.pos, expectTip), 0.0, 1e-3);
	CHECK_NEAR(Dot(t.rot * Vec3{ 0, 1, 0 }, hand.rot * Normalize(c.handPointing)), 1.0, 1e-4);
	// flat side faces away from the palm
	CHECK(Dot(t.rot * Vec3{ 0, 0, 1 }, hand.rot * c.handPalm) < -0.9f);
	// the other hand is mirrored (x flipped in hand space)
	const Pose w = s.WrenchTool(hand);
	const Vec3 g{ -c.handGripPos.x, c.handGripPos.y, c.handGripPos.z };
	const Vec3 pt{ -c.handPointing.x, c.handPointing.y, c.handPointing.z };
	CHECK_NEAR(Distance(w.pos, hand.TransformPoint(g) + hand.rot * (Normalize(pt) * c.toolLength)), 0.0, 1e-3);
	// rotation is orthonormal
	const Mat3 o = t.rot.Transposed() * t.rot;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) CHECK_NEAR(o.m[i][j], i == j ? 1.0 : 0.0, 1e-4);
}

TEST(FistGripBotSolves)
{
	// with the fist grip the player must aim; a hand pose that puts the tip in the keyhole is
	// found by inverting the grip, then the usual roll / twist works
	Config c;
	c.leverControls = false;  // wrist controls
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     probe(SimpleParams(25, 6), f, c, Mat3{}, Mat3{});
	Pose            id;
	const Pose      gripPick = probe.PickTool(id);    // tool pose for a hand at identity
	const Pose      gripWrench = probe.WrenchTool(id);
	auto handFor = [&](const Pose& grip, const Vec3& point, float rollDeg) {
		Pose h;
		const Mat3 toolRot = AxisAngle(f.forward, rollDeg * kDegToRad) * f.Basis();
		h.rot = toolRot * grip.rot.Transposed();
		h.pos = point - h.rot * grip.pos;
		return h;
	};
	LockSession s(SimpleParams(25, 6), f, c, handFor(gripPick, f.PickPoint(c), 0).rot, handFor(gripWrench, f.WrenchPoint(c), 0).rot);
	bool unlocked = false;
	for (int i = 0; i < 400 && !unlocked; ++i) {
		FrameInput in;
		in.dt = 1.0f / 90.0f;
		in.hmd = Hmd();
		in.pickHand.valid = in.tensionHand.valid = true;
		in.pickHand.pose = handFor(gripPick, f.PickPoint(c), 25.0f);
		in.tensionHand.pose = handFor(gripWrench, f.WrenchPoint(c), i < 10 ? 0.0f : c.fullTurnTwist * 1.2f);
		unlocked = Has(s.Update(in), SessionEvent::kUnlocked);
	}
	CHECK(s.PickInserted());
	CHECK(unlocked);
}

namespace
{
	// Lever controls, right hand: a hand (controller attachment, ref = lock basis) whose pick tip
	// sits in a_spot with the handle at a_angle (0 = up, 90 = right, 180 = down), tilted a_tilt out
	// of the lock face.
	Pose LeverHand(const LockFrame& f, const Config& c, const Vec3& a_spot, float a_angle, float a_tilt, float a_extra = 0.0f)
	{
		const float a = a_angle * kDegToRad, t = a_tilt * kDegToRad;
		const Vec3  handle = (f.right * std::sin(a) + f.up * std::cos(a)) * std::cos(t) - f.forward * std::sin(t);
		const Vec3  y = -handle;
		const Vec3  z = Normalize(-f.forward - y * Dot(-f.forward, y));
		Pose        p;
		p.rot = Mat3::FromColumns(Cross(y, z), y, z);
		p.pos = a_spot + handle * (c.toolLength + a_extra);
		return p;
	}

	// Lever controls, left hand: the blade straight in the keyhole, the back of the hand (tool +Z)
	// pointing at a_blade (0 = up, -90 = left = the start position).
	Pose KeyHand(const LockFrame& f, const Config& c, float a_blade)
	{
		const float a = a_blade * kDegToRad;
		const Vec3  y = f.forward;
		const Vec3  z = f.right * std::sin(a) + f.up * std::cos(a);
		Pose        p;
		p.rot = Mat3::FromColumns(Cross(y, z), y, z);
		p.pos = f.WrenchPoint(c) - y * c.toolLength;
		return p;
	}

	Config LeverCfg()
	{
		Config c;
		c.handGrip = false;
		c.leverControls = true;
		return c;
	}

	FrameInput LeverInput(const LockFrame& f, const Config& c, float pickHandle, float blade, float dt = 1.0f / 90.0f)
	{
		FrameInput in;
		in.dt = dt;
		in.hmd = Hmd();
		in.pickHand.valid = in.tensionHand.valid = true;
		in.pickHand.pose = LeverHand(f, c, f.PickPoint(c), pickHandle, 40.0f);
		in.tensionHand.pose = KeyHand(f, c, blade);
		return in;
	}
}

TEST(InsertedToolsPointAtTheHand)
{
	// once in, a tool keeps its tip in the keyhole and its handle towards the real hand
	for (const bool lever : { false, true }) {
		Config c = TestCfg();
		c.leverControls = lever;
		const LockFrame f = MakeLockFrame(Hmd(), c);
		LockSession     s(SimpleParams(0, 10), f, c, f.Basis(), f.Basis());
		auto input = [&] { return lever ? LeverInput(f, c, 90.0f, -90.0f) : MakeInput(f, c, 0.0f, 0.0f, true); };
		for (int i = 0; i < 5; ++i) s.Update(input());
		CHECK(s.PickInserted());
		CHECK(s.WrenchInserted());
		FrameInput in = input();
		in.pickHand.pose.pos = in.pickHand.pose.pos + f.right * 4.0f - f.forward * 2.0f;  // hand off to the side
		in.tensionHand.pose.pos = in.tensionHand.pose.pos - f.up * 4.0f + f.forward * 2.0f;
		s.Update(in);
		CHECK(s.PickInserted());
		CHECK(s.WrenchInserted());
		const Pose pick = s.PickVisual(in.pickHand.pose);
		const Pose wrench = s.WrenchVisual(in.tensionHand.pose);
		CHECK(Length(pick.pos - f.PickPoint(c)) < 0.001f);
		CHECK(Length(wrench.pos - f.WrenchPoint(c)) < 0.001f);
		CHECK(Dot(pick.rot * Vec3{ 0, 1, 0 }, Normalize(f.PickPoint(c) - s.PickGrip(in.pickHand.pose))) > 0.999f);
		CHECK(Dot(wrench.rot * Vec3{ 0, 1, 0 }, Normalize(f.WrenchPoint(c) - s.WrenchGrip(in.tensionHand.pose))) > 0.999f);
		// still a proper rotation
		CHECK(std::fabs(Dot(Cross(wrench.rot * Vec3{ 1, 0, 0 }, wrench.rot * Vec3{ 0, 1, 0 }), wrench.rot * Vec3{ 0, 0, 1 }) - 1.0f) < 0.001f);
	}
}

TEST(LeverPickRangeIsTheRightHalf)
{
	Config          c = LeverCfg();
	c.pickFilterMinCutoff = 0.0f;
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(60, 5), f, c, f.Basis(), f.Basis());
	for (float a : { 0.0f, 45.0f, 90.0f, 170.0f }) {
		for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, a, -90.0f));
		CHECK(s.PickInserted());
		CHECK_NEAR(s.PickAngle(), a - 90.0f, 0.5);  // top = -90 .. bottom = +90 in the simulation
		// shown pivoting in its spot with the handle at that angle
		const Pose v = s.PickVisual(LeverHand(f, c, f.PickPoint(c), a, 40.0f));
		CHECK_NEAR(Distance(v.pos, f.PickPoint(c)), 0.0, 1e-3);
		float shown = 0.0f;
		CHECK(s.OrbitAngle(v.pos - v.rot * Vec3{ 0, 5, 0 }, f.PickPoint(c), shown));
		CHECK_NEAR(shown, a, 0.5);
	}
	// the hook (tool +X) points left at the top, up half way, right at the bottom
	auto hook = [&](float a) {
		for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, a, -90.0f));
		return s.PickVisual(LeverHand(f, c, f.PickPoint(c), a, 40.0f)).rot * Vec3{ 1, 0, 0 };
	};
	CHECK(Dot(hook(0.0f), -f.right) > 0.9f);
	CHECK(Dot(hook(90.0f), f.up) > 0.9f);
	CHECK(Dot(hook(180.0f), f.right) > 0.9f);
	// past the bottom it stops
	for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, 190.0f, -90.0f));
	CHECK_NEAR(s.PickAngle(), 90.0, 0.5);
	// rolling the wrist doesn't move the pick
	FrameInput in = LeverInput(f, c, 20.0f, -90.0f);
	for (int i = 0; i < 5; ++i) s.Update(in);
	in.pickHand.pose.rot = in.pickHand.pose.rot * AxisAngle({ 0, 1, 0 }, 1.2f);
	for (int i = 0; i < 5; ++i) s.Update(in);
	CHECK(s.PickInserted());
	CHECK_NEAR(s.PickAngle(), -70.0, 0.5);
}

TEST(LeverPickOnlyGoesInFromTheRightHalf)
{
	Config          c = LeverCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(0, 5), f, c, f.Basis(), f.Basis());
	for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, 270.0f, -90.0f));  // handle to the left
	CHECK(!s.PickInserted());
	for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, 100.0f, -90.0f));
	CHECK(s.PickInserted());
}

TEST(LeverWrenchTurnsLikeAKey)
{
	Config          c = LeverCfg();
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(0, 10), f, c, f.Basis(), f.Basis());
	// coming in already turned (back of the hand up) does nothing - the lock can't jump
	for (int i = 0; i < 30; ++i) s.Update(LeverInput(f, c, 90.0f, 0.0f));
	CHECK(s.PickInserted());
	CHECK(!s.WrenchInserted());
	CHECK(s.LockAngle() == 0.0f);
	// back of the hand to the left: in
	for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, 90.0f, -90.0f));
	CHECK(s.WrenchInserted());
	// counter-clockwise does nothing
	for (int i = 0; i < 30; ++i) s.Update(LeverInput(f, c, 90.0f, -120.0f));
	CHECK(s.TurnInput() == 0.0f);
	// half way (back of the hand up-left)
	for (int i = 0; i < 90; ++i) s.Update(LeverInput(f, c, 90.0f, -45.0f));
	CHECK_NEAR(s.TurnInput(), 0.5, 0.01);
	CHECK_NEAR(s.LockAngle(), 0.5, 0.01);
	// the blade is drawn turned with the cylinder: flat side up-left
	const Pose v = s.WrenchVisual(KeyHand(f, c, -45.0f));
	CHECK(Dot(v.rot * Vec3{ 0, 0, 1 }, Normalize(f.up - f.right)) > 0.99f);
	CHECK(Dot(v.rot * Vec3{ 0, 1, 0 }, f.forward) > 0.99f);
	// back of the hand up: unlocked (the pick is on the sweet spot: handle at 3 o'clock = 0)
	bool unlocked = false;
	for (int i = 0; i < 120 && !unlocked; ++i) unlocked = Has(s.Update(LeverInput(f, c, 90.0f, 0.0f)), SessionEvent::kUnlocked);
	CHECK(unlocked);
}

TEST(LeverLeftHandedIsMirrored)
{
	// the pick in the left hand: it searches along the left half, the blade (right hand) starts
	// with the back of the hand to the right and turns the lock anticlockwise
	Config c = LeverCfg();
	c.leftHandedPick = true;
	c.pickFilterMinCutoff = 0.0f;
	const LockFrame f = MakeLockFrame(Hmd(), c);
	auto input = [&](float a_pick, float a_blade) {
		FrameInput in;
		in.dt = 1.0f / 90.0f;
		in.hmd = Hmd();
		in.pickHand.valid = in.tensionHand.valid = true;
		in.pickHand.pose = LeverHand(f, c, f.PickPoint(c), a_pick, 40.0f);
		in.tensionHand.pose = KeyHand(f, c, a_blade);
		return in;
	};
	{
		LockSession s(SimpleParams(0, 10), f, c, f.Basis(), f.Basis());
		for (int i = 0; i < 5; ++i) s.Update(input(90.0f, 90.0f));  // the right half: not the pick's range
		CHECK(!s.PickInserted());
	}
	LockSession s(SimpleParams(0, 10), f, c, f.Basis(), f.Basis());
	for (int i = 0; i < 5; ++i) s.Update(input(-90.0f, 90.0f));  // handle to the left, back of the hand right
	CHECK(s.PickInserted());
	CHECK(s.WrenchInserted());
	CHECK_NEAR(s.PickAngle(), 0.0, 0.5);
	// drawn where the hand is
	const Pose v = s.PickVisual(LeverHand(f, c, f.PickPoint(c), -90.0f, 40.0f));
	CHECK(Dot(v.pos - v.rot * Vec3{ 0, 5, 0 } - f.PickPoint(c), f.right) < -1.0f);
	// back of the hand up: turned (anticlockwise)
	for (int i = 0; i < 120; ++i) s.Update(input(-90.0f, 0.0f));
	CHECK(s.TurnInput() > 0.9f);
	CHECK(s.Side() < 0.0f);
}

TEST(LocksmithPutsTheSweetSpotNearTheFirstPick)
{
	Config c = LeverCfg();
	c.pickFilterMinCutoff = 0.0f;
	const LockFrame f = MakeLockFrame(Hmd(), c);
	for (const float r : { 0.0f, 0.3f, 0.7f, 1.0f }) {
		LockParams p = SimpleParams(80, 3);  // far from where the pick goes in
		p.startArc = 30.0f;
		p.startRandom01 = r;
		LockSession s(p, f, c, f.Basis(), f.Basis());
		for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, 40.0f, -90.0f));  // in at -50 in the simulation
		CHECK(s.PickInserted());
		const float d = std::fabs(s.Params().sweetCenter - (40.0f - 90.0f));
		CHECK(d <= 30.0f + 1e-3f);
		CHECK(d >= 7.5f - 1e-3f);  // close, never right under it
		// a second pick (after taking it out) doesn't move it again
		const float placed = s.Params().sweetCenter;
		FrameInput out = LeverInput(f, c, 40.0f, -90.0f);
		out.pickHand.pose.pos = out.pickHand.pose.pos - f.forward * 20.0f;
		for (int i = 0; i < 5; ++i) s.Update(out);
		for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, 150.0f, -90.0f));
		CHECK_NEAR(s.Params().sweetCenter, placed, 1e-4);
	}
	// near the end of the range it goes to the other side
	LockParams p = SimpleParams(0, 3);
	p.startArc = 30.0f;
	p.startRandom01 = 1.0f;  // +30
	LockSession s(p, f, c, f.Basis(), f.Basis());
	for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, 170.0f, -90.0f));  // +80
	CHECK(s.PickInserted());
	CHECK(s.Params().sweetCenter < 80.0f);
	// without the perk it stays where it was
	LockSession plain(SimpleParams(80, 3), f, c, f.Basis(), f.Basis());
	for (int i = 0; i < 5; ++i) plain.Update(LeverInput(f, c, 40.0f, -90.0f));
	CHECK_NEAR(plain.Params().sweetCenter, 80.0, 1e-4);
}

TEST(LeverPickHeldWhileTurning)
{
	Config          c = LeverCfg();
	c.pickFilterMinCutoff = 0.0f;
	const LockFrame f = MakeLockFrame(Hmd(), c);
	LockSession     s(SimpleParams(70, 3), f, c, f.Basis(), f.Basis());
	for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, 130.0f, -90.0f));
	CHECK(s.WrenchInserted());
	CHECK_NEAR(s.PickAngle(), 40.0, 0.5);
	for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, 130.0f, -70.0f));  // some tension
	for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, 100.0f, -70.0f));  // still under tension
	CHECK_NEAR(s.PickAngle(), 40.0, 0.5);
	// ...but drawn where the hand holds it
	const Pose v = s.PickVisual(LeverHand(f, c, f.PickPoint(c), 100.0f, 40.0f));
	float      shown = 0.0f;
	CHECK(s.OrbitAngle(v.pos - v.rot * Vec3{ 0, 5, 0 }, f.PickPoint(c), shown));
	CHECK_NEAR(shown, 100.0, 0.5);
	for (int i = 0; i < 5; ++i) s.Update(LeverInput(f, c, 100.0f, -90.0f));  // released
	CHECK_NEAR(s.PickAngle(), 10.0, 0.5);
}

TEST(LeverBotSolvesLocks)
{
	Config        c = LeverCfg();
	VanillaTuning t;
	std::mt19937  rng(77);
	std::normal_distribution<float> tremor(0.0f, 1.0f);
	std::uniform_real_distribution<float> uni(0.0f, 1.0f);
	for (int level = 0; level <= 4; ++level) {
		int solved = 0;
		for (int trial = 0; trial < 10; ++trial) {
			const LockFrame f = MakeLockFrame(Hmd(), c);
			LockSession     s(ComputeLockParams(static_cast<LockLevel>(level), 30.0f, 1.0f, false, t, c, uni(rng)), f, c, f.Basis(), f.Basis());
			bool            done = false;
			// a: pick angle in the simulation's -90..90 = handle at a + 90
			auto step = [&](float a, float turnFrac) {
				FrameInput in = LeverInput(f, c, a + 90.0f + tremor(rng), -90.0f + turnFrac * c.fullTurnTwist + tremor(rng));
				const auto& ev = s.Update(in);
				done |= Has(ev, SessionEvent::kUnlocked);
				if (Has(ev, SessionEvent::kPickBroken)) {
					// take the broken pick out and turn the blade back, as a player has to
					FrameInput out = LeverInput(f, c, a + 90.0f, -90.0f);
					out.pickHand.pose.pos = out.pickHand.pose.pos - f.forward * 15.0f;
					for (int k = 0; k < 60; ++k) s.Update(out);
				}
			};
			float best = 0.0f, bestTurn = -1.0f, centre = 0.0f, span = 90.0f;
			for (int pass = 0; pass < 7 && !done; ++pass) {
				const float inc = std::max(0.6f, span / 7.0f);
				bestTurn = -1.0f;
				for (float a = std::max(-90.0f, centre - span); a <= std::min(90.0f, centre + span) && !done; a += inc) {
					for (int k = 0; k < 8; ++k) step(a, 0.0f);
					float reached = 0.0f;
					for (int k = 0; k < 25 && !done; ++k) {
						step(a, 1.0f);
						reached = s.LockAngle();
						if (s.AllowedTurn() < 0.999f && reached >= s.AllowedTurn() - 0.01f) break;
					}
					if (reached > bestTurn) {
						bestTurn = reached;
						best = a;
					}
				}
				centre = best;
				span = std::max(1.5f, span * 0.4f);
			}
			for (int k = 0; k < 8; ++k) step(best, 0.0f);
			for (int k = 0; k < 200 && !done; ++k) step(best, 1.0f);
			solved += done ? 1 : 0;
		}
		std::printf("    lever level %d: solved %d/10\n", level, solved);
		CHECK(solved >= 9);
	}
}

TEST(MeshFitToolParts)
{
	// a pick lying along -Z far from the origin: thin shaft box plus a handle box
	MeshPart shaft;
	shaft.centre = { 30, -120, 60 };
	for (int i = 0; i < 8; ++i) {
		shaft.corners.push_back(shaft.centre + Vec3{ (i & 1) ? 1.0f : -1.0f, (i & 2) ? 3.0f : -3.0f, (i & 4) ? 40.0f : -40.0f });
	}
	const Vec3 top = shaft.centre + Vec3{ 0, 0, 40 };
	const Vec3 bottom = shaft.centre - Vec3{ 0, 0, 40 };
	const MeshPlacement m = FitToolParts({ shaft }, 16.0f, false);
	CHECK(m.valid);
	CHECK_NEAR(Length(m.Apply(top)), 0.0, 1e-3);                      // + end is the tip, at the origin
	CHECK_NEAR(m.Apply(bottom).y, -16.0, 1e-3);                        // handle along -Y, scaled
	CHECK_NEAR(std::fabs(m.Apply(shaft.centre + Vec3{ 0, 3, 0 }).x), 3.0 * 16.0 / 80.0, 1e-3);  // 2nd axis -> X
	const MeshPlacement f = FitToolParts({ shaft }, 16.0f, true);
	CHECK_NEAR(Length(f.Apply(bottom)), 0.0, 1e-3);                    // flipped: the other end
	CHECK_NEAR(f.Apply(top).y, -16.0, 1e-3);
	// the tip is the end nearer the hint (e.g. the lock in the menu's space)
	const Vec3          below{ 30, -120, -500 };
	const MeshPlacement h = FitToolParts({ shaft }, 16.0f, false, &below);
	CHECK_NEAR(Length(h.Apply(bottom)), 0.0, 1e-3);
	CHECK_NEAR(Length(FitToolParts({ shaft }, 16.0f, true, &below).Apply(top)), 0.0, 1e-3);
	// sphere-only parts still work
	MeshPart sphere;
	sphere.centre = { 5, 5, 5 };
	sphere.radius = 2;
	CHECK(FitToolParts({ sphere }, 10.0f, false).valid);
	CHECK(!FitToolParts({}, 10.0f, false).valid);
}

TEST(MeshFitLockParts)
{
	// numbers from the vanilla LockPickShiv01.nif (bounding spheres only). In game the keyhole
	// axis turned out to be the model's Y axis, front towards -Y where the screwdriver starts.
	MeshPart body{ { 1.4f, -4.2f, 0.0f }, 86.3f };
	MeshPart side{ { -51.2f, -1.7f, 0.0f }, 58.1f };
	MeshPart t1{ { 14.2f, -2.2f, -1.6f }, 16.9f };
	MeshPart t2{ { 14.5f, -3.0f, 1.5f }, 23.1f };
	t1.cylinder = t2.cylinder = true;
	const Vec3    shiv{ 101.0f, -165.0f, -88.0f };  // centre of the screwdriver parts
	const LockFit f = FitLockParts({ body, side, t1, t2 }, shiv, 8.0f);
	CHECK(f.place.valid);
	CHECK(f.fromCylinder);
	CHECK_NEAR(f.forward.y, 1.0, 1e-6);                                 // into the lock = +Y
	CHECK_NEAR(Length(f.place.Apply(f.keyhole)), 0.0, 1e-3);            // keyhole at the origin
	CHECK(f.keyhole.y < -2.6f - 16.0f);                                 // on the front of the tumbler
	CHECK_NEAR(f.keyhole.x, 14.35, 1e-3);                               // centred on the tumbler
	CHECK(f.place.Apply(body.centre).y > 0.0f);                         // body behind the keyhole
	CHECK_NEAR(f.place.Apply(f.keyhole + Vec3{ 0, 0, 10 }).z, 10.0 * f.place.scale, 1e-3);  // mesh up stays up
	CHECK(f.place.Apply(f.keyhole + Vec3{ 10, 0, 0 }).x > 0.0f);        // mesh +X is the player's right
	CHECK(f.place.scale > 0.05f && f.place.scale < 0.1f);
	// no hint: the menu convention (front = -Y)
	const LockFit g = FitLockParts({ body }, body.centre, 8.0f);
	CHECK(g.place.valid && !g.fromCylinder);
	CHECK_NEAR(g.forward.y, 1.0, 1e-6);
}

TEST(GripRotationTurnsTool)
{
	Config c;
	const LockFrame f = MakeLockFrame(Hmd(), c);
	Pose hand;
	LockSession s(SimpleParams(0, 10), f, c, Mat3{}, Mat3{});
	const Vec3 d0 = s.PickTool(hand).rot * Vec3{ 0, 1, 0 };
	c.gripRotation = { 0, 0, 90 };  // yaw
	s.Retune(c);
	const Vec3 d1 = s.PickTool(hand).rot * Vec3{ 0, 1, 0 };
	CHECK_NEAR(Dot(d0, d1), 0.0, 1e-4);
	const Vec3 w1 = s.WrenchTool(hand).rot * Vec3{ 0, 1, 0 };
	// mirrored: the left hand yaws the other way (x component flips sign relative to right)
	CHECK_NEAR(w1.x, -d1.x, 1e-4);
	// the keyhole spots grow with the visual scale
	c.visualScale = 2.0f;
	c.pickPointOffset = 0.8f;
	CHECK_NEAR(Distance(f.PickPoint(c), f.keyhole), 2.0 * c.pickPointOffset, 1e-4);
}

TEST(LockProfileMarkupRoundTrip)
{
	MeshPart body{ { 1.4f, -4.2f, 0.0f }, 86.3f };
	MeshPart t1{ { 14.2f, -2.2f, -1.6f }, 16.9f };
	t1.cylinder = true;
	const LockFit auto_ = FitLockParts({ body, t1 }, { 101, -165, -88 }, 8.0f);
	// the user marks: centre 1.0 right / 0.5 down / 0.2 deep of where the mod thought, the
	// keyhole's top 30 deg clockwise from up
	const Vec3  centre{ 1.0f, 0.2f, -0.5f };
	const float a = 30.0f * kDegToRad;
	const Vec3  top = centre + Vec3{ std::sin(a), 0.0f, std::cos(a) };
	const LockProfile p = ProfileFromMarks(centre, top, auto_.place, 0.0f);
	CHECK(p.valid);
	CHECK_NEAR(p.radius, 0.0, 1e-6);  // the target size stays fixed
	CHECK_NEAR(p.roll, -30.0, 1e-3);
	CHECK_NEAR(Distance(auto_.place.Apply(p.keyhole), centre), 0.0, 1e-4);  // it is the marked point
	// applied: the marked centre moves onto the keyhole and the marked top points up
	const LockFit fitted = ApplyLockProfile(auto_, p);
	CHECK_NEAR(Length(fitted.place.Apply(p.keyhole)), 0.0, 1e-4);
	const Vec3 topModel = (auto_.place.rot.Transposed() * (top - auto_.place.offset)) / auto_.place.scale;
	const Vec3 topNow = fitted.place.Apply(topModel);
	CHECK_NEAR(topNow.x, 0.0, 1e-4);
	CHECK(topNow.z > 0.9f);
	// saved and read back
	const std::string text = WriteLockProfile("; profiles\n", "Meshes\\Interface/Lockpicking/LockPickShiv01.NIF", p);
	const auto        all = ParseLockProfiles(text);
	CHECK(all.size() == 1);
	CHECK(all.contains("interface\\lockpicking\\lockpickshiv01.nif"));
	const LockProfile& r = all.at("interface\\lockpicking\\lockpickshiv01.nif");
	CHECK_NEAR(Distance(r.keyhole, p.keyhole), 0.0, 2e-3);
	CHECK_NEAR(r.radius, p.radius, 2e-3);
	CHECK_NEAR(r.roll, p.roll, 1e-2);
	// marking again on the fitted model gives the same profile
	const LockProfile again = ProfileFromMarks({}, { 0, 0, 1 }, fitted.place, p.roll);
	CHECK_NEAR(Distance(again.keyhole, p.keyhole), 0.0, 1e-3);
	CHECK_NEAR(again.roll, p.roll, 1e-3);
}

TEST(BuiltInToolAlignments)
{
	const auto& picks = DefaultToolProfiles("pick");
	const auto& shivs = DefaultToolProfiles("shiv");
	CHECK(picks.size() == 2 && shivs.size() == 1);
	CHECK(picks.contains(NormalizeModelPath("Clutter\\Common\\Lockpick.nif")));
	CHECK(picks.contains(NormalizeModelPath("Clutter\\SkeletonKey\\SkeletonKey.nif")));
	CHECK(shivs.contains(NormalizeModelPath("Interface\\Lockpicking\\LockPickShiv01.nif")));
	for (const auto* set : { &picks, &shivs }) {
		for (const auto& [model, p] : *set) {
			CHECK(p.valid);
			// a clean rotation
			CHECK_NEAR(Length(p.rot * Vec3{ 1, 0, 0 }), 1.0, 1e-4);
			CHECK_NEAR(Dot(p.rot * Vec3{ 1, 0, 0 }, p.rot * Vec3{ 0, 1, 0 }), 0.0, 1e-4);
		}
	}
	// the measured values themselves
	const ToolProfile& w = shivs.begin()->second;
	CHECK_NEAR(Distance(w.offset, Vec3{ -1.1958f, 0.0641f, -4.1449f }), 0.0, 1e-4);
	CHECK_NEAR(Dot(w.rot * Vec3{ 0, 1, 0 }, Normalize(Vec3{ -0.1464f, 0.9380f, -0.3141f })), 1.0, 1e-4);
}

TEST(ToolAlignmentRoundTrip)
{
	MeshPlacement place;
	place.rot = AxisAngle({ 1, 0, 0 }, 0.7f);
	place.scale = 0.08f;
	place.offset = { 0, 0, 0 };  // the tip is at the origin
	place.valid = true;

	ToolProfile a;
	a.rot = AxisAngle({ 0, 0, 1 }, 0.4f) * AxisAngle({ 0, 1, 0 }, -1.1f);
	a.offset = { 0.5f, -1.0f, 2.0f };
	a.scale = 1.25f;
	a.valid = true;
	const MeshPlacement m = ApplyToolProfile(place, a);
	// a model point lands where turning, sizing about the tip and moving it would put it
	const Vec3 point{ 3, -40, 7 };
	const Vec3 before = place.rot * (point * place.scale) + place.offset;
	const Vec3 expected = a.rot * (before * a.scale) + a.offset;
	CHECK_NEAR(Distance(m.rot * (point * m.scale) + m.offset, expected), 0.0, 1e-4);

	// saved and read back, next to a lock profile in the same section
	LockProfile lock;
	lock.keyhole = { 1, 2, 3 };
	lock.radius = 4;
	lock.valid = true;
	std::string text = WriteLockProfile("", "Meshes\\Interface\\Lockpicking\\LockPickShiv01.nif", lock);
	text = WriteToolProfile(text, "Interface/Lockpicking/LockPickShiv01.nif", "shiv", a);
	const auto shivs = ParseToolProfiles(text, "shiv");
	CHECK(ParseToolProfiles(text, "pick").empty());
	CHECK(ParseLockProfiles(text).size() == 1);
	const auto it = shivs.find("interface\\lockpicking\\lockpickshiv01.nif");
	CHECK(it != shivs.end());
	if (it != shivs.end()) {
		const MeshPlacement back = ApplyToolProfile(place, it->second);
		CHECK_NEAR(Distance(back.rot * (point * back.scale) + back.offset, expected), 0.0, 1e-2);
	}
	CHECK(ToolProfileText("pick", a).find("fPickHoldScale = 1.2500") != std::string::npos);
}

TEST(RegripKeepsTheModelStill)
{
	ToolProfile a;
	a.rot = AxisAngle({ 0, 1, 0 }, 0.5f);
	a.offset = { 1, 0, -2 };
	a.scale = 0.8f;
	a.valid = true;
	Pose from;
	from.rot = AxisAngle({ 1, 0, 0 }, 0.3f) * AxisAngle({ 0, 0, 1 }, 1.2f);
	from.pos = { 10, 20, 30 };
	Pose to;
	to.rot = AxisAngle({ 0, 1, 1 }, -0.9f);
	to.pos = { 14, 18, 25 };
	const ToolProfile b = Regrip(a, from, to);
	CHECK_NEAR(b.scale, 0.8, 1e-6);
	// a point of the model, in the room, before and after
	for (const Vec3 x : { Vec3{ 0, 0, 0 }, Vec3{ 0, -16, 0 }, Vec3{ 2, 3, -1 } }) {
		const Vec3 before = from.rot * (a.rot * (x * a.scale) + a.offset) + from.pos;
		const Vec3 after = to.rot * (b.rot * (x * b.scale) + b.offset) + to.pos;
		CHECK_NEAR(Distance(before, after), 0.0, 1e-4);
	}
}

TEST(ObjectAnchorGuessAndOtherSide)
{
	// a chest: on its front (-Y) face, in the middle, below the lid, facing into the chest
	const ObjectAnchor chest = GuessObjectAnchor({ -40, -25, 0 }, { 40, 25, 50 }, false);
	CHECK(chest.valid);
	CHECK_NEAR(chest.keyhole.x, 0.0, 1e-4);
	CHECK(chest.keyhole.y < -25.0f && chest.keyhole.y > -27.0f);
	CHECK(chest.keyhole.z > 25.0f && chest.keyhole.z < 40.0f);
	CHECK_NEAR(Dot(chest.rot.Column(1), Vec3{ 0, 1, 0 }), 1.0, 1e-4);
	CHECK_NEAR(Dot(chest.rot.Column(0), Vec3{ 1, 0, 0 }), 1.0, 1e-4);  // right, as MakeLockFrame
	// a door hinged at its origin, thin along Y: at handle height, near its free edge
	const ObjectAnchor door = GuessObjectAnchor({ 0, -4, 0 }, { 90, 4, 180 }, true);
	CHECK(door.keyhole.x > 70.0f && door.keyhole.x < 90.0f);
	CHECK_NEAR(door.keyhole.z, 64.0, 1e-3);
	CHECK_NEAR(Dot(door.rot.Column(1), Vec3{ 0, 1, 0 }), 1.0, 1e-4);
	// thin along X instead, free edge at -Y
	const ObjectAnchor side = GuessObjectAnchor({ -3, -100, 0 }, { 3, 0, 150 }, true);
	CHECK_NEAR(Dot(side.rot.Column(1), Vec3{ 1, 0, 0 }), 1.0, 1e-4);
	CHECK(side.keyhole.y < -80.0f);
	CHECK(side.keyhole.x < -3.0f);
	// the other side: through the door, facing back; twice = the same
	const ObjectAnchor back = OtherSide(door, { 0, -4, 0 }, { 90, 4, 180 });
	CHECK_NEAR(back.keyhole.x, door.keyhole.x, 1e-4);
	CHECK_NEAR(back.keyhole.y, -door.keyhole.y, 1e-4);
	CHECK_NEAR(Dot(back.rot.Column(1), Vec3{ 0, -1, 0 }), 1.0, 1e-4);
	CHECK_NEAR(Dot(Cross(back.rot.Column(0), back.rot.Column(1)), back.rot.Column(2)), 1.0, 1e-4);  // still a rotation
	const ObjectAnchor again = OtherSide(back, { 0, -4, 0 }, { 90, 4, 180 });
	CHECK_NEAR(Distance(again.keyhole, door.keyhole), 0.0, 1e-4);
	CHECK_NEAR(Dot(again.rot.Column(0), door.rot.Column(0)), 1.0, 1e-4);
	// a trapdoor hinged at its origin, lying flat: on top, near the free edge, facing down
	const ObjectAnchor trap = GuessObjectAnchor({ -50, 0, -4 }, { 50, 120, 4 }, true);
	CHECK(trap.keyhole.y > 100.0f && trap.keyhole.y < 120.0f);
	CHECK(trap.keyhole.z > 4.0f && trap.keyhole.z < 5.0f);
	CHECK_NEAR(Dot(trap.rot.Column(1), Vec3{ 0, 0, -1 }), 1.0, 1e-4);
	CHECK_NEAR(Dot(trap.rot.Column(2), Vec3{ 0, -1, 0 }), 1.0, 1e-4);
	const ObjectAnchor below = OtherSide(trap, { -50, 0, -4 }, { 50, 120, 4 });
	CHECK(below.keyhole.z < -4.0f);
	CHECK_NEAR(Dot(below.rot.Column(1), Vec3{ 0, 0, 1 }), 1.0, 1e-4);
}

TEST(ObjectAnchorPlacementRoundTrip)
{
	ObjectAnchor a;
	a.keyhole = { 12, -30, 41 };
	a.rot = AxisAngle({ 0, 0, 1 }, 0.3f) * AxisAngle({ 1, 0, 0 }, -0.2f);
	a.valid = true;
	Pose object;
	object.rot = AxisAngle({ 0, 0, 1 }, 2.1f);
	object.pos = { 1000, -2000, 50 };
	const Pose lock = PlaceOnObject(a, object, 1.5f);
	CHECK_NEAR(Distance(lock.pos, object.pos + object.rot * (a.keyhole * 1.5f)), 0.0, 1e-3);
	const ObjectAnchor b = ObjectAnchorFromPose(lock, object, 1.5f);
	CHECK_NEAR(Distance(b.keyhole, a.keyhole), 0.0, 1e-3);
	CHECK_NEAR(Dot(b.rot.Column(1), a.rot.Column(1)), 1.0, 1e-4);
	CHECK_NEAR(Dot(b.rot.Column(2), a.rot.Column(2)), 1.0, 1e-4);
	// saved and read back, next to a lock profile, under the object's model
	std::string text = WriteLockProfile("", "Interface\\Lockpicking\\LockPickShiv01.nif", LockProfile{ { 1, 2, 3 }, 0, 0, true });
	text = WriteObjectAnchor(text, "Meshes\\Clutter\\Containers\\Chest01.NIF", a);
	const auto all = ParseObjectAnchors(text);
	CHECK(all.size() == 1);
	CHECK(ParseLockProfiles(text).size() == 1);
	const auto it = all.find("clutter\\containers\\chest01.nif");
	CHECK(it != all.end());
	if (it != all.end()) {
		CHECK_NEAR(Distance(it->second.keyhole, a.keyhole), 0.0, 2e-3);
		CHECK_NEAR(Dot(it->second.rot.Column(0), a.rot.Column(0)), 1.0, 1e-4);
	}
	CHECK(ObjectAnchorText(a).find("vObjectKeyhole = 12.000, -30.000, 41.000") != std::string::npos);
	for (const auto& [model, d] : DefaultObjectAnchors()) {
		CHECK(d.valid);
		CHECK_NEAR(Dot(Cross(d.rot.Column(0), d.rot.Column(1)), d.rot.Column(2)), 1.0, 1e-4);
	}
}

namespace
{
	// a little plugin file, written the way the game's are
	struct PluginWriter
	{
		std::string bytes;

		static std::string U32(std::uint32_t a_v) { return std::string{ char(a_v), char(a_v >> 8), char(a_v >> 16), char(a_v >> 24) }; }
		static std::string Field(const char* a_type, const std::string& a_data)
		{
			return std::string(a_type, 4) + char(a_data.size()) + char(a_data.size() >> 8) + a_data;
		}
		static std::string Record(const char* a_type, const std::string& a_fields, std::uint32_t a_flags = 0, std::uint32_t a_id = 0)
		{
			return std::string(a_type, 4) + U32(std::uint32_t(a_fields.size())) + U32(a_flags) + U32(a_id) + std::string(8, '\0') + a_fields;
		}
		static std::string Group(const char* a_label, const std::string& a_content, std::uint32_t a_type = 0)
		{
			return std::string("GRUP") + U32(std::uint32_t(a_content.size() + 24)) + std::string(a_label, 4) + U32(a_type) + std::string(8, '\0') + a_content;
		}
		static std::string Ref(std::uint32_t a_base, bool a_locked, std::uint32_t a_flags = 0, char a_level = 0)
		{
			std::string f = Field("NAME", U32(a_base));
			if (a_locked) {
				f += Field("XLOC", a_level + std::string(19, '\0'));
			}
			f += Field("DATA", std::string(24, '\0'));
			return Record("REFR", f, a_flags);
		}
	};
}

TEST(LockableModelsRoundTrip)
{
	std::vector<LockableModel> list{ { "Clutter\\Containers\\Chest01.nif", false, { -40, -25, 0 }, { 40, 25, 50 }, 312, "Chest" },
		{ "Architecture\\Whiterun\\WRDoor01.nif", true, { 0, -4, 0 }, { 90, 4, 180 }, 7, "" } };
	const auto back = ParseLockableModels(WriteLockableModels(list) + "garbage line\r\n");
	CHECK(back.size() == 2);
	if (back.size() == 2) {
		CHECK(back[0].model == list[0].model && !back[0].door && back[0].locked == 312 && back[0].name == "Chest");
		CHECK(back[1].door && back[1].name.empty());
		CHECK_NEAR(Distance(back[1].boundMax, Vec3{ 90, 4, 180 }), 0.0, 1e-4);
		CHECK_NEAR(Distance(back[0].boundMin, Vec3{ -40, -25, 0 }), 0.0, 1e-4);
	}
}

TEST(PluginScanFindsLockedReferences)
{
	using W = PluginWriter;
	const std::string tes4 = W::Record("TES4", W::Field("HEDR", std::string(12, '\0')) + W::Field("MAST", std::string("Skyrim.esm") + '\0') +
	                                               W::Field("DATA", std::string(8, '\0')) + W::Field("MAST", std::string("Dawnguard.esm") + '\0') +
	                                               W::Field("DATA", std::string(8, '\0')));
	// a chest base record outside the cells: not a reference, skipped
	const std::string conts = W::Group("CONT", W::Record("CONT", W::Field("EDID", "Chest\0") + W::Field("NAME", W::U32(0x0000AAAA))));
	// an interior cell: two locked chests of one kind, an open one, a locked door from Dawnguard,
	// one compressed reference, and a door only its key opens
	const std::string cellChildren = W::Ref(0x00012345, true) + W::Ref(0x00012345, true) + W::Ref(0x00099999, false) +
	                                 W::Ref(0x01000777, true) + W::Ref(0x00055555, true, 0x00040000) +
	                                 W::Ref(0x00066666, true, 0, char(0xFF));
	const std::string cells = W::Group("CELL", W::Group("\0\0\0\0", W::Record("CELL", W::Field("EDID", "Test\0")) +
	                                                                    W::Group("\x01\0\0\0", cellChildren, 6), 2));
	// an exterior: a locked door the plugin itself adds (index 2 = after its two masters)
	const std::string worlds = W::Group("WRLD", W::Record("WRLD", W::Field("EDID", "Tamriel\0")) +
	                                                W::Group("\x3C\0\0\0", W::Group("\0\0\0\0", W::Ref(0x02000801, true), 9), 1));
	std::istringstream in(tes4 + conts + cells + worlds);
	const LockedRefScan scan = ScanLockedReferences(in);
	CHECK(scan.ok);
	CHECK(scan.masters.size() == 2 && scan.masters[0] == "Skyrim.esm" && scan.masters[1] == "Dawnguard.esm");
	CHECK(scan.references == 7);
	CHECK(scan.compressedSkipped == 1);
	CHECK(scan.bases.size() == 3);
	CHECK(scan.bases.contains(0x00012345) && scan.bases.at(0x00012345) == 2);
	CHECK(!scan.bases.contains(0x00099999));
	CHECK(!scan.bases.contains(0x00066666));
	CHECK(!scan.bases.contains(0x0000AAAA));
	CHECK(scan.bases.contains(0x01000777) && scan.bases.contains(0x02000801));
	CHECK(FormFile(0x00012345, scan, "Mine.esp") == "Skyrim.esm");
	CHECK(FormFile(0x01000777, scan, "Mine.esp") == "Dawnguard.esm");
	CHECK(FormFile(0x02000801, scan, "Mine.esp") == "Mine.esp");

	std::istringstream notPlugin("not a plugin at all, just text");
	CHECK(!ScanLockedReferences(notPlugin).ok);
}

TEST(LockVariationsDescribesItsSections)
{
	LockVariations lv;
	lv.AddFile("[Dwemer]\nChest = A.nif\nDoor = B.nif\nLockpick = C.nif\n[]\nChest = D.nif\n", "x_LID.ini");
	const auto lines = lv.Describe();
	CHECK(lines.size() == 2);
	CHECK(!lines.empty() && lines[0] == "[Dwemer] (x_LID.ini): 1 chest, 1 door, 1 lockpick model(s)");
}

TEST(StaleMcmValuesAreLeftOut)
{
	const std::set<std::string> offered = McmSettingKeys(R"({ "content": [ { "id": "bEnabled:General", "type": "toggle" },
		{ "text": "x", "type": "text" }, { "id" : "fToolMeshLength:Visuals" } ] })");
	CHECK(offered.size() == 2 && offered.contains("general.benabled") && offered.contains("visuals.ftoolmeshlength"));
	std::vector<std::string> dropped;
	const std::string        kept = KeepIniKeys("\xEF\xBB\xBF[General]\nbEnabled=0\nbInGameEditor=1\n; note\n[Visuals]\nfToolMeshLength = 2\nbFlipPickMesh=1\n", offered, &dropped);
	CHECK(dropped.size() == 2);
	Config c;
	CHECK(c.LoadFromIni(kept).empty());
	CHECK(!c.enabled);
	CHECK_NEAR(c.toolMeshLength, 2.0, 1e-6);
	CHECK(!c.flipPickMesh);
	CHECK(kept.find("; note") != std::string::npos);
}

TEST(IniParsing)
{
	const std::string ini = R"(
; comment
[General]
bEnabled = false
bLeftHandedPick=1   ; inline comment
[Controls]
fInsertRadius = 8.5
vPickTipOffset = 1, 2.5, -3
[Visuals]
sMissing = "quoted ; value"
fLockMeshRadius = abc
)";
	Config c;
	c.LoadFromIni(ini);
	CHECK(!c.enabled);
	CHECK(c.leftHandedPick);
	CHECK_NEAR(c.insertRadius, 8.5, 1e-6);
	CHECK_NEAR(c.pickTipOffset.x, 1.0, 1e-6);
	CHECK_NEAR(c.pickTipOffset.y, 2.5, 1e-6);
	CHECK_NEAR(c.pickTipOffset.z, -3.0, 1e-6);
	CHECK_NEAR(c.lockMeshRadius, Config{}.lockMeshRadius, 1e-6);  // malformed -> default kept
	CHECK(c.holdRadius >= c.insertRadius);
}

int main()
{
	for (auto& t : Registry()) {
		const int before = g_failures;
		std::printf("[ RUN  ] %s\n", t.name);
		t.fn();
		std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
	}
	std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

TEST(SetIniValuesKeepsTheRest)
{
	const std::string in = "; my comment\n[General]\nbEnabled=1\n\n[Visuals]\nfLockMeshRadius = 1.0 ; inline\nbFlip=0\n\n[Other]\nx=1\n";
	const std::string out = SetIniValues(in, { { "Visuals", "fLockMeshRadius", "2.5" }, { "visuals", "fNew", "3" }, { "Controls", "fToolLength", "12" }, { "General", "bEnabled", "0" } });
	CHECK(out.find("; my comment") != std::string::npos);
	CHECK(out.find("fLockMeshRadius=2.5") != std::string::npos);
	CHECK(out.find("fLockMeshRadius = 1.0") == std::string::npos);
	CHECK(out.find("bEnabled=0") != std::string::npos);
	CHECK(out.find("x=1") != std::string::npos);
	// the new key lands inside [Visuals], before [Other]
	CHECK(out.find("fNew=3") < out.find("[Other]"));
	CHECK(out.find("fNew=3") > out.find("[Visuals]"));
	CHECK(out.find("[Controls]\nfToolLength=12") != std::string::npos);
	Config c;
	CHECK(c.LoadFromIni(out).size() == 3);  // bflip, fnew, x are not ours
	CHECK_NEAR(c.lockMeshRadius, 2.5, 1e-6);
	CHECK_NEAR(c.toolLength, 12.0, 1e-6);
	CHECK(!c.enabled);
	CHECK(SetIniValues("", { { "A", "k", "v" } }) == "[A]\nk=v\n");
}

TEST(McmFileWithUtf8Signature)
{
	// MCM Helper writes a UTF-8 BOM; the first section must still be recognised
	const std::string text = "\xEF\xBB\xBF[Visuals]\r\nbDebugView=1\r\nbShowTargets=1\r\n[General]\r\nbEnabled=0\r\n";
	Config c;
	CHECK(c.LoadFromIni(text).empty());
	CHECK(c.debugView);
	CHECK(c.showTargets);
	CHECK(!c.enabled);
	const std::string out = SetIniValues(text, { { "Visuals", "fLockMeshRadius", "2" } });
	CHECK(out.find("[Visuals]\nbDebugView=1\nbShowTargets=1\nfLockMeshRadius=2\n") == 0);
}

TEST(VectorKeys)
{
	Config c;
	CHECK(c.LoadFromIni("[Controls]\nvGripRotation = 1, 2, 3\nvHandGripPos = 4 5 6\nvPickTipOffset = bad\n").empty());
	CHECK_NEAR(Distance(c.gripRotation, Vec3{ 1, 2, 3 }), 0.0, 1e-6);
	CHECK_NEAR(Distance(c.handGripPos, Vec3{ 4, 5, 6 }), 0.0, 1e-6);
	CHECK_NEAR(Length(c.pickTipOffset), 0.0, 1e-6);  // malformed: kept
}

// --- Lock Variations (*_LID.ini) ---

namespace
{
	LockVariations::Query LvQuery(const std::string& a_model, bool a_door = false)
	{
		LockVariations::Query q;
		q.modelPath = a_model;
		q.isDoor = a_door;
		return q;
	}

	std::function<bool(const std::string&)> LvIs(std::set<std::string> a_ids)
	{
		return [a_ids](const std::string& a_id) { return a_ids.contains(a_id); };
	}
}

TEST(LockVariationsSubstringAndCase)
{
	LockVariations lv;
	lv.AddFile("[Clutter/Containers/Chest01]\nChest = Interface/Lockpicking/IronLock.nif\n", "A_LID.ini");
	CHECK(lv.SectionCount() == 1);
	auto c = lv.Choose(LvQuery("Meshes\\CLUTTER\\Containers\\Chest01Large.nif"));
	CHECK(c.lockModel == "Interface/Lockpicking/IronLock.nif");  // kept as written
	CHECK(c.pickModel.empty());
	CHECK(c.any());
	CHECK(c.lockSource.find("A_LID.ini") != std::string::npos);
	CHECK(!lv.Choose(LvQuery("clutter\\containers\\barrel01.nif")).any());
	// a leading "meshes\" on the section is not part of the substring either
	LockVariations lv2;
	lv2.AddFile("[meshes\\clutter\\containers\\chest01]\nChest=x\\lock.nif\n", "B_LID.ini");
	CHECK(lv2.Choose(LvQuery("clutter/containers/chest01.nif")).lockModel == "x\\lock.nif");
}

TEST(LockVariationsCommentsAndBlankLines)
{
	LockVariations lv;
	lv.AddFile(
		"\xEF\xBB\xBF; header comment\n"
		"\n"
		"# another\r\n"
		"Chest = outside\\any\\section.nif\r\n"
		"[chest] ; trailing comment\r\n"
		"   \r\n"
		";Chest = commented\\out.nif\n"
		"Chest = my\\lock.nif ; why\n"
		"NotAKey\n"
		"Chest =\n",
		"C_LID.ini");
	CHECK(lv.SectionCount() == 1);
	const auto c = lv.Choose(LvQuery("clutter\\chest.nif"));
	CHECK(c.lockModel == "my\\lock.nif");
}

TEST(LockVariationsDoorVsChest)
{
	LockVariations lv;
	lv.AddFile("[]\nChest = c.nif\nDoor = d.nif\nDoor2 = d2.nif\n", "D_LID.ini");
	CHECK(lv.Choose(LvQuery("anything.nif", false)).lockModel == "c.nif");
	CHECK(lv.Choose(LvQuery("anything.nif", true)).lockModel == "d.nif");
	// empty model path still matches the catch-all
	CHECK(lv.Choose(LvQuery("", true)).lockModel == "d.nif");
}

TEST(LockVariationsLockpickChoice)
{
	LockVariations lv;
	lv.AddFile("[]\nLockpick = Interface/Lockpicking/BonePick.nif\nLockpickingUnlock = MyUnlockSound\n", "E_LID.ini");
	const auto c = lv.Choose(LvQuery("x.nif"));
	CHECK(c.lockModel.empty());
	CHECK(c.pickModel == "Interface/Lockpicking/BonePick.nif");
	// no lock model: the sounds come with the pick's section
	CHECK(c.sounds.unlock == "MyUnlockSound");

	// "LockpickingUnlock" is a sound, never a lockpick model
	LockVariations lv2;
	lv2.AddFile("[]\nLockpickingUnlock = Interface/Lockpicking/NotAPick.nif\nlockpickingunlock = Snd\n", "F_LID.ini");
	CHECK(!lv2.Choose(LvQuery("x.nif")).any());
}

TEST(LockVariationsLockAndPickIndependent)
{
	LockVariations lv;
	lv.AddFile(
		"[dwemer]\nChest = dwe\\lock.nif\nCylinderTurn = DweTurn\n"
		"[]\nChest = generic\\lock.nif\nLockpick = generic\\pick.nif\nCylinderTurn = GenTurn\n",
		"G_LID.ini");
	const auto c = lv.Choose(LvQuery("dungeons\\dwemer\\chest.nif"));
	CHECK(c.lockModel == "dwe\\lock.nif");
	CHECK(c.pickModel == "generic\\pick.nif");
	CHECK(c.sounds.turn == "DweTurn");  // sounds follow the lock model's section
}

TEST(LockVariationsDefaultModelsSkipped)
{
	LockVariations lv;
	lv.AddFile(
		"[chest]\n"
		"Chest = meshes/interface/lockpicking/lockpickshiv01.NIF\n"
		"Lockpick = Interface\\Lockpicking\\LockPick01.nif\n"
		"Chest = second\\lock.nif\n"
		"[]\n"
		"Lockpick = fallback\\pick.nif\n",
		"H_LID.ini");
	const auto c = lv.Choose(LvQuery("chest.nif"));
	CHECK(c.lockModel == "second\\lock.nif");
	CHECK(c.pickModel == "fallback\\pick.nif");  // the vanilla pick in [chest] does not count
}

TEST(LockVariationsForwardSlashes)
{
	LockVariations lv;
	lv.AddFile("[Architecture/Whiterun/WRDoor]\nDoor = Interface/Lockpicking/WR.nif\n", "I_LID.ini");
	CHECK(lv.Choose(LvQuery("Architecture\\Whiterun\\WRDoorMain01.nif", true)).lockModel == "Interface/Lockpicking/WR.nif");
	CHECK(lv.Choose(LvQuery("data/meshes/architecture/whiterun/wrdoormain01.nif", true)).lockModel == "Interface/Lockpicking/WR.nif");
	CHECK(!lv.Choose(LvQuery("architecture/windhelm/whdoor.nif", true)).any());
}

TEST(LockVariationsLocationGating)
{
	LockVariations lv;
	lv.AddFile(
		"[chest|RiftenLocation]\nChest = riften\\lock.nif\n"
		"[chest]\nChest = plain\\lock.nif\n",
		"J_LID.ini");
	CHECK(lv.SectionCount() == 2);

	auto q = LvQuery("chest.nif");
	q.locationMatches = LvIs({ "RiftenLocation" });
	CHECK(lv.Choose(q).lockModel == "riften\\lock.nif");

	q.locationMatches = LvIs({ "WhiterunLocation" });
	CHECK(lv.Choose(q).lockModel == "plain\\lock.nif");

	// no location known: the location condition does not apply
	q.locationMatches = nullptr;
	CHECK(lv.Choose(q).lockModel == "riften\\lock.nif");

	// the location id is offered for resolving
	const auto ids = lv.ConditionIds();
	CHECK(std::find(ids.begin(), ids.end(), "RiftenLocation") != ids.end());
}

TEST(LockVariationsConditionalOrdering)
{
	LockVariations lv;
	lv.AddFile(
		"[]\n"
		"Chest = plain.nif\n"
		"Chest|TexA,0x1234~Foo.esp = cond.nif\n"
		"Chest|TexB|underwater = wet.nif\n"
		"Chest|TexC = cond2.nif\n",
		"K_LID.ini");
	auto q = LvQuery("x.nif");
	CHECK(lv.Choose(q).lockModel == "plain.nif");  // nothing matches, no predicate

	q.conditionMatches = LvIs({ "0x1234~Foo.esp" });
	CHECK(lv.Choose(q).lockModel == "cond.nif");  // conditional before plain, though written after

	q.conditionMatches = LvIs({ "TexC", "TexA" });
	CHECK(lv.Choose(q).lockModel == "cond.nif");  // file order among conditional ones

	q.conditionMatches = LvIs({ "TexC" });
	CHECK(lv.Choose(q).lockModel == "cond2.nif");

	// flagged models come first; the underwater flag decides on its own
	q.underwater = true;
	q.conditionMatches = nullptr;
	CHECK(lv.Choose(q).lockModel == "wet.nif");

	const auto ids = lv.ConditionIds();
	CHECK(ids.size() == 4);
	CHECK(std::find(ids.begin(), ids.end(), "0x1234~Foo.esp") != ids.end());
	CHECK(std::find(ids.begin(), ids.end(), "TexB") != ids.end());
}

TEST(LockVariationsUnderwaterFlag)
{
	LockVariations lv;
	lv.AddFile("[]\nDoor||Underwater = wet\\door.nif\n", "L_LID.ini");
	auto q = LvQuery("door.nif", true);
	CHECK(!lv.Choose(q).any());
	q.underwater = true;
	CHECK(lv.Choose(q).lockModel == "wet\\door.nif");
	CHECK(lv.ConditionIds().empty());
}

TEST(LockVariationsSounds)
{
	LockVariations lv;
	lv.AddFile(
		"[special]\nChest = s.nif\nCylinderSqueakA = SqA\ncylindersqueakb = SqB\nCYLINDERSTOP = St\nCylinderTurn = Tu\nPickMovement = Pm\nLockpickingUnlock = Un\n"
		"[]\nChest = g.nif\n",
		"M_LID.ini");
	const auto s = lv.Choose(LvQuery("special.nif")).sounds;
	CHECK(s.squeakA == "SqA");
	CHECK(s.squeakB == "SqB");
	CHECK(s.stop == "St");
	CHECK(s.turn == "Tu");
	CHECK(s.pickMovement == "Pm");
	CHECK(s.unlock == "Un");

	const auto d = lv.Choose(LvQuery("other.nif")).sounds;
	CHECK(d.squeakA == "UILockpickingCylinderSqueakA");
	CHECK(d.squeakB == "UILockpickingCylinderSqueakB");
	CHECK(d.stop == "UILockpickingCylinderStop");
	CHECK(d.turn == "UILockpickingCylinderTurn");
	CHECK(d.pickMovement == "UILockpickingPickMovement");
	CHECK(d.unlock == "UILockpickingUnlock");

	// nothing chosen: vanilla sounds
	LockVariations empty;
	CHECK(!empty.Choose(LvQuery("x.nif")).any());
	CHECK(empty.Choose(LvQuery("x.nif")).sounds.turn == "UILockpickingCylinderTurn");
}

TEST(LockVariationsMergeAcrossFiles)
{
	LockVariations lv;
	lv.AddFile("[Chest]\nChest|TexA = first.nif\nCylinderTurn = TurnA\nCylinderStop = StopA\n", "A_LID.ini");
	lv.AddFile("[CHEST]\nChest|TexB = second.nif\nChest = plain.nif\nCylinderTurn = TurnB\n", "B_LID.ini");
	CHECK(lv.SectionCount() == 1);

	auto q = LvQuery("chest.nif");
	q.conditionMatches = LvIs({ "TexA", "TexB" });
	auto c = lv.Choose(q);
	CHECK(c.lockModel == "first.nif");  // the later file's models come after
	CHECK(c.sounds.turn == "TurnB");    // its sounds replace
	CHECK(c.sounds.stop == "StopA");    // ...key by key
	CHECK(c.lockSource.find("A_LID.ini") != std::string::npos);
	CHECK(c.lockSource.find("B_LID.ini") != std::string::npos);

	q.conditionMatches = LvIs({ "TexB" });
	CHECK(lv.Choose(q).lockModel == "second.nif");
	q.conditionMatches = nullptr;
	CHECK(lv.Choose(q).lockModel == "plain.nif");
}

TEST(LockVariationsSectionPriority)
{
	LockVariations lv;
	// written generic first, in separate files, to show the order does not come from the files
	lv.AddFile("[]\nChest = any.nif\n[chest]\nChest = chest.nif\n", "A_LID.ini");
	lv.AddFile("[containers\\chest]\nChest = containers.nif\n[a]\nChest = a.nif\n", "B_LID.ini");
	CHECK(lv.SectionCount() == 4);

	// the longest matching substring wins over a shorter one that also matches
	CHECK(lv.Choose(LvQuery("clutter\\containers\\chest01.nif")).lockModel == "containers.nif");
	CHECK(lv.Choose(LvQuery("dungeons\\chest01.nif")).lockModel == "chest.nif");
	// "a" (shorter) is still before the catch-all
	CHECK(lv.Choose(LvQuery("barrel.nif")).lockModel == "a.nif");
	CHECK(lv.Choose(LvQuery("box.nif")).lockModel == "any.nif");

	// a specific section with no model of this kind falls through to the next one
	LockVariations lv2;
	lv2.AddFile("[chest]\nDoor = d.nif\n[]\nChest = any.nif\n", "C_LID.ini");
	CHECK(lv2.Choose(LvQuery("chest.nif")).lockModel == "any.nif");

	// same substring: the one with a location first, even when written after
	LockVariations lv3;
	lv3.AddFile("[chest]\nChest = plain.nif\n[chest|0x00018A56]\nChest = here.nif\n", "D_LID.ini");
	auto q = LvQuery("chest.nif");
	q.locationMatches = LvIs({ "0x00018A56" });
	CHECK(lv3.Choose(q).lockModel == "here.nif");
}

TEST(TranslationFilesParse)
{
	// UTF-16 LE with a byte order mark, as the game wants them; texts come back as UTF-8
	const std::u16string src = u"$VRLP_A\tПривет\r\n$VRLP_B\t{} \U0001F512 {}\r\nnot a key\r\n$VRLP_C\r\n";
	std::string bytes = "\xFF\xFE";
	for (const char16_t u : src) {
		bytes += static_cast<char>(u & 0xFF);
		bytes += static_cast<char>(u >> 8);
	}
	const auto t = ParseTranslations(bytes);
	CHECK(t.size() == 2);
	CHECK(t.at("$VRLP_A") == "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
	CHECK(t.at("$VRLP_B") == "{} \xF0\x9F\x94\x92 {}");
	// UTF-8 (with or without its signature) is read as is
	const auto u = ParseTranslations("\xEF\xBB\xBF$VRLP_A\tx\n$VRLP_B\ty z\n");
	CHECK(u.size() == 2);
	CHECK(u.at("$VRLP_A") == "x");
	CHECK(u.at("$VRLP_B") == "y z");
}

TEST(FormatTextFillsInOrder)
{
	CHECK(FormatText("{} chests and {} doors", { "3", "2" }) == "3 chests and 2 doors");
	CHECK(FormatText("no args", { "x" }) == "no args");
	CHECK(FormatText("{} and {}", { "one" }) == "one and {}");
	CHECK(FormatText("{}", {}) == "{}");
}

#ifdef VRLP_SOURCE_DIR
namespace
{
	std::string ReadFile(const char* a_path)
	{
		std::ifstream     file(a_path);
		std::stringstream ss;
		ss << file.rdbuf();
		return ss.str();
	}
}

TEST(VersionsMatch)
{
	// CMakeLists.txt is the source; every other place that states the version must agree
	const std::string cmake = ReadFile(VRLP_SOURCE_DIR "/CMakeLists.txt");
	const auto        at = cmake.find("project(VRLockpicking VERSION ") + 30;
	const std::string v = cmake.substr(at, cmake.find(' ', at) - at);
	const auto        dot1 = v.find('.'), dot2 = v.rfind('.');
	const std::string major = v.substr(0, dot1), minor = v.substr(dot1 + 1, dot2 - dot1 - 1), patch = v.substr(dot2 + 1);
	const std::string header = ReadFile(VRLP_SOURCE_DIR "/src/plugin/Version.h");
	CHECK(header.find("VRLP_VERSION_MAJOR " + major) != std::string::npos);
	CHECK(header.find("VRLP_VERSION_MINOR " + minor) != std::string::npos);
	CHECK(header.find("VRLP_VERSION_PATCH " + patch) != std::string::npos);
	const std::string rc = ReadFile(VRLP_SOURCE_DIR "/src/plugin/version.rc");
	CHECK(rc.find("FILEVERSION " + major + "," + minor + "," + patch + ",0") != std::string::npos);
	CHECK(rc.find("\"FileVersion\", \"" + v + "\"") != std::string::npos);
	CHECK(ReadFile(VRLP_SOURCE_DIR "/vcpkg.json").find("\"" + v + "\"") != std::string::npos);
}

TEST(McmDefaultsMatchConfig)
{
	// every MCM setting must be one the plugin reads, with the plugin's own default
	const std::string text = ReadFile(VRLP_SOURCE_DIR "/package/mcm/settings.ini");
	CHECK(!text.empty());
	Config     c;
	const auto unknown = c.LoadFromIni(text);
	for (const auto& k : unknown) {
		std::printf("    unknown MCM key %s\n", k.c_str());
	}
	CHECK(unknown.empty());
	CHECK(c == Config{});
	// and every control in config.json has a default
	const std::string json = ReadFile(VRLP_SOURCE_DIR "/package/mcm/config.json");
	const auto        keys = ParseIni(text);
	std::size_t       pos = 0, ids = 0;
	while ((pos = json.find("\"id\": \"", pos)) != std::string::npos) {
		pos += 7;
		const std::string id = json.substr(pos, json.find('"', pos) - pos);
		const auto        colon = id.find(':');
		std::string       key = id.substr(colon + 1) + "." + id.substr(0, colon);
		std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		if (!keys.contains(key)) {
			std::printf("    MCM control %s has no default\n", id.c_str());
		}
		CHECK(keys.contains(key));
		++ids;
	}
	CHECK(ids == keys.size());
	// the plugin reads the same list out of config.json to ignore stale MCM values
	const auto offered = McmSettingKeys(json);
	CHECK(offered.size() == keys.size());
	for (const auto& [key, value] : keys) {
		CHECK(offered.contains(key));
	}
}

TEST(ShippedIniMatchesDefaults)
{
	const std::string text = ReadFile(VRLP_SOURCE_DIR "/package/VRLockpicking.ini");
	CHECK(!text.empty());
	const Config d;
	Config       c;
	const auto   unknown = c.LoadFromIni(text);
	for (const auto& k : unknown) {
		std::printf("    unknown INI key %s\n", k.c_str());
	}
	CHECK(unknown.empty());
	CHECK(c == d);  // every value in the shipped INI is the default
}
TEST(TranslationsCoverEveryText)
{
	// the languages Skyrim VR runs in; a language without a file would show the raw $keys
	const char* const languages[] = { "ENGLISH", "FRENCH", "GERMAN", "ITALIAN", "SPANISH", "POLISH", "RUSSIAN", "JAPANESE", "CHINESE" };
	const std::string dir = VRLP_SOURCE_DIR "/package/translations/";
	const auto        english = ParseTranslations(ReadFile((dir + "VRLockpicking_ENGLISH.txt").c_str()));
	CHECK(english.size() > 100);
	auto placeholders = [](const std::string& a_text) {
		std::size_t n = 0;
		for (std::size_t pos = 0; (pos = a_text.find("{}", pos)) != std::string::npos; pos += 2) {
			++n;
		}
		return n;
	};

	// every key the settings menu and the plugin use is in English, and every English key is used
	std::set<std::string> used;
	auto collect = [&used](const std::string& a_text) {
		for (std::size_t pos = 0; (pos = a_text.find("\"$VRLP_", pos)) != std::string::npos;) {
			++pos;
			const std::size_t end = a_text.find('"', pos);
			const std::string key = a_text.substr(pos, end - pos);
			if (std::ranges::all_of(key.substr(1), [](unsigned char ch) { return std::isalnum(ch) || ch == '_'; })) {
				used.insert(key);  // not "$VRLP_..." in a comment
			}
			pos = end;
		}
	};
	const std::string json = ReadFile(VRLP_SOURCE_DIR "/package/mcm/config.json");
	collect(json);
	for (const char* field : { "\"text\": \"", "\"help\": \"", "\"pageDisplayName\": \"" }) {
		for (std::size_t pos = 0; (pos = json.find(field, pos)) != std::string::npos;) {
			pos += std::strlen(field);
			if (json[pos] != '$') {
				std::printf("    untranslated MCM text: %s\n", json.substr(pos, json.find('"', pos) - pos).c_str());
			}
			CHECK(json[pos] == '$');
		}
	}
	for (const auto& entry : std::filesystem::directory_iterator(VRLP_SOURCE_DIR "/src/plugin")) {
		collect(ReadFile(entry.path().string().c_str()));
	}
	for (const auto& key : used) {
		if (!english.contains(key)) {
			std::printf("    %s has no English text\n", key.c_str());
		}
		CHECK(english.contains(key));
	}
	for (const auto& [key, text] : english) {
		if (!used.contains(key)) {
			std::printf("    %s is not used\n", key.c_str());
		}
		CHECK(used.contains(key));
	}

	// every language has every text, with the same placeholders
	for (const char* language : languages) {
		const std::string path = dir + "VRLockpicking_" + language + ".txt";
		const auto        t = ParseTranslations(ReadFile(path.c_str()));
		if (t.size() != english.size()) {
			std::printf("    %s: %zu texts, English has %zu\n", language, t.size(), english.size());
		}
		CHECK(t.size() == english.size());
		for (const auto& [key, text] : english) {
			const auto it = t.find(key);
			if (it == t.end() || it->second.empty() || placeholders(it->second) != placeholders(text)) {
				std::printf("    %s: %s missing, empty or with other placeholders\n", language, key.c_str());
				CHECK(false);
			}
		}
	}
}
#endif
