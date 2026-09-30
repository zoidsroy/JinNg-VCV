#pragma once
// Per-track clock divider and multiplier (docs/SPEC.md §3.6).
//
// Division passes the 1st, (1+div)th, (1+2*div)th ... incoming pulse. Multiplication
// follows the manual: on each (divided) pulse, emit a pulse immediately, then mul-1 more
// spaced by the last measured period / mul. A new pulse aborts whatever is left and
// starts over, so the output stays phase-locked to the input and tracks its tempo one
// period late. Division is applied before multiplication (open question 5).

#include <cmath>

namespace iqs {

struct ClockDivMul {
	int count = 0;             // incoming pulses since the last one that passed
	float sincePassed = INFINITY;
	float period = 0.f;        // between passed pulses; 0 until two have been seen
	int pending = 0;           // multiplied pulses still to emit
	float untilNext = 0.f;
	float interval = 0.f;
	int mul = 1;

	// An incoming clock pulse. Returns whether the track pulses now.
	bool edge(int div, int mulNow) {
		div = div < 1 ? 1 : div;
		mul = mulNow < 1 ? 1 : mulNow;
		bool pass = count == 0;
		count = (count + 1) % div;
		if (!pass)
			return false;
		if (std::isfinite(sincePassed))
			period = sincePassed;
		sincePassed = 0.f;
		pending = 0;
		if (mul > 1 && period > 0.f) {
			interval = period / mul;
			pending = mul - 1;
			untilNext = interval;
		}
		return true;
	}

	// Advances time. Returns whether a multiplied pulse is due in this sample.
	bool tick(float dt) {
		sincePassed += dt;
		if (pending <= 0)
			return false;
		untilNext -= dt;
		if (untilNext > 0.f)
			return false;
		pending--;
		untilNext += interval;
		return true;
	}

	// Drops any multiplied pulses still scheduled (e.g. while paused).
	void cancel() {
		pending = 0;
	}

	// Realigns the divider so the next incoming pulse passes. If the reset trails a pulse
	// that already passed, that pulse counts as the first one and its multiplied pulses
	// carry on.
	void reset(bool pulseJustPassed, int div) {
		if (pulseJustPassed) {
			count = div > 1 ? 1 : 0;
		}
		else {
			count = 0;
			pending = 0;
		}
	}

	// The track's pulse period, or 0 while it is not known yet.
	float pulsePeriod() const {
		return period > 0.f ? period / mul : 0.f;
	}
};

} // namespace iqs
