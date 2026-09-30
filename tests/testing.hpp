#pragma once
// A minimal test framework plus sequence-building helpers shared by the test files.

#include "Sequence.hpp"
#include "Playhead.hpp"

#include <array>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace testing {

struct Counters {
	int checks = 0;
	int failures = 0;
};
inline Counters& counters() {
	static Counters c;
	return c;
}

struct TestCase {
	const char* name;
	std::function<void()> fn;
};
inline std::vector<TestCase>& registry() {
	static std::vector<TestCase> r;
	return r;
}
struct Register {
	Register(const char* name, std::function<void()> fn) {
		registry().push_back({name, fn});
	}
};

inline std::string toString(const std::string& s) { return "\"" + s + "\""; }
inline std::string toString(int v) { return std::to_string(v); }

} // namespace testing

#define TEST(name) \
	static void name(); \
	static testing::Register reg_##name(#name, name); \
	static void name()

#define CHECK(cond) \
	do { \
		testing::counters().checks++; \
		if (!(cond)) { \
			testing::counters().failures++; \
			std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

#define CHECK_EQ(a, b) \
	do { \
		testing::counters().checks++; \
		auto _a = (a); \
		auto _b = (b); \
		if (!(_a == _b)) { \
			testing::counters().failures++; \
			std::printf("  FAIL %s:%d: %s == %s\n    got:      %s\n    expected: %s\n", __FILE__, __LINE__, #a, #b, \
			            testing::toString(_a).c_str(), testing::toString(_b).c_str()); \
		} \
	} while (0)

// --- Sequence helpers --------------------------------------------------------

inline iqs::Step makeStep(int cvA, int duration, int gate) {
	iqs::Step s;
	s.cvA = (uint8_t) cvA;
	s.duration = (uint8_t) duration;
	s.gate = (uint8_t) gate;
	return s;
}

// Builds track 0 from (cvA, duration, gate) triples, all in one pattern.
inline iqs::Sequence build(std::initializer_list<std::array<int, 3>> steps) {
	iqs::Sequence seq;
	for (const auto& s : steps)
		seq.appendStep(0, makeStep(s[0], s[1], s[2]));
	return seq;
}

// Clocks the playhead `pulses` times and records, per pulse, which step is playing
// ("-" when armed or stalled) and whether its gate is high ("^" high, "_" low).
inline std::string run(const iqs::Track& t, iqs::Playhead& ph, int pulses) {
	std::string out;
	for (int i = 0; i < pulses; i++) {
		ph.clock(t);
		if (ph.armed())
			out += "-_";
		else
			out += std::to_string(ph.step) + (ph.gate(t) ? "^" : "_");
		out += ' ';
	}
	if (!out.empty())
		out.pop_back();
	return out;
}
