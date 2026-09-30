// Tests for groups (docs/SPEC-ER102.md §4): Euclidean masks, selecting steps, group
// MATH, the GROUP MODIFIERS screen, and the X/Y/Z modulation bus during playback.

#include "Engine.hpp"
#include "Groups.hpp"

#include "testing.hpp"

#include <cmath>

using namespace iqs;

static std::string pattern(int k, int n) {
	uint8_t bits[MAX_EUCLID];
	euclid(k, n, bits);
	std::string s;
	for (int i = 0; i < n; i++)
		s += bits[i] ? '1' : '0';
	return s;
}

static bool isRotation(const std::string& a, const std::string& b) {
	return a.size() == b.size() && (a + a).find(b) != std::string::npos;
}

// --- Euclidean masks -----------------------------------------------------------------

TEST(euclid_matches_the_manuals_examples) {
	// Exactly as printed on page 13...
	CHECK_EQ(pattern(2, 4), std::string("1010"));
	CHECK_EQ(pattern(3, 8), std::string("10010010"));
	CHECK_EQ(pattern(5, 12), std::string("100101001010"));
	CHECK_EQ(pattern(7, 12), std::string("101101011010"));
	CHECK_EQ(pattern(8, 12), std::string("101101101101"));
	CHECK_EQ(pattern(6, 16), std::string("1001010010010100"));
	CHECK_EQ(pattern(16, 40), std::string("1001010010100101001010010100101001010010"));
	// ...and these two as rotations of what is printed (a rotation is the same rhythm;
	// the manual says masks may need rotating to taste).
	CHECK(isRotation(pattern(5, 16), "1000100100100100"));
	CHECK(isRotation(pattern(7, 17), "10010101001010010"));
}

TEST(euclid_edges_and_counts) {
	CHECK_EQ(pattern(0, 5), std::string("00000"));
	CHECK_EQ(pattern(5, 5), std::string("11111"));
	CHECK_EQ(pattern(1, 4), std::string("1000"));
	// Always exactly k ones, whatever k and n.
	bool counts = true;
	for (int n = 1; n <= MAX_EUCLID; n++) {
		for (int k = 0; k <= n; k++) {
			std::string p = pattern(k, n);
			int ones = 0;
			for (char c : p)
				ones += c == '1';
			counts = counts && ones == k && (int) p.size() == n;
		}
	}
	CHECK(counts);
}

TEST(euclid_mask_repeats_over_longer_ranges) {
	// Manual: E(1,4) over a 10-step pattern becomes 1000100010.
	Sequence seq;
	for (int i = 0; i < 10; i++)
		seq.appendStep(0, makeStep(i, 1, 1));
	applyEuclid(seq.tracks[0], 0, 9, 3, 1, 4);
	std::string got;
	for (const Step& s : seq.tracks[0].steps)
		got += inGroup(s, 3) ? '1' : '0';
	CHECK_EQ(got, std::string("1000100010"));
}

// --- Selecting through the panel ------------------------------------------------------

struct GroupRig {
	Engine e;
	explicit GroupRig(const Sequence& s) {
		e.live = s;
		e.liveReplaced();
		e.setExpander(true);
	}
	void tap(int b) {
		e.press(b);
		e.release(b);
	}
	void clock(int n = 1) {
		for (int k = 0; k < n; k++) {
			e.process(1e-3f, true, false, false);
			for (int i = 0; i < 9; i++)
				e.process(1e-3f, false, false, false);
		}
	}
	std::string members(int g, int t = 0) const {
		std::string out;
		for (const Step& s : e.live.tracks[t].steps)
			out += inGroup(s, g) ? '1' : '0';
		return out;
	}
	void focusGroup(int g) {
		tap(FOCUS_GROUP);
		e.turnLeft(g - e.panel.focusedGroup);
	}
};

static Sequence eightSteps(int duration = 1) {
	Sequence s;
	for (int i = 0; i < 8; i++)
		s.appendStep(0, makeStep(10 + i, duration, duration));
	return s;
}

TEST(groups_select_steps_one_by_one) {
	GroupRig r(eightSteps());
	r.focusGroup(2);
	CHECK_EQ(r.e.view().groupFocused, 2);
	r.tap(FOCUS_STEP);
	r.e.turnLeft(3);
	r.tap(BUTTON_DESELECT);
	CHECK_EQ(r.members(2), std::string("00010000"));
	CHECK(r.e.panel.groupMemberLed(r.e.live));
	CHECK(r.e.view().groupHasMembers);
	r.tap(BUTTON_DESELECT);
	CHECK_EQ(r.members(2), std::string("00000000"));
	CHECK(!r.e.view().groupHasMembers);
}

TEST(groups_euclidean_selection_from_the_panel) {
	// Manual walkthrough: every 3rd of 8 steps into group 1, E(3,8).
	GroupRig r(eightSteps());
	r.focusGroup(0);
	r.tap(FOCUS_PATTERN);
	r.tap(BUTTON_DESELECT);
	CHECK(r.e.view().euclid);
	CHECK_EQ(r.e.view().euclidN, 8); // starts at E(L, L)
	CHECK_EQ(r.e.view().euclidM, 8);
	r.e.turnLeft(-5);                // E(3, 8)
	r.tap(BUTTON_INSERT);            // ignored while choosing (and inserts nothing)
	CHECK_EQ(r.e.live.tracks[0].numSteps(), 8);
	r.tap(BUTTON_DESELECT);          // apply
	CHECK(!r.e.view().euclid);
	CHECK_EQ(r.members(0), std::string("10010010"));
	// INDEX flips between select-all and select-none; DELETE sets N to zero.
	r.tap(BUTTON_DESELECT);
	r.tap(FOCUS_INDEX);
	CHECK_EQ(r.e.view().euclidN, 0);
	r.tap(FOCUS_INDEX);
	CHECK_EQ(r.e.view().euclidN, 8);
	r.tap(BUTTON_DELETE);
	CHECK_EQ(r.e.view().euclidN, 0);
	r.e.turnRight(-4);   // M = 4
	r.e.turnLeft(1);     // E(1, 4), repeated over the pattern
	r.tap(BUTTON_DESELECT);
	CHECK_EQ(r.members(0), std::string("10001000"));
}

TEST(groups_selection_copy_delete_invert_rotate) {
	GroupRig r(eightSteps());
	r.focusGroup(0);
	r.tap(FOCUS_PATTERN);
	r.tap(BUTTON_DESELECT);
	r.e.turnLeft(-5);
	r.tap(BUTTON_DESELECT); // group 0 = 10010010
	r.focusGroup(0);
	r.tap(BUTTON_COPY);
	CHECK_EQ((int) r.e.panel.focusLed(FOCUS_GROUP), (int) LED_BLINK);
	r.e.turnLeft(1);        // group 1
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_DESELECT); // group 1 already has step 0 ...
	r.e.turnLeft(1);
	r.tap(BUTTON_DESELECT); // ... and step 1
	r.tap(FOCUS_GROUP);
	r.tap(BUTTON_INSERT);   // union with group 0's selection
	CHECK_EQ(r.members(1), std::string("11010010"));
	r.tap(BUTTON_COPY);     // clear the clipboard
	r.tap(BUTTON_INVERT);
	CHECK_EQ(r.members(1), std::string("00101101"));
	r.tap(BUTTON_ROTATE);   // one step later
	CHECK_EQ(r.members(1), std::string("10010110"));
	r.e.press(BUTTON_INVERT);
	r.tap(BUTTON_ROTATE);   // back
	r.e.release(BUTTON_INVERT);
	CHECK_EQ(r.members(1), std::string("00101101"));
	r.tap(BUTTON_DELETE);
	CHECK_EQ(r.members(1), std::string("00000000"));
	CHECK_EQ(r.members(0), std::string("10010010")); // other groups untouched
}

TEST(groups_math_changes_only_members_on_every_track) {
	Sequence s = eightSteps();
	s.appendStep(1, makeStep(50, 1, 1));
	s.appendStep(1, makeStep(60, 1, 1));
	GroupRig r(s);
	setInGroup(r.e.live.tracks[0].steps[2], 4, true);
	setInGroup(r.e.live.tracks[1].steps[1], 4, true);
	r.focusGroup(4);
	r.e.press(BUTTON_MATH);
	r.e.turnRight(12); // CV-A + 12
	r.e.release(BUTTON_MATH);
	CHECK_EQ((int) r.e.live.tracks[0].steps[2].cvA, 24);
	CHECK_EQ((int) r.e.live.tracks[0].steps[3].cvA, 13);
	CHECK_EQ((int) r.e.live.tracks[1].steps[1].cvA, 72);
	CHECK_EQ((int) r.e.live.tracks[1].steps[0].cvA, 50);
	// The group's transform, not the track's, was edited.
	CHECK_EQ((int) r.e.live.groups[4].transform[MATH_CV_A].add, 12);
	CHECK_EQ((int) r.e.live.tracks[0].transform[MATH_CV_A].add, 0);
}

// --- GROUP MODIFIERS screen -------------------------------------------------------------

TEST(slope_knob_steps_by_magnitude) {
	CHECK(std::fabs(adjustSlope(0.f, 1) - 0.01f) < 1e-4f);
	CHECK(std::fabs(adjustSlope(0.99f, 1) - 1.0f) < 1e-4f);
	CHECK(std::fabs(adjustSlope(1.0f, 1) - 1.1f) < 1e-4f);
	CHECK(std::fabs(adjustSlope(9.9f, 1) - 10.f) < 1e-4f);
	CHECK(std::fabs(adjustSlope(99.f, 5) - 99.f) < 1e-4f);
	CHECK(std::fabs(adjustSlope(-0.99f, -1) + 1.0f) < 1e-4f);
	CHECK(std::fabs(adjustSlope(-9.9f, -1) + 10.f) < 1e-4f);
	CHECK_EQ(formatSlope(-0.05f), std::string("-0.05"));
	CHECK_EQ(formatSlope(2.5f), std::string("2.5"));
	CHECK_EQ(formatSlope(-40.f), std::string("-40"));
	CHECK_EQ(formatSlopeShort(0.f), std::string("0"));
	CHECK_EQ(formatSlopeShort(0.05f), std::string("0."));
	CHECK_EQ(formatSlopeShort(-4.f), std::string("-4"));
	CHECK_EQ(formatSlopeShort(-40.f), std::string("--"));
}

TEST(modifier_screen_edits_slopes_and_transforms) {
	GroupRig r(eightSteps());
	ModBus bus;
	r.e.setModulation(bus, MODIFIER_SLOPE, 1); // Y
	r.tap(FOCUS_GROUP_MODIFIER);
	CHECK(r.e.view().slopeScreen);
	CHECK_EQ((int) r.e.panel.focusLed(FOCUS_GROUP_MODIFIER), (int) LED_BLINK);
	CHECK_EQ(r.e.view().leftFocus, (int) FOCUS_GROUP);
	r.e.turnLeft(2);   // LEFT: the group
	r.tap(FOCUS_GATE); // RIGHT focus buttons pick the parameter
	r.e.turnRight(25); // 0.25
	CHECK(std::fabs(r.e.live.groups[2].slope[1][MATH_GATE] - 0.25f) < 1e-4f);
	CHECK(r.e.view().slopes[MATH_GATE] > 0.24f);
	// HIGH: the transform editor instead.
	r.e.setModulation(bus, MODIFIER_HIGH, 0);
	CHECK(r.e.view().mathScreen);
	r.e.turnLeft(1);   // LEFT now picks the operation: G
	r.e.turnRight(1);  // x2
	CHECK_EQ((int) r.e.live.groups[2].high[0][MATH_GATE].geo, 2);
	CHECK_EQ((int) r.e.live.groups[2].low[0][MATH_GATE].geo, 1);
	r.tap(FOCUS_GROUP_MODIFIER); // leave
	CHECK(!r.e.view().slopeScreen);
	CHECK(!r.e.view().mathScreen);
}

// --- The bus during playback -------------------------------------------------------------

TEST(bus_low_and_high_transforms_follow_the_gate) {
	// Steps of 2 pulses; group 0 = step 1 only. X LOW doubles its duration.
	GroupRig r(eightSteps(2));
	setInGroup(r.e.live.tracks[0].steps[1], 0, true);
	r.e.live.groups[0].low[0][MATH_DURATION].geo = 2;
	ModBus bus;
	r.e.setModulation(bus, MODIFIER_SLOPE, 0);
	std::string order;
	for (int i = 0; i < 8; i++) {
		r.clock();
		order += std::to_string(r.e.tr.playheads[0].step);
	}
	CHECK_EQ(order, std::string("00111122")); // step 1 lasts 4 pulses
	// With X's gate high the HIGH transform (identity here) applies instead.
	bus.gate[0] = true;
	r.e.setModulation(bus, MODIFIER_SLOPE, 0);
	r.e.tr.rewind(r.e.live, false);
	order.clear();
	for (int i = 0; i < 6; i++) {
		r.clock();
		order += std::to_string(r.e.tr.playheads[0].step);
	}
	CHECK_EQ(order, std::string("001122"));
}

TEST(bus_can_skip_and_mute_member_steps) {
	GroupRig r(eightSteps());
	setInGroup(r.e.live.tracks[0].steps[1], 0, true);
	setInGroup(r.e.live.tracks[0].steps[2], 0, true);
	// X HIGH: DURATION x0 skips; Y HIGH: GATE x0 mutes.
	r.e.live.groups[0].high[0][MATH_DURATION].geo = 0;
	ModBus bus;
	bus.gate[0] = true;
	r.e.setModulation(bus, MODIFIER_SLOPE, 0);
	std::string order;
	for (int i = 0; i < 4; i++) {
		r.clock();
		order += std::to_string(r.e.tr.playheads[0].step);
	}
	CHECK_EQ(order, std::string("0345"));
	r.e.live.groups[0].high[0][MATH_DURATION].geo = 1;
	r.e.live.groups[0].high[1][MATH_GATE].geo = 0;
	bus.gate[1] = true;
	r.e.setModulation(bus, MODIFIER_SLOPE, 0);
	r.e.tr.rewind(r.e.live, false);
	std::string gates;
	for (int i = 0; i < 4; i++) {
		r.clock();
		gates += r.e.gate(0) ? '^' : '_';
	}
	CHECK_EQ(gates, std::string("^__^"));
}

TEST(bus_slopes_add_volts_and_pulses) {
	GroupRig r(eightSteps(2));
	setInGroup(r.e.live.tracks[0].steps[0], 0, true);
	r.e.live.groups[0].slope[0][MATH_CV_A] = 1.f;      // + Vx volts
	r.e.live.groups[0].slope[0][MATH_DURATION] = 2.f;  // + 2 Vx pulses
	ModBus bus;
	bus.cv[0] = 0.5f;
	r.e.setModulation(bus, MODIFIER_SLOPE, 0);
	r.clock();
	float base = r.e.live.tracks[0].tableA[10];
	CHECK(std::fabs(r.e.cvA(0) - (base + 0.5f)) < 1e-4f);
	// CV slopes follow the bus continuously.
	bus.cv[0] = 1.5f;
	r.e.setModulation(bus, MODIFIER_SLOPE, 0);
	CHECK(std::fabs(r.e.cvA(0) - (base + 1.5f)) < 1e-4f);
	// Step 0 plays 2 + 2*0.5 = 3 pulses (fixed when it started).
	r.clock(2);
	CHECK_EQ(r.e.tr.playheads[0].step, 0);
	r.clock();
	CHECK_EQ(r.e.tr.playheads[0].step, 1);
	CHECK(std::fabs(r.e.cvA(0) - r.e.live.tracks[0].tableA[11]) < 1e-4f); // not a member
}

TEST(bus_random_is_drawn_every_time_a_step_plays) {
	GroupRig r(eightSteps());
	for (Step& s : r.e.live.tracks[0].steps)
		setInGroup(s, 0, true);
	r.e.live.groups[0].low[0][MATH_CV_A].random = 12;
	ModBus bus;
	r.e.setModulation(bus, MODIFIER_SLOPE, 0);
	bool inRange = true, varied = false;
	float first = -1.f;
	for (int i = 0; i < 64; i++) {
		r.clock();
		int step = r.e.tr.playheads[0].step;
		float lo = r.e.live.tracks[0].tableA[10 + step];
		float hi = r.e.live.tracks[0].tableA[22 + step];
		float v = r.e.cvA(0);
		inRange = inRange && v >= lo - 1e-4f && v <= hi + 1e-4f;
		if (step == 0) {
			if (first < 0.f)
				first = v;
			else
				varied = varied || v != first;
		}
	}
	CHECK(inRange && varied);
	// The stored steps are untouched: this is non-destructive.
	CHECK_EQ((int) r.e.live.tracks[0].steps[0].cvA, 10);
}

TEST(bus_does_nothing_without_the_expander) {
	GroupRig r(eightSteps(2));
	setInGroup(r.e.live.tracks[0].steps[0], 0, true);
	r.e.live.groups[0].low[0][MATH_DURATION].geo = 0;
	r.e.setExpander(false);
	r.clock();
	CHECK_EQ(r.e.tr.playheads[0].step, 0); // not skipped
}

TEST(groups_count_in_undo_comparisons) {
	Sequence a = eightSteps();
	Sequence b = a;
	setInGroup(b.tracks[0].steps[3], 5, true);
	CHECK(!a.sameContent(b));
	b = a;
	b.groups[7].slope[2][MATH_GATE] = 0.5f;
	CHECK(!a.sameContent(b));
	b = a;
	b.groups[1].high[0][MATH_CV_A].add = 3;
	CHECK(!a.sameContent(b));
}
