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
};

} // namespace iqs
