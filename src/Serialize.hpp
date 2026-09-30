#pragma once
// Patch (de)serialization of the sequence. Kept out of src/core because it uses
// Rack's jansson.
//
// A sequence is {"tracks": [...], "groups": [...]} (older patches: just the tracks
// array). Each step is stored compactly as [cvA, cvB, duration, gate, flags, groups]
// with flags bit 0 = smooth A, bit 1 = smooth B, bit 2 = ratchet, and groups the
// membership mask. Patterns are [length, flags] with the same smooth bits.

#include "plugin.hpp"
#include "core/Sequence.hpp"
#include "core/VoltageTables.hpp"
#include "core/Groups.hpp"

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

inline json_t* transformSetToJson(const TransformSet& tf) {
	json_t* a = json_array();
	for (const Transform& x : tf)
		json_array_append_new(a, json_pack("[iiiii]", x.add, x.geo, x.jitter, x.random, x.quantize));
	return a;
}

inline void transformSetFromJson(TransformSet& tf, json_t* a) {
	for (int k = 0; k < MATH_PARAMS && k < (int) json_array_size(a); k++) {
		json_t* xJ = json_array_get(a, k);
		auto field = [&](size_t i, int lo, int hi) {
			return clamp((int) json_integer_value(json_array_get(xJ, i)), lo, hi);
		};
		Transform& x = tf[k];
		x.add = (int8_t) field(0, -MAX_VALUE, MAX_VALUE);
		x.geo = (int8_t) field(1, -MAX_VALUE, MAX_VALUE);
		x.jitter = (uint8_t) field(2, 0, MAX_VALUE);
		x.random = (uint8_t) field(3, 0, MAX_VALUE);
		x.quantize = (uint8_t) field(4, 1, MAX_VALUE);
	}
}

// Only groups that differ from the default are stored.
inline json_t* groupsToJson(const Sequence& seq) {
	json_t* groupsJ = json_array();
	for (int g = 0; g < NUM_GROUPS; g++) {
		const Group& grp = seq.groups[g];
		if (grp == Group())
			continue;
		json_t* gJ = json_object();
		json_object_set_new(gJ, "index", json_integer(g));
		json_object_set_new(gJ, "transform", transformSetToJson(grp.transform));
		json_t* highJ = json_array();
		json_t* lowJ = json_array();
		json_t* slopeJ = json_array();
		for (int c = 0; c < NUM_MOD_CHANNELS; c++) {
			json_array_append_new(highJ, transformSetToJson(grp.high[c]));
			json_array_append_new(lowJ, transformSetToJson(grp.low[c]));
			json_t* row = json_array();
			for (float k : grp.slope[c])
				json_array_append_new(row, json_real(k));
			json_array_append_new(slopeJ, row);
		}
		json_object_set_new(gJ, "high", highJ);
		json_object_set_new(gJ, "low", lowJ);
		json_object_set_new(gJ, "slope", slopeJ);
		json_array_append_new(groupsJ, gJ);
	}
	return groupsJ;
}

inline void groupsFromJson(Sequence& seq, json_t* groupsJ) {
	for (size_t i = 0; i < json_array_size(groupsJ); i++) {
		json_t* gJ = json_array_get(groupsJ, i);
		int g = (int) json_integer_value(json_object_get(gJ, "index"));
		if (g < 0 || g >= NUM_GROUPS)
			continue;
		Group& grp = seq.groups[g];
		transformSetFromJson(grp.transform, json_object_get(gJ, "transform"));
		json_t* highJ = json_object_get(gJ, "high");
		json_t* lowJ = json_object_get(gJ, "low");
		json_t* slopeJ = json_object_get(gJ, "slope");
		for (int c = 0; c < NUM_MOD_CHANNELS; c++) {
			transformSetFromJson(grp.high[c], json_array_get(highJ, c));
			transformSetFromJson(grp.low[c], json_array_get(lowJ, c));
			json_t* row = json_array_get(slopeJ, c);
			for (int k = 0; k < MATH_PARAMS && k < (int) json_array_size(row); k++)
				grp.slope[c][k] = clamp((float) json_number_value(json_array_get(row, k)), -99.f, 99.f);
		}
	}
}

inline json_t* tracksToJson(const Sequence& seq);
inline void tracksFromJson(Sequence& seq, json_t* tracksJ);

inline json_t* sequenceToJson(const Sequence& seq) {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "tracks", tracksToJson(seq));
	json_object_set_new(rootJ, "groups", groupsToJson(seq));
	return rootJ;
}

inline void sequenceFromJson(Sequence& seq, json_t* j) {
	if (json_is_array(j)) { // before groups existed
		tracksFromJson(seq, j);
		return;
	}
	if (!json_is_object(j))
		return;
	tracksFromJson(seq, json_object_get(j, "tracks"));
	groupsFromJson(seq, json_object_get(j, "groups"));
}

inline json_t* tracksToJson(const Sequence& seq) {
	json_t* tracksJ = json_array();
	for (const Track& t : seq.tracks) {
		json_t* trackJ = json_object();

		json_t* stepsJ = json_array();
		for (const Step& s : t.steps) {
			int flags = (s.smoothA ? 1 : 0) | (s.smoothB ? 2 : 0) | (s.ratchet ? 4 : 0);
			json_t* stepJ = json_pack("[iiiiii]", s.cvA, s.cvB, s.duration, s.gate, flags, s.groups);
			json_array_append_new(stepsJ, stepJ);
		}
		json_object_set_new(trackJ, "steps", stepsJ);

		json_t* patternsJ = json_array();
		for (const Pattern& p : t.patterns) {
			int flags = (p.smoothA ? 1 : 0) | (p.smoothB ? 2 : 0);
			json_array_append_new(patternsJ, json_pack("[ii]", p.length, flags));
		}
		json_object_set_new(trackJ, "patterns", patternsJ);

		json_object_set_new(trackJ, "smoothA", json_boolean(t.smoothA));
		json_object_set_new(trackJ, "smoothB", json_boolean(t.smoothB));
		const TrackOptions& o = t.options;
		json_t* optionsJ = json_object();
		json_object_set_new(optionsJ, "noteDisplayA", json_boolean(o.noteDisplayA));
		json_object_set_new(optionsJ, "noteDisplayB", json_boolean(o.noteDisplayB));
		json_object_set_new(optionsJ, "clockDiv", json_integer(o.clockDiv));
		json_object_set_new(optionsJ, "clockMul", json_integer(o.clockMul));
		json_object_set_new(optionsJ, "triggerMode", json_boolean(o.triggerMode));
		json_object_set_new(trackJ, "options", optionsJ);

		json_object_set_new(trackJ, "loopStart", json_integer(t.loopStart));
		json_object_set_new(trackJ, "loopEnd", json_integer(t.loopEnd));
		json_object_set_new(trackJ, "tableA", tableToJson(t.tableA));
		json_object_set_new(trackJ, "tableB", tableToJson(t.tableB));
		json_object_set_new(trackJ, "voltageGrain", json_integer(t.voltageGrain));
		json_t* mathJ = json_array();
		for (const MathOp& op : t.math)
			json_array_append_new(mathJ, json_pack("[ii]", op.type, op.operand));
		json_object_set_new(trackJ, "math", mathJ);
		json_object_set_new(trackJ, "transform", transformSetToJson(t.transform));
		// Parts: only the ones that set something, as [part, resetTo, loopStart, loopEnd].
		json_t* partsJ = json_array();
		for (int p = 0; p < NUM_PARTS; p++) {
			const PartPoints& pp = t.parts[p];
			if (!pp.empty())
				json_array_append_new(partsJ, json_pack("[iiii]", p, pp.resetTo, pp.loopStart, pp.loopEnd));
		}
		json_object_set_new(trackJ, "parts", partsJ);
		json_array_append_new(tracksJ, trackJ);
	}
	return tracksJ;
}

// Rebuilds the sequence, enforcing every hardware limit so a hand-edited or corrupt
// patch cannot break playback invariants.
inline void tracksFromJson(Sequence& seq, json_t* tracksJ) {
	if (!json_is_array(tracksJ))
		return;
	seq.clearAll();
	for (int ti = 0; ti < NUM_TRACKS; ti++) {
		json_t* trackJ = json_array_get(tracksJ, ti);
		if (!trackJ)
			continue;
		Track& t = seq.tracks[ti];

		tableFromJson(t.tableA, json_object_get(trackJ, "tableA"));
		tableFromJson(t.tableB, json_object_get(trackJ, "tableB"));
		t.voltageGrain = (uint8_t) clamp((int) json_integer_value(json_object_get(trackJ, "voltageGrain")), 0, GRAIN_LEN - 1);
		json_t* mathJ = json_object_get(trackJ, "math");
		for (int k = 0; k < MATH_PARAMS && k < (int) json_array_size(mathJ); k++) {
			json_t* opJ = json_array_get(mathJ, k);
			MathOp& op = t.math[k];
			op.type = (uint8_t) clamp((int) json_integer_value(json_array_get(opJ, 0)), 0, MATH_TYPES - 1);
			int lo = op.type == MATH_ADD || op.type == MATH_GEO ? -MAX_VALUE : 0;
			op.operand = (int8_t) clamp((int) json_integer_value(json_array_get(opJ, 1)), lo, MAX_VALUE);
		}
		transformSetFromJson(t.transform, json_object_get(trackJ, "transform"));

		json_t* stepsJ = json_object_get(trackJ, "steps");
		json_t* patternsJ = json_object_get(trackJ, "patterns");
		size_t next = 0;
		for (size_t p = 0; p < json_array_size(patternsJ); p++) {
			if (!seq.appendPattern(ti))
				break;
			json_t* patternJ = json_array_get(patternsJ, p);
			int len, patternFlags;
			if (json_is_array(patternJ)) {
				len = (int) json_integer_value(json_array_get(patternJ, 0));
				patternFlags = (int) json_integer_value(json_array_get(patternJ, 1));
			}
			else {
				// Patches from before pattern smoothing stored bare lengths.
				len = (int) json_integer_value(patternJ);
				patternFlags = 0;
			}
			t.patterns.back().smoothA = patternFlags & 1;
			t.patterns.back().smoothB = patternFlags & 2;
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
				s.groups = (uint16_t) json_integer_value(json_array_get(stepJ, 5));
				if (!seq.appendStep(ti, s))
					break;
			}
		}

		// Part points refer to steps, so they are read (and checked) after the steps.
		json_t* partsJ = json_object_get(trackJ, "parts");
		for (size_t k = 0; k < json_array_size(partsJ); k++) {
			json_t* pJ = json_array_get(partsJ, k);
			int p = (int) json_integer_value(json_array_get(pJ, 0));
			if (p <= STOP_PART || p >= NUM_PARTS)
				continue;
			auto point = [&](size_t i) {
				int v = (int) json_integer_value(json_array_get(pJ, i));
				return (int16_t) ((v >= 0 && v < t.numSteps()) ? v : -1);
			};
			t.parts[p].resetTo = point(1);
			t.parts[p].loopStart = point(2);
			t.parts[p].loopEnd = point(3);
		}

		auto loopPoint = [&](const char* key) {
			json_t* j = json_object_get(trackJ, key);
			int v = j ? (int) json_integer_value(j) : -1;
			return (v >= 0 && v < t.numSteps()) ? v : -1;
		};
		t.loopStart = loopPoint("loopStart");
		t.loopEnd = loopPoint("loopEnd");

		t.smoothA = json_boolean_value(json_object_get(trackJ, "smoothA"));
		t.smoothB = json_boolean_value(json_object_get(trackJ, "smoothB"));
		t.options = TrackOptions();
		if (json_t* optionsJ = json_object_get(trackJ, "options")) {
			TrackOptions& o = t.options;
			auto flag = [&](const char* key, bool def) {
				json_t* j = json_object_get(optionsJ, key);
				return j ? json_boolean_value(j) : def;
			};
			auto ratio = [&](const char* key) {
				json_t* j = json_object_get(optionsJ, key);
				return (uint8_t) (j ? clamp((int) json_integer_value(j), 1, MAX_VALUE) : 1);
			};
			o.noteDisplayA = flag("noteDisplayA", true);
			o.noteDisplayB = flag("noteDisplayB", true);
			o.clockDiv = ratio("clockDiv");
			o.clockMul = ratio("clockMul");
			o.triggerMode = flag("triggerMode", false);
		}
	}
}

} // namespace iqs
