// Tests for storage (SPEC-ER102 §7): the 127 snapshot slots with the expander, and MIDI
// file import (src/core/Midi.hpp).

#include "Engine.hpp"
#include "Midi.hpp"
#include "Snapshots.hpp"

#include "testing.hpp"

using namespace iqs;

// --- Snapshot slots -------------------------------------------------------------------

TEST(snapshot_slot_names) {
	CHECK_EQ(SnapshotStore::name(0, true), std::string("--"));
	CHECK_EQ(SnapshotStore::name(-9, true), std::string("t1"));
	CHECK_EQ(SnapshotStore::name(-1, true), std::string("t9"));
	CHECK_EQ(SnapshotStore::name(1, true), std::string("A1"));
	CHECK_EQ(SnapshotStore::name(16, true), std::string("b7"));
	CHECK_EQ(SnapshotStore::name(117, true), std::string("q9"));
	CHECK_EQ(SnapshotStore::name(16, false), std::string("16"));
	// 9 templates + the blank + 117 regular slots = 127.
	CHECK_EQ(SnapshotStore::maxSlot(true) - SnapshotStore::minSlot(true) + 1, 127);
}

TEST(snapshot_slots_with_and_without_the_expander) {
	Engine e;
	e.live = build({{12, 1, 1}});
	e.liveReplaced();
	auto tap = [&](int b) {
		e.press(b);
		e.release(b);
	};
	tap(FOCUS_SNAPSHOT);
	e.turnLeft(-100);
	CHECK_EQ(e.view().snapshot, 0); // alone: the blank is the first
	e.setExpander(true);
	e.turnLeft(-100);
	CHECK_EQ(e.view().snapshot, -9); // templates come before it
	tap(BUTTON_SAVE);
	tap(BUTTON_SAVE); // into t1
	CHECK(e.snapshots.saved(-9));
	e.turnLeft(200);
	CHECK_EQ(e.view().snapshot, 117);
	tap(BUTTON_SAVE);
	tap(BUTTON_SAVE);
	CHECK(e.snapshots.saved(117));
	// Detaching pulls the selection back into range; the saved slots stay.
	e.setExpander(false);
	CHECK_EQ(e.view().snapshot, NUM_STANDALONE_SLOTS);
	CHECK(e.snapshots.saved(-9) && e.snapshots.saved(117));
	// Only saved slots take memory.
	int allocated = 0;
	for (const auto& p : e.snapshots.slots)
		allocated += p != nullptr;
	CHECK_EQ(allocated, 2);
}

TEST(snapshot_template_loads_back) {
	Engine e;
	e.live = build({{12, 1, 1}, {24, 1, 1}});
	e.liveReplaced();
	e.setExpander(true);
	e.snapshotSlot = -3;
	e.press(BUTTON_SAVE);
	e.press(BUTTON_SAVE);
	e.live.clearAll();
	e.press(BUTTON_LOAD);
	e.press(BUTTON_LOAD);
	CHECK_EQ(e.live.tracks[0].numSteps(), 2);
}

// --- MIDI import ------------------------------------------------------------------------

// Writes a Standard MIDI File, to feed the parser.
struct MidiWriter {
	std::vector<uint8_t> bytes;
	std::vector<std::vector<uint8_t>> tracks;

	static void var(std::vector<uint8_t>& out, uint32_t v) {
		uint8_t buf[4];
		int n = 0;
		buf[n++] = v & 0x7f;
		while (v >>= 7)
			buf[n++] = 0x80 | (v & 0x7f);
		while (n--)
			out.push_back(buf[n]);
	}
	std::vector<uint8_t>& track() {
		tracks.push_back(std::vector<uint8_t>());
		return tracks.back();
	}
	static void event(std::vector<uint8_t>& t, uint32_t delta, std::initializer_list<uint8_t> msg) {
		var(t, delta);
		t.insert(t.end(), msg.begin(), msg.end());
	}
	std::vector<uint8_t> build(int format, int division) {
		std::vector<uint8_t> out = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, (uint8_t) format, 0, (uint8_t) tracks.size(),
		                            (uint8_t) (division >> 8), (uint8_t) division};
		for (auto& t : tracks) {
			std::vector<uint8_t> body = t;
			body.insert(body.end(), {0x00, 0xff, 0x2f, 0x00}); // end of track
			uint32_t n = (uint32_t) body.size();
			out.insert(out.end(), {'M', 'T', 'r', 'k', (uint8_t) (n >> 24), (uint8_t) (n >> 16), (uint8_t) (n >> 8), (uint8_t) n});
			out.insert(out.end(), body.begin(), body.end());
		}
		return out;
	}
};

static std::string describe(const Track& t) {
	// "cvA/duration/gate" per step, patterns separated by "|"
	std::string out;
	int i = 0;
	for (size_t p = 0; p < t.patterns.size(); p++) {
		if (p)
			out += "| ";
		for (int k = 0; k < t.patterns[p].length; k++, i++) {
			const Step& s = t.steps[i];
			out += std::to_string(s.cvA) + "/" + std::to_string(s.duration) + "/" + std::to_string(s.gate) + " ";
		}
	}
	if (!out.empty())
		out.pop_back();
	return out;
}

static bool import(const std::vector<uint8_t>& file, Sequence& seq, MidiImportOptions opt = MidiImportOptions()) {
	MidiSong song;
	std::string error;
	if (!parseMidi(file.data(), file.size(), song, error))
		return false;
	bool truncated;
	return midiToSequence(song, seq, opt, truncated);
}

TEST(midi_single_track_melody) {
	// 96 ticks per quarter, 4 pulses per quarter: 24 ticks = 1 pulse.
	MidiWriter w;
	auto& t = w.track();
	MidiWriter::event(t, 0, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}); // a tempo meta event
	MidiWriter::event(t, 48, {0x90, 60, 127});  // leading rest of 2 pulses, then C4
	MidiWriter::event(t, 48, {0x80, 60, 0});    // held 2 pulses
	MidiWriter::event(t, 48, {0x90, 64, 64});   // E4 two pulses later
	MidiWriter::event(t, 96, {64, 0});          // running status, note-on velocity 0 = off
	Sequence seq;
	CHECK(import(w.build(0, 96), seq));
	// rest 2 | C4 (24) 4 pulses gated 2 | E4 (28) 4 pulses gated 4
	CHECK_EQ(describe(seq.tracks[0]), std::string("12/2/0 24/4/2 28/4/4"));
	CHECK_EQ((int) seq.tracks[0].steps[1].cvB, 96);
	CHECK_EQ((int) seq.tracks[0].steps[2].cvB, 48);
	CHECK_EQ(seq.tracks[1].numSteps(), 0);
}

TEST(midi_channels_go_to_tracks_across_smf1_tracks) {
	MidiWriter w;
	auto& t1 = w.track();
	MidiWriter::event(t1, 0, {0x90, 48, 100}); // channel 1
	MidiWriter::event(t1, 24, {0x80, 48, 0});
	auto& t2 = w.track();
	MidiWriter::event(t2, 0, {0x93, 72, 100}); // channel 4
	MidiWriter::event(t2, 48, {0x83, 72, 0});
	MidiWriter::event(t2, 0, {0x94, 50, 100}); // channel 5: ignored
	MidiWriter::event(t2, 24, {0x84, 50, 0});
	Sequence seq;
	CHECK(import(w.build(1, 96), seq));
	CHECK_EQ(describe(seq.tracks[0]), std::string("12/1/1"));
	CHECK_EQ(describe(seq.tracks[3]), std::string("36/2/2"));
	CHECK_EQ(seq.totalSteps(), 2);
}

TEST(midi_overlaps_long_notes_and_patterns) {
	MidiWriter w;
	auto& t = w.track();
	// A starts, B starts before A ends (A is cut), B lasts 150 pulses (split and tied).
	MidiWriter::event(t, 0, {0x90, 40, 100});
	MidiWriter::event(t, 48, {0x90, 41, 100});
	MidiWriter::event(t, 24, {0x80, 40, 0});
	MidiWriter::event(t, 150 * 24 - 24, {0x80, 41, 0});
	Sequence seq;
	CHECK(import(w.build(0, 96), seq));
	CHECK_EQ(describe(seq.tracks[0]), std::string("4/2/2 5/99/99 5/51/51"));
	// Patterns are cut every 16 steps.
	MidiWriter w2;
	auto& u = w2.track();
	for (int i = 0; i < 20; i++) {
		MidiWriter::event(u, 0, {0x90, 60, 100});
		MidiWriter::event(u, 24, {0x80, 60, 0});
	}
	Sequence seq2;
	CHECK(import(w2.build(0, 96), seq2));
	CHECK_EQ(seq2.tracks[0].patterns.size(), (size_t) 2);
	CHECK_EQ((int) seq2.tracks[0].patterns[0].length, 16);
	CHECK_EQ((int) seq2.tracks[0].patterns[1].length, 4);
}

TEST(midi_rejects_what_it_cannot_read) {
	MidiSong song;
	std::string error;
	const uint8_t notMidi[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	CHECK(!parseMidi(notMidi, sizeof(notMidi), song, error));
	MidiWriter w;
	w.track();
	std::vector<uint8_t> smpte = w.build(0, 96);
	smpte[12] = 0xe7; // negative division: SMPTE
	CHECK(!parseMidi(smpte.data(), smpte.size(), song, error));
	std::vector<uint8_t> type2 = w.build(2, 96);
	CHECK(!parseMidi(type2.data(), type2.size(), song, error));
	// A truncated file does not crash; it just yields what was there.
	MidiWriter w3;
	auto& t = w3.track();
	MidiWriter::event(t, 0, {0x90, 60, 100});
	MidiWriter::event(t, 24, {0x80, 60, 0});
	std::vector<uint8_t> cut = w3.build(0, 96);
	cut.resize(cut.size() - 6);
	MidiSong partial;
	parseMidi(cut.data(), cut.size(), partial, error);
	// No notes on channels 1-4 is reported.
	Sequence seq;
	bool truncated;
	MidiSong empty;
	CHECK(!midiToSequence(empty, seq, MidiImportOptions(), truncated));
}

TEST(midi_stops_at_the_step_limit) {
	MidiWriter w;
	for (int ch = 0; ch < 4; ch++) {
		auto& t = w.track();
		for (int i = 0; i < 600; i++) {
			MidiWriter::event(t, 0, {(uint8_t) (0x90 | ch), 60, 100});
			MidiWriter::event(t, 24, {(uint8_t) (0x80 | ch), 60, 0});
		}
	}
	MidiSong song;
	std::string error;
	std::vector<uint8_t> file = w.build(1, 96);
	CHECK(parseMidi(file.data(), file.size(), song, error));
	Sequence seq;
	bool truncated = false;
	CHECK(midiToSequence(song, seq, MidiImportOptions(), truncated));
	CHECK(truncated);
	CHECK_EQ(seq.totalSteps(), MAX_TOTAL_STEPS);
}
