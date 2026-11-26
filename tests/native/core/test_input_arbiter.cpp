// Host-side unit tests for the unified input arbitration core (InputArbiterCore.h).
// Shares the exact header used by InputArbiter.cpp so priority/mask/pointer regressions
// are caught directly on PC without requiring a device.
#include "check.h"

#include "InputArbiterCore.h"

#include <cstdint>
#include <utility>
#include <vector>

using namespace input_arbiter;

static void testOverlayMasksDisjointAndCoverage()
{
	// Java-owned and GameMain-owned bit ranges must never overlap so lock-free CAS
	// updates from GameMain cannot clobber Android UI thread bits.
	CHECK_EQ(kJavaOverlayMask & kGameMainOverlayMask, 0u);

	// Modal touch block layers: JavaDialog, JavaKeyboard, ImGuiKeyboard, Scoreboard.
	CHECK(shouldBypassExternalTouch(kOverlayJavaDialog));
	CHECK(shouldBypassExternalTouch(kOverlayJavaKeyboard));
	CHECK(shouldBypassExternalTouch(kOverlayImGuiKeyboard));
	CHECK(shouldBypassExternalTouch(kOverlayScoreboard));
	CHECK(isModalBlockingGtaTouch(kOverlayJavaDialog));
	CHECK(isModalBlockingGtaTouch(kOverlayJavaKeyboard));
	CHECK(isModalBlockingGtaTouch(kOverlayImGuiKeyboard));
	CHECK(isModalBlockingGtaTouch(kOverlayScoreboard));

	// Non-modal overlays must NOT block external touch / CLEO / TextDraw / GTA routing.
	const uint32_t nonModal = kOverlayCleoMenu | kOverlayServerMenu | kOverlayEditObject
			| kOverlayObjectSelect | kOverlayTextDrawSelect;
	CHECK(!shouldBypassExternalTouch(kOverlayNone));
	CHECK(!shouldBypassExternalTouch(nonModal));
	CHECK(!isModalBlockingGtaTouch(nonModal));

	// Java overlays are handled on the Java side before JNI Back dispatch;
	// all 7 native modal layers (including CLEO menu) must claim Back.
	CHECK(!hasNativeBackTarget(kOverlayNone));
	CHECK(!hasNativeBackTarget(kOverlayJavaDialog | kOverlayJavaKeyboard));
	CHECK(hasNativeBackTarget(kOverlayImGuiKeyboard));
	CHECK(hasNativeBackTarget(kOverlayScoreboard));
	CHECK(hasNativeBackTarget(kOverlayCleoMenu));
	CHECK(hasNativeBackTarget(kOverlayServerMenu));
	CHECK(hasNativeBackTarget(kOverlayEditObject));
	CHECK(hasNativeBackTarget(kOverlayObjectSelect));
	CHECK(hasNativeBackTarget(kOverlayTextDrawSelect));
}

static void testBackPriorityOrder()
{
	// Verify strict top-to-bottom dismissal order by stripping the top bit at each step.
	uint32_t flags = kOverlayImGuiKeyboard
			| kOverlayScoreboard
			| kOverlayCleoMenu
			| kOverlayServerMenu
			| kOverlayEditObject
			| kOverlayObjectSelect
			| kOverlayTextDrawSelect
			| kOverlayJavaDialog
			| kOverlayJavaKeyboard;

	CHECK(resolveTopBackTarget(flags) == NativeBackTarget::ImGuiKeyboard);
	flags &= ~kOverlayImGuiKeyboard;

	CHECK(resolveTopBackTarget(flags) == NativeBackTarget::Scoreboard);
	flags &= ~kOverlayScoreboard;

	CHECK(resolveTopBackTarget(flags) == NativeBackTarget::CleoMenu);
	flags &= ~kOverlayCleoMenu;

	CHECK(resolveTopBackTarget(flags) == NativeBackTarget::ServerMenu);
	flags &= ~kOverlayServerMenu;

	CHECK(resolveTopBackTarget(flags) == NativeBackTarget::EditObject);
	flags &= ~kOverlayEditObject;

	CHECK(resolveTopBackTarget(flags) == NativeBackTarget::ObjectSelect);
	flags &= ~kOverlayObjectSelect;

	CHECK(resolveTopBackTarget(flags) == NativeBackTarget::TextDrawSelect);
	flags &= ~kOverlayTextDrawSelect;

	// Remaining Java flags yield None on the native side.
	CHECK(resolveTopBackTarget(flags) == NativeBackTarget::None);
	CHECK(resolveTopBackTarget(kOverlayNone) == NativeBackTarget::None);
}

static void testRadarSuppressionAndGraceWindow()
{
	CHECK(!isRadarSuppressed(kOverlayNone, 1000, 0));
	CHECK(isRadarSuppressed(kOverlayJavaKeyboard, 1000, 0));
	CHECK(isRadarSuppressed(kOverlayImGuiKeyboard, 1000, 0));
	CHECK(!isRadarSuppressed(kOverlayScoreboard | kOverlayJavaDialog, 1000, 0));

	// 250ms grace window after Java keyboard closes so collapsing IME doesn't open radar map.
	CHECK(isRadarSuppressed(kOverlayNone, 1000, 1000));
	CHECK(isRadarSuppressed(kOverlayNone, 1100, 1000));
	CHECK(isRadarSuppressed(kOverlayNone, 1249, 1000));
	CHECK(!isRadarSuppressed(kOverlayNone, 1250, 1000));
	CHECK(!isRadarSuppressed(kOverlayNone, 2000, 1000));
	// Negative clock delta must not falsely suppress.
	CHECK(!isRadarSuppressed(kOverlayNone, 900, 1000));
}

static void testGtaPointerTracker()
{
	struct Ev { int type, num, x, y; };
	std::vector<Ev> log;
	auto emit = [&](int t, int n, int x, int y) { log.push_back({t, n, x, y}); };

	GtaPointerTracker tracker;
	CHECK_EQ(tracker.activeCount(), 0);

	// Finger 0 holds joystick; finger 1 drags camera.
	tracker.forward(GtaPointerTracker::kTouchDown, 0, 100, 500, emit);
	tracker.forward(GtaPointerTracker::kTouchDown, 1, 900, 400, emit);
	CHECK(tracker.isDown(0));
	CHECK(tracker.isDown(1));
	CHECK_EQ(tracker.activeCount(), 2);

	// Finger 1 hits an overlay (ButtonPanel / TextDraw / CLEO): release ONLY finger 1,
	// preserving finger 0's joystick hold and emitting finger 1's last known coordinates!
	tracker.forward(3 /* move */, 1, 915, 420, emit);
	log.clear();
	tracker.releasePointer(1, emit);
	CHECK_EQ(log.size(), 1u);
	CHECK_EQ(log[0].type, GtaPointerTracker::kTouchUp);
	CHECK_EQ(log[0].num, 1);
	CHECK_EQ(log[0].x, 915);
	CHECK_EQ(log[0].y, 420);
	CHECK(tracker.isDown(0));
	CHECK(!tracker.isDown(1));
	CHECK_EQ(tracker.activeCount(), 1);

	// Releasing an already-up pointer emits nothing.
	log.clear();
	tracker.releasePointer(1, emit);
	CHECK(log.empty());

	// Add fingers 2 and 3, then a modal opens -> releaseAll releases 0, 2, 3 cleanly
	// at their last recorded coordinates (never snapping to (0, 0)).
	tracker.forward(GtaPointerTracker::kTouchDown, 2, 300, 305, emit);
	tracker.forward(GtaPointerTracker::kTouchDown, 3, 400, 410, emit);
	CHECK_EQ(tracker.activeCount(), 3);

	log.clear();
	tracker.releaseAll(emit);
	CHECK_EQ(log.size(), 3u);
	CHECK_EQ(log[0].num, 0);
	CHECK_EQ(log[0].x, 100);
	CHECK_EQ(log[0].y, 500);
	CHECK_EQ(log[1].num, 2);
	CHECK_EQ(log[1].x, 300);
	CHECK_EQ(log[1].y, 305);
	CHECK_EQ(log[2].num, 3);
	CHECK_EQ(log[2].x, 400);
	CHECK_EQ(log[2].y, 410);
	CHECK_EQ(tracker.activeCount(), 0);

	// Second releaseAll is a no-op.
	log.clear();
	tracker.releaseAll(emit);
	CHECK(log.empty());

	// Out-of-range pointer IDs are forwarded safely without corrupting slot tracking.
	log.clear();
	tracker.forward(GtaPointerTracker::kTouchDown, -1, 1, 2, emit);
	tracker.forward(GtaPointerTracker::kTouchDown, 4, 3, 4, emit);
	tracker.releasePointer(-1, emit);
	tracker.releasePointer(4, emit);
	CHECK_EQ(log.size(), 2u);
	CHECK_EQ(tracker.activeCount(), 0);
	CHECK(!tracker.isDown(-1));
	CHECK(!tracker.isDown(4));
}

int main()
{
	RUN(testOverlayMasksDisjointAndCoverage);
	RUN(testBackPriorityOrder);
	RUN(testRadarSuppressionAndGraceWindow);
	RUN(testGtaPointerTracker);
	return check::finish();
}
