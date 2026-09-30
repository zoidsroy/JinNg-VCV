#pragma once
// Groups (docs/SPEC-ER102.md §4): step selections, Euclidean masks, and what the X/Y/Z
// modulation bus does to member steps while they play.

#include "Transform.hpp"
#include <cmath>
#include <cstdio>
#include <string>

namespace iqs {

static constexpr int MAX_EUCLID = 99;

// The Euclidean rhythm E(k, n), as Bjorklund's pairing procedure (in Toussaint's form)
// lays it out: start with k [1] and n-k [0] sequences and keep appending the remainder
// sequences to the leading ones until at most one remainder is left. Matches the
// manual's examples up to rotation (which does not change the pattern; see the spec).
// Writes n bits to out; uses fixed buffers only.
inline void euclid(int k, int n, uint8_t* out) {
	n = std::max(1, std::min(n, MAX_EUCLID));
	k = std::max(0, std::min(k, n));
	if (k == 0 || k == n) {
		for (int i = 0; i < n; i++)
			out[i] = k == n;
		return;
	}
	// seq[i] holds the bits of sequence i; the first `a` sequences are the leading ones,
	// the next `b` the remainders.
	uint8_t seq[MAX_EUCLID][MAX_EUCLID];
	uint8_t next[MAX_EUCLID][MAX_EUCLID];
	int len[MAX_EUCLID], nextLen[MAX_EUCLID];
	int a = k, b = n - k;
	for (int i = 0; i < n; i++) {
		seq[i][0] = i < k;
		len[i] = 1;
	}
	while (b > 1) {
		int m = std::min(a, b);
		int count = 0;
		for (int i = 0; i < m; i++) {
			// leading i + remainder i
			std::copy(seq[i], seq[i] + len[i], next[count]);
			std::copy(seq[a + i], seq[a + i] + len[a + i], next[count] + len[i]);
			nextLen[count] = len[i] + len[a + i];
			count++;
		}
		// The leftovers become the new remainders.
		int restFrom = a > b ? m : a + m;
		int restCount = a > b ? a - m : b - m;
		for (int i = 0; i < restCount; i++) {
			std::copy(seq[restFrom + i], seq[restFrom + i] + len[restFrom + i], next[count + i]);
			nextLen[count + i] = len[restFrom + i];
		}
		a = count;
		b = restCount;
		for (int i = 0; i < a + b; i++) {
			std::copy(next[i], next[i] + nextLen[i], seq[i]);
			len[i] = nextLen[i];
		}
	}
	int pos = 0;
	for (int i = 0; i < a + b; i++) {
		for (int j = 0; j < len[i]; j++)
			out[pos++] = seq[i][j];
	}
}

// --- Membership -------------------------------------------------------------------

inline bool inGroup(const Step& s, int g) {
	return (s.groups >> g) & 1u;
}
inline void setInGroup(Step& s, int g, bool on) {
	if (on)
		s.groups = (uint16_t) (s.groups | (1u << g));
	else
		s.groups = (uint16_t) (s.groups & ~(1u << g));
}

inline int countMembers(const Track& t, int g) {
	int n = 0;
	for (const Step& s : t.steps)
		n += inGroup(s, g);
	return n;
}

// Selects steps [first, last] into group g by the mask E(k, n) repeated as needed:
// 1s join the group, 0s leave it.
inline void applyEuclid(Track& t, int first, int last, int g, int k, int n) {
	uint8_t mask[MAX_EUCLID];
	euclid(k, n, mask);
	n = std::max(1, std::min(n, MAX_EUCLID));
	for (int i = first; i <= last && i < t.numSteps(); i++)
		setInGroup(t.steps[i], g, mask[(i - first) % n]);
}

inline void invertMembership(Track& t, int g) {
	for (Step& s : t.steps)
		setInGroup(s, g, !inGroup(s, g));
}

// Moves the selection one step later (forward) or earlier, wrapping around the track.
inline void rotateMembership(Track& t, int g, bool forward) {
	int n = t.numSteps();
	if (n < 2)
		return;
	if (forward) {
		bool carry = inGroup(t.steps[n - 1], g);
		for (int i = n - 1; i > 0; i--)
			setInGroup(t.steps[i], g, inGroup(t.steps[i - 1], g));
		setInGroup(t.steps[0], g, carry);
	}
	else {
		bool carry = inGroup(t.steps[0], g);
		for (int i = 0; i < n - 1; i++)
			setInGroup(t.steps[i], g, inGroup(t.steps[i + 1], g));
		setInGroup(t.steps[n - 1], g, carry);
	}
}

// --- The modulation bus -------------------------------------------------------------

struct ModBus {
	float cv[NUM_MOD_CHANNELS] = {};
	bool gate[NUM_MOD_CHANNELS] = {};
};

inline uint8_t clampParam(float v) {
	return (uint8_t) std::max(0, std::min((int) std::lround(v), MAX_VALUE));
}

// How a step plays with the bus applied: for each of its groups (in order) and each
// channel, the channel's HIGH or LOW transform (by its gate), then the slopes on
// DURATION and GATE. CV-A/CV-B slopes act on the output voltage instead (cvSlope).
// Random and jitter are drawn afresh every time the step plays.
inline Step modulate(const Sequence& seq, const Step& s, const ModBus& bus, Rng& rng) {
	Step e = s;
	for (int g = 0; g < NUM_GROUPS; g++) {
		if (!inGroup(s, g))
			continue;
		const Group& grp = seq.groups[g];
		float dDuration = 0.f, dGate = 0.f;
		for (int c = 0; c < NUM_MOD_CHANNELS; c++) {
			const TransformSet& tf = bus.gate[c] ? grp.high[c] : grp.low[c];
			e.cvA = (uint8_t) applyTransform(tf[MATH_CV_A], e.cvA, false, rng);
			e.cvB = (uint8_t) applyTransform(tf[MATH_CV_B], e.cvB, false, rng);
			e.duration = (uint8_t) applyTransform(tf[MATH_DURATION], e.duration, false, rng);
			e.gate = (uint8_t) applyTransform(tf[MATH_GATE], e.gate, false, rng);
			dDuration += grp.slope[c][MATH_DURATION] * bus.cv[c];
			dGate += grp.slope[c][MATH_GATE] * bus.cv[c];
		}
		e.duration = clampParam(e.duration + dDuration);
		e.gate = clampParam(e.gate + dGate);
	}
	return e;
}

// Volts added to CV-A (param MATH_CV_A) or CV-B by the slopes of a step's groups.
inline float cvSlope(const Sequence& seq, uint16_t groups, int param, const ModBus& bus) {
	float v = 0.f;
	for (int g = 0; g < NUM_GROUPS; g++) {
		if (!((groups >> g) & 1u))
			continue;
		for (int c = 0; c < NUM_MOD_CHANNELS; c++)
			v += seq.groups[g].slope[c][param] * bus.cv[c];
	}
	return v;
}

// --- Slope editing ------------------------------------------------------------------
// The RIGHT knob steps a slope by 0.01 below 1, by 0.1 up to 9.9 and by 1 from 10 to 99
// (in both directions). Positions 0..558 cover -99 .. 99 on that grid.

static constexpr int SLOPE_POSITIONS = 559;

inline float slopeAt(int p) {
	p = std::max(0, std::min(p, SLOPE_POSITIONS - 1));
	if (p < 90)
		return (float) (-99 + p);
	if (p < 180)
		return -9.9f + (p - 90) * 0.1f;
	if (p < 379)
		return -0.99f + (p - 180) * 0.01f;
	if (p < 469)
		return 1.0f + (p - 379) * 0.1f;
	return (float) (10 + (p - 469));
}

inline int slopePosition(float k) {
	// Nearest grid position (the grid is monotonic, so a binary search would do; the
	// table is small and this runs only on knob turns).
	int best = 0;
	for (int p = 1; p < SLOPE_POSITIONS; p++) {
		if (std::fabs(slopeAt(p) - k) < std::fabs(slopeAt(best) - k))
			best = p;
	}
	return best;
}

inline float adjustSlope(float k, int d) {
	return slopeAt(slopePosition(k) + d);
}

// Full form for the 4-digit VOLTAGE display: "-0.05", "1.5", "-40".
inline std::string formatSlope(float k) {
	char buf[16];
	if (std::fabs(k) >= 9.95f)
		std::snprintf(buf, sizeof(buf), "%d", (int) std::lround(k));
	else if (std::fabs(k) >= 0.995f)
		std::snprintf(buf, sizeof(buf), "%.1f", k);
	else
		std::snprintf(buf, sizeof(buf), "%.2f", k);
	return buf;
}

// Short form for a 2-digit display: whole numbers from -9 to 99, "0." for a non-zero
// slope under 1, "-0." likewise negative, and "--" below -9 (VOLTAGE shows the exact
// value of the focused one).
inline std::string formatSlopeShort(float k) {
	if (k == 0.f)
		return "0";
	int n = (int) std::lround(k);
	if (n == 0)
		return k > 0 ? "0." : "-0.";
	if (n < -9)
		return "--";
	return std::to_string(n);
}

} // namespace iqs
