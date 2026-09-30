#pragma once
// The ER-102 transform (docs/SPEC-ER102.md §2): five operations per step parameter, all
// active at once, applied in this order:
//   destructive (MATH):       P' = Q( G * (Rd > 0 ? RANDOM(Rd) : P) + JITTER(Jt) + A, Qt )
//   non-destructive (groups): P' = Q( G * (P + RANDOM(Rd))          + JITTER(Jt) + A, Qt )
// A result outside the parameter's range leaves it unchanged.

#include "Math.hpp"
#include <cmath>

namespace iqs {

// The five operations, in the order the LEFT knob walks through them.
enum TransformOp { OP_ADD, OP_GEO, OP_JITTER, OP_RANDOM, OP_QUANTIZE, OP_LEN };

inline int applyTransform(const Transform& t, int v, bool destructive, Rng& rng) {
	int base = v;
	if (destructive) {
		if (t.random > 0)
			base = rng.range(0, t.random);
	}
	else if (t.random > 0) {
		base = v + rng.range(0, t.random);
	}
	int x = t.geo >= 0 ? base * t.geo : base / -t.geo;
	if (t.jitter > 0)
		x += rng.range(-t.jitter, t.jitter);
	x += t.add;
	if (t.quantize > 1)
		x = (int) std::lround((double) x / t.quantize) * t.quantize;
	return (x < 0 || x > MAX_VALUE) ? v : x;
}

// --- Editing ------------------------------------------------------------------

// G's values in knob order: 0, /99, /98 ... /2, 1, 2 ... 99.
inline int geoPosition(int g) {
	if (g == 0)
		return 0;
	if (g < 0)
		return 100 + g; // /99 -> 1, /2 -> 98
	return 98 + g;      // 1 -> 99, 99 -> 197
}
inline int geoFromPosition(int p) {
	if (p <= 0)
		return 0;
	if (p <= 98)
		return p - 100;
	return p - 98;
}

inline void adjustTransform(Transform& t, int op, int d) {
	auto clampTo = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
	switch (op) {
		case OP_ADD: t.add = (int8_t) clampTo(t.add + d, -MAX_VALUE, MAX_VALUE); break;
		case OP_GEO: t.geo = (int8_t) geoFromPosition(clampTo(geoPosition(t.geo) + d, 0, 197)); break;
		case OP_JITTER: t.jitter = (uint8_t) clampTo(t.jitter + d, 0, MAX_VALUE); break;
		case OP_RANDOM: t.random = (uint8_t) clampTo(t.random + d, 0, MAX_VALUE); break;
		case OP_QUANTIZE: t.quantize = (uint8_t) clampTo(t.quantize + d, 1, MAX_VALUE); break;
		default: break;
	}
}

// Display: the code for the left-hand display and the value for the right-hand one.
// Signs live in the code ("-A" subtracts, "-G" divides) so the value fits two digits.
inline const char* transformCode(const Transform& t, int op) {
	switch (op) {
		case OP_ADD: return t.geo == 0 ? (t.add < 0 ? "-S" : "S") : (t.add < 0 ? "-A" : "A");
		case OP_GEO: return t.geo < 0 ? "-G" : "G";
		case OP_JITTER: return "Jt";
		case OP_RANDOM: return "rd";
		case OP_QUANTIZE: return "Qt";
		default: return "";
	}
}
inline int transformValue(const Transform& t, int op) {
	switch (op) {
		case OP_ADD: return std::abs((int) t.add);
		case OP_GEO: return std::abs((int) t.geo);
		case OP_JITTER: return t.jitter;
		case OP_RANDOM: return t.random;
		case OP_QUANTIZE: return t.quantize;
		default: return 0;
	}
}

// Applies a destructive transform set (one per step parameter) to steps [first, last].
inline void applyTransforms(Track& t, const TransformSet& tf, int first, int last, Rng& rng) {
	for (int i = first; i <= last && i < t.numSteps(); i++) {
		Step& s = t.steps[i];
		s.cvA = (uint8_t) applyTransform(tf[MATH_CV_A], s.cvA, true, rng);
		s.cvB = (uint8_t) applyTransform(tf[MATH_CV_B], s.cvB, true, rng);
		s.duration = (uint8_t) applyTransform(tf[MATH_DURATION], s.duration, true, rng);
		s.gate = (uint8_t) applyTransform(tf[MATH_GATE], s.gate, true, rng);
	}
}

inline TransformSet invertedAll(const TransformSet& tf) {
	TransformSet out;
	for (int i = 0; i < MATH_PARAMS; i++)
		out[i] = tf[i].inverted();
	return out;
}

} // namespace iqs
