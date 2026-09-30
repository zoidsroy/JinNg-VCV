#pragma once
// VOLTAGE display formatting (docs/SPEC.md §4.5).
//
// Note display is {octave}.{natural note}.{percent of a whole tone above it}, with
// n.000V = n.C.00: A#3 is 3.A.50, a quarter-tone above G4 is 4.G.25.

#include <cmath>
#include <cstdio>
#include <string>

namespace iqs {

inline std::string formatVoltage(float v) {
	char buf[16];
	snprintf(buf, sizeof(buf), "%.3f", v);
	return buf;
}

inline std::string formatNote(float v) {
	static const int NATURAL_SEMITONES[7] = {0, 2, 4, 5, 7, 9, 11};
	static const char NATURAL_NAMES[7] = {'C', 'D', 'E', 'F', 'G', 'A', 'B'};

	// Work in hundredths of a whole tone (= 50 per semitone) so rounding happens once.
	long units = std::lround(v * 12.f * 50.f);
	if (units < 0)
		units = 0;
	int octave = (int) (units / 600);
	int inOctave = (int) (units % 600);

	int n = 6;
	while (NATURAL_SEMITONES[n] * 50 > inOctave)
		n--;
	int percent = inOctave - NATURAL_SEMITONES[n] * 50;

	char buf[16];
	snprintf(buf, sizeof(buf), "%d.%c.%02d", octave, NATURAL_NAMES[n], percent);
	return buf;
}

} // namespace iqs
