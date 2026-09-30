#pragma once
// Recording (docs/SPEC-ER102.md §5): the expander's ARM / PUNCH controls and its six
// inputs, in three modes.
//   ALTER:     each step an armed track reaches is rewritten from the inputs as it starts.
//   STEP:      D-1 inserts and D-2 deletes steps at the edit cursor of armed tracks.
//   REAL-TIME: a performance on A-1/A-2/AD-1 is measured against the track's clock and
//              written as new steps; the inputs pass straight to the outputs meanwhile.
// Only patched inputs are used; an unpatched one leaves its step parameter alone.

#include "Editor.hpp"
#include "Panel.hpp"
#include "Transport.hpp"

namespace iqs {

// The expander's RECORD MODE switch, in its positions' order (0 = handle down).
enum RecordMode { RECORD_REALTIME, RECORD_STEP, RECORD_ALTER };

struct RecordInputs {
	float a1 = 0.f, a2 = 0.f, ad1 = 0.f, ad2 = 0.f;
	bool a1Patched = false, a2Patched = false, ad1Patched = false, ad2Patched = false;
	bool d1 = false, d2 = false;
	bool punchGate = false;
};

// The real-time configuration screen shown on arming (manual p.19).
struct RealtimeConfig {
	bool cvATrigger = true;  // a change of CV-A (after quantizing) starts a new step
	bool cvBTrigger = false;
	int durationGrid = 1;    // DURATION rounded to multiples of this
	int gateGrid = 1;        // GATE likewise
	int focus = FOCUS_TRACK; // where a take goes: TRACK end, after the PATTERN, after the STEP
	bool waitForFirstNote = true;
};

struct Recorder {
	static constexpr float GATE_THRESHOLD = 1.5f;

	int mode = RECORD_STEP;
	bool armed[NUM_TRACKS] = {};
	bool punchLatch = false; // toggled by the PUNCH button
	bool configScreen = false;
	RealtimeConfig config;
	RecordInputs in;

	// Real-time: one take per armed track.
	struct Take {
		bool started = false;
		int pattern = -1;
		int step = -1;        // the step being recorded
		int nextPos = 0;      // where the next one goes
		double start = 0.0;   // in the track's pulses
		double gateOff = 0.0;
		bool gateOpen = false;
		bool cvSampled = false;
		int lastIndexA = -1, lastIndexB = -1;
	};
	Take takes[NUM_TRACKS];
	bool noteSeen = false; // rehearsal: pass-thru before punching in
	bool prevGate = false, prevD1 = false, prevD2 = false, prevPunchGate = false;
	// Step mode: the steps D-1 inserted, which follow the inputs while D-1 is held.
	int heldInsert[NUM_TRACKS] = {-1, -1, -1, -1};

	bool punched() const {
		return punchLatch || in.punchGate;
	}
	bool anyArmed() const {
		for (bool a : armed) {
			if (a)
				return true;
		}
		return false;
	}
	bool gateHigh() const {
		return in.ad1Patched && in.ad1 > GATE_THRESHOLD;
	}

	// Real-time pass-thru (manual p.20): armed and playing, and either punched in or
	// rehearsing (a note was played while punched out).
	bool passThru(int t, bool paused) const {
		return mode == RECORD_REALTIME && armed[t] && !paused && (punched() || noteSeen);
	}

	// REC LED: a take is running (real-time), or remote control is on (step/alter).
	bool recording(bool paused) const {
		if (paused || !anyArmed())
			return false;
		if (mode == RECORD_REALTIME) {
			for (const Take& tk : takes) {
				if (tk.started)
					return true;
			}
			return false;
		}
		return punched();
	}

	static int quantize(const VoltageTable& table, float v) {
		return table.closestIndex(std::max(0.f, std::min(v, MAX_VOLTAGE)));
	}
	// GATE / DURATION from a voltage: floor(V * 20), 0..99 (manual p.21).
	static uint8_t fromVolts(float v) {
		return (uint8_t) std::max(0, std::min((int) std::floor(v * 20.f), MAX_VALUE));
	}

	// Writes the patched inputs into a step (alter and step modes).
	void applyInputs(const Track& t, Step& s) const {
		if (in.a1Patched)
			s.cvA = (uint8_t) quantize(t.tableA, in.a1);
		if (in.a2Patched)
			s.cvB = (uint8_t) quantize(t.tableB, in.a2);
		if (in.ad1Patched)
			s.gate = fromVolts(in.ad1);
		if (in.ad2Patched)
			s.duration = fromVolts(in.ad2);
	}

	// --- Buttons --------------------------------------------------------------------

	// ARM on the selected track: arms it (opening the real-time configuration screen in
	// real-time mode); pressed while that screen is up it closes it; otherwise disarms.
	void pressArm(Sequence& live, Transport& tr, int track, uint32_t& generation) {
		if (configScreen) {
			configScreen = false;
			return;
		}
		if (armed[track]) {
			finishTake(live, tr, track, generation);
			armed[track] = false;
			return;
		}
		armed[track] = true;
		if (mode == RECORD_REALTIME)
			configScreen = true;
	}

	void pressPunch(Sequence& live, Transport& tr, uint32_t& generation) {
		punchLatch = !punchLatch;
		if (!punched())
			punchOut(live, tr, generation);
	}

	// RESET, PAUSE and punching out end takes and pass-thru.
	void punchOut(Sequence& live, Transport& tr, uint32_t& generation) {
		punchLatch = false;
		noteSeen = false;
		for (int t = 0; t < NUM_TRACKS; t++)
			finishTake(live, tr, t, generation);
	}

	// --- Every sample -----------------------------------------------------------------

	void process(Sequence& live, Transport& tr, Sequence& edited, Transport& editTr, Panel& panel,
	             uint32_t& generation) {
		bool gate = gateHigh();
		bool gateRose = gate && !prevGate;
		bool gateFell = !gate && prevGate;
		bool d1Rose = in.d1 && !prevD1;
		bool d1Fell = !in.d1 && prevD1;
		bool d2Rose = in.d2 && !prevD2;
		bool punchGateFell = !in.punchGate && prevPunchGate;
		prevGate = gate;
		prevD1 = in.d1;
		prevD2 = in.d2;
		prevPunchGate = in.punchGate;

		if (punchGateFell && !punched())
			punchOut(live, tr, generation);
		if (tr.paused) {
			if (noteSeen || anyTakeStarted())
				punchOut(live, tr, generation);
			return;
		}

		switch (mode) {
			case RECORD_ALTER:
				if (punched())
					alter(live, tr, generation);
				break;
			case RECORD_STEP:
				if (punched())
					stepRecord(edited, editTr, panel, d1Rose, d1Fell, d2Rose, generation);
				break;
			case RECORD_REALTIME:
				if (gateRose && !punched() && anyArmed())
					noteSeen = true; // rehearsing
				if (punched())
					realtime(live, tr, gate, gateRose, gateFell, generation);
				break;
		}
	}

private:
	bool anyTakeStarted() const {
		for (const Take& tk : takes) {
			if (tk.started)
				return true;
		}
		return false;
	}

	// --- ALTER ------------------------------------------------------------------------

	// A step is rewritten as the play cursor reaches it. A single looped step is
	// rewritten continuously, since it is also the next one to play.
	void alter(Sequence& live, Transport& tr, uint32_t& generation) {
		for (int t = 0; t < NUM_TRACKS; t++) {
			if (!armed[t])
				continue;
			Track& track = live.tracks[t];
			const Playhead& ph = tr.playheads[t];
			if (ph.armed() || ph.step >= track.numSteps())
				continue;
			bool single = track.loopStart >= 0 && track.loopStart == track.loopEnd && track.loopStart == ph.step;
			if (!(tr.events[t] & Transport::EVENT_STEP) && !single)
				continue;
			Step before = track.steps[ph.step];
			applyInputs(track, track.steps[ph.step]);
			const Step& after = track.steps[ph.step];
			if (after.cvA != before.cvA || after.cvB != before.cvB || after.gate != before.gate || after.duration != before.duration)
				generation++;
		}
	}

	// --- STEP ------------------------------------------------------------------------

	// D-1 inserts a step after the edit cursor on every armed track (its parameters follow
	// the inputs while D-1 is held); D-2 deletes the step at the cursor.
	void stepRecord(Sequence& seq, Transport& tr, Panel& panel, bool d1Rose, bool d1Fell, bool d2Rose,
	                uint32_t& generation) {
		for (int t = 0; t < NUM_TRACKS; t++) {
			if (!armed[t])
				continue;
			Track& track = seq.tracks[t];
			EditCursor& c = panel.cursors[t];
			if (d1Rose) {
				if (c.pattern < 0) {
					if (!edit::insertEmptyPattern(seq, t, 0, tr.playheads[t]))
						continue;
					c.pattern = 0;
					c.step = -1;
				}
				Step s = c.step >= 0 ? track.steps[c.step] : track.defaultStep();
				applyInputs(track, s);
				int pos = c.step >= 0 ? c.step + 1 : edit::patternStart(track, c.pattern);
				if (edit::insertSteps(seq, t, c.pattern, pos, &s, 1, tr.playheads[t])) {
					c.step = pos;
					heldInsert[t] = pos;
					generation++;
				}
			}
			else if (in.d1 && heldInsert[t] >= 0 && heldInsert[t] < track.numSteps()) {
				Step& s = track.steps[heldInsert[t]];
				Step before = s;
				applyInputs(track, s);
				if (s.cvA != before.cvA || s.cvB != before.cvB || s.gate != before.gate || s.duration != before.duration)
					generation++;
			}
			if (d1Fell)
				heldInsert[t] = -1;
			if (d2Rose && c.step >= 0) {
				int pos = c.step;
				int p = c.pattern;
				edit::removeStep(seq, t, pos, tr.playheads[t]);
				int end = edit::patternEnd(track, p);
				int first = edit::patternStart(track, p);
				c.step = pos < end ? pos : (end > first ? end - 1 : -1);
				heldInsert[t] = -1;
				generation++;
			}
		}
	}

	// --- REAL-TIME --------------------------------------------------------------------

	static int roundTo(double pulses, int grid) {
		grid = std::max(1, grid);
		return (int) std::lround(pulses / grid) * grid;
	}

	void realtime(Sequence& live, Transport& tr, bool gate, bool gateRose, bool gateFell, uint32_t& generation) {
		for (int t = 0; t < NUM_TRACKS; t++) {
			if (!armed[t])
				continue;
			Take& tk = takes[t];
			Track& track = live.tracks[t];
			double now = tr.pulseTime(t);

			int indexA = in.a1Patched ? quantize(track.tableA, in.a1) : -1;
			int indexB = in.a2Patched ? quantize(track.tableB, in.a2) : -1;
			// A new step on a new note, or (legato) when a triggering CV changes while the
			// note is held (or when there is no gate input at all).
			bool cvChanged = (config.cvATrigger && indexA >= 0 && tk.lastIndexA >= 0 && indexA != tk.lastIndexA) ||
			                 (config.cvBTrigger && indexB >= 0 && tk.lastIndexB >= 0 && indexB != tk.lastIndexB);
			bool legato = gate || !in.ad1Patched;
			tk.lastIndexA = indexA;
			tk.lastIndexB = indexB;

			if (!tk.started) {
				if (gateRose || !config.waitForFirstNote)
					startTake(live, tr, t, now);
				if (!tk.started)
					continue;
				newStep(live, tr, t, now, generation);
				if (!gateRose)
					tk.gateOpen = false; // not waiting for a note: the take opens with a rest
			}
			else if (gateRose || (cvChanged && legato)) {
				newStep(live, tr, t, now, generation);
			}
			if (gateFell && tk.gateOpen) {
				tk.gateOpen = false;
				tk.gateOff = now;
			}
			// The CVs are read half a pulse into the step, once they have settled.
			if (!tk.cvSampled && now - tk.start >= 0.5 && tk.step >= 0 && tk.step < track.numSteps()) {
				Step& s = track.steps[tk.step];
				if (indexA >= 0)
					s.cvA = (uint8_t) indexA;
				if (indexB >= 0)
					s.cvB = (uint8_t) indexB;
				tk.cvSampled = true;
				generation++;
			}
		}
	}

	// Where a take goes (the configuration screen's focus): a new pattern at the end of
	// the track, a new pattern after the playing one, or after the playing step.
	void startTake(Sequence& live, Transport& tr, int t, double now) {
		Take& tk = takes[t];
		Track& track = live.tracks[t];
		Playhead& ph = tr.playheads[t];
		int playingPattern = -1, s;
		if (ph.step < track.numSteps())
			track.locate(ph.step, playingPattern, s);
		int np = edit::numPatterns(track);
		if (config.focus == FOCUS_STEP && playingPattern >= 0) {
			tk.pattern = playingPattern;
			tk.nextPos = ph.step + 1;
		}
		else {
			int at = config.focus == FOCUS_PATTERN && playingPattern >= 0 ? playingPattern + 1 : np;
			if (!edit::insertEmptyPattern(live, t, at, ph))
				return;
			tk.pattern = at;
			tk.nextPos = edit::patternStart(track, at);
		}
		tk.started = true;
		tk.step = -1;
		tk.start = now;
	}

	// Closes the step being recorded at `now` and opens the next one.
	void newStep(Sequence& live, Transport& tr, int t, double now, uint32_t& generation) {
		Take& tk = takes[t];
		Track& track = live.tracks[t];
		closeStep(live, t, now);
		Step s = tk.step >= 0 && tk.step < track.numSteps() ? track.steps[tk.step] : track.defaultStep();
		s.duration = 1; // placeholders until the step closes
		s.gate = 1;
		// A full pattern continues in a new one right after it.
		if (track.patterns[tk.pattern].length >= MAX_STEPS_PER_PATTERN) {
			if (!edit::insertEmptyPattern(live, t, tk.pattern + 1, tr.playheads[t])) {
				stopTake(t);
				return;
			}
			tk.pattern++;
			tk.nextPos = edit::patternStart(track, tk.pattern);
		}
		if (!edit::insertSteps(live, t, tk.pattern, tk.nextPos, &s, 1, tr.playheads[t])) {
			stopTake(t); // the 2000-step limit
			return;
		}
		tk.step = tk.nextPos;
		tk.nextPos++;
		tk.start = now;
		tk.gateOpen = true;
		tk.cvSampled = false;
		generation++;
	}

	// DURATION: the time until `now`, rounded to the grid (at least 1, at most 99). GATE:
	// the time the note was held (all of it if still held), rounded to its grid.
	void closeStep(Sequence& live, int t, double now) {
		Take& tk = takes[t];
		Track& track = live.tracks[t];
		if (tk.step < 0 || tk.step >= track.numSteps())
			return;
		Step& s = track.steps[tk.step];
		int duration = std::max(1, std::min(roundTo(now - tk.start, config.durationGrid), MAX_VALUE));
		int gate = tk.gateOpen ? duration : roundTo(tk.gateOff - tk.start, config.gateGrid);
		s.duration = (uint8_t) duration;
		s.gate = (uint8_t) std::max(0, std::min(gate, MAX_VALUE));
	}

	void finishTake(Sequence& live, Transport& tr, int t, uint32_t& generation) {
		Take& tk = takes[t];
		if (tk.started) {
			closeStep(live, t, tr.pulseTime(t));
			generation++;
		}
		stopTake(t);
	}

	void stopTake(int t) {
		takes[t] = Take();
	}
};

} // namespace iqs
