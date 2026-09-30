#pragma once
// Structural edits on a track (docs/SPEC.md §4.4). Every edit keeps the track's
// invariants: hardware limits, loop points pointing at the same steps, and the play
// cursor still on the step it was playing. Functions return false and change nothing
// when an edit would exceed a limit.

#include "Playhead.hpp"
#include "Sequence.hpp"

namespace iqs {
namespace edit {

inline int numPatterns(const Track& t) {
	return (int) t.patternLengths.size();
}

inline int patternStart(const Track& t, int p) {
	int start = 0;
	for (int i = 0; i < p && i < numPatterns(t); i++)
		start += t.patternLengths[i];
	return start;
}

inline int patternEnd(const Track& t, int p) {
	return patternStart(t, p) + t.patternLengths[p];
}

// Shifts indices at or after `pos` by `n` (n < 0 for removals); an index inside a
// removed range becomes -1.
inline int shiftIndex(int idx, int pos, int n) {
	if (idx < 0 || idx < pos)
		return idx;
	if (n < 0 && idx < pos - n)
		return -1;
	return idx + n;
}

inline void shiftAfterInsert(Track& t, Playhead& ph, int pos, int n, bool hadSteps) {
	t.loopStart = shiftIndex(t.loopStart, pos, n);
	t.loopEnd = shiftIndex(t.loopEnd, pos, n);
	// Inserting in front of the playing step pushes it back; it keeps playing.
	if (hadSteps && ph.step >= pos)
		ph.step += n;
}

inline void shiftAfterRemove(Track& t, Playhead& ph, int pos, int n) {
	// A loop point on a deleted step is cleared (spec §3.8, open question 6).
	t.loopStart = shiftIndex(t.loopStart, pos, -n);
	t.loopEnd = shiftIndex(t.loopEnd, pos, -n);
	// If the playing step was deleted, playback continues from the step that took its
	// place; its pulse count carries over so the rhythm does not stumble.
	if (ph.step >= pos + n)
		ph.step -= n;
	else if (ph.step >= pos)
		ph.step = pos;
	if (ph.step >= t.numSteps()) {
		ph.step = 0;
		ph.pulse = -1;
	}
}

// Inserts n steps at flat index `pos`, which must lie within pattern p (inclusive of
// its end).
inline bool insertSteps(Sequence& seq, int track, int p, int pos, const Step* src, int n, Playhead& ph) {
	Track& t = seq.tracks[track];
	if (p < 0 || p >= numPatterns(t) || n <= 0)
		return false;
	if (pos < patternStart(t, p) || pos > patternEnd(t, p))
		return false;
	if (seq.totalSteps() + n > MAX_TOTAL_STEPS || t.patternLengths[p] + n > MAX_STEPS_PER_PATTERN)
		return false;
	bool hadSteps = t.numSteps() > 0;
	t.steps.insert(t.steps.begin() + pos, src, src + n);
	t.patternLengths[p] += n;
	shiftAfterInsert(t, ph, pos, n, hadSteps);
	return true;
}

inline bool removeStep(Sequence& seq, int track, int pos, Playhead& ph) {
	Track& t = seq.tracks[track];
	if (pos < 0 || pos >= t.numSteps())
		return false;
	int p, s;
	t.locate(pos, p, s);
	t.steps.erase(t.steps.begin() + pos);
	t.patternLengths[p]--;
	shiftAfterRemove(t, ph, pos, 1);
	return true;
}

// Inserts `count` patterns before pattern index `at` (at == numPatterns appends).
// `lengths` gives each new pattern's length and `src` their steps back to back.
inline bool insertPatterns(Sequence& seq, int track, int at, const uint8_t* lengths, int count, const Step* src, Playhead& ph) {
	Track& t = seq.tracks[track];
	if (at < 0 || at > numPatterns(t) || count <= 0)
		return false;
	int n = 0;
	for (int i = 0; i < count; i++)
		n += lengths[i];
	if (numPatterns(t) + count > MAX_PATTERNS || seq.totalSteps() + n > MAX_TOTAL_STEPS)
		return false;
	int pos = patternStart(t, at);
	bool hadSteps = t.numSteps() > 0;
	t.patternLengths.insert(t.patternLengths.begin() + at, lengths, lengths + count);
	if (n > 0) {
		t.steps.insert(t.steps.begin() + pos, src, src + n);
		shiftAfterInsert(t, ph, pos, n, hadSteps);
	}
	return true;
}

inline bool insertEmptyPattern(Sequence& seq, int track, int at, Playhead& ph) {
	uint8_t zero = 0;
	return insertPatterns(seq, track, at, &zero, 1, nullptr, ph);
}

inline bool removePattern(Sequence& seq, int track, int p, Playhead& ph) {
	Track& t = seq.tracks[track];
	if (p < 0 || p >= numPatterns(t))
		return false;
	int pos = patternStart(t, p);
	int n = t.patternLengths[p];
	t.steps.erase(t.steps.begin() + pos, t.steps.begin() + pos + n);
	t.patternLengths.erase(t.patternLengths.begin() + p);
	if (n > 0)
		shiftAfterRemove(t, ph, pos, n);
	return true;
}

// Splits a step in two, preserving its total duration; an odd duration leaves the
// extra pulse on the first half (5 -> 3 + 2).
inline bool splitStep(Sequence& seq, int track, int pos, Playhead& ph) {
	Track& t = seq.tracks[track];
	if (pos < 0 || pos >= t.numSteps())
		return false;
	int p, s;
	t.locate(pos, p, s);
	Step second = t.steps[pos];
	int d = t.steps[pos].duration;
	second.duration = (uint8_t) (d / 2);
	if (!insertSteps(seq, track, p, pos + 1, &second, 1, ph))
		return false;
	t.steps[pos].duration = (uint8_t) (d - d / 2);
	return true;
}

// Splits pattern p so that step `stepInPattern` starts the second half.
inline bool splitPattern(Sequence& seq, int track, int p, int stepInPattern) {
	Track& t = seq.tracks[track];
	if (p < 0 || p >= numPatterns(t) || numPatterns(t) >= MAX_PATTERNS)
		return false;
	int len = t.patternLengths[p];
	int k = std::max(0, std::min(stepInPattern, len));
	t.patternLengths[p] = (uint8_t) k;
	t.patternLengths.insert(t.patternLengths.begin() + p + 1, (uint8_t) (len - k));
	return true;
}

// Overwrites a whole track (steps, patterns, loop points and tables) with a copy.
inline bool replaceTrack(Sequence& seq, int track, const Track& src, Playhead& ph) {
	Track& t = seq.tracks[track];
	if (seq.totalSteps() - t.numSteps() + src.numSteps() > MAX_TOTAL_STEPS)
		return false;
	t = src;
	ph.validate(t);
	return true;
}

// Moves DURATION from the following step into step `pos` (negative d gives it back),
// keeping their sum, so the rest of the track stays in time (manual: swing/shuffle).
inline bool transferDuration(Track& t, int pos, int d) {
	if (pos < 0 || pos + 1 >= t.numSteps())
		return false;
	int a = t.steps[pos].duration + d;
	int b = t.steps[pos + 1].duration - d;
	if (a < 0 || a > MAX_VALUE || b < 0 || b > MAX_VALUE)
		return false;
	t.steps[pos].duration = (uint8_t) a;
	t.steps[pos + 1].duration = (uint8_t) b;
	return true;
}

} // namespace edit
} // namespace iqs
