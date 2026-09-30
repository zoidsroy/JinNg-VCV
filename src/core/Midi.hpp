#pragma once
// MIDI file import (SPEC-ER102 §7, manual pp.26-27, "simple method"): a Standard MIDI
// File of type 0 or 1 becomes a sequence, MIDI channel n (1-4) going to track n.
//
// Each channel is read as a monophonic line: a note that starts while another is
// playing ends it. Every note becomes one step whose DURATION runs to the next note's
// start and whose GATE is the note's length; silence before a note becomes a rest step
// (GATE 0). Steps longer than 99 pulses are split, tied with full gates. Times are
// counted in clock pulses at `pulsesPerQuarter` (4, the ER-101's usual 16th-note clock).
// CV-A is the note number minus `noteOffset` (so C2 = index 0, C4 = 24 = 2V in 12ET),
// CV-B the velocity scaled to 0..96 (0..8V in 12ET). Patterns are cut every
// `stepsPerPattern` steps.

#include "Sequence.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace iqs {

struct MidiNote {
	long start = 0; // ticks
	long end = 0;
	int note = 60;
	int velocity = 100;
};

struct MidiSong {
	int ticksPerQuarter = 96;
	std::vector<MidiNote> channels[16];
};

struct MidiImportOptions {
	int pulsesPerQuarter = 4;
	int noteOffset = 36;
	int stepsPerPattern = 16;
};

// Parses an SMF. Returns false with `error` set on anything it cannot read (including
// SMPTE time division and type 2 files).
inline bool parseMidi(const uint8_t* data, size_t size, MidiSong& song, std::string& error) {
	size_t pos = 0;
	auto need = [&](size_t n) { return pos + n <= size; };
	auto u32 = [&]() {
		uint32_t v = ((uint32_t) data[pos] << 24) | ((uint32_t) data[pos + 1] << 16) | ((uint32_t) data[pos + 2] << 8) | data[pos + 3];
		pos += 4;
		return v;
	};
	auto u16 = [&]() {
		uint16_t v = (uint16_t) ((data[pos] << 8) | data[pos + 1]);
		pos += 2;
		return v;
	};
	if (!need(14) || std::string((const char*) data, 4) != "MThd") {
		error = "not a MIDI file";
		return false;
	}
	pos = 4;
	uint32_t headerLength = u32();
	int format = u16();
	int tracks = u16();
	int division = u16();
	if (format > 1) {
		error = "type 2 MIDI files are not supported";
		return false;
	}
	if (division & 0x8000) {
		error = "SMPTE timing is not supported";
		return false;
	}
	song.ticksPerQuarter = std::max(1, division);
	pos = 8 + headerLength;

	for (int tr = 0; tr < tracks; tr++) {
		if (!need(8))
			break;
		std::string id((const char*) data + pos, 4);
		pos += 4;
		uint32_t length = u32();
		size_t end = std::min(size, pos + (size_t) length);
		if (id != "MTrk") {
			pos = end;
			continue;
		}
		long tick = 0;
		uint8_t status = 0;
		// Open notes per channel and key, so note-offs can find their note-ons.
		long openStart[16][128];
		int openVelocity[16][128];
		for (auto& row : openStart)
			std::fill(row, row + 128, -1L);
		auto readVar = [&]() {
			uint32_t v = 0;
			for (int i = 0; i < 4 && pos < end; i++) {
				uint8_t b = data[pos++];
				v = (v << 7) | (b & 0x7f);
				if (!(b & 0x80))
					break;
			}
			return v;
		};
		auto close = [&](int ch, int key) {
			if (openStart[ch][key] < 0)
				return;
			MidiNote n;
			n.start = openStart[ch][key];
			n.end = tick;
			n.note = key;
			n.velocity = openVelocity[ch][key];
			song.channels[ch].push_back(n);
			openStart[ch][key] = -1;
		};
		while (pos < end) {
			tick += readVar();
			if (pos >= end)
				break;
			uint8_t b = data[pos];
			if (b & 0x80) {
				status = b;
				pos++;
			}
			else if (!(status & 0x80)) {
				error = "corrupt track (data without status)";
				return false;
			}
			if (status == 0xff) { // meta event
				if (pos >= end)
					break;
				pos++; // type
				uint32_t len = readVar();
				pos += len;
				status = 0; // meta events cancel running status
				continue;
			}
			if (status == 0xf0 || status == 0xf7) { // sysex
				uint32_t len = readVar();
				pos += len;
				status = 0;
				continue;
			}
			int kind = status & 0xf0;
			int ch = status & 0x0f;
			int dataBytes = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
			if (pos + dataBytes > end)
				break;
			int d1 = data[pos] & 0x7f;
			int d2 = dataBytes == 2 ? (data[pos + 1] & 0x7f) : 0;
			pos += dataBytes;
			if (kind == 0x90 && d2 > 0) {
				close(ch, d1); // a repeated note-on ends the previous one
				openStart[ch][d1] = tick;
				openVelocity[ch][d1] = d2;
			}
			else if (kind == 0x80 || (kind == 0x90 && d2 == 0)) {
				close(ch, d1);
			}
		}
		// Notes never switched off end with their track.
		for (int ch = 0; ch < 16; ch++) {
			for (int key = 0; key < 128; key++)
				close(ch, key);
		}
		pos = end;
	}
	for (auto& notes : song.channels)
		std::stable_sort(notes.begin(), notes.end(), [](const MidiNote& a, const MidiNote& b) { return a.start < b.start; });
	return true;
}

// Builds the sequence. Returns false if nothing on channels 1-4 had any notes. Stops
// quietly at the 2000-step limit (`truncated` tells).
inline bool midiToSequence(const MidiSong& song, Sequence& seq, const MidiImportOptions& opt, bool& truncated) {
	seq.clearAll();
	truncated = false;
	bool any = false;
	auto pulses = [&](long ticks) {
		return (long) std::lround((double) ticks * opt.pulsesPerQuarter / song.ticksPerQuarter);
	};
	for (int t = 0; t < NUM_TRACKS; t++) {
		const std::vector<MidiNote>& notes = song.channels[t];
		if (notes.empty())
			continue;
		any = true;
		int inPattern = 0;
		Step last;
		auto emit = [&](Step s) {
			if (inPattern == opt.stepsPerPattern) {
				seq.appendPattern(t);
				inPattern = 0;
			}
			if (!seq.appendStep(t, s)) {
				truncated = true;
				return false;
			}
			inPattern++;
			return true;
		};
		// A span of `length` pulses, gate `gate`, split into steps of at most 99.
		auto emitSpan = [&](Step s, long length, long gate) {
			while (length > 0) {
				long d = std::min<long>(length, MAX_VALUE);
				s.duration = (uint8_t) d;
				s.gate = (uint8_t) std::max<long>(0, std::min(gate, d));
				if (!emit(s))
					return false;
				length -= d;
				gate -= d;
			}
			return true;
		};
		long cursor = 0;
		bool ok = true;
		for (size_t i = 0; i < notes.size() && ok; i++) {
			long start = pulses(notes[i].start);
			long nextStart = i + 1 < notes.size() ? pulses(notes[i + 1].start) : -1;
			// Of notes starting together, the last one wins.
			if (nextStart == start)
				continue;
			long end = std::max(start + 1, pulses(notes[i].end));
			if (start > cursor) {
				Step rest = last;
				ok = emitSpan(rest, start - cursor, 0);
			}
			if (!ok)
				break;
			Step s;
			s.cvA = (uint8_t) std::max(0, std::min(notes[i].note - opt.noteOffset, MAX_VALUE));
			s.cvB = (uint8_t) std::lround(notes[i].velocity * 96.0 / 127.0);
			long length = nextStart > start ? nextStart - start : end - start;
			ok = emitSpan(s, length, std::min(end - start, length));
			last = s;
			cursor = std::max(cursor, start) + length;
		}
	}
	return any;
}

} // namespace iqs
