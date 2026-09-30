// Tests for the expander's first stage (docs/SPEC-ER102.md §2, §6): the five-operation
// transform, the MATH screen with the expander attached, and rotoinversion.

#include "Engine.hpp"
#include "Transform.hpp"

#include "testing.hpp"

using namespace iqs;

static Transform tf(int add, int geo, int jitter, int random, int quantize) {
	Transform t;
	t.add = (int8_t) add;
	t.geo = (int8_t) geo;
	t.jitter = (uint8_t) jitter;
	t.random = (uint8_t) random;
	t.quantize = (uint8_t) quantize;
	return t;
}

// --- The transform formula -------------------------------------------------------

TEST(transform_single_operations) {
	Rng rng;
	CHECK_EQ(applyTransform(Transform(), 37, true, rng), 37);  // identity
	CHECK_EQ(applyTransform(tf(12, 1, 0, 0, 1), 20, true, rng), 32);
	CHECK_EQ(applyTransform(tf(-12, 1, 0, 0, 1), 20, true, rng), 8);
	CHECK_EQ(applyTransform(tf(0, 2, 0, 0, 1), 20, true, rng), 40);
	CHECK_EQ(applyTransform(tf(0, -2, 0, 0, 1), 21, true, rng), 10);  // divide by 2
	CHECK_EQ(applyTransform(tf(16, 0, 0, 0, 1), 3, true, rng), 16);   // G = 0: set to A
	CHECK_EQ(applyTransform(tf(0, 1, 0, 0, 8), 13, true, rng), 16);   // nearest multiple of 8
	CHECK_EQ(applyTransform(tf(0, 1, 0, 0, 8), 11, true, rng), 8);
	// Out of range: unchanged (the manual's example: CV-A 12 minus 50).
	CHECK_EQ(applyTransform(tf(-50, 1, 0, 0, 1), 12, true, rng), 12);
	CHECK_EQ(applyTransform(tf(0, 2, 0, 0, 1), 64, true, rng), 64);
}

TEST(transform_random_and_jitter) {
	Rng rng;
	// Destructive Rd replaces the value: A = 12, Rd = 7 gives 12..19 (manual example).
	bool inRange = true;
	for (int i = 0; i < 300; i++) {
		int v = applyTransform(tf(12, 1, 0, 7, 1), 50, true, rng);
		inRange = inRange && v >= 12 && v <= 19;
	}
	CHECK(inRange);
	// Non-destructive Rd adds to the value.
	inRange = true;
	for (int i = 0; i < 300; i++) {
		int v = applyTransform(tf(0, 1, 0, 7, 1), 50, false, rng);
		inRange = inRange && v >= 50 && v <= 57;
	}
	CHECK(inRange);
	// Jitter: +-Jt around the value.
	inRange = true;
	for (int i = 0; i < 300; i++) {
		int v = applyTransform(tf(0, 1, 3, 0, 1), 50, true, rng);
		inRange = inRange && v >= 47 && v <= 53;
	}
	CHECK(inRange);
	// Random whole-tone pitches (manual): G = 2, Rd = 6 over a root of 24.
	bool wholeTones = true;
	for (int i = 0; i < 300; i++) {
		int v = applyTransform(tf(24, 2, 0, 6, 1), 0, true, rng);
		wholeTones = wholeTones && v >= 24 && v <= 36 && v % 2 == 0;
	}
	CHECK(wholeTones);
}

TEST(transform_geometric_knob_order) {
	Transform t;
	adjustTransform(t, OP_GEO, 1);
	CHECK_EQ((int) t.geo, 2);
	adjustTransform(t, OP_GEO, -2);
	CHECK_EQ((int) t.geo, -2); // 2 -> 1 -> /2
	adjustTransform(t, OP_GEO, -97);
	CHECK_EQ((int) t.geo, -99);
	adjustTransform(t, OP_GEO, -1);
	CHECK_EQ((int) t.geo, 0);  // below /99 comes zero
	adjustTransform(t, OP_GEO, -5);
	CHECK_EQ((int) t.geo, 0);
	adjustTransform(t, OP_GEO, 500);
	CHECK_EQ((int) t.geo, 99);
	CHECK_EQ(std::string(transformCode(t, OP_GEO)), std::string("G"));
	Transform q;
	adjustTransform(q, OP_QUANTIZE, -5);
	CHECK_EQ((int) q.quantize, 1);
}

TEST(transform_invert_touches_only_add_and_geo) {
	Transform t = tf(12, 3, 4, 5, 6);
	Transform i = t.inverted();
	CHECK_EQ((int) i.add, -12);
	CHECK_EQ((int) i.geo, -3);
	CHECK_EQ((int) i.jitter, 4);
	CHECK_EQ((int) i.random, 5);
	CHECK_EQ((int) i.quantize, 6);
	CHECK_EQ((int) tf(0, -4, 0, 0, 1).inverted().geo, 4);
	CHECK_EQ((int) tf(0, 0, 0, 0, 1).inverted().geo, 0);
	CHECK_EQ((int) tf(0, 1, 0, 0, 1).inverted().geo, 1);
	CHECK_EQ(std::string(transformCode(i, OP_ADD)), std::string("-A"));
	CHECK_EQ(std::string(transformCode(i, OP_GEO)), std::string("-G"));
	CHECK_EQ(std::string(transformCode(tf(5, 0, 0, 0, 1), OP_ADD)), std::string("S"));
}

// --- The MATH screen with the expander -------------------------------------------

struct XRig {
	Engine e;
	explicit XRig(const Sequence& s) {
		e.live = s;
		e.liveReplaced();
		e.setExpander(true);
	}
	void tap(int b) {
		e.press(b);
		e.release(b);
	}
	std::string param(int which) const {
		std::string out;
		for (const Step& st : e.live.tracks[0].steps) {
			int v = which == 0 ? st.cvA : which == 1 ? st.cvB : which == 2 ? st.duration : st.gate;
			out += std::to_string(v) + " ";
		}
		if (!out.empty())
			out.pop_back();
		return out;
	}
};

static Sequence fourSteps() {
	return build({{0, 1, 1}, {4, 2, 2}, {7, 3, 3}, {4, 4, 4}});
}

TEST(expander_math_add_then_double) {
	XRig r(fourSteps());
	r.tap(FOCUS_PATTERN);
	r.e.press(BUTTON_MATH);
	CHECK(r.e.view().expander);
	r.e.turnRight(7); // CV-A, operation A
	r.e.release(BUTTON_MATH);
	CHECK_EQ(r.param(0), std::string("7 11 14 11"));
	// Now G: the LEFT knob moves to the next operation, the RIGHT knob sets it.
	r.e.press(BUTTON_MATH);
	r.e.turnLeft(1);
	CHECK_EQ(r.e.view().transformOp[MATH_CV_A], (int) OP_GEO);
	r.e.turnRight(1); // G = 2
	r.e.release(BUTTON_MATH);
	// Each application runs every operation: (v * 2) + 7.
	CHECK_EQ(r.param(0), std::string("21 29 35 29"));
	CHECK_EQ(r.param(2), std::string("1 2 3 4")); // other parameters untouched
}

TEST(expander_math_row_buttons_pick_the_parameter) {
	XRig r(fourSteps());
	r.tap(FOCUS_TRACK);
	r.e.press(BUTTON_MATH);
	r.tap(FOCUS_STEP); // third row: DURATION
	CHECK_EQ(r.e.view().rightFocus, (int) FOCUS_DURATION);
	r.e.turnLeft(1); // G
	r.e.turnRight(1); // x2
	r.e.release(BUTTON_MATH);
	CHECK_EQ(r.param(2), std::string("2 4 6 8"));
}

TEST(expander_invert_in_screen_and_while_applying) {
	XRig r(build({{20, 1, 1}}));
	r.tap(FOCUS_STEP);
	r.e.press(BUTTON_MATH);
	r.e.turnRight(5); // A = 5
	r.tap(BUTTON_INVERT); // A = -5
	CHECK_EQ((int) r.e.live.tracks[0].transform[MATH_CV_A].add, -5);
	r.tap(BUTTON_INVERT); // back to +5
	r.e.release(BUTTON_MATH);
	CHECK_EQ(r.param(0), std::string("25"));
	// Holding INVERT while pressing and releasing MATH applies the inverse: subtract 5.
	r.e.press(BUTTON_INVERT);
	r.tap(BUTTON_MATH);
	r.e.release(BUTTON_INVERT);
	CHECK_EQ(r.param(0), std::string("20"));
	CHECK_EQ((int) r.e.live.tracks[0].transform[MATH_CV_A].add, 5); // stored one unchanged
}

TEST(expander_math_delete_resets_to_identity) {
	XRig r(build({{20, 1, 1}}));
	r.e.press(BUTTON_MATH);
	r.e.turnRight(9);
	r.tap(BUTTON_DELETE);
	CHECK(r.e.live.tracks[0].transform == TransformSet());
	CHECK_EQ(r.e.live.tracks[0].numSteps(), 1);
	r.e.release(BUTTON_MATH);
	CHECK_EQ(r.param(0), std::string("20"));
}

TEST(without_expander_math_stays_single_operation) {
	XRig r(build({{20, 1, 1}}));
	r.e.setExpander(false);
	r.tap(FOCUS_STEP);
	r.e.press(BUTTON_MATH);
	r.e.turnRight(3);
	r.e.release(BUTTON_MATH);
	CHECK_EQ(r.param(0), std::string("23"));
	CHECK(r.e.live.tracks[0].transform == TransformSet());
}

// --- Rotoinversion ----------------------------------------------------------------

TEST(rotoinversion_on_one_parameter) {
	XRig r(fourSteps());
	r.tap(FOCUS_PATTERN);
	r.tap(FOCUS_DURATION);
	r.tap(BUTTON_INVERT); // reverse DURATION only
	CHECK_EQ(r.param(2), std::string("4 3 2 1"));
	CHECK_EQ(r.param(0), std::string("0 4 7 4"));
	r.tap(BUTTON_ROTATE); // shift forward: last wraps to first
	CHECK_EQ(r.param(2), std::string("1 4 3 2"));
	r.e.press(BUTTON_INVERT); // held as a modifier: shift backward...
	r.tap(BUTTON_ROTATE);
	r.e.release(BUTTON_INVERT); // ...and no reverse on release
	CHECK_EQ(r.param(2), std::string("4 3 2 1"));
}

TEST(rotoinversion_needs_pattern_or_track_focus) {
	XRig r(fourSteps());
	r.tap(FOCUS_STEP);
	r.tap(FOCUS_CV_A);
	r.tap(BUTTON_INVERT);
	CHECK_EQ(r.param(0), std::string("0 4 7 4"));
	r.tap(FOCUS_TRACK);
	r.tap(BUTTON_ROTATE);
	CHECK_EQ(r.param(0), std::string("4 0 4 7"));
}

TEST(rotoinversion_is_an_edit_follow_refuses_it) {
	XRig r(fourSteps());
	r.e.setMode(MODE_FOLLOW);
	r.tap(FOCUS_PATTERN);
	r.tap(BUTTON_ROTATE);
	CHECK_EQ(r.param(0), std::string("0 4 7 4"));
}
