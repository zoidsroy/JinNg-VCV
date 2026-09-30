#pragma once
// Sequence data model (docs/SPEC.md §2). Plain C++11, no Rack dependency, so it can be
// unit-tested on its own.
//
// A track's steps are stored as one flat list; patterns are a list of lengths (plus their
// smooth flags) that partition it. Playback only ever needs the flat index, and loop
// points are flat indices too. Every vector reserves its hardware maximum up front, so
// edits made on the audio thread never reallocate.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace iqs {

static constexpr int NUM_TRACKS = 4;
static constexpr int MAX_TOTAL_STEPS = 2000;
static constexpr int MAX_PATTERNS = 100;
static constexpr int MAX_STEPS_PER_PATTERN = 100;
static constexpr int TABLE_SIZE = 100;
static constexpr int MAX_VALUE = 99;
static constexpr float MAX_VOLTAGE = 8.192f;

struct Step {
	uint8_t cvA = 12;
	uint8_t cvB = 12;
	uint8_t duration = 0;
	uint8_t gate = 0;
	bool smoothA = false;
	bool smoothB = false;
	bool ratchet = false;
};

struct Pattern {
	uint8_t length = 0;
	bool smoothA = false;
	bool smoothB = false;
};

// Per-track settings from the track options screen (spec §4.8).
struct TrackOptions {
	bool noteDisplayA = true; // CV-A shown as a note (Nt) rather than a number (Nr)
	bool noteDisplayB = true;
	uint8_t clockDiv = 1;     // 1..99
	uint8_t clockMul = 1;     // 1..99
	bool triggerMode = false; // GATE outputs triggers (tr) rather than gates (Gt)
};

// A MATH operation for one step parameter (manual, Math Operations). The type codes on
// the display are A (add), G (geometric: multiply, or divide for a negative operand),
// S (set), rd (random 0..N) and Jt (jitter by -N..N).
enum MathType { MATH_ADD, MATH_GEO, MATH_SET, MATH_RANDOM, MATH_JITTER, MATH_TYPES };

struct MathOp {
	uint8_t type = MATH_ADD;
	int8_t operand = 0; // -99..99 for add/geometric, 0..99 otherwise
};

// The parameters a MathOp applies to, in display order.
enum MathParam { MATH_CV_A, MATH_CV_B, MATH_DURATION, MATH_GATE, MATH_PARAMS };

// The ER-102's five-operation transform for one step parameter (see Transform.hpp for
// how it is applied and edited).
struct Transform {
	int8_t add = 0;       // A (or S, the value to set, when geo == 0): -99..99
	int8_t geo = 1;       // G: n > 0 multiplies by n, n < 0 divides by -n, 0 multiplies by zero
	uint8_t jitter = 0;   // Jt: 0..99
	uint8_t random = 0;   // Rd: 0..99
	uint8_t quantize = 1; // Qt: 1..99

	bool operator==(const Transform& o) const {
		return add == o.add && geo == o.geo && jitter == o.jitter && random == o.random && quantize == o.quantize;
	}
	bool operator!=(const Transform& o) const {
		return !(*this == o);
	}

	// INVERT: subtract instead of add, divide instead of multiply. Only A and G change.
	Transform inverted() const {
		Transform t = *this;
		t.add = (int8_t) -add;
		if (geo > 1 || geo < -1)
			t.geo = (int8_t) -geo;
		return t;
	}
};
typedef std::array<Transform, MATH_PARAMS> TransformSet;

struct VoltageTable {
	std::array<float, TABLE_SIZE> volts;

	VoltageTable() {
		set12ET();
	}

	void set12ET() {
		for (int i = 0; i < TABLE_SIZE; i++)
			volts[i] = std::min(i / 12.f, MAX_VOLTAGE);
	}

	float operator[](int i) const {
		return volts[std::max(0, std::min(i, TABLE_SIZE - 1))];
	}

	// Index whose voltage is closest to v; new steps default to the one nearest 1.0V.
	int closestIndex(float v) const {
		int best = 0;
		for (int i = 1; i < TABLE_SIZE; i++) {
			if (std::fabs(volts[i] - v) < std::fabs(volts[best] - v))
				best = i;
		}
		return best;
	}
};

struct Track {
	std::vector<Step> steps;
	std::vector<Pattern> patterns;
	int loopStart = -1; // flat step index, or -1 for none
	int loopEnd = -1;
	bool smoothA = false; // track-wide smoothing
	bool smoothB = false;
	TrackOptions options;
	VoltageTable tableA;
	VoltageTable tableB;
	// Last used voltage editing granularity (fine/coarse/super coarse), kept per track as
	// the hardware keeps it in snapshots.
	uint8_t voltageGrain = 0;
	// The track's prepared MATH transform, one operation per step parameter.
	std::array<MathOp, MATH_PARAMS> math;
	// Its five-operation counterpart, which MATH uses while the expander is connected.
	TransformSet transform;

	Track() {
		steps.reserve(MAX_TOTAL_STEPS);
		patterns.reserve(MAX_PATTERNS);
	}

	// Copy assignment must keep the reserved capacity, which std::vector's own
	// assignment does whenever the source fits.
	Track(const Track& o) : Track() {
		*this = o;
	}
	Track& operator=(const Track& o) {
		steps.assign(o.steps.begin(), o.steps.end());
		patterns.assign(o.patterns.begin(), o.patterns.end());
		loopStart = o.loopStart;
		loopEnd = o.loopEnd;
		smoothA = o.smoothA;
		smoothB = o.smoothB;
		options = o.options;
		tableA = o.tableA;
		tableB = o.tableB;
		voltageGrain = o.voltageGrain;
		math = o.math;
		transform = o.transform;
		return *this;
	}

	int numSteps() const {
		return (int) steps.size();
	}

	Step defaultStep() const {
		Step s;
		s.cvA = (uint8_t) tableA.closestIndex(1.f);
		s.cvB = (uint8_t) tableB.closestIndex(1.f);
		return s;
	}

	// Which pattern a flat step index falls in, and its position inside that pattern.
	void locate(int stepIndex, int& pattern, int& stepInPattern) const {
		int start = 0;
		for (int p = 0; p < (int) patterns.size(); p++) {
			if (stepIndex < start + patterns[p].length) {
				pattern = p;
				stepInPattern = stepIndex - start;
				return;
			}
			start += patterns[p].length;
		}
		pattern = -1;
		stepInPattern = -1;
	}
};

struct Sequence {
	std::array<Track, NUM_TRACKS> tracks;

	int totalSteps() const {
		int n = 0;
		for (const Track& t : tracks)
			n += t.numSteps();
		return n;
	}

	// Appends an empty pattern to a track. Returns false at the hardware limit.
	bool appendPattern(int track) {
		Track& t = tracks[track];
		if ((int) t.patterns.size() >= MAX_PATTERNS)
			return false;
		t.patterns.push_back(Pattern());
		return true;
	}

	// Appends a step to a track's last pattern, creating one if the track has none.
	// Returns false at any of the hardware limits.
	bool appendStep(int track, const Step& step) {
		Track& t = tracks[track];
		if (totalSteps() >= MAX_TOTAL_STEPS)
			return false;
		if (t.patterns.empty() && !appendPattern(track))
			return false;
		if (t.patterns.back().length >= MAX_STEPS_PER_PATTERN)
			return false;
		t.steps.push_back(step);
		t.patterns.back().length++;
		return true;
	}

	void clearTrack(int track) {
		Track& t = tracks[track];
		t.steps.clear();
		t.patterns.clear();
		t.loopStart = -1;
		t.loopEnd = -1;
		t.smoothA = false;
		t.smoothB = false;
	}

	// Whether two sequences hold the same music (steps, patterns, loops, flags, options,
	// tables, math). Used to tell whether a burst of panel activity changed anything.
	bool sameContent(const Sequence& o) const {
		for (int i = 0; i < NUM_TRACKS; i++) {
			const Track& a = tracks[i];
			const Track& b = o.tracks[i];
			if (a.steps.size() != b.steps.size() || a.patterns.size() != b.patterns.size())
				return false;
			for (size_t k = 0; k < a.steps.size(); k++) {
				const Step& s = a.steps[k];
				const Step& t = b.steps[k];
				if (s.cvA != t.cvA || s.cvB != t.cvB || s.duration != t.duration || s.gate != t.gate ||
				    s.smoothA != t.smoothA || s.smoothB != t.smoothB || s.ratchet != t.ratchet)
					return false;
			}
			for (size_t k = 0; k < a.patterns.size(); k++) {
				const Pattern& p = a.patterns[k];
				const Pattern& q = b.patterns[k];
				if (p.length != q.length || p.smoothA != q.smoothA || p.smoothB != q.smoothB)
					return false;
			}
			const TrackOptions& x = a.options;
			const TrackOptions& y = b.options;
			if (a.loopStart != b.loopStart || a.loopEnd != b.loopEnd || a.smoothA != b.smoothA ||
			    a.smoothB != b.smoothB || a.voltageGrain != b.voltageGrain || x.noteDisplayA != y.noteDisplayA ||
			    x.noteDisplayB != y.noteDisplayB || x.clockDiv != y.clockDiv || x.clockMul != y.clockMul ||
			    x.triggerMode != y.triggerMode || a.tableA.volts != b.tableA.volts || a.tableB.volts != b.tableB.volts)
				return false;
			for (int k = 0; k < MATH_PARAMS; k++) {
				if (a.math[k].type != b.math[k].type || a.math[k].operand != b.math[k].operand)
					return false;
			}
			if (a.transform != b.transform)
				return false;
		}
		return true;
	}

	// Back to a fresh state: no steps, default tables, options and math. Unlike
	// assigning a new Sequence this does not allocate.
	void clearAll() {
		for (int i = 0; i < NUM_TRACKS; i++) {
			clearTrack(i);
			Track& t = tracks[i];
			t.options = TrackOptions();
			t.tableA = VoltageTable();
			t.tableB = VoltageTable();
			t.voltageGrain = 0;
			t.math = std::array<MathOp, MATH_PARAMS>();
			t.transform = TransformSet();
		}
	}
};

} // namespace iqs
