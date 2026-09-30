// Tests for the time-based features: ratchet, trigger mode, clock division and
// multiplication, and smoothing (src/core/Transport.hpp, ClockDivMul.hpp), plus the
// panel controls that set them (SMOOTH, the track options screen).

#include "Panel.hpp"
#include "Transport.hpp"

#include "testing.hpp"

#include <cmath>

using namespace iqs;

// Runs a sequence against a steady external clock, one sample at a time.
struct Clocked {
	Sequence seq;
	Transport tr;
	float dt;
	int samplesPerClock;
	long sample = 0;

	Clocked(const Sequence& s, float dt, int samplesPerClock) : seq(s), dt(dt), samplesPerClock(samplesPerClock) {
		tr.rewind(seq, false);
	}

	bool clockNow() const {
		return sample % samplesPerClock == 0;
	}
	void step() {
		tr.process(seq, dt, clockNow(), false, false);
		sample++;
	}
	void run(long n) {
		for (long i = 0; i < n; i++)
			step();
	}
	float cvA() const { return tr.cvA(seq, 0); }
	bool gate(int t = 0) const { return tr.gate(seq, t); }
	int pos(int t = 0) const { return tr.playheads[t].step; }
	int pulse(int t = 0) const { return tr.playheads[t].pulse; }
};

static bool near(float a, float b, float tol = 0.02f) {
	return std::fabs(a - b) <= tol;
}

// --- Ratchet (gate mode) -----------------------------------------------------

TEST(ratchet_gate_repeats_gate_length) {
	// Manual figure 7: DURATION 8, GATE 2 -> high 2, low 2, twice.
	Sequence seq = build({{12, 8, 2}});
	seq.tracks[0].steps[0].ratchet = true;
	Playhead ph;
	CHECK_EQ(run(seq.tracks[0], ph, 8), std::string("0^ 0^ 0_ 0_ 0^ 0^ 0_ 0_"));
	// GATE 1 is the fastest ratchet: four hits.
	seq.tracks[0].steps[0].gate = 1;
	Playhead ph2;
	CHECK_EQ(run(seq.tracks[0], ph2, 8), std::string("0^ 0_ 0^ 0_ 0^ 0_ 0^ 0_"));
	// GATE >= DURATION stays legato; GATE 0 stays a rest.
	seq.tracks[0].steps[0].gate = 8;
	Playhead ph3;
	CHECK_EQ(run(seq.tracks[0], ph3, 3), std::string("0^ 0^ 0^"));
	seq.tracks[0].steps[0].gate = 0;
	Playhead ph4;
	CHECK_EQ(run(seq.tracks[0], ph4, 3), std::string("0_ 0_ 0_"));
}

// --- Clock division / multiplication -----------------------------------------

TEST(clock_divider_passes_every_nth_pulse) {
	ClockDivMul c;
	std::string out;
	for (int i = 0; i < 9; i++)
		out += c.edge(3, 1) ? '1' : '0';
	CHECK_EQ(out, std::string("100100100"));
}

TEST(clock_multiplier_fills_in_after_one_period) {
	// External clock every 40 samples, x4: the first period has only the edge pulse
	// (no period measured yet), then a pulse every 10 samples, in phase with the edge.
	ClockDivMul c;
	std::string out;
	for (int i = 0; i < 120; i++) {
		bool sub = c.tick(1.f);
		bool ext = i % 40 == 0 && c.edge(1, 4);
		out += ext ? 'E' : (sub ? 'x' : '.');
	}
	std::string expected;
	for (int i = 0; i < 120; i++) {
		if (i % 40 == 0)
			expected += 'E';
		else if (i >= 40 && i % 10 == 0)
			expected += 'x';
		else
			expected += '.';
	}
	CHECK_EQ(out, expected);
	CHECK(near(c.pulsePeriod(), 10.f));
}

TEST(clock_multiplier_resyncs_on_early_edge) {
	ClockDivMul c;
	c.edge(1, 4);
	for (int i = 0; i < 40; i++)
		c.tick(1.f);
	c.edge(1, 4); // period 40 -> pulses due at +10, +20, +30
	int subs = 0;
	for (int i = 0; i < 15; i++)
		subs += c.tick(1.f);
	CHECK_EQ(subs, 1);
	// The next edge comes early: whatever was left is dropped and it starts over.
	c.edge(1, 4);
	CHECK_EQ(c.pending, 3);
}

TEST(divided_and_multiplied_track_in_transport) {
	// Track 1: /2. Track 2: x2. Track 3: /2 x4. All steps 1 pulse long.
	Sequence seq;
	for (int t = 0; t < 3; t++) {
		for (int i = 0; i < 99; i++)
			seq.appendStep(t, makeStep(12, 1, 1));
	}
	seq.tracks[0].options.clockDiv = 2;
	seq.tracks[1].options.clockMul = 2;
	seq.tracks[2].options.clockDiv = 2;
	seq.tracks[2].options.clockMul = 4;
	Clocked c(seq, 1e-3f, 20);
	c.run(20 * 8); // 8 external pulses, settling the period measurements
	int base[3] = {c.pos(0), c.pos(1), c.pos(2)};
	c.run(20 * 8); // 8 more
	CHECK_EQ(c.pos(0) - base[0], 4);
	CHECK_EQ(c.pos(1) - base[1], 16);
	CHECK_EQ(c.pos(2) - base[2], 16);
}

TEST(reset_realigns_the_divider) {
	Sequence seq = build({{12, 1, 1}, {12, 1, 1}, {12, 1, 1}, {12, 1, 1}});
	seq.tracks[0].options.clockDiv = 2;
	Transport tr;
	tr.rewind(seq, false);
	tr.process(seq, 1e-3f, true, false, false);  // passes: step 0
	tr.process(seq, 1e-3f, false, false, false);
	for (int i = 0; i < 20; i++)
		tr.process(seq, 1e-3f, false, false, false);
	tr.process(seq, 1e-3f, true, false, false);  // swallowed by the divider
	CHECK_EQ(tr.playheads[0].step, 0);
	for (int i = 0; i < 20; i++)
		tr.process(seq, 1e-3f, false, false, false);
	tr.process(seq, 1e-3f, false, true, true);   // reset
	for (int i = 0; i < 20; i++)
		tr.process(seq, 1e-3f, false, false, false);
	tr.process(seq, 1e-3f, true, false, false);  // passes again right away
	CHECK_EQ(tr.playheads[0].step, 0);
	CHECK(!tr.playheads[0].armed());
}

// --- Trigger mode ------------------------------------------------------------

TEST(trigger_length_is_gate_times_0_1ms) {
	// 100kHz: 1 sample = 0.01ms. GATE 10 -> 1ms -> 100 samples high.
	Sequence seq = build({{12, 4, 10}});
	seq.tracks[0].options.triggerMode = true;
	Clocked c(seq, 1e-5f, 1000);
	int high = 0;
	for (int i = 0; i < 1000; i++) {
		c.step();
		high += c.gate();
	}
	CHECK(high >= 99 && high <= 101);
}

TEST(trigger_ratchet_spreads_gate_count_over_step) {
	// GATE 4 with ratchet: four 0.5ms triggers across the step once the period is known.
	Sequence seq = build({{12, 8, 4}});
	seq.tracks[0].steps[0].ratchet = true;
	seq.tracks[0].options.triggerMode = true;
	Clocked c(seq, 1e-5f, 1000); // 10ms per pulse, 80ms per step
	c.run(8000);                  // first pass measures the clock
	int rises = 0, high = 0;
	bool prev = c.gate();
	std::vector<long> riseAt;
	for (int i = 0; i < 8000; i++) {
		c.step();
		bool g = c.gate();
		if (g && !prev) {
			rises++;
			riseAt.push_back(c.sample);
		}
		high += g;
		prev = g;
	}
	CHECK_EQ(rises, 4);
	CHECK(high >= 4 * 49 && high <= 4 * 51);
	// Evenly spaced: every 2 pulses = 20ms = 2000 samples.
	if (riseAt.size() == 4)
		CHECK(std::labs(riseAt[3] - riseAt[2] - 2000) <= 2 && std::labs(riseAt[1] - riseAt[0] - 2000) <= 2);
}

TEST(pause_mutes_triggers_too) {
	Sequence seq = build({{12, 1, 99}});
	seq.tracks[0].options.triggerMode = true;
	Clocked c(seq, 1e-5f, 1000);
	c.step();
	CHECK(c.gate());
	c.tr.paused = true;
	CHECK(!c.gate());
}

// --- Smoothing ---------------------------------------------------------------

TEST(triangle_lfo_from_the_manual) {
	// Tips & Tricks: CV 0 -> 96 -> 0, 16 pulses each way, GATE 0, SMOOTH on.
	Sequence seq = build({{0, 16, 0}, {96, 16, 0}});
	seq.tracks[0].steps[0].smoothA = true;
	seq.tracks[0].steps[1].smoothA = true;
	Clocked c(seq, 1e-3f, 10); // 10 samples per pulse, 320 per cycle
	c.run(320);                // measure the clock
	c.run(1);
	CHECK(near(c.cvA(), 0.f, 0.05f));
	c.run(80);                 // quarter cycle: halfway up
	CHECK(near(c.cvA(), 4.f, 0.1f));
	c.run(80);                 // top
	CHECK(near(c.cvA(), 8.f, 0.1f));
	c.run(80);
	CHECK(near(c.cvA(), 4.f, 0.1f));
	// Continuous: no sample-to-sample jump bigger than one sample's worth of ramp.
	float prev = c.cvA(), worst = 0.f;
	for (int i = 0; i < 640; i++) {
		c.step();
		worst = std::max(worst, std::fabs(c.cvA() - prev));
		prev = c.cvA();
	}
	CHECK(worst < 8.f / 160 + 0.01f);
}

TEST(sawtooth_lfo_ramps_to_a_zero_length_step) {
	// Tips & Tricks: step 1 (CV 0, DURATION 0) is never played, but step 2 still ramps
	// down to it, then jumps back up: a falling sawtooth.
	Sequence seq = build({{0, 0, 0}, {96, 32, 0}});
	seq.tracks[0].steps[0].smoothA = true;
	seq.tracks[0].steps[1].smoothA = true;
	Clocked c(seq, 1e-3f, 10);
	c.run(320 + 1);
	CHECK(near(c.cvA(), 8.f, 0.05f));
	c.run(160);
	CHECK(near(c.cvA(), 4.f, 0.1f));
	c.run(158);
	CHECK(c.cvA() < 0.2f);
	c.run(2);
	CHECK(near(c.cvA(), 8.f, 0.05f));
}

TEST(smoothing_starts_when_the_gate_falls) {
	// Manual figure 6: DURATION 3, GATE 2 -> flat for 2 pulses, ramp during the 3rd.
	Sequence seq = build({{12, 3, 2}, {24, 3, 2}});
	seq.tracks[0].steps[0].smoothA = true;
	Clocked c(seq, 1e-3f, 10);
	c.run(60 + 1); // one full cycle to learn the period; now at step 0 pulse 0
	CHECK(near(c.cvA(), 1.f));
	c.run(15);
	CHECK(near(c.cvA(), 1.f));       // still inside the gate
	c.run(10);
	CHECK(near(c.cvA(), 1.5f, 0.1f)); // halfway through the ramp
	c.run(5);
	CHECK(near(c.cvA(), 2.f, 0.05f)); // step 1 reached, and it is not smoothed
	c.run(29);
	CHECK(near(c.cvA(), 2.f, 0.05f));
}

TEST(pattern_and_track_smoothing_flags_apply) {
	Sequence seq = build({{0, 2, 0}, {24, 2, 0}});
	Clocked c(seq, 1e-3f, 10);
	c.run(40 + 11); // halfway through step 0
	CHECK(near(c.cvA(), 0.f));
	c.seq.tracks[0].patterns[0].smoothA = true;
	c.tr.times[0].pattern = 0;
	CHECK(near(c.cvA(), 1.f, 0.1f));
	c.seq.tracks[0].patterns[0].smoothA = false;
	c.seq.tracks[0].smoothA = true;
	CHECK(near(c.cvA(), 1.f, 0.1f));
	// Smoothing is per output: B is unaffected.
	CHECK(near(c.tr.cvB(c.seq, 0), 1.f));
}

TEST(trigger_mode_smooths_over_the_second_half) {
	Sequence seq = build({{0, 4, 99}, {24, 4, 99}});
	seq.tracks[0].steps[0].smoothA = true;
	seq.tracks[0].options.triggerMode = true;
	Clocked c(seq, 1e-3f, 10);
	c.run(80 + 1);
	c.run(19);
	CHECK(near(c.cvA(), 0.f)); // first half: flat, despite GATE 99
	c.run(10);
	CHECK(near(c.cvA(), 1.f, 0.1f));
}

// --- Panel: SMOOTH button and track options ----------------------------------

TEST(smooth_button_follows_focus_and_table) {
	Sequence seq = build({{0, 1, 0}, {1, 1, 0}});
	Transport tr;
	Panel p;
	p.normalizeCursors(seq);
	p.press(seq, tr, FOCUS_STEP);
	p.press(seq, tr, BUTTON_SMOOTH);
	CHECK(seq.tracks[0].steps[0].smoothA);
	CHECK(!seq.tracks[0].steps[0].smoothB);
	CHECK(p.smoothLed(seq));
	p.table = TABLE_B;
	CHECK(!p.smoothLed(seq));
	p.press(seq, tr, FOCUS_PATTERN);
	p.press(seq, tr, BUTTON_SMOOTH);
	CHECK(seq.tracks[0].patterns[0].smoothB);
	p.press(seq, tr, FOCUS_TRACK);
	p.press(seq, tr, BUTTON_SMOOTH);
	CHECK(seq.tracks[0].smoothB);
	CHECK(!seq.tracks[0].smoothA);
	p.table = TABLE_REF;
	p.press(seq, tr, BUTTON_SMOOTH); // REF: nothing to smooth
	CHECK(seq.tracks[0].smoothB);
}

TEST(track_options_screen) {
	Sequence seq = build({{12, 1, 1}});
	Transport tr;
	Panel p;
	p.normalizeCursors(seq);
	p.press(seq, tr, FOCUS_TRACK); // already focused: a focus press opens the screen
	CHECK(p.view(seq).optionsScreen);
	p.press(seq, tr, FOCUS_DURATION);
	p.turnRight(seq, 2);
	CHECK_EQ((int) seq.tracks[0].options.clockDiv, 3);
	p.turnRight(seq, -10);
	CHECK_EQ((int) seq.tracks[0].options.clockDiv, 1);
	p.press(seq, tr, FOCUS_STEP);
	p.turnLeft(seq, 3);
	CHECK_EQ((int) seq.tracks[0].options.clockMul, 4);
	p.press(seq, tr, FOCUS_GATE);
	CHECK(seq.tracks[0].options.triggerMode);
	p.press(seq, tr, FOCUS_CV_A);
	CHECK(!seq.tracks[0].options.noteDisplayA);
	// Editing buttons do nothing here, and the step is untouched.
	p.press(seq, tr, BUTTON_INSERT);
	p.release(seq, tr, BUTTON_INSERT);
	CHECK_EQ(seq.tracks[0].numSteps(), 1);
	CHECK_EQ((int) seq.tracks[0].steps[0].gate, 1);
	p.press(seq, tr, FOCUS_TRACK); // focus TRACK
	p.press(seq, tr, FOCUS_TRACK); // focus press: close
	CHECK(!p.view(seq).optionsScreen);
}
