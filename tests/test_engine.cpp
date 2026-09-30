// Tests for the Engine (src/core/Engine.hpp): the MODE switch with HOLD/COMMIT and
// FOLLOW, snapshots, quantized reset, and the MATH screen (src/core/Math.hpp).

#include "Engine.hpp"
#include "Math.hpp"

#include "testing.hpp"

using namespace iqs;

struct EngineRig {
	Engine e;
	static constexpr float DT = 1e-3f;

	explicit EngineRig(const Sequence& s) {
		e.live = s;
		e.liveReplaced();
	}
	void tap(int b) {
		e.press(b);
		e.release(b);
	}
	// One clock pulse, then a few quiet samples.
	void clock() {
		e.process(DT, true, false, false);
		for (int i = 0; i < 9; i++)
			e.process(DT, false, false, false);
	}
	void idle() { e.process(DT, false, false, false); }
	const Track& live(int t = 0) const { return e.live.tracks[t]; }
	const Track& shadow(int t = 0) const { return e.shadow.tracks[t]; }
	int playing(int t = 0) const { return e.tr.playheads[t].step; }
	std::string message() const {
		const char* m = e.view().message;
		return m ? m : "(none)";
	}
};

static Sequence threeSteps(int duration) {
	return build({{10, duration, 1}, {20, duration, 1}, {30, duration, 1}});
}

// --- HOLD and COMMIT -----------------------------------------------------------

TEST(hold_edits_a_shadow_until_commit) {
	EngineRig r(threeSteps(1));
	r.e.setMode(MODE_HOLD);
	r.tap(FOCUS_CV_A);
	r.e.turnRight(5);
	CHECK_EQ((int) r.shadow().steps[0].cvA, 15);
	CHECK_EQ((int) r.live().steps[0].cvA, 10); // still playing the original
	r.clock();
	CHECK(r.e.cvA(0) == r.live().tableA[10]);
	// COMMIT with INDEX focused writes it back at once.
	r.tap(FOCUS_INDEX);
	r.tap(BUTTON_COMMIT);
	CHECK_EQ((int) r.live().steps[0].cvA, 15);
	// Still in HOLD: further edits go to the shadow again.
	r.tap(FOCUS_STEP);
	r.e.turnRight(1);
	CHECK_EQ((int) r.live().steps[0].cvA, 15);
}

TEST(leaving_hold_discards_uncommitted_edits) {
	EngineRig r(threeSteps(1));
	r.e.setMode(MODE_HOLD);
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_INSERT);
	r.tap(BUTTON_INSERT);
	CHECK_EQ(r.shadow().numSteps(), 5);
	r.e.setMode(MODE_EDIT);
	CHECK_EQ(r.live().numSteps(), 3);
	// The cursor sat on a step that only existed in the shadow; it is pulled back.
	CHECK(r.e.view().stepInPattern >= 0 && r.e.view().stepInPattern < 3);
}

TEST(commit_quantized_to_the_step) {
	EngineRig r(threeSteps(4));
	r.clock(); // step 0 starts
	r.e.setMode(MODE_HOLD);
	r.tap(FOCUS_CV_A);
	r.e.turnRight(1);
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_COMMIT);
	CHECK_EQ((int) r.e.commitLed(), (int) LED_BLINK);
	r.clock();
	r.clock();
	r.clock();
	CHECK_EQ((int) r.live().steps[0].cvA, 10); // step 0 is still going
	r.clock();                                 // step 1 starts: now
	CHECK_EQ((int) r.live().steps[0].cvA, 11);
	CHECK_EQ((int) r.e.commitLed(), (int) LED_OFF);
	CHECK_EQ(r.playing(), 1);
}

TEST(commit_quantized_to_the_pattern_and_track) {
	Sequence seq = build({{1, 1, 1}, {2, 1, 1}});
	seq.appendPattern(0);
	seq.appendStep(0, makeStep(3, 1, 1));
	EngineRig r(seq);
	r.clock(); // step 0
	r.e.setMode(MODE_HOLD);
	r.tap(FOCUS_PATTERN);
	r.tap(FOCUS_CV_A);
	r.e.turnRight(40); // the cursor's step: step 0
	r.tap(BUTTON_COMMIT);
	r.clock(); // step 1: same pattern
	CHECK_EQ((int) r.live().steps[0].cvA, 1);
	r.clock(); // step 2: pattern 2 begins
	CHECK_EQ((int) r.live().steps[0].cvA, 41);

	// TRACK: waits for the wrap-around.
	r.e.turnRight(1);
	r.tap(FOCUS_TRACK);
	r.tap(BUTTON_COMMIT);
	CHECK_EQ((int) r.live().steps[0].cvA, 41);
	r.clock(); // back to step 0: the track ended
	CHECK_EQ((int) r.live().steps[0].cvA, 42);
}

TEST(second_commit_press_commits_at_once) {
	EngineRig r(threeSteps(8));
	r.clock();
	r.e.setMode(MODE_HOLD);
	r.tap(FOCUS_CV_A);
	r.e.turnRight(3);
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_COMMIT);
	CHECK_EQ((int) r.live().steps[0].cvA, 10);
	r.tap(BUTTON_COMMIT);
	CHECK_EQ((int) r.live().steps[0].cvA, 13);
}

TEST(commit_outside_hold_does_nothing) {
	EngineRig r(threeSteps(1));
	r.tap(BUTTON_COMMIT);
	CHECK_EQ((int) r.e.commitLed(), (int) LED_OFF);
}

// --- FOLLOW --------------------------------------------------------------------

TEST(follow_shows_the_play_cursor) {
	EngineRig r(threeSteps(1));
	r.e.setMode(MODE_FOLLOW);
	r.clock();
	r.clock();
	CHECK_EQ(r.e.view().stepInPattern, 1);
	r.clock();
	CHECK_EQ(r.e.view().stepInPattern, 2);
}

TEST(follow_refuses_edits_unless_paused) {
	EngineRig r(threeSteps(1));
	r.e.setMode(MODE_FOLLOW);
	r.clock();
	r.tap(FOCUS_CV_A);
	r.e.turnRight(1);
	CHECK_EQ((int) r.live().steps[0].cvA, 10);
	CHECK_EQ(r.message(), std::string("TILt"));
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_INSERT);
	r.tap(BUTTON_DELETE);
	CHECK_EQ(r.live().numSteps(), 3);
	// Loop points and smoothing are fine on a moving target.
	r.tap(BUTTON_LOOP_START);
	CHECK_EQ(r.live().loopStart, 0);
	// Paused, editing works (on the step under the play cursor).
	r.e.tr.paused = true;
	r.idle();
	r.tap(FOCUS_CV_A);
	r.e.turnRight(1);
	CHECK_EQ((int) r.live().steps[0].cvA, 11);
}

TEST(follow_scrubs_the_play_cursor) {
	EngineRig r(threeSteps(4));
	r.e.setMode(MODE_FOLLOW);
	r.clock();
	r.tap(FOCUS_STEP);
	r.e.turnLeft(2);
	CHECK_EQ(r.playing(), 2);
	r.clock();
	CHECK_EQ(r.playing(), 2); // it plays on from there
}

// --- Snapshots -------------------------------------------------------------------

TEST(save_and_load_a_snapshot) {
	EngineRig r(threeSteps(1));
	r.e.live.tracks[1].options.clockDiv = 3;
	r.tap(BUTTON_SAVE);
	CHECK(r.message() == "Abrt" || r.message() == "");
	r.tap(BUTTON_SAVE); // confirm: into slot 1
	CHECK(r.e.snapshotUsed[0]);
	// Change things, move around, then load it back.
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_DELETE);
	r.e.live.tracks[1].options.clockDiv = 1;
	r.clock();
	r.clock();
	r.tap(BUTTON_LOAD);
	r.tap(BUTTON_LOAD);
	CHECK_EQ(r.live().numSteps(), 3);
	CHECK_EQ((int) r.live(1).options.clockDiv, 3);
	CHECK(r.e.tr.playheads[0].armed());
	CHECK_EQ(r.playing(), 0);
	CHECK_EQ(r.e.view().stepInPattern, 0);
}

TEST(any_other_button_cancels_load) {
	EngineRig r(threeSteps(1));
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_LOAD);
	r.tap(BUTTON_INSERT); // cancels, and does not insert
	CHECK_EQ(r.live().numSteps(), 3);
	CHECK_EQ(r.message(), std::string("(none)"));
	r.tap(BUTTON_LOAD);   // a fresh first press, not a confirmation
	CHECK_EQ(r.live().numSteps(), 3);
}

TEST(blank_snapshot_clears_everything) {
	EngineRig r(threeSteps(1));
	r.e.live.tracks[0].tableA.volts[0] = 5.f;
	r.tap(FOCUS_SNAPSHOT);
	r.e.turnLeft(-1);
	CHECK_EQ(r.e.view().snapshot, 0);
	r.tap(BUTTON_SAVE); // the blank snapshot cannot be saved over
	CHECK_EQ(r.message(), std::string("(none)"));
	r.tap(BUTTON_LOAD);
	r.tap(BUTTON_LOAD);
	CHECK_EQ(r.live().numSteps(), 0);
	CHECK(r.live().tableA[0] == 0.f);
	CHECK_EQ(r.e.view().pattern, -1);
}

TEST(snapshot_selection_range) {
	EngineRig r(threeSteps(1));
	r.tap(FOCUS_SNAPSHOT);
	r.e.turnLeft(40);
	CHECK_EQ(r.e.view().snapshot, NUM_SNAPSHOTS);
	r.e.turnLeft(-40);
	CHECK_EQ(r.e.view().snapshot, 0);
}

TEST(load_in_hold_is_cued_with_commit) {
	EngineRig r(threeSteps(1));
	r.tap(BUTTON_SAVE);
	r.tap(BUTTON_SAVE);
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_DELETE); // live now has 2 steps
	r.e.setMode(MODE_HOLD);
	r.tap(BUTTON_LOAD);
	r.tap(BUTTON_LOAD);
	CHECK_EQ(r.shadow().numSteps(), 3);
	CHECK_EQ(r.live().numSteps(), 2);
	r.tap(FOCUS_INDEX);
	r.tap(BUTTON_COMMIT);
	CHECK_EQ(r.live().numSteps(), 3);
}

// --- Quantized reset ---------------------------------------------------------------

TEST(reset_quantized_to_the_step_by_holding_step) {
	EngineRig r(threeSteps(2));
	r.clock();
	r.clock();
	r.clock(); // step 1, pulse 0
	CHECK_EQ(r.playing(), 1);
	r.e.press(FOCUS_STEP);
	CHECK(r.e.pressResetButton());
	r.e.release(FOCUS_STEP);
	r.clock(); // step 1, pulse 1: nothing yet
	CHECK_EQ(r.playing(), 1);
	r.clock(); // step 2 would start: the reset lands here instead
	CHECK_EQ(r.playing(), 0);
	CHECK_EQ(r.e.tr.playheads[0].pulse, 0);
	CHECK(r.e.gate(0));
}

TEST(holding_track_for_reset_does_not_open_options) {
	EngineRig r(threeSteps(1));
	r.e.press(FOCUS_TRACK); // TRACK is already focused
	CHECK(r.e.pressResetButton());
	r.e.release(FOCUS_TRACK);
	CHECK(!r.e.view().optionsScreen);
	CHECK(!r.e.pressResetButton()); // nothing held: a plain reset
}

// --- MATH --------------------------------------------------------------------------

TEST(math_add_to_a_pattern) {
	// Manual, Math section: add 7 to CV-A of every step in the pattern.
	EngineRig r(build({{0, 4, 2}, {4, 4, 2}, {7, 4, 2}, {4, 4, 2}}));
	r.tap(FOCUS_PATTERN);
	r.e.press(BUTTON_MATH);
	CHECK(r.e.view().mathScreen);
	r.e.turnRight(7); // CV-A is the focused right parameter
	CHECK_EQ(std::string(mathCode(r.e.view().math[MATH_CV_A])), std::string("A"));
	r.e.release(BUTTON_MATH);
	CHECK(!r.e.view().mathScreen);
	std::string cvs;
	for (const Step& s : r.live().steps)
		cvs += std::to_string(s.cvA) + " ";
	CHECK_EQ(cvs, std::string("7 11 14 11 "));
	// The transform stays prepared: MATH again adds another 7.
	r.tap(BUTTON_MATH);
	CHECK_EQ((int) r.live().steps[0].cvA, 14);
}

TEST(math_out_of_bounds_leaves_values_alone) {
	EngineRig r(build({{10, 64, 1}, {90, 32, 1}}));
	r.tap(FOCUS_TRACK);
	r.e.press(BUTTON_MATH);
	r.tap(FOCUS_STEP); // DURATION's row: A -> G
	r.tap(FOCUS_DURATION);
	r.e.turnRight(1);  // operand 0 -> 1 ... to x2
	r.e.turnRight(1);
	CHECK_EQ(std::string(mathCode(r.e.view().math[MATH_DURATION])), std::string("G"));
	r.e.release(BUTTON_MATH);
	// 64 x 2 = 128 is out of range and stays 64 (the manual's example); 32 -> 64.
	CHECK_EQ((int) r.live().steps[0].duration, 64);
	CHECK_EQ((int) r.live().steps[1].duration, 64);
}

TEST(math_ops_individually) {
	Rng rng;
	MathOp op;
	op.type = MATH_GEO;
	op.operand = -4; // divide by 4
	CHECK_EQ(applyMathOp(op, 17, rng), 4);
	CHECK_EQ(std::string(mathCode(op)), std::string("-G"));
	op.type = MATH_ADD;
	op.operand = -12;
	CHECK_EQ(applyMathOp(op, 20, rng), 8);
	CHECK_EQ(applyMathOp(op, 5, rng), 5);
	op.type = MATH_SET;
	op.operand = 16;
	CHECK_EQ(applyMathOp(op, 3, rng), 16);
	op.type = MATH_RANDOM;
	op.operand = 48;
	bool inRange = true, varied = false;
	int first = applyMathOp(op, 0, rng);
	for (int i = 0; i < 200; i++) {
		int v = applyMathOp(op, 0, rng);
		inRange = inRange && v >= 0 && v <= 48;
		varied = varied || v != first;
	}
	CHECK(inRange && varied);
	op.type = MATH_JITTER;
	op.operand = 2;
	inRange = true;
	for (int i = 0; i < 200; i++) {
		int v = applyMathOp(op, 50, rng);
		inRange = inRange && v >= 48 && v <= 52;
	}
	CHECK(inRange);
	// Cycling into a type whose operand can't be negative clamps it.
	MathOp c;
	c.operand = -5;
	cycleMathType(c, 2); // A -> S
	CHECK_EQ((int) c.type, (int) MATH_SET);
	CHECK_EQ((int) c.operand, 0);
	cycleMathType(c, 3); // S -> A (wraps)
	CHECK_EQ((int) c.type, (int) MATH_ADD);
}

TEST(math_screen_can_be_pinned_and_cleared) {
	EngineRig r(build({{10, 1, 1}}));
	r.tap(FOCUS_STEP);
	r.e.press(BUTTON_MATH);
	CHECK(r.message() == "PIN" || r.message() == "");
	r.tap(FOCUS_VOLTAGE); // pin
	r.e.release(BUTTON_MATH);
	CHECK(r.e.view().mathScreen);
	CHECK_EQ((int) r.live().steps[0].cvA, 10); // releasing a pinned screen does not apply
	r.e.turnRight(5);
	r.tap(BUTTON_MATH); // MATH applies while pinned
	CHECK_EQ((int) r.live().steps[0].cvA, 15);
	r.tap(BUTTON_DELETE); // back to the identity transform; no step deleted
	CHECK_EQ((int) r.e.view().math[MATH_CV_A].operand, 0);
	CHECK_EQ(r.live().numSteps(), 1);
	r.tap(FOCUS_VOLTAGE); // leave
	CHECK(!r.e.view().mathScreen);
	CHECK_EQ((int) r.live().steps[0].cvA, 15);
}

TEST(math_left_buttons_cycle_their_rows_operation) {
	EngineRig r(build({{10, 1, 1}}));
	r.e.press(BUTTON_MATH);
	r.tap(FOCUS_SNAPSHOT); // GATE's row
	r.tap(FOCUS_SNAPSHOT);
	CHECK_EQ(std::string(mathCode(r.e.view().math[MATH_GATE])), std::string("S"));
	CHECK_EQ(r.e.view().mathRow, (int) MATH_GATE);
	r.e.turnLeft(1);
	CHECK_EQ(std::string(mathCode(r.e.view().math[MATH_GATE])), std::string("rd"));
	CHECK_EQ(r.e.view().leftFocus, (int) FOCUS_TRACK); // the apply scope is untouched
	r.e.release(BUTTON_MATH);
}
