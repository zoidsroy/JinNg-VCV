// Tests for parts (docs/SPEC-ER102.md §3): editing a part's RESET TO step and loop,
// triggering parts by button or CV, and the FIRST / LAST / USER transitions.

#include "Engine.hpp"

#include "testing.hpp"

using namespace iqs;

struct PartRig {
	Engine e;

	explicit PartRig(const Sequence& s) {
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
	int playing(int t = 0) const { return e.tr.playheads[t].step; }
	const Track& track(int t = 0) const { return e.live.tracks[t]; }
	// Focus a track and step (left encoder), leaving STEP focused.
	void goTo(int t, int step) {
		if (e.panel.leftFocus != FOCUS_TRACK) // (pressing it again opens the options screen)
			tap(FOCUS_TRACK);
		e.turnLeft(t - e.panel.track);
		tap(FOCUS_STEP);
		e.turnLeft(-200);
		e.turnLeft(step);
	}
	void focusPart(int p) {
		tap(FOCUS_PART);
		e.turnLeft(p - e.panel.focusedPart);
	}
};

// Eight one-pulse steps on track 0: "intro" 0-1, "verse" 2-4, "chorus" 5-7.
static Sequence song() {
	Sequence s;
	for (int i = 0; i < 8; i++)
		s.appendStep(0, makeStep(i, 1, 1));
	return s;
}

// --- Editing parts -----------------------------------------------------------------

TEST(parts_existing_loops_become_part_one) {
	Sequence s = song();
	s.tracks[0].loopStart = 2;
	s.tracks[0].loopEnd = 4;
	Engine e;
	e.live = s;
	e.liveReplaced();
	e.setExpander(true);
	CHECK_EQ((int) e.live.tracks[0].parts[1].loopStart, 2);
	CHECK_EQ((int) e.live.tracks[0].parts[1].loopEnd, 4);
	CHECK_EQ(e.live.tracks[0].loopStart, 2); // still playing the same loop
}

TEST(parts_loop_buttons_edit_the_focused_part) {
	PartRig r(song());
	r.focusPart(3);
	r.goTo(0, 5);
	r.tap(BUTTON_LOOP_START);
	r.e.turnLeft(2);
	r.tap(BUTTON_LOOP_END);
	CHECK_EQ((int) r.track().parts[3].loopStart, 5);
	CHECK_EQ((int) r.track().parts[3].loopEnd, 7);
	// Part 3 is not playing (part 1 is), so playback's loop is untouched.
	CHECK_EQ(r.track().loopStart, -1);
	CHECK_EQ((int) r.e.panel.loopLed(r.e.live, false), (int) LED_ON);
	// Editing the playing part is heard at once.
	r.focusPart(1);
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_LOOP_END);
	CHECK_EQ((int) r.track().parts[1].loopEnd, 7);
	CHECK_EQ(r.track().loopEnd, 7);
}

TEST(parts_reset_to_toggles_and_lights) {
	PartRig r(song());
	r.focusPart(2);
	r.goTo(0, 4);
	r.tap(BUTTON_RESET_TO);
	CHECK_EQ((int) r.track().parts[2].resetTo, 4);
	CHECK(r.e.panel.resetToLed(r.e.live));
	r.e.turnLeft(1);
	CHECK(!r.e.panel.resetToLed(r.e.live));
	r.tap(BUTTON_RESET_TO); // moves it here
	CHECK_EQ((int) r.track().parts[2].resetTo, 5);
	r.tap(BUTTON_RESET_TO); // and clears it
	CHECK_EQ((int) r.track().parts[2].resetTo, -1);
}

TEST(parts_stop_part_cannot_be_edited) {
	PartRig r(song());
	r.focusPart(0);
	r.goTo(0, 3);
	r.tap(BUTTON_RESET_TO);
	r.tap(BUTTON_LOOP_START);
	CHECK(r.track().parts[0].empty());
}

TEST(parts_copy_paste_and_clear) {
	PartRig r(song());
	r.focusPart(4);
	r.goTo(0, 2);
	r.tap(BUTTON_RESET_TO);
	r.tap(BUTTON_LOOP_START);
	r.focusPart(4);
	r.tap(BUTTON_COPY);
	CHECK_EQ((int) r.e.panel.focusLed(FOCUS_PART), (int) LED_BLINK);
	r.e.turnLeft(3); // part 7
	r.tap(BUTTON_INSERT);
	CHECK(r.track().parts[7] == r.track().parts[4]);
	r.tap(BUTTON_COPY); // clear the clipboard
	r.tap(BUTTON_DELETE);
	CHECK(r.track().parts[7].empty());
	CHECK(!r.track().parts[4].empty());
}

TEST(parts_quick_navigation_while_holding_part) {
	PartRig r(song());
	r.focusPart(1);
	r.goTo(0, 3);
	r.tap(BUTTON_RESET_TO);
	r.e.turnLeft(2);
	r.tap(BUTTON_LOOP_START);
	r.focusPart(1);
	r.e.press(FOCUS_PART);
	r.e.turnLeft(-10);
	CHECK_EQ(r.e.panel.cursors[0].step, 0); // first step
	r.e.turnLeft(1);
	CHECK_EQ(r.e.panel.cursors[0].step, 3); // RESET TO
	r.e.turnLeft(1);
	CHECK_EQ(r.e.panel.cursors[0].step, 5); // LOOP START (no LOOP END set)
	r.e.turnLeft(1);
	CHECK_EQ(r.e.panel.cursors[0].step, 7); // last step
	r.e.release(FOCUS_PART);
	CHECK_EQ(r.e.panel.focusedPart, 1); // the knob did not change the part
}

TEST(parts_points_follow_step_edits) {
	PartRig r(song());
	r.focusPart(2);
	r.goTo(0, 6);
	r.tap(BUTTON_RESET_TO);
	r.goTo(0, 0);
	r.tap(BUTTON_INSERT); // a step after step 0 pushes the rest along
	CHECK_EQ((int) r.track().parts[2].resetTo, 7);
	r.e.turnLeft(6); // onto it
	r.tap(BUTTON_DELETE);
	CHECK_EQ((int) r.track().parts[2].resetTo, -1);
}

// --- Selecting and triggering -------------------------------------------------------

TEST(parts_select_cv_picks_the_part) {
	PartRig r(song());
	r.tap(FOCUS_PART);
	r.e.setPartInputs(true, 0.35f, false);
	CHECK_EQ(r.e.panel.focusedPart, 3);
	r.e.turnLeft(1);
	CHECK_EQ(std::string(r.e.view().message ? r.e.view().message : ""), std::string("PLUG"));
	CHECK_EQ(r.e.panel.focusedPart, 3);
	r.e.setPartInputs(true, 20.f, false);
	CHECK_EQ(r.e.panel.focusedPart, 99);
	r.e.setPartInputs(true, -3.f, false);
	CHECK_EQ(r.e.panel.focusedPart, 0);
}

TEST(parts_activate_gate_plays_parts_live) {
	PartRig r(song());
	r.e.transition = Engine::TRANSITION_LAST;
	r.clock();
	r.e.setPartInputs(true, 0.2f, false);
	r.e.setPartInputs(true, 0.2f, true); // rising edge: part 2 pending
	CHECK_EQ(r.e.pendingPart, 2);
	r.e.setPartInputs(true, 0.5f, true); // held: the pending part follows SELECT
	CHECK_EQ(r.e.pendingPart, 5);
	r.e.setPartInputs(true, 0.7f, false); // released: stays
	CHECK_EQ(r.e.pendingPart, 5);
}

// --- Transitions ----------------------------------------------------------------------

// Part 2: "Intro-[Verse-Chorus]" (the manual's most common kind): reset to the intro,
// then loop verse and chorus.
static void makeIntroPart(PartRig& r) {
	PartPoints& p = r.e.live.tracks[0].parts[2];
	p.resetTo = 0;
	p.loopStart = 2;
	p.loopEnd = 7;
}

TEST(parts_user_transition_is_immediate_without_reset) {
	PartRig r(song());
	makeIntroPart(r);
	r.e.transition = Engine::TRANSITION_USER;
	r.clock(4); // on step 3
	r.focusPart(2);
	r.tap(BUTTON_TRANSITION);
	CHECK_EQ(r.e.playingPart, 2);
	CHECK_EQ(r.e.pendingPart, -1);
	CHECK_EQ(r.playing(), 3); // no reset
	CHECK_EQ(r.track().loopStart, 2);
	r.clock(5); // 4 5 6 7 then back to the loop start
	CHECK_EQ(r.playing(), 2);
}

TEST(parts_first_transition_waits_for_a_loop_end_then_resets) {
	PartRig r(song());
	makeIntroPart(r);
	// Reset to step 1, so the reset is told apart from the wrap-around to step 0.
	r.e.live.tracks[0].parts[2].resetTo = 1;
	r.clock(3); // step 2
	r.focusPart(2);
	r.tap(BUTTON_TRANSITION);
	CHECK_EQ(r.e.pendingPart, 2);
	CHECK_EQ((int) r.e.panel.focusLed(FOCUS_PART), (int) LED_BLINK);
	r.clock(5); // steps 3..7: part 1 still playing the whole track
	CHECK_EQ(r.e.playingPart, 1);
	r.clock(); // the track wraps: part 2 starts, resetting to step 1 at once
	CHECK_EQ(r.e.playingPart, 2);
	CHECK_EQ(r.playing(), 1);
	CHECK(r.e.gate(0));
	r.clock(8); // 2..7, then the loop again: 2 3
	CHECK_EQ(r.playing(), 3);
}

TEST(parts_first_versus_last_with_two_tracks) {
	// Track 0 loops every 2 pulses, track 1 every 4.
	Sequence s;
	for (int i = 0; i < 2; i++)
		s.appendStep(0, makeStep(i, 1, 1));
	for (int i = 0; i < 4; i++)
		s.appendStep(1, makeStep(i, 1, 1));
	for (int mode = 0; mode < 2; mode++) {
		PartRig r(s);
		r.e.transition = mode == 0 ? Engine::TRANSITION_FIRST : Engine::TRANSITION_LAST;
		r.clock(); // both on step 0
		r.focusPart(3);
		r.tap(BUTTON_TRANSITION);
		r.clock(2); // track 0 wraps
		CHECK_EQ(r.e.playingPart, mode == 0 ? 3 : 1);
		r.clock(2); // track 1 wraps too
		CHECK_EQ(r.e.playingPart, 3);
	}
}

TEST(parts_naked_loop_keeps_position) {
	PartRig r(song());
	PartPoints& p = r.e.live.tracks[0].parts[5];
	p.loopStart = 4;
	p.loopEnd = 6; // no RESET TO
	r.clock(3);
	r.focusPart(5);
	r.tap(BUTTON_TRANSITION);
	r.clock(6); // 3..7, then wrap: part 5 starts without a reset
	CHECK_EQ(r.e.playingPart, 5);
	CHECK_EQ(r.playing(), 0); // the wrap itself went to step 0
	r.clock(4); // 1 2 3 4
	r.clock(3); // 5 6 then back to 4
	CHECK_EQ(r.playing(), 4);
}

TEST(parts_stop_part_silences_and_restarts_at_once) {
	PartRig r(song());
	makeIntroPart(r);
	r.clock(2);
	r.focusPart(0);
	r.tap(BUTTON_TRANSITION);
	r.clock(7); // wraps: STOP takes over
	CHECK_EQ(r.e.playingPart, 0);
	int at = r.playing();
	r.clock(3);
	CHECK_EQ(r.playing(), at); // nothing moves
	CHECK(!r.e.gate(0));
	// Leaving STOP does not wait for a loop that will never end.
	r.focusPart(2);
	r.tap(BUTTON_TRANSITION);
	CHECK_EQ(r.e.playingPart, 2);
	CHECK(r.e.tr.playheads[0].armed());
	r.clock();
	CHECK_EQ(r.playing(), 0); // the intro, on the next clock
	CHECK(r.e.gate(0));
}

TEST(parts_reset_goes_to_the_playing_parts_reset_step) {
	PartRig r(song());
	makeIntroPart(r);
	r.e.live.tracks[0].parts[2].resetTo = 5;
	r.e.transition = Engine::TRANSITION_USER;
	r.focusPart(2);
	r.tap(BUTTON_TRANSITION);
	r.clock(3);
	r.e.process(1e-3f, false, true, true); // RESET
	for (int i = 0; i < 20; i++)
		r.e.process(1e-3f, false, false, false);
	r.clock();
	CHECK_EQ(r.playing(), 5);
}

TEST(parts_without_expander_nothing_changes) {
	PartRig r(song());
	r.e.setExpander(false);
	r.goTo(0, 3);
	r.tap(BUTTON_LOOP_START);
	CHECK_EQ(r.track().loopStart, 3); // the track's own loop again
	r.tap(BUTTON_TRANSITION);
	CHECK_EQ(r.e.pendingPart, -1);
}
