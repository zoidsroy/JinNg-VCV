#pragma once
// Patch (de)serialization of the sequence. Kept out of src/core because it uses
// Rack's jansson.
//
// Each step is stored compactly as [cvA, cvB, duration, gate, flags] with flags
// bit 0 = smooth A, bit 1 = smooth B, bit 2 = ratchet.

#include "plugin.hpp"
#include "core/Sequence.hpp"

namespace iqs {

inline json_t* tableToJson(const VoltageTable& tbl) {
	json_t* a = json_array();
	for (float v : tbl.volts)
		json_array_append_new(a, json_real(v));
	return a;
}

inline void tableFromJson(VoltageTable& tbl, json_t* a) {
	if (!json_is_array(a))
		return;
	for (int i = 0; i < TABLE_SIZE && i < (int) json_array_size(a); i++)
		tbl.volts[i] = clamp((float) json_number_value(json_array_get(a, i)), 0.f, MAX_VOLTAGE);
}

inline json_t* sequenceToJson(const Sequence& seq) {
	json_t* tracksJ = json_array();
	for (const Track& t : seq.tracks) {
		json_t* trackJ = json_object();

		json_t* stepsJ = json_array();
		for (const Step& s : t.steps) {
			int flags = (s.smoothA ? 1 : 0) | (s.smoothB ? 2 : 0) | (s.ratchet ? 4 : 0);
			json_t* stepJ = json_pack("[iiiii]", s.cvA, s.cvB, s.duration, s.gate, flags);
			json_array_append_new(stepsJ, stepJ);
		}
		json_object_set_new(trackJ, "steps", stepsJ);

		json_t* patternsJ = json_array();
		for (uint8_t len : t.patternLengths)
			json_array_append_new(patternsJ, json_integer(len));
		json_object_set_new(trackJ, "patterns", patternsJ);

		json_object_set_new(trackJ, "loopStart", json_integer(t.loopStart));
		json_object_set_new(trackJ, "loopEnd", json_integer(t.loopEnd));
		json_object_set_new(trackJ, "tableA", tableToJson(t.tableA));
		json_object_set_new(trackJ, "tableB", tableToJson(t.tableB));
		json_array_append_new(tracksJ, trackJ);
	}
	return tracksJ;
}

// Rebuilds the sequence, enforcing every hardware limit so a hand-edited or corrupt
// patch cannot break playback invariants.
inline void sequenceFromJson(Sequence& seq, json_t* tracksJ) {
	if (!json_is_array(tracksJ))
		return;
	for (int ti = 0; ti < NUM_TRACKS; ti++) {
		seq.clearTrack(ti);
		json_t* trackJ = json_array_get(tracksJ, ti);
		if (!trackJ)
			continue;
		Track& t = seq.tracks[ti];

		tableFromJson(t.tableA, json_object_get(trackJ, "tableA"));
		tableFromJson(t.tableB, json_object_get(trackJ, "tableB"));

		json_t* stepsJ = json_object_get(trackJ, "steps");
		json_t* patternsJ = json_object_get(trackJ, "patterns");
		size_t next = 0;
		for (size_t p = 0; p < json_array_size(patternsJ); p++) {
			if (!seq.appendPattern(ti))
				break;
			int len = (int) json_integer_value(json_array_get(patternsJ, p));
			for (int i = 0; i < len && next < json_array_size(stepsJ); i++, next++) {
				json_t* stepJ = json_array_get(stepsJ, next);
				auto field = [&](size_t k) {
					return clamp((int) json_integer_value(json_array_get(stepJ, k)), 0, MAX_VALUE);
				};
				Step s;
				s.cvA = (uint8_t) field(0);
				s.cvB = (uint8_t) field(1);
				s.duration = (uint8_t) field(2);
				s.gate = (uint8_t) field(3);
				int flags = (int) json_integer_value(json_array_get(stepJ, 4));
				s.smoothA = flags & 1;
				s.smoothB = flags & 2;
				s.ratchet = flags & 4;
				if (!seq.appendStep(ti, s))
					break;
			}
		}

		auto loopPoint = [&](const char* key) {
			json_t* j = json_object_get(trackJ, key);
			int v = j ? (int) json_integer_value(j) : -1;
			return (v >= 0 && v < t.numSteps()) ? v : -1;
		};
		t.loopStart = loopPoint("loopStart");
		t.loopEnd = loopPoint("loopEnd");
	}
}

} // namespace iqs
