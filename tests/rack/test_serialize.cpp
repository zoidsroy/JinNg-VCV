// Round-trip test for src/Serialize.hpp, which needs Rack's jansson. Built and run by
// `make test-rack` (links libRack; needs Rack's install folder on PATH to run).

#include "Serialize.hpp"

#include <cstdio>

Plugin* pluginInstance = nullptr;

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			failures++; \
			std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

static iqs::Sequence richSequence() {
	iqs::Sequence s;
	for (int i = 0; i < 6; i++) {
		iqs::Step st;
		st.cvA = (uint8_t) (10 + i);
		st.cvB = (uint8_t) (20 + i);
		st.duration = (uint8_t) (1 + i);
		st.gate = (uint8_t) i;
		st.smoothA = i % 2;
		st.ratchet = i == 3;
		st.groups = (uint16_t) (i == 2 ? 0x8001 : 0);
		s.appendStep(0, st);
		if (i == 2)
			s.appendPattern(0);
	}
	s.appendStep(2, iqs::Step());
	iqs::Track& t = s.tracks[0];
	t.loopStart = 1;
	t.loopEnd = 4;
	t.patterns[1].smoothB = true;
	t.smoothA = true;
	t.options.clockDiv = 3;
	t.options.clockMul = 2;
	t.options.triggerMode = true;
	t.options.noteDisplayB = false;
	t.tableA.volts[5] = 1.234f;
	t.voltageGrain = 2;
	t.math[iqs::MATH_GATE].type = iqs::MATH_JITTER;
	t.math[iqs::MATH_GATE].operand = 4;
	t.transform[iqs::MATH_CV_A].add = -7;
	t.transform[iqs::MATH_CV_A].geo = -3;
	t.transform[iqs::MATH_DURATION].quantize = 4;
	t.parts[5].resetTo = 0;
	t.parts[5].loopStart = 2;
	t.parts[99].loopEnd = 5;
	iqs::Group& g = s.groups[15];
	g.transform[iqs::MATH_GATE].random = 9;
	g.high[1][iqs::MATH_DURATION].geo = 0;
	g.low[2][iqs::MATH_CV_B].jitter = 3;
	g.slope[0][iqs::MATH_CV_A] = -0.05f;
	g.slope[2][iqs::MATH_GATE] = 40.f;
	return s;
}

int main() {
	// Round trip.
	iqs::Sequence a = richSequence();
	json_t* j = iqs::sequenceToJson(a);
	char* text = json_dumps(j, JSON_COMPACT);
	json_t* parsed = json_loads(text, 0, nullptr);
	iqs::Sequence b;
	b.appendStep(1, iqs::Step()); // leftovers must not survive a load
	iqs::sequenceFromJson(b, parsed);
	CHECK(a.sameContent(b));
	CHECK(b.tracks[1].numSteps() == 0);
	std::printf("serialized size: %zu bytes\n", std::strlen(text));
	free(text);
	json_decref(j);
	json_decref(parsed);

	// Patches from before groups: a bare tracks array, bare pattern lengths, 5-field steps.
	const char* old = "[{\"steps\":[[12,12,4,2,0],[24,12,4,2,1]],\"patterns\":[2],\"loopStart\":1,\"loopEnd\":-1}]";
	json_t* oldJ = json_loads(old, 0, nullptr);
	iqs::Sequence c;
	iqs::sequenceFromJson(c, oldJ);
	CHECK(c.tracks[0].numSteps() == 2);
	CHECK(c.tracks[0].patterns.size() == 1 && c.tracks[0].patterns[0].length == 2);
	CHECK(c.tracks[0].steps[1].cvA == 24 && c.tracks[0].steps[1].smoothA);
	CHECK(c.tracks[0].steps[1].groups == 0);
	CHECK(c.tracks[0].loopStart == 1);
	json_decref(oldJ);

	// Hostile input stays within limits.
	const char* bad = "{\"tracks\":[{\"steps\":[[300,-5,99,99,0,65535]],\"patterns\":[[5,0]],"
	                  "\"loopStart\":77,\"parts\":[[0,0,0,0],[150,0,0,0],[3,9,0,-1]]}],"
	                  "\"groups\":[{\"index\":40},{\"index\":2,\"slope\":[[500,0,0,0]]}]}";
	json_t* badJ = json_loads(bad, 0, nullptr);
	iqs::Sequence d;
	iqs::sequenceFromJson(d, badJ);
	CHECK(d.tracks[0].numSteps() == 1);
	CHECK(d.tracks[0].steps[0].cvA == 99 && d.tracks[0].steps[0].cvB == 0);
	CHECK(d.tracks[0].loopStart == -1);          // past the end
	CHECK(d.tracks[0].parts[0].empty());         // STOP is not stored
	CHECK(d.tracks[0].parts[3].resetTo == -1);   // step 9 does not exist
	CHECK(d.tracks[0].parts[3].loopStart == 0);
	CHECK(d.groups[2].slope[0][0] == 99.f);
	json_decref(badJ);

	std::printf("%s\n", failures == 0 ? "serialize: all passed" : "serialize: FAILED");
	return failures == 0 ? 0 : 1;
}
