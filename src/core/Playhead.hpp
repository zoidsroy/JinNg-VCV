#pragma once
// Per-track play cursor (docs/SPEC.md §3). It only counts clock pulses; everything that
// needs real time (clock div/mul, smoothing, trigger lengths) lives in Transport.
//
// A step lasts `duration` pulses and its gate is high for the first `gate` of them.
// `pulse` is the pulse currently sounding within the step, or -1 when the cursor is
// armed: parked on a step but waiting for the clock that starts it.

#include "Sequence.hpp"

namespace iqs {

struct Playhead {
	int step = 0;
	int pulse = -1;

	bool armed() const {
		return pulse < 0;
	}

	// The step after i, following loop points: LOOP END jumps back to LOOP START (or the
	// first step), as does running off the end of the track.
	static int nextIndex(const Track& t, int i) {
		int wrap = t.loopStart >= 0 ? t.loopStart : 0;
		if (i == t.loopEnd || i + 1 >= t.numSteps())
			return wrap;
		return i + 1;
	}

	// The next step after i with a non-zero duration, or -1 if nothing reachable has one.
	static int nextPlayable(const Track& t, int i) {
		int j = i;
		for (int k = 0; k < t.numSteps(); k++) {
			j = nextIndex(t, j);
			if (t.steps[j].duration > 0)
				return j;
		}
		return -1;
	}

	static int firstPlayable(const Track& t) {
		if (t.numSteps() == 0)
			return -1;
		if (t.steps[0].duration > 0)
			return 0;
		return nextPlayable(t, 0);
	}

	// Rewinds to the first step, ignoring loop points, or to `target` if it is a valid
	// step (a part's RESET TO step; a zero-length target moves on to the next playable
	// step). With `immediate` that step starts sounding now; otherwise on the next clock.
	void reset(const Track& t, bool immediate, int target = -1) {
		int first;
		if (target >= 0 && target < t.numSteps())
			first = t.steps[target].duration > 0 ? target : nextPlayable(t, target);
		else
			first = firstPlayable(t);
		step = first >= 0 ? first : 0;
		pulse = (immediate && first >= 0) ? 0 : -1;
	}

	// Keeps the cursor valid after the track was edited underneath it.
	void validate(const Track& t) {
		if (step >= t.numSteps()) {
			step = 0;
			pulse = -1;
		}
	}

	// Advances by one clock pulse.
	void clock(const Track& t) {
		validate(t);
		if (t.numSteps() == 0)
			return;

		if (armed()) {
			// The armed step may have been edited to zero length since.
			if (t.steps[step].duration == 0) {
				int next = nextPlayable(t, step);
				if (next < 0)
					return;
				step = next;
			}
			pulse = 0;
			return;
		}

		pulse++;
		if (pulse < t.steps[step].duration)
			return;

		int next = nextPlayable(t, step);
		if (next < 0) {
			// Every reachable step has zero duration: stall rather than spin.
			pulse = -1;
			return;
		}
		step = next;
		pulse = 0;
	}

	const Step* current(const Track& t) const {
		if (step < 0 || step >= t.numSteps())
			return nullptr;
		return &t.steps[step];
	}

	// The gate in gate mode (trigger mode is timed by the Transport). With ratchet on,
	// the gate repeats high `gate` pulses, low `gate` pulses until the step ends.
	bool gate(const Track& t) const {
		const Step* s = current(t);
		if (!s || armed())
			return false;
		if (s->ratchet && s->gate > 0 && s->gate < s->duration)
			return pulse % (2 * s->gate) < s->gate;
		return pulse < std::min(s->gate, s->duration);
	}

	float cvA(const Track& t) const {
		const Step* s = current(t);
		return s ? t.tableA[s->cvA] : 0.f;
	}

	float cvB(const Track& t) const {
		const Step* s = current(t);
		return s ? t.tableB[s->cvB] : 0.f;
	}
};

} // namespace iqs
