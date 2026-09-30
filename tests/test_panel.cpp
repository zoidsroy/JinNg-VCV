// Tests for editing: src/core/Editor.hpp and the front-panel state machine in
// src/core/Panel.hpp. Where possible they follow the manual's own procedures.

#include "Editor.hpp"
#include "Panel.hpp"
#include "Transport.hpp"

#include "testing.hpp"

using namespace iqs;

// A sequencer with its panel, driven like a user would.
struct Rig {
	Sequence seq;
	Transport tr;
	Panel panel;

	Rig() {}
	explicit Rig(const Sequence& s) : seq(s) {
		tr.rewind(seq, false);
		panel.normalizeCursors(seq);
	}

	void down(int b) { panel.press(seq, tr, b); }
	void up(int b) { panel.release(seq, tr, b); }
	void tap(int b) {
		down(b);
		up(b);
	}
	void left(int d) { panel.turnLeft(seq, tr, d); }
	void right(int d) { panel.turnRight(seq, d); }

	const Track& track(int t = 0) const { return seq.tracks[t]; }
	PanelView view() const { return panel.view(seq); }

	// cvA of each step of a track, e.g. "1 2 3"; patterns separated by "|".
	std::string cvs(int t = 0) const {
		const Track& tr0 = seq.tracks[t];
		std::string out;
		int i = 0;
		for (size_t p = 0; p < tr0.patterns.size(); p++) {
			if (p > 0)
				out += "| ";
			for (int k = 0; k < tr0.patterns[p].length; k++, i++)
				out += std::to_string(tr0.steps[i].cvA) + " ";
		}
		if (!out.empty())
			out.pop_back();
		return out;
	}
	std::string durations(int t = 0) const {
		std::string out;
		for (const Step& s : seq.tracks[t].steps)
			out += std::to_string(s.duration) + " ";
		if (!out.empty())
			out.pop_back();
		return out;
	}
};

static std::string str(const char* s) {
	return s ? std::string(s) : std::string("(none)");
}

// --- The manual's Quick Start ------------------------------------------------

TEST(quick_start_recipe) {
	Rig r;
	// 5. Focus STEP and INSERT a step, DURATION 4, GATE 2. CV-A defaults to 12.
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_INSERT);
	CHECK_EQ(r.track().numSteps(), 1);
	CHECK_EQ((int) r.track().steps[0].cvA, 12);
	CHECK_EQ((int) r.track().steps[0].duration, 0);
	r.tap(FOCUS_DURATION);
	r.right(4);
	r.tap(FOCUS_GATE);
	r.right(2);
	// 7. The gate opens on every beat and closes half-way.
	Playhead ph;
	CHECK_EQ(run(r.track(), ph, 5), std::string("0^ 0^ 0_ 0_ 0^"));

	// 8. INSERT again appends a duplicate of the step.
	r.tap(BUTTON_INSERT);
	CHECK_EQ(r.track().numSteps(), 2);
	CHECK_EQ((int) r.track().steps[1].duration, 4);
	CHECK_EQ((int) r.track().steps[1].gate, 2);
	CHECK_EQ(r.view().stepInPattern, 1);

	// 9. CV-A of the new step to 24: an octave up in 12ET.
	r.tap(FOCUS_CV_A);
	r.right(12);
	CHECK_EQ(r.cvs(), std::string("12 24"));
	CHECK_EQ(r.view().voltage == 2.f, true);

	// 10. DURATION 8: the higher note plays twice as long.
	r.tap(FOCUS_DURATION);
	r.right(4);
	Playhead ph2;
	CHECK_EQ(run(r.track(), ph2, 12), std::string("0^ 0^ 0_ 0_ 1^ 1^ 1_ 1_ 1_ 1_ 1_ 1_"));

	// 11. GATE down to zero makes it a rest.
	r.tap(FOCUS_GATE);
	r.right(-5);
	CHECK_EQ((int) r.track().steps[1].gate, 0);
}

// --- INSERT ------------------------------------------------------------------

TEST(insert_mode_is_chosen_while_holding_insert) {
	Rig r(build({{12, 5, 1}}));
	r.tap(FOCUS_STEP);
	r.down(BUTTON_INSERT);
	CHECK_EQ(str(r.view().message), std::string("AFtr"));
	r.right(-1);
	CHECK_EQ(str(r.view().message), std::string("SPLt"));
	r.right(-1);
	CHECK_EQ(str(r.view().message), std::string("bEFr"));
	r.right(-3); // stops at the end
	CHECK_EQ(str(r.view().message), std::string("bEFr"));
	r.right(1);
	CHECK_EQ(str(r.view().message), std::string("SPLt"));
	// The right encoder did not edit the step meanwhile.
	CHECK_EQ((int) r.track().steps[0].cvA, 12);
	r.up(BUTTON_INSERT);
	CHECK_EQ(str(r.view().message), std::string("(none)"));
}

TEST(split_step_preserves_duration) {
	Rig r(build({{12, 5, 1}}));
	r.tap(FOCUS_STEP);
	r.down(BUTTON_INSERT);
	r.right(-1);
	r.up(BUTTON_INSERT);
	CHECK_EQ(r.durations(), std::string("3 2"));

	Rig r2(build({{12, 4, 1}}));
	r2.tap(FOCUS_STEP);
	r2.down(BUTTON_INSERT);
	r2.right(-1);
	r2.up(BUTTON_INSERT);
	CHECK_EQ(r2.durations(), std::string("2 2"));
}

TEST(insert_before_puts_the_copy_in_front) {
	Rig r(build({{1, 1, 1}, {2, 1, 1}}));
	r.tap(FOCUS_STEP);
	r.left(1); // on step "2"
	r.down(BUTTON_INSERT);
	r.right(-2);
	r.up(BUTTON_INSERT);
	CHECK_EQ(r.cvs(), std::string("1 2 2"));
	CHECK_EQ(r.view().stepInPattern, 1);
	r.tap(FOCUS_CV_A);
	r.right(5);
	CHECK_EQ(r.cvs(), std::string("1 7 2"));
}

TEST(patterns_insert_and_navigate) {
	Rig r;
	r.tap(FOCUS_PATTERN);
	r.tap(BUTTON_INSERT); // empty pattern 1
	CHECK_EQ(r.view().pattern, 0);
	CHECK_EQ(r.view().stepInPattern, -1);
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_INSERT);
	r.tap(BUTTON_INSERT);
	r.tap(FOCUS_PATTERN);
	r.tap(BUTTON_INSERT); // empty pattern 2, after pattern 1
	CHECK_EQ(r.view().pattern, 1);
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_INSERT);
	CHECK_EQ(r.track().patterns.size(), (size_t) 2);
	CHECK_EQ((int) r.track().patterns[0].length, 2);
	CHECK_EQ((int) r.track().patterns[1].length, 1);

	// STEP moves across pattern boundaries.
	r.left(-1);
	CHECK_EQ(r.view().pattern, 0);
	CHECK_EQ(r.view().stepInPattern, 1);
	// PATTERN lands on the first step going forward, the last going back.
	r.tap(FOCUS_PATTERN);
	r.left(-1);
	r.left(1);
	CHECK_EQ(r.view().pattern, 1);
	CHECK_EQ(r.view().stepInPattern, 0);
	r.left(-1);
	CHECK_EQ(r.view().pattern, 0);
	CHECK_EQ(r.view().stepInPattern, 1);
}

TEST(split_pattern_at_cursor) {
	Rig r(build({{1, 1, 1}, {2, 1, 1}, {3, 1, 1}}));
	r.tap(FOCUS_STEP);
	r.left(1);
	r.tap(FOCUS_PATTERN);
	r.down(BUTTON_INSERT);
	r.right(-1);
	r.up(BUTTON_INSERT);
	CHECK_EQ(r.cvs(), std::string("1 | 2 3"));
}

TEST(full_track_refuses_insert) {
	Sequence seq;
	for (int i = 0; i < MAX_STEPS_PER_PATTERN; i++)
		seq.appendStep(0, makeStep(12, 1, 1));
	Rig r(seq);
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_INSERT);
	CHECK_EQ(r.track().numSteps(), MAX_STEPS_PER_PATTERN);
	CHECK_EQ(str(r.view().message), std::string("FULL"));
	r.panel.tick(2.f);
	CHECK_EQ(str(r.view().message), std::string("(none)"));
}

// --- DELETE ------------------------------------------------------------------

TEST(delete_step_keeps_cursor_in_place) {
	Rig r(build({{1, 1, 1}, {2, 1, 1}, {3, 1, 1}}));
	r.tap(FOCUS_STEP);
	r.left(1);
	r.tap(BUTTON_DELETE);
	CHECK_EQ(r.cvs(), std::string("1 3"));
	CHECK_EQ(r.view().step.cvA == 3, true);
	r.tap(BUTTON_DELETE);
	CHECK_EQ(r.cvs(), std::string("1"));
	CHECK_EQ(r.view().stepInPattern, 0);
	r.tap(BUTTON_DELETE);
	CHECK_EQ(r.cvs(), std::string(""));
	CHECK_EQ(r.view().stepInPattern, -1);
	r.tap(BUTTON_DELETE); // nothing left to delete
	CHECK_EQ(r.track().patterns.size(), (size_t) 1);
}

TEST(delete_pattern) {
	Sequence seq = build({{1, 1, 1}});
	seq.appendPattern(0);
	seq.appendStep(0, makeStep(2, 1, 1));
	Rig r(seq);
	r.tap(FOCUS_PATTERN);
	r.tap(BUTTON_DELETE);
	CHECK_EQ(r.cvs(), std::string("2"));
	CHECK_EQ(r.view().pattern, 0);
	CHECK_EQ(r.view().stepInPattern, 0);
}

TEST(clear_track_needs_confirmation) {
	Rig r(build({{1, 1, 1}, {2, 1, 1}}));
	// TRACK is focused already; pressing it again would open the options screen.
	r.tap(BUTTON_DELETE);
	CHECK_EQ(str(r.view().message), std::string("CLr"));
	CHECK_EQ(r.track().numSteps(), 2);
	// Any other button aborts, and does nothing else: COPY's release must not copy.
	r.tap(BUTTON_COPY);
	CHECK_EQ(str(r.view().message), std::string("Abrt"));
	CHECK(!r.panel.copyLed());
	CHECK_EQ(r.track().numSteps(), 2);
	r.tap(BUTTON_DELETE);
	r.tap(BUTTON_DELETE);
	CHECK_EQ(r.track().numSteps(), 0);
	CHECK_EQ(r.view().pattern, -1);
}

// --- COPY / paste ------------------------------------------------------------

TEST(copy_step_and_paste_after) {
	Rig r(build({{1, 1, 1}, {2, 1, 1}, {3, 1, 1}}));
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_COPY);
	CHECK(r.panel.copyLed());
	CHECK_EQ((int) r.panel.focusLed(FOCUS_STEP), (int) LED_BLINK);
	r.left(2);
	r.tap(BUTTON_INSERT);
	r.tap(BUTTON_INSERT); // repeated pastes chain
	CHECK_EQ(r.cvs(), std::string("1 2 3 1 1"));
	CHECK_EQ(r.view().stepInPattern, 4);
	// COPY again clears the clipboard; INSERT then makes a plain new step.
	r.tap(BUTTON_COPY);
	CHECK(!r.panel.copyLed());
}

TEST(copy_range_by_holding_copy) {
	Rig r(build({{1, 1, 1}, {2, 1, 1}, {3, 1, 1}}));
	r.tap(FOCUS_STEP);
	r.left(2);
	r.down(BUTTON_COPY);
	r.left(-1); // select backwards: steps 2..3
	r.up(BUTTON_COPY);
	CHECK_EQ(r.panel.clip.steps.size(), (size_t) 2);
	r.left(-5);
	r.down(BUTTON_INSERT);
	r.right(-2);
	r.up(BUTTON_INSERT); // paste before step 1
	CHECK_EQ(r.cvs(), std::string("2 3 1 2 3"));
	CHECK_EQ((int) r.view().step.cvA, 1);
}

TEST(copy_pattern_and_paste_to_build_a_song) {
	// Manual, Math section walkthrough (minus the MATH part): copy a 4-step pattern
	// and paste it after itself repeatedly.
	Sequence seq = build({{0, 4, 2}, {4, 4, 2}, {7, 4, 2}, {4, 4, 2}});
	Rig r(seq);
	r.tap(FOCUS_PATTERN);
	r.tap(BUTTON_COPY);
	CHECK_EQ((int) r.panel.focusLed(FOCUS_PATTERN), (int) LED_BLINK);
	r.tap(BUTTON_INSERT);
	r.tap(BUTTON_INSERT);
	CHECK_EQ(r.cvs(), std::string("0 4 7 4 | 0 4 7 4 | 0 4 7 4"));
	CHECK_EQ(r.view().pattern, 2);
}

TEST(step_clipboard_on_pattern_focus_inserts_empty_pattern) {
	Rig r(build({{1, 1, 1}}));
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_COPY);
	r.tap(FOCUS_PATTERN);
	r.tap(BUTTON_INSERT);
	CHECK_EQ(r.cvs(), std::string("1 |"));
}

TEST(copy_track_onto_another) {
	Rig r(build({{1, 2, 1}, {2, 2, 1}}));
	r.seq.tracks[0].loopEnd = 1;
	r.tap(BUTTON_COPY); // TRACK is focused already
	r.left(2);
	r.tap(BUTTON_INSERT);
	CHECK_EQ(r.cvs(2), std::string("1 2"));
	CHECK_EQ(r.track(2).loopEnd, 1);
	CHECK_EQ(r.view().stepInPattern, 0);
}

// --- Parameters --------------------------------------------------------------

TEST(parameters_clamp_to_0_99) {
	Rig r(build({{95, 1, 1}}));
	r.tap(FOCUS_CV_A);
	r.right(10);
	CHECK_EQ((int) r.track().steps[0].cvA, 99);
	r.right(-200);
	CHECK_EQ((int) r.track().steps[0].cvA, 0);
}

TEST(holding_duration_moves_the_step_boundary) {
	// Manual, Swinging and Shuffling: 16 + 16 -> 17 + 15, sum unchanged.
	Rig r(build({{1, 16, 1}, {2, 16, 1}}));
	r.tap(FOCUS_DURATION);
	r.down(FOCUS_DURATION);
	r.right(1);
	CHECK_EQ(r.durations(), std::string("17 15"));
	r.right(-3);
	CHECK_EQ(r.durations(), std::string("14 18"));
	r.right(20); // would push the next step below zero: refused
	CHECK_EQ(r.durations(), std::string("14 18"));
	r.up(FOCUS_DURATION);
	r.right(1); // released: a plain edit again
	CHECK_EQ(r.durations(), std::string("15 18"));
}

TEST(focus_press_on_gate_toggles_ratchet) {
	Rig r(build({{1, 8, 2}}));
	r.tap(FOCUS_GATE);
	CHECK(!r.track().steps[0].ratchet);
	r.tap(FOCUS_GATE);
	CHECK(r.track().steps[0].ratchet);
	r.tap(FOCUS_GATE);
	CHECK(!r.track().steps[0].ratchet);
}

TEST(table_switch_selects_what_voltage_shows) {
	Sequence seq = build({{24, 1, 1}});
	seq.tracks[0].steps[0].cvB = 36;
	Rig r(seq);
	CHECK_EQ(r.view().index, 24);
	r.panel.table = TABLE_B;
	CHECK_EQ(r.view().index, 36);
	CHECK_EQ(r.view().voltage == 3.f, true);
}

// --- Loop points -------------------------------------------------------------

TEST(loop_buttons_toggle_at_cursor) {
	Rig r(build({{1, 1, 1}, {2, 1, 1}, {3, 1, 1}}));
	r.tap(FOCUS_STEP);
	r.left(1);
	r.tap(BUTTON_LOOP_START);
	CHECK_EQ(r.track().loopStart, 1);
	CHECK_EQ((int) r.panel.loopLed(r.seq, true), (int) LED_ON);
	r.left(1);
	CHECK_EQ((int) r.panel.loopLed(r.seq, true), (int) LED_OFF);
	// Pressing twice anywhere clears: the first press moves it here, the second clears.
	r.tap(BUTTON_LOOP_START);
	CHECK_EQ(r.track().loopStart, 2);
	r.tap(BUTTON_LOOP_START);
	CHECK_EQ(r.track().loopStart, -1);
}

TEST(loop_a_pattern) {
	Sequence seq = build({{1, 1, 1}, {2, 1, 1}});
	seq.appendPattern(0);
	seq.appendStep(0, makeStep(3, 1, 1));
	seq.appendStep(0, makeStep(4, 1, 1));
	Rig r(seq);
	r.tap(FOCUS_PATTERN);
	r.left(1);
	r.tap(BUTTON_LOOP_START);
	r.tap(BUTTON_LOOP_END);
	CHECK_EQ(r.track().loopStart, 2);
	CHECK_EQ(r.track().loopEnd, 3);
	CHECK_EQ((int) r.panel.loopLed(r.seq, true), (int) LED_ON);
	CHECK_EQ((int) r.panel.loopLed(r.seq, false), (int) LED_ON);
	// Only the pattern plays now, after the first pass.
	Playhead ph;
	std::string order;
	for (int i = 0; i < 8; i++) {
		ph.clock(r.track());
		order += std::to_string(ph.step);
	}
	CHECK_EQ(order, std::string("01232323"));
	// A loop point inside the pattern (not on its boundary) blinks.
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_LOOP_START); // clear (cursor is on step 2 = loop start)
	r.left(1);
	r.tap(BUTTON_LOOP_START);
	r.tap(FOCUS_PATTERN);
	CHECK_EQ((int) r.panel.loopLed(r.seq, true), (int) LED_BLINK);
}

TEST(loop_points_follow_edits) {
	Rig r(build({{1, 1, 1}, {2, 1, 1}, {3, 1, 1}}));
	r.seq.tracks[0].loopStart = 2;
	r.seq.tracks[0].loopEnd = 1;
	r.tap(FOCUS_STEP);
	r.down(BUTTON_INSERT);
	r.right(-2);
	r.up(BUTTON_INSERT); // new step before step 0
	CHECK_EQ(r.track().loopStart, 3);
	CHECK_EQ(r.track().loopEnd, 2);
	r.left(2); // onto the loop end step
	r.tap(BUTTON_DELETE);
	CHECK_EQ(r.track().loopEnd, -1);
	CHECK_EQ(r.track().loopStart, 2);
}

// --- Editing while playing ---------------------------------------------------

TEST(playing_step_survives_edits_around_it) {
	Rig r(build({{1, 2, 2}, {2, 2, 2}, {3, 2, 2}}));
	Playhead& ph = r.tr.playheads[0];
	run(r.track(), ph, 3); // on step 1 (cvA 2), pulse 0
	CHECK_EQ(ph.step, 1);
	r.tap(FOCUS_STEP);
	r.down(BUTTON_INSERT);
	r.right(-2);
	r.up(BUTTON_INSERT); // insert in front of step 0
	CHECK_EQ(ph.step, 2);
	CHECK_EQ((int) r.track().steps[ph.step].cvA, 2);
	// Deleting the playing step hands over to the next one without a hiccup.
	r.left(2);
	r.tap(BUTTON_DELETE);
	CHECK_EQ((int) r.track().steps[ph.step].cvA, 3);
	CHECK_EQ(run(r.track(), ph, 2), std::string("2^ 0^"));
}

TEST(editing_an_empty_track_is_harmless) {
	Rig r;
	r.tap(FOCUS_STEP);
	r.left(3);
	r.tap(FOCUS_CV_A);
	r.right(5);
	r.tap(BUTTON_DELETE);
	r.tap(BUTTON_COPY);
	r.tap(BUTTON_LOOP_START);
	r.tap(FOCUS_PATTERN);
	r.left(-2);
	r.tap(BUTTON_DELETE);
	CHECK_EQ(r.track().numSteps(), 0);
	CHECK(!r.panel.copyLed());
	CHECK_EQ(r.view().pattern, -1);
}
