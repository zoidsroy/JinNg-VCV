// Tests for recording (docs/SPEC-ER102.md §5; src/core/Recorder.hpp): alter, step and
// real-time modes, pass-thru and the real-time configuration screen.

#include "Engine.hpp"

#include "testing.hpp"

#include <cmath>

using namespace iqs;

// A sequencer with the expander, driven one sample at a time; a clock pulse every
// SAMPLES_PER_PULSE samples. `in` is what is patched into the recording jacks.
struct RecRig {
	static constexpr int SAMPLES_PER_PULSE = 10;
	Engine e;
	RecordInputs in;
	int mode;
	long sample = 0;

	RecRig(const Sequence& s, int recordMode) : mode(recordMode) {
		e.live = s;
		e.liveReplaced();
		e.setExpander(true);
		e.setRecordInputs(in, mode);
	}
	void tap(int b) {
		e.press(b);
		e.release(b);
	}
	void run(int samples) {
		for (int i = 0; i < samples; i++) {
			e.setRecordInputs(in, mode);
			e.process(1e-3f, sample % SAMPLES_PER_PULSE == 0, false, false);
			sample++;
		}
	}
	void pulses(int n) { run(n * SAMPLES_PER_PULSE); }
	// Arms the selected track; in real-time mode also closes the configuration screen.
	void arm() {
		tap(BUTTON_ARM);
		if (e.recorder.configScreen)
			tap(BUTTON_ARM);
	}
	void selectTrack(int t) {
		if (e.panel.leftFocus != FOCUS_TRACK)
			tap(FOCUS_TRACK);
		e.turnLeft(t - e.panel.track);
	}
	const Track& track(int t = 0) const { return e.live.tracks[t]; }
	std::string steps(int t = 0) const {
		// "cvA/duration/gate" per step
		std::string out;
		for (const Step& s : e.live.tracks[t].steps)
			out += std::to_string(s.cvA) + "/" + std::to_string(s.duration) + "/" + std::to_string(s.gate) + " ";
		if (!out.empty())
			out.pop_back();
		return out;
	}
};

static Sequence fourSteps() {
	return build({{10, 1, 1}, {11, 1, 1}, {12, 1, 1}, {13, 1, 1}});
}

// --- ALTER -----------------------------------------------------------------------------

TEST(alter_rewrites_steps_as_they_play) {
	RecRig r(fourSteps(), RECORD_ALTER);
	r.in.a1Patched = true;
	r.in.a1 = 2.f; // index 24 in 12ET
	r.arm();
	r.pulses(2); // not punched in yet: nothing changes
	CHECK_EQ(r.steps(), std::string("10/1/1 11/1/1 12/1/1 13/1/1"));
	r.tap(BUTTON_PUNCH);
	CHECK(r.e.recorder.recording(false));
	r.pulses(2); // steps 2 and 3 play
	r.tap(BUTTON_PUNCH);
	r.pulses(4);
	CHECK_EQ(r.steps(), std::string("10/1/1 11/1/1 24/1/1 24/1/1"));
}

TEST(alter_uses_only_patched_inputs_and_the_gate_scale) {
	RecRig r(fourSteps(), RECORD_ALTER);
	r.in.ad1Patched = true;
	r.in.ad1 = 0.25f; // GATE = floor(0.25 * 20) = 5
	r.in.ad2Patched = true;
	r.in.ad2 = 10.f; // DURATION caps at 99
	r.arm();
	r.in.punchGate = true; // punching in by gate
	r.pulses(1);
	CHECK_EQ(r.steps().substr(0, 7), std::string("10/99/5"));
	r.in.punchGate = false;
	r.pulses(1);
	CHECK(!r.e.recorder.recording(false));
}

TEST(alter_on_a_single_looped_step_follows_live) {
	RecRig r(fourSteps(), RECORD_ALTER);
	r.e.live.tracks[0].loopStart = 1;
	r.e.live.tracks[0].loopEnd = 1;
	r.in.a1Patched = true;
	r.in.a1 = 1.f;
	r.arm();
	r.tap(BUTTON_PUNCH);
	r.pulses(3); // now looping step 1
	r.in.a1 = 3.f;
	r.run(3); // mid-pulse: reflected at once
	CHECK_EQ((int) r.track().steps[1].cvA, 36);
}

// --- STEP ------------------------------------------------------------------------------

TEST(step_mode_inserts_and_deletes_at_the_edit_cursor) {
	RecRig r(fourSteps(), RECORD_STEP);
	r.tap(FOCUS_STEP);
	r.e.turnLeft(1); // cursor on step 1
	r.in.a1Patched = true;
	r.in.a1 = 1.5f;       // index 18
	r.in.ad2Patched = true;
	r.in.ad2 = 0.2f;      // DURATION 4
	r.arm();
	r.tap(BUTTON_PUNCH);
	r.in.d1 = true;       // insert
	r.run(1);
	CHECK_EQ(r.steps(), std::string("10/1/1 11/1/1 18/4/1 12/1/1 13/1/1"));
	CHECK_EQ(r.e.panel.cursors[0].step, 2);
	r.in.a1 = 2.f;        // still held: the new step follows
	r.run(1);
	r.in.d1 = false;
	r.run(1);
	r.in.a1 = 3.f;        // released: locked
	r.run(1);
	CHECK_EQ((int) r.track().steps[2].cvA, 24);
	r.in.d2 = true;       // delete the step at the cursor
	r.run(1);
	r.in.d2 = false;
	r.run(1);
	CHECK_EQ(r.steps(), std::string("10/1/1 11/1/1 12/1/1 13/1/1"));
}

TEST(step_mode_needs_punch_and_arm) {
	RecRig r(fourSteps(), RECORD_STEP);
	r.in.d1 = true;
	r.run(1);
	CHECK_EQ(r.track().numSteps(), 4);
	r.in.d1 = false;
	r.arm();
	r.in.d1 = true;
	r.run(1);
	CHECK_EQ(r.track().numSteps(), 4); // armed but not punched in
}

TEST(step_mode_in_hold_edits_the_shadow) {
	RecRig r(fourSteps(), RECORD_STEP);
	r.e.setMode(MODE_HOLD);
	r.tap(FOCUS_STEP);
	r.arm();
	r.tap(BUTTON_PUNCH);
	r.in.d1 = true;
	r.run(1);
	CHECK_EQ(r.e.shadow.tracks[0].numSteps(), 5);
	CHECK_EQ(r.track().numSteps(), 4);
}

// --- REAL-TIME ---------------------------------------------------------------------------

TEST(realtime_records_a_performance_as_steps) {
	Sequence s = fourSteps(); // track 0 plays; track 1 records
	RecRig r(s, RECORD_REALTIME);
	r.selectTrack(1);
	r.arm();
	r.in.a1Patched = r.in.ad1Patched = true;
	r.pulses(2); // settle the clock
	r.tap(BUTTON_PUNCH);
	r.pulses(1); // punched in, waiting for the first note: nothing yet
	CHECK_EQ(r.track(1).numSteps(), 0);
	// Note 1: 1V, held 2 pulses, then 2 pulses of silence.
	r.in.a1 = 1.f;
	r.in.ad1 = 5.f;
	r.pulses(2);
	r.in.ad1 = 0.f;
	r.pulses(2);
	// Note 2: 2V, held 3 pulses (legato into note 3 via a pitch change).
	r.in.a1 = 2.f;
	r.in.ad1 = 5.f;
	r.pulses(3);
	r.in.a1 = 3.f; // CV-A change while held: a new step
	r.pulses(1);
	r.in.ad1 = 0.f;
	r.pulses(1);
	r.tap(BUTTON_PUNCH); // punch out closes the last step
	CHECK_EQ(r.steps(1), std::string("12/4/2 24/3/3 36/2/1"));
	CHECK_EQ(r.track(1).patterns.size(), (size_t) 1); // TRACK focus: a new pattern at the end
	CHECK(!r.e.recorder.recording(false));
	CHECK_EQ(r.steps(0), std::string("10/1/1 11/1/1 12/1/1 13/1/1")); // other track untouched
}

TEST(realtime_quantizes_to_the_grids) {
	RecRig r(Sequence(), RECORD_REALTIME);
	r.tap(BUTTON_ARM);            // opens the configuration screen
	CHECK(r.e.view().recordConfig);
	r.tap(FOCUS_DURATION);
	r.e.turnRight(3);             // DURATION grid 4
	r.tap(FOCUS_GATE);
	r.e.turnRight(1);             // GATE grid 2
	r.tap(FOCUS_CV_A);            // CV-A changes no longer start steps
	CHECK(!r.e.view().recordCvATrigger);
	r.tap(BUTTON_ARM);            // close
	CHECK(!r.e.view().recordConfig);
	CHECK(r.e.recorder.armed[0]);
	r.in.a1Patched = r.in.ad1Patched = true;
	r.pulses(2);
	r.tap(BUTTON_PUNCH);
	// A slightly sloppy note: on for 2.7 pulses, next note 3.8 pulses after the first.
	r.in.ad1 = 5.f;
	r.run(27);
	r.in.a1 = 4.f;  // ignored as a trigger now
	r.in.ad1 = 0.f;
	r.run(11);
	r.in.ad1 = 5.f;
	r.run(20);
	r.tap(BUTTON_PUNCH);
	CHECK_EQ(r.track().numSteps(), 2);
	CHECK_EQ((int) r.track().steps[0].duration, 4); // 3.8 -> 4
	CHECK_EQ((int) r.track().steps[0].gate, 2);     // 2.7 -> 2 (grid 2)
}

TEST(realtime_pass_thru_monitoring_and_rehearsal) {
	RecRig r(fourSteps(), RECORD_REALTIME);
	r.arm();
	r.in.a1Patched = r.in.ad1Patched = true;
	r.in.a1 = -3.f; // passed on unprocessed, even out of the sequencer's range
	r.pulses(1);
	CHECK(r.e.cvA(0) >= 0.f);   // armed but not punched in and no note: normal output
	r.in.ad1 = 5.f;             // a note while punched out: rehearsal pass-thru
	r.run(1);
	CHECK(std::fabs(r.e.cvA(0) + 3.f) < 1e-6f);
	CHECK(r.e.gate(0));
	CHECK_EQ(r.track().numSteps(), 4); // and nothing is recorded
	// RESET ends pass-thru.
	r.e.process(1e-3f, false, true, true);
	r.in.ad1 = 0.f;
	r.run(30);
	CHECK(r.e.cvA(0) >= 0.f);
	// Unarmed tracks never pass through.
	CHECK(!r.e.passThru(1));
}

TEST(realtime_pause_ends_the_take) {
	RecRig r(Sequence(), RECORD_REALTIME);
	r.arm();
	r.in.ad1Patched = true;
	r.pulses(2);
	r.tap(BUTTON_PUNCH);
	r.in.ad1 = 5.f;
	r.pulses(3);
	CHECK(r.e.recorder.recording(false));
	r.e.tr.paused = true;
	r.run(1);
	CHECK(!r.e.recorder.recording(false));
	CHECK_EQ(r.track().numSteps(), 1);
	CHECK_EQ((int) r.track().steps[0].duration, 3);
}

TEST(realtime_long_takes_continue_in_new_patterns) {
	RecRig r(Sequence(), RECORD_REALTIME);
	r.arm();
	r.in.ad1Patched = true;
	r.pulses(2);
	r.tap(BUTTON_PUNCH);
	for (int i = 0; i < 105; i++) {
		r.in.ad1 = 5.f;
		r.run(5);
		r.in.ad1 = 0.f;
		r.run(5);
	}
	r.tap(BUTTON_PUNCH);
	CHECK_EQ(r.track().numSteps(), 105);
	CHECK_EQ(r.track().patterns.size(), (size_t) 2);
	CHECK_EQ((int) r.track().patterns[0].length, 100);
}

TEST(realtime_config_screen_and_arm_toggling) {
	RecRig r(Sequence(), RECORD_REALTIME);
	r.tap(BUTTON_ARM);
	CHECK(r.e.recorder.configScreen);
	CHECK_EQ((int) r.e.focusLed(FOCUS_TRACK), (int) LED_BLINK);
	r.tap(FOCUS_PATTERN); // takes go after the playing pattern
	CHECK_EQ(r.e.recorder.config.focus, (int) FOCUS_PATTERN);
	CHECK_EQ((int) r.e.focusLed(FOCUS_PATTERN), (int) LED_BLINK);
	r.tap(BUTTON_ARM);
	CHECK(!r.e.recorder.configScreen);
	CHECK(r.e.recorder.armed[0]);
	r.tap(BUTTON_ARM); // disarm
	CHECK(!r.e.recorder.armed[0]);
	// Changing the record mode stops everything in progress.
	r.tap(BUTTON_ARM);
	r.mode = RECORD_ALTER;
	r.run(1);
	CHECK(!r.e.recorder.configScreen);
}
