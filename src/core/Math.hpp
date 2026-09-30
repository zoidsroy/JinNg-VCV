#pragma once
// MATH transforms (manual, Math Operations; spec §4.6). Each step parameter has its own
// operation; applying the transform runs all four over the focused step, pattern or
// track. A result outside 0..99 leaves that parameter unchanged.

#include "Sequence.hpp"
#include <cstdint>

namespace iqs {

// Small deterministic PRNG for the random and jitter operations.
struct Rng {
	uint32_t state = 0x9e3779b9u;

	uint32_t next() {
		uint32_t x = state;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		return state = x;
	}
	// Uniform integer in [lo, hi].
	int range(int lo, int hi) {
		return lo + (int) (next() % (uint32_t) (hi - lo + 1));
	}
};

inline int mathOperandMin(int type) {
	return (type == MATH_ADD || type == MATH_GEO) ? -MAX_VALUE : 0;
}

inline int applyMathOp(const MathOp& op, int v, Rng& rng) {
	int n = op.operand;
	int out = v;
	switch (op.type) {
		case MATH_ADD: out = v + n; break;
		// Geometric: multiply by n, or divide by -n for a negative operand (0 and +-1 do
		// nothing).
		case MATH_GEO: out = n > 0 ? v * n : (n < 0 ? v / -n : v); break;
		case MATH_SET: out = n; break;
		case MATH_RANDOM: out = rng.range(0, n); break;
		case MATH_JITTER: out = v + rng.range(-n, n); break;
		default: break;
	}
	return (out < 0 || out > MAX_VALUE) ? v : out;
}

// Applies a track's transform to its steps [first, last].
inline void applyMath(Track& t, int first, int last, Rng& rng) {
	for (int i = first; i <= last && i < t.numSteps(); i++) {
		Step& s = t.steps[i];
		s.cvA = (uint8_t) applyMathOp(t.math[MATH_CV_A], s.cvA, rng);
		s.cvB = (uint8_t) applyMathOp(t.math[MATH_CV_B], s.cvB, rng);
		s.duration = (uint8_t) applyMathOp(t.math[MATH_DURATION], s.duration, rng);
		s.gate = (uint8_t) applyMathOp(t.math[MATH_GATE], s.gate, rng);
	}
}

// Cycles an operation's type (A -> G -> S -> rd -> Jt -> A ...), keeping its operand in
// the new type's range.
inline void cycleMathType(MathOp& op, int d) {
	op.type = (uint8_t) (((op.type + d) % MATH_TYPES + MATH_TYPES) % MATH_TYPES);
	int lo = mathOperandMin(op.type);
	if (op.operand < lo)
		op.operand = (int8_t) lo;
}

inline void adjustMathOperand(MathOp& op, int d) {
	int v = op.operand + d;
	op.operand = (int8_t) std::max(mathOperandMin(op.type), std::min(v, MAX_VALUE));
}

// The type code for the left-hand display. The sign of add/geometric operands shows
// here ("-A" subtract, "-G" divide) so the 2-digit operand display fits 0..99.
inline const char* mathCode(const MathOp& op) {
	switch (op.type) {
		case MATH_ADD: return op.operand < 0 ? "-A" : "A";
		case MATH_GEO: return op.operand < 0 ? "-G" : "G";
		case MATH_SET: return "S";
		case MATH_RANDOM: return "rd";
		case MATH_JITTER: return "Jt";
		default: return "";
	}
}

} // namespace iqs
