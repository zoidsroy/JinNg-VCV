// Tests for the sequence model, playback, transport and note formatting.

#include "Sequence.hpp"
#include "Playhead.hpp"
#include "NoteFormat.hpp"
#include "Transport.hpp"

#include "testing.hpp"

#include <cmath>

using namespace iqs;

// --- Data model ------------------------------------------------------------

TEST(default_table_is_12et) {
	VoltageTable tbl;
	CHECK(std::fabs(tbl[12] - 1.f) < 1e-6f);
	CHECK(std::fabs(tbl[24] - 2.f) < 1e-6f);
	CHECK(std::fabs(tbl[96] - 8.f) < 1e-6f);
	CHECK(tbl[99] <= MAX_VOLTAGE);
	CHECK_EQ(tbl.closestIndex(1.f), 12);
}

TEST(new_step_defaults) {
	Track t;
	Step s = t.defaultStep();
	CHECK_EQ((int) s.cvA, 12);
	CHECK_EQ((int) s.cvB, 12);
	CHECK_EQ((int) s.duration, 0);
	CHECK_EQ((int) s.gate, 0);
}

TEST(total_step_limit_is_shared_across_tracks) {
	Sequence seq;
	int added = 0;
	for (int track = 0; track < NUM_TRACKS; track++) {
		for (int p = 0; p < 10; p++) {
			seq.appendPattern(track);
			for (int i = 0; i < 100; i++)
				added += seq.appendStep(track, Step());
		}
	}
	CHECK_EQ(added, MAX_TOTAL_STEPS);
	CHECK_EQ(seq.totalSteps(), MAX_TOTAL_STEPS);
	CHECK(!seq.appendStep(3, Step()));
}

TEST(pattern_limits) {
	Sequence seq;
	for (int i = 0; i < MAX_STEPS_PER_PATTERN; i++)
		CHECK(seq.appendStep(0, Step()));
	CHECK(!seq.appendStep(0, Step()));

	Sequence seq2;
	for (int i = 0; i < MAX_PATTERNS; i++)
		CHECK(seq2.appendPattern(1));
	CHECK(!seq2.appendPattern(1));
}

TEST(reserved_capacity_survives_copy) {
	Sequence a;
	a.appendStep(0, Step());
	Sequence b = a;
	CHECK(b.tracks[0].steps.capacity() >= (size_t) MAX_TOTAL_STEPS);
	b = a;
	CHECK(b.tracks[0].steps.capacity() >= (size_t) MAX_TOTAL_STEPS);
	CHECK_EQ(b.tracks[0].numSteps(), 1);
}

TEST(locate_step_in_patterns) {
	Sequence seq;
	seq.appendStep(0, Step());
	seq.appendStep(0, Step());
	seq.appendPattern(0); // empty pattern in the middle
	seq.appendPattern(0);
	seq.appendStep(0, Step());
	int p, s;
	seq.tracks[0].locate(1, p, s);
	CHECK_EQ(p, 0);
	CHECK_EQ(s, 1);
	seq.tracks[0].locate(2, p, s);
	CHECK_EQ(p, 2);
	CHECK_EQ(s, 0);
	seq.tracks[0].locate(3, p, s);
	CHECK_EQ(p, -1);
}

// --- Playback --------------------------------------------------------------

TEST(empty_track_does_nothing) {
	Sequence seq;
	Playhead ph;
	CHECK_EQ(run(seq.tracks[0], ph, 3), std::string("-_ -_ -_"));
	CHECK(!ph.gate(seq.tracks[0]));
	CHECK(std::fabs(ph.cvA(seq.tracks[0])) < 1e-6f);
}

TEST(first_clock_starts_first_step) {
	Sequence seq = build({{12, 4, 2}});
	Playhead ph;
	CHECK(ph.armed());
	CHECK(!ph.gate(seq.tracks[0]));
	// The armed cursor already presents the first step's CV.
	CHECK(std::fabs(ph.cvA(seq.tracks[0]) - 1.f) < 1e-6f);
	CHECK_EQ(run(seq.tracks[0], ph, 1), std::string("0^"));
}

TEST(quick_start_duration_and_gate) {
	// Manual Quick Start: DURATION 4, GATE 2 -> gate high for half of each step.
	Sequence seq = build({{12, 4, 2}, {24, 4, 2}});
	Playhead ph;
	CHECK_EQ(run(seq.tracks[0], ph, 9), std::string("0^ 0^ 0_ 0_ 1^ 1^ 1_ 1_ 0^"));
}

TEST(cv_follows_voltage_table) {
	Sequence seq = build({{12, 1, 1}, {24, 1, 1}});
	Playhead ph;
	ph.clock(seq.tracks[0]);
	CHECK(std::fabs(ph.cvA(seq.tracks[0]) - 1.f) < 1e-6f);
	ph.clock(seq.tracks[0]);
	CHECK(std::fabs(ph.cvA(seq.tracks[0]) - 2.f) < 1e-6f);
}

TEST(gate_zero_is_a_rest_and_gate_at_duration_is_legato) {
	Sequence seq = build({{12, 2, 0}, {12, 3, 3}, {12, 2, 50}});
	Playhead ph;
	CHECK_EQ(run(seq.tracks[0], ph, 7), std::string("0_ 0_ 1^ 1^ 1^ 2^ 2^"));
}

TEST(zero_duration_steps_are_skipped) {
	Sequence seq = build({{12, 0, 0}, {12, 2, 1}, {12, 0, 5}, {12, 0, 5}, {12, 1, 1}});
	Playhead ph;
	// Reset/first clock skips step 0 too.
	CHECK_EQ(run(seq.tracks[0], ph, 5), std::string("1^ 1_ 4^ 1^ 1_"));
}

TEST(all_zero_durations_stall) {
	Sequence seq = build({{12, 0, 1}, {12, 0, 1}});
	Playhead ph;
	CHECK_EQ(run(seq.tracks[0], ph, 3), std::string("-_ -_ -_"));
}

TEST(single_step_loops_on_itself) {
	Sequence seq = build({{12, 2, 1}});
	Playhead ph;
	CHECK_EQ(run(seq.tracks[0], ph, 4), std::string("0^ 0_ 0^ 0_"));
}

// Manual figure 5: the four loop point cases on a 6-step track.
static Sequence sixSteps() {
	return build({{0, 1, 1}, {1, 1, 1}, {2, 1, 1}, {3, 1, 1}, {4, 1, 1}, {5, 1, 1}});
}

static std::string order(Sequence& seq, int pulses) {
	Playhead ph;
	std::string out;
	for (int i = 0; i < pulses; i++) {
		ph.clock(seq.tracks[0]);
		out += std::to_string(ph.step);
	}
	return out;
}

TEST(loop_none) {
	Sequence seq = sixSteps();
	CHECK_EQ(order(seq, 9), std::string("012345012"));
}

TEST(loop_start_only) {
	Sequence seq = sixSteps();
	seq.tracks[0].loopStart = 2;
	CHECK_EQ(order(seq, 10), std::string("0123452345"));
}

TEST(loop_end_only) {
	Sequence seq = sixSteps();
	seq.tracks[0].loopEnd = 3;
	CHECK_EQ(order(seq, 9), std::string("012301230"));
}

TEST(loop_start_and_end) {
	Sequence seq = sixSteps();
	seq.tracks[0].loopStart = 1;
	seq.tracks[0].loopEnd = 3;
	CHECK_EQ(order(seq, 8), std::string("01231231"));
}

TEST(loop_single_step) {
	Sequence seq = sixSteps();
	seq.tracks[0].loopStart = 4;
	seq.tracks[0].loopEnd = 4;
	CHECK_EQ(order(seq, 8), std::string("01234444"));
}

TEST(loop_skips_zero_duration_step_at_loop_start) {
	Sequence seq = sixSteps();
	seq.tracks[0].steps[2].duration = 0;
	seq.tracks[0].loopStart = 2;
	seq.tracks[0].loopEnd = 4;
	CHECK_EQ(order(seq, 8), std::string("01343434"));
}

TEST(reset_ignores_loop_points) {
	Sequence seq = sixSteps();
	seq.tracks[0].loopStart = 3;
	seq.tracks[0].loopEnd = 4;
	Playhead ph;
	run(seq.tracks[0], ph, 6);
	ph.reset(seq.tracks[0], false);
	CHECK_EQ(ph.step, 0);
	CHECK(ph.armed());
	CHECK_EQ(run(seq.tracks[0], ph, 6), std::string("0^ 1^ 2^ 3^ 4^ 3^"));
}

TEST(reset_immediate_starts_sounding_now) {
	Sequence seq = build({{12, 3, 3}, {12, 1, 1}});
	Playhead ph;
	run(seq.tracks[0], ph, 4);
	ph.reset(seq.tracks[0], true);
	CHECK(!ph.armed());
	CHECK(ph.gate(seq.tracks[0]));
	// The reset itself counted as pulse 0 of step 0.
	CHECK_EQ(run(seq.tracks[0], ph, 3), std::string("0^ 0^ 1^"));
}

TEST(edit_shrinking_track_under_cursor) {
	Sequence seq = sixSteps();
	Playhead ph;
	run(seq.tracks[0], ph, 5); // on step 4
	seq.clearTrack(0);
	seq.appendStep(0, makeStep(12, 1, 1));
	// The stale cursor falls back to the first step, which the next clock starts.
	CHECK_EQ(run(seq.tracks[0], ph, 2), std::string("0^ 0^"));
}

// --- Transport (clock / reset) ---------------------------------------------

struct Sim {
	static constexpr float DT = 1.f / 48000.f;
	Sequence seq = build({{12, 1, 1}, {24, 1, 1}, {36, 1, 1}});
	Transport tr;

	Sim(int mode = RESET_ARMS_FIRST_STEP) {
		tr.resetMode = mode;
		tr.rewind(seq, false);
	}
	void tick(bool clock = false, bool resetEdge = false, bool resetHeld = false) {
		tr.process(seq, DT, clock, resetEdge, resetHeld || resetEdge);
	}
	void idle(int samples, bool resetHeld = false) {
		for (int i = 0; i < samples; i++)
			tick(false, false, resetHeld);
	}
	// "step:pulse", or "-" when armed.
	std::string pos() const {
		const Playhead& ph = tr.playheads[0];
		return ph.armed() ? std::string("-") : std::to_string(ph.step) + ":" + std::to_string(ph.pulse);
	}
};

TEST(transport_reset_then_clock) {
	Sim s;
	s.tick(true);
	s.idle(1000);
	s.tick(true);
	CHECK_EQ(s.pos(), std::string("1:0"));
	s.idle(1000);
	s.tick(false, true);
	CHECK_EQ(s.pos(), std::string("-"));
	s.idle(1000);
	s.tick(true);
	CHECK_EQ(s.pos(), std::string("0:0"));
}

TEST(transport_reset_and_clock_on_same_sample) {
	Sim s;
	s.tick(true);
	s.idle(1000);
	s.tick(true);
	s.idle(1000);
	s.tick(true, true);
	CHECK_EQ(s.pos(), std::string("0:0"));
	s.idle(1000);
	s.tick(true);
	CHECK_EQ(s.pos(), std::string("1:0"));
}

TEST(transport_reset_one_sample_after_clock) {
	Sim s;
	s.tick(true);
	s.idle(1000);
	s.tick(true);        // advances to step 1 ...
	s.tick(false, true); // ... but the reset right behind it makes this beat step 0
	CHECK_EQ(s.pos(), std::string("0:0"));
	CHECK(s.tr.gate(s.seq, 0));
	s.idle(1000);
	s.tick(true);
	CHECK_EQ(s.pos(), std::string("1:0"));
}

TEST(transport_clock_during_short_reset_pulse_counts) {
	Sim s;
	s.tick(true);
	s.idle(1000);
	s.tick(false, true);
	s.idle(24, true); // 0.5ms into a 1ms reset trigger
	s.tick(true, false, true);
	CHECK_EQ(s.pos(), std::string("0:0"));
}

TEST(transport_reset_held_high_parks_the_sequencer) {
	Sim s;
	s.tick(true);
	s.idle(1000);
	s.tick(false, true);
	s.idle(1000, true);
	for (int i = 0; i < 3; i++) {
		s.tick(true, false, true);
		s.idle(1000, true);
	}
	CHECK_EQ(s.pos(), std::string("-"));
	s.idle(1000);
	s.tick(true);
	CHECK_EQ(s.pos(), std::string("0:0"));
}

TEST(transport_reset_starts_first_step_mode) {
	Sim s(RESET_STARTS_FIRST_STEP);
	s.tick(true);
	s.idle(1000);
	s.tick(true);
	s.idle(1000);
	s.tick(false, true);
	CHECK_EQ(s.pos(), std::string("0:0"));
	s.idle(1000);
	s.tick(true);
	CHECK_EQ(s.pos(), std::string("1:0"));

	// A clock with the reset is the same beat, not an extra one.
	s.idle(1000);
	s.tick(true, true);
	CHECK_EQ(s.pos(), std::string("0:0"));
}

TEST(transport_pause_ignores_clock_and_mutes_gates) {
	Sim s;
	s.tick(true);
	CHECK(s.tr.gate(s.seq, 0));
	s.tr.paused = true;
	CHECK(!s.tr.gate(s.seq, 0));
	s.idle(1000);
	s.tick(true);
	CHECK_EQ(s.pos(), std::string("0:0"));
	s.tr.paused = false;
	s.idle(1000);
	s.tick(true);
	CHECK_EQ(s.pos(), std::string("1:0"));
}

// --- Note display ----------------------------------------------------------

TEST(note_format_examples_from_manual) {
	CHECK_EQ(formatNote(0.f), std::string("0.C.00"));
	CHECK_EQ(formatNote(1.f), std::string("1.C.00"));
	CHECK_EQ(formatNote(2.f), std::string("2.C.00"));
	CHECK_EQ(formatNote(3.f + 10 / 12.f), std::string("3.A.50"));  // A#3
	CHECK_EQ(formatNote(5.f + 10 / 12.f), std::string("5.A.50"));  // Bb5
	CHECK_EQ(formatNote(4.f + 7.5f / 12.f), std::string("4.G.25")); // half-sharp G4
	CHECK_EQ(formatNote(4.f + 8.5f / 12.f), std::string("4.G.75"));
	CHECK_EQ(formatNote(4.f / 12.f), std::string("0.E.00"));
	CHECK_EQ(formatNote(11.f / 12.f), std::string("0.B.00"));
	CHECK_EQ(formatNote(0.9999f), std::string("1.C.00"));
}

TEST(voltage_format) {
	CHECK_EQ(formatVoltage(1.f), std::string("1.000"));
	CHECK_EQ(formatVoltage(8.192f), std::string("8.192"));
}
