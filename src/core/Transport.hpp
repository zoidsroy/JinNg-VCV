#pragma once
// CLOCK / RESET / PAUSE handling for all four tracks (docs/SPEC.md §3.1, §3.7).
// Takes already-detected edges, so it stays independent of Rack's trigger classes.

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

	int resetMode = RESET_ARMS_FIRST_STEP;
	bool paused = false;
	Playhead playheads[NUM_TRACKS];

	float sinceClock = INFINITY;
	float sinceReset = INFINITY;
	// Whether the last reset started the first step itself (rather than arming it).
	bool resetStartedStep = false;

	void rewind(const Sequence& seq, bool immediate) {
		for (int t = 0; t < NUM_TRACKS; t++)
			playheads[t].reset(seq.tracks[t], immediate);
	}

	// One sample. `resetHeld` is the RESET level (input or button); while it stays high
	// the sequencer is parked and ignores the clock.
	void process(const Sequence& seq, float dt, bool clockEdge, bool resetEdge, bool resetHeld) {
		if (resetEdge) {
			// A reset trailing a clock belongs to that clock, so the first step has to
			// start now or it would sound a whole pulse late.
			bool afterClock = sinceClock < SIMULTANEOUS_S && !paused;
			resetStartedStep = resetMode == RESET_STARTS_FIRST_STEP || afterClock;
			rewind(seq, resetStartedStep);
			sinceReset = 0.f;
		}

		if (clockEdge) {
			sinceClock = 0.f;
			// A clock arriving with a reset is the reset's own beat: it starts the armed
			// first step, or is swallowed when the reset already started it. Only later
			// clocks, while RESET is still held, are ignored.
			bool withReset = sinceReset < SIMULTANEOUS_S;
			bool swallowed = withReset && resetStartedStep;
			bool held = resetHeld && !withReset;
			if (!paused && !held && !swallowed) {
				for (int t = 0; t < NUM_TRACKS; t++)
					playheads[t].clock(seq.tracks[t]);
			}
		}
		else {
			sinceClock += dt;
		}
		sinceReset += dt;
	}

	bool gate(const Sequence& seq, int track) const {
		return !paused && playheads[track].gate(seq.tracks[track]);
	}
};

} // namespace iqs
