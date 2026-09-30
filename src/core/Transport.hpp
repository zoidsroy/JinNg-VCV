#pragma once
// CLOCK / RESET / PAUSE handling and everything time-based for the four tracks
// (docs/SPEC.md §3): per-track clock division and multiplication, trigger-mode gates,
// and smoothed CV. Takes already-detected edges, so it stays independent of Rack.

#include "ClockDivMul.hpp"
#include "Playhead.hpp"
#include <cmath>

namespace iqs {

// What a RESET does to the step it rewinds to (open question 1 in the spec).
enum ResetMode {
	// Park on the first step; the next clock starts it.
	RESET_ARMS_FIRST_STEP,
	// The first step starts sounding at the reset; the next clock is its 2nd pulse.
	RESET_STARTS_FIRST_STEP,
};

struct Transport {
	// Clock and reset edges closer together than this are one event: patches usually
	// derive both from one source, and each cable hop in Rack adds a sample of delay.
	static constexpr float SIMULTANEOUS_S = 1e-3f;
	// Trigger mode: a GATE value of n gives an n x 0.1ms trigger; ratchet triggers are
	// always 0.5ms.
	static constexpr float TRIGGER_UNIT_S = 1e-4f;
	static constexpr float RATCHET_TRIGGER_S = 5e-4f;
	// How often the cached pattern index is refreshed, to pick up edits.
	static constexpr int PATTERN_REFRESH_SAMPLES = 256;

	// Timing state of one track beyond its pulse count.
	struct TrackTime {
		ClockDivMul clock;
		float sincePulse = 0.f;     // since the track's last pulse
		float sinceStepStart = 0.f; // since the current step started
		int triggersFired = 0;      // trigger mode: triggers fired in this step
		float triggerLeft = 0.f;    // trigger mode: time the current trigger stays high
		int pattern = -1;           // pattern of the playing step (for its smooth flags)
		int lastStep = -1;          // the previous step started, to spot a wrap-around
	};

	// Boundaries a track crossed during the last process() call. A step starting is always
	// EVENT_STEP; it is also EVENT_PATTERN when it is in another pattern, and EVENT_TRACK
	// (and EVENT_PATTERN) when playback wrapped around, i.e. the track ended.
	enum Event : uint8_t { EVENT_STEP = 1, EVENT_PATTERN = 2, EVENT_TRACK = 4 };

	int resetMode = RESET_ARMS_FIRST_STEP;
	bool paused = false;
	// The STOP part is playing: nothing advances and every gate is closed.
	bool stopped = false;
	// Where RESET sends each track (the playing part's RESET TO step), -1 for the first step.
	int resetTargets[NUM_TRACKS] = {-1, -1, -1, -1};
	Playhead playheads[NUM_TRACKS];
	TrackTime times[NUM_TRACKS];
	uint8_t events[NUM_TRACKS] = {};

	float sinceClock = INFINITY;
	float sinceReset = INFINITY;
	// Whether the last reset started the first step itself (rather than arming it).
	bool resetStartedStep = false;
	int refreshCountdown = 0;

	void rewind(const Sequence& seq, bool immediate) {
		for (int t = 0; t < NUM_TRACKS; t++)
			rewindTrack(seq, t, immediate, resetTargets[t]);
	}

	void rewindTrack(const Sequence& seq, int t, bool immediate, int target) {
		playheads[t].reset(seq.tracks[t], immediate, target);
		if (!playheads[t].armed())
			startStep(seq, t);
	}

	// One sample. `resetHeld` is the RESET level (input or button); while it stays high
	// the sequencer is parked and ignores the clock.
	void process(const Sequence& seq, float dt, bool clockEdge, bool resetEdge, bool resetHeld) {
		for (uint8_t& e : events)
			e = 0;
		if (resetEdge) {
			// A reset trailing a clock belongs to that clock, so the first step has to
			// start now or it would sound a whole pulse late.
			bool afterClock = sinceClock < SIMULTANEOUS_S && !paused;
			resetStartedStep = resetMode == RESET_STARTS_FIRST_STEP || afterClock;
			for (int t = 0; t < NUM_TRACKS; t++)
				times[t].clock.reset(afterClock, seq.tracks[t].options.clockDiv);
			rewind(seq, resetStartedStep);
			sinceReset = 0.f;
		}

		// A clock arriving with a reset is the reset's own beat: it starts the armed
		// first step, or is swallowed when the reset already started it. Only later
		// clocks, while RESET is still held, are ignored.
		bool withReset = sinceReset < SIMULTANEOUS_S;
		bool held = resetHeld && !withReset;
		bool accepted = clockEdge && !paused && !held && !stopped;
		bool swallowed = accepted && withReset && resetStartedStep;
		if (clockEdge)
			sinceClock = 0.f;
		else
			sinceClock += dt;
		sinceReset += dt;

		bool refresh = --refreshCountdown <= 0;
		if (refresh)
			refreshCountdown = PATTERN_REFRESH_SAMPLES;

		for (int t = 0; t < NUM_TRACKS; t++) {
			const Track& track = seq.tracks[t];
			TrackTime& tt = times[t];
			Playhead& ph = playheads[t];

			bool multiplied = tt.clock.tick(dt);
			if (paused || held || stopped) {
				tt.clock.cancel();
				multiplied = false;
			}
			bool external = accepted && tt.clock.edge(track.options.clockDiv, track.options.clockMul);
			// An external pulse replaces a multiplied one due in the same sample.
			bool pulse = external ? !swallowed : multiplied;

			if (pulse) {
				ph.clock(track);
				if (ph.pulse == 0)
					startStep(seq, t);
			}
			if (pulse || external)
				tt.sincePulse = 0.f;
			else
				tt.sincePulse += dt;
			tt.sinceStepStart += dt;
			if (refresh)
				tt.pattern = patternOf(track, ph);

			updateTriggers(track, t, dt);
		}
	}

	bool gate(const Sequence& seq, int t) const {
		if (paused || stopped)
			return false;
		const Track& track = seq.tracks[t];
		if (track.options.triggerMode)
			return times[t].triggerLeft > 0.f;
		return playheads[t].gate(track);
	}

	float cvA(const Sequence& seq, int t) const {
		return cv(seq, t, false);
	}

	float cvB(const Sequence& seq, int t) const {
		return cv(seq, t, true);
	}

	// The track's pulse period in seconds, 0 while unknown.
	float pulsePeriod(int t) const {
		return times[t].clock.pulsePeriod();
	}

private:
	static int patternOf(const Track& track, const Playhead& ph) {
		int p, s;
		track.locate(ph.step, p, s);
		return p;
	}

	void startStep(const Sequence& seq, int t) {
		const Track& track = seq.tracks[t];
		TrackTime& tt = times[t];
		tt.sinceStepStart = 0.f;
		tt.triggersFired = 0;
		int step = playheads[t].step;
		int pattern = patternOf(track, playheads[t]);
		uint8_t e = EVENT_STEP;
		if (step <= tt.lastStep)
			e |= EVENT_TRACK | EVENT_PATTERN;
		else if (pattern != tt.pattern)
			e |= EVENT_PATTERN;
		events[t] |= e;
		tt.lastStep = step;
		tt.pattern = pattern;
		const Step* s = playheads[t].current(track);
		if (track.options.triggerMode && s && !s->ratchet && s->gate > 0) {
			tt.triggerLeft = s->gate * TRIGGER_UNIT_S;
			tt.triggersFired = 1;
		}
	}

	// Trigger mode with ratchet: GATE = n spreads n 0.5ms triggers evenly over the step
	// (spec §3.3; the spacing is an assumption). Until the clock period is known only the
	// first one can be placed.
	void updateTriggers(const Track& track, int t, float dt) {
		TrackTime& tt = times[t];
		tt.triggerLeft -= dt;
		const Playhead& ph = playheads[t];
		const Step* s = ph.current(track);
		if (!track.options.triggerMode || !s || ph.armed() || !s->ratchet)
			return;
		int n = s->gate;
		if (tt.triggersFired >= n)
			return;
		float period = pulsePeriod(t);
		if (tt.triggersFired > 0 && period <= 0.f)
			return;
		float due = tt.triggersFired * s->duration * period / n;
		if (tt.sinceStepStart >= due) {
			tt.triggerLeft = RATCHET_TRIGGER_S;
			tt.triggersFired++;
		}
	}

	// Smoothing (spec §3.5): once the gate falls, the CV ramps linearly to the next step's
	// value, arriving as that step starts. In trigger mode the ramp takes the second half
	// of the step. The "next step" is the literal next one (following loop points) even
	// if its duration is 0: that is how the manual's sawtooth LFO gets its reset level.
	// Progress is counted in pulses plus the fraction of the current pulse elapsed, so
	// the ramp follows tempo changes.
	float cv(const Sequence& seq, int t, bool b) const {
		const Track& track = seq.tracks[t];
		const Playhead& ph = playheads[t];
		const Step* s = ph.current(track);
		if (!s)
			return 0.f;
		const VoltageTable& table = b ? track.tableB : track.tableA;
		float from = table[b ? s->cvB : s->cvA];
		if (ph.armed() || !smoothed(track, t, *s, b))
			return from;

		const Step& next = track.steps[Playhead::nextIndex(track, ph.step)];
		float to = table[b ? next.cvB : next.cvA];
		float start = track.options.triggerMode ? s->duration * 0.5f : (float) std::min(s->gate, s->duration);
		float length = s->duration - start;
		if (length <= 0.f)
			return from;
		float period = pulsePeriod(t);
		float phase = period > 0.f ? std::min(times[t].sincePulse / period, 1.f) : 0.f;
		float progress = (ph.pulse + phase - start) / length;
		progress = std::max(0.f, std::min(progress, 1.f));
		return from + (to - from) * progress;
	}

	bool smoothed(const Track& track, int t, const Step& s, bool b) const {
		if (b ? (s.smoothB || track.smoothB) : (s.smoothA || track.smoothA))
			return true;
		int p = times[t].pattern;
		if (p < 0 || p >= (int) track.patterns.size())
			return false;
		return b ? track.patterns[p].smoothB : track.patterns[p].smoothA;
	}
};

} // namespace iqs
