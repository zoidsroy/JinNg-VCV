#pragma once
// Reference voltage tables and voltage editing (docs/SPEC.md §2, §4.5).
//
// Eight built-in reference tables plus eight user tables. The manual only describes the
// built-ins in words; 12ET, 24ET and L-8 follow exactly from the description, the others
// are reconstructions (marked "approximate" below) until real data is available.

#include "Sequence.hpp"
#include <cmath>

namespace iqs {

static constexpr int NUM_BUILTIN_TABLES = 8;
static constexpr int NUM_USER_TABLES = 8;
static constexpr int NUM_REF_TABLES = NUM_BUILTIN_TABLES + NUM_USER_TABLES;

// Four characters each, as shown on the VOLTAGE display.
inline const char* refTableName(int i) {
	static const char* const names[NUM_REF_TABLES] = {
		"12Et", "24Et", "22Jt", "bLUE", "PEnt", "L-8", "E-8", "LE-8",
		"USr1", "USr2", "USr3", "USr4", "USr5", "USr6", "USr7", "USr8",
	};
	return (i >= 0 && i < NUM_REF_TABLES) ? names[i] : "";
}

namespace detail {

inline float clampVolts(float v) {
	return std::max(0.f, std::min(v, MAX_VOLTAGE));
}

// A scale given as semitones within an octave, repeated upward from 0V.
inline VoltageTable scaleTable(const int* semitones, int count, int firstIndex = 0) {
	VoltageTable t;
	for (int i = 0; i < TABLE_SIZE; i++) {
		int k = i - firstIndex;
		if (k < 0)
			continue;
		t.volts[i] = clampVolts(k / count + semitones[k % count] / 12.f);
	}
	return t;
}

inline VoltageTable build(int which) {
	VoltageTable t;
	switch (which) {
		case 0: // 12ET: semitones
			t.set12ET();
			break;
		case 1: // 24ET: quarter tones
			for (int i = 0; i < TABLE_SIZE; i++)
				t.volts[i] = clampVolts(i / 24.f);
			break;
		case 2: { // 22JT: the 22 shrutis (approximate: one common set of ratios)
			static const double ratios[22] = {
				1.0, 256.0 / 243, 16.0 / 15, 10.0 / 9, 9.0 / 8, 32.0 / 27, 6.0 / 5, 5.0 / 4,
				81.0 / 64, 4.0 / 3, 27.0 / 20, 45.0 / 32, 729.0 / 512, 3.0 / 2, 128.0 / 81, 8.0 / 5,
				5.0 / 3, 27.0 / 16, 16.0 / 9, 9.0 / 5, 15.0 / 8, 243.0 / 128,
			};
			for (int i = 0; i < TABLE_SIZE; i++)
				t.volts[i] = clampVolts((float) (i / 22 + std::log2(ratios[i % 22])));
			break;
		}
		case 3: { // BLUE: diatonic plus flat 3rd and flat 7th (approximate layout)
			static const int blues[9] = {0, 2, 3, 4, 5, 7, 9, 10, 11};
			t = scaleTable(blues, 9);
			break;
		}
		case 4: { // PEnt: 0-49 major pentatonic, 50-99 minor pentatonic
			static const int major[5] = {0, 2, 4, 7, 9};
			static const int minor[5] = {0, 3, 5, 7, 10};
			VoltageTable a = scaleTable(major, 5);
			VoltageTable b = scaleTable(minor, 5, 50);
			for (int i = 0; i < TABLE_SIZE; i++)
				t.volts[i] = i < 50 ? a.volts[i] : b.volts[i];
			break;
		}
		case 5: // L-8: 0 to 8.0V in 100mV steps
			for (int i = 0; i < TABLE_SIZE; i++)
				t.volts[i] = std::min(i * 0.1f, 8.f);
			break;
		case 6: // E-8: exponential 0 to 8.0V (approximate curve)
			for (int i = 0; i < TABLE_SIZE; i++)
				t.volts[i] = clampVolts(8.f * (std::pow(801.f, i / 99.f) - 1.f) / 800.f);
			break;
		case 7: // LE-8: 0-0.1V linear in 2mV steps, then exponential 0.1-0.8V (approximate)
			for (int i = 0; i < TABLE_SIZE; i++)
				t.volts[i] = i <= 50 ? i * 0.002f : 0.1f * std::pow(8.f, (i - 50) / 49.f);
			break;
		default:
			break;
	}
	return t;
}

} // namespace detail

inline const VoltageTable& builtinTable(int i) {
	static const VoltageTable tables[NUM_BUILTIN_TABLES] = {
		detail::build(0), detail::build(1), detail::build(2), detail::build(3),
		detail::build(4), detail::build(5), detail::build(6), detail::build(7),
	};
	return tables[std::max(0, std::min(i, NUM_BUILTIN_TABLES - 1))];
}

// The user reference tables, shared by every instance (they live outside any patch, as
// they live outside any snapshot on the hardware). They start out as 12ET.
struct RefTables {
	VoltageTable user[NUM_USER_TABLES];

	const VoltageTable& get(int i) const {
		return i < NUM_BUILTIN_TABLES ? builtinTable(i) : user[std::min(i - NUM_BUILTIN_TABLES, NUM_USER_TABLES - 1)];
	}
	bool writable(int i) const {
		return i >= NUM_BUILTIN_TABLES && i < NUM_REF_TABLES;
	}
};

// --- Voltage editing ---------------------------------------------------------

enum Granularity { GRAIN_FINE, GRAIN_COARSE, GRAIN_SUPER_COARSE, GRAIN_LEN };

// Moves a table voltage by d increments (manual, Adjustment Granularity):
//   number display: 2mV / 100mV / 1V
//   note display:   1% of a whole tone / to the next chromatic note / an octave
// Note-display steps land on their grid, so an off-grid voltage snaps onto it first.
inline float nudgeVoltage(float v, int d, int grain, bool note) {
	const float eps = 1e-4f;
	float out = v;
	if (grain == GRAIN_SUPER_COARSE) {
		out = v + d;
	}
	else if (!note) {
		out = v + d * (grain == GRAIN_FINE ? 0.002f : 0.1f);
	}
	else {
		float perVolt = grain == GRAIN_FINE ? 600.f : 12.f; // 1% whole tone, semitone
		float units = v * perVolt;
		float base = d > 0 ? std::floor(units + eps) : std::ceil(units - eps);
		out = (base + d) / perVolt;
	}
	return detail::clampVolts(out);
}

} // namespace iqs
