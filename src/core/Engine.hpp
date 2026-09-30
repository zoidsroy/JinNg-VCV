#pragma once
// The whole sequencer: sequence, transport and panel, plus what spans them (spec §4.3,
// §4.7, §3.7):
//  - the MODE switch. EDIT edits the playing sequence; FOLLOW shows (and scrubs) the play
//    cursor; HOLD edits a shadow copy that COMMIT writes back, either immediately or
//    quantized to the end of the current step / pattern / track.
//  - 16 snapshots plus the blank one ("--"), with LOAD/SAVE asking for confirmation.
//  - RESET quantized to the end of the current step / pattern / track by holding that
//    focus button.
// Plain C++ with no Rack dependency; the module feeds it edges and reads outputs back.

#include "Panel.hpp"
#include "Transport.hpp"
#include <array>

namespace iqs {

static constexpr int NUM_SNAPSHOTS = 16;

struct Engine {
	enum Quantize { Q_NONE, Q_STEP, Q_PATTERN, Q_TRACK };
	enum SnapshotAction { SNAP_NONE, SNAP_LOAD, SNAP_SAVE };

	Sequence live;
	Sequence shadow; // HOLD mode edits this
	Transport tr;
	Transport shadowTr; // play cursors the shadow's edits shift; never clocked
	Panel panel;

	std::array<Sequence, NUM_SNAPSHOTS> snapshots;
	std::array<bool, NUM_SNAPSHOTS> snapshotUsed = {};
	int snapshotSlot = 1; // 0 is the blank snapshot "--", 1..16 the saved ones
	int snapshotArmed = SNAP_NONE;

	// Bumped by anything that may have changed the edited sequence, so the host can tell
	// when to take an undo snapshot. Panel activity that changes nothing bumps it too;
	// the host compares contents before recording a step.
	uint32_t editGeneration = 0;

	int mode = MODE_EDIT;
	int pendingCommit = Q_NONE;
	int commitTrack = 0;
	int pendingReset = Q_NONE;
	int resetTrack = 0;

	// --- The sequence the panel works on ---------------------------------------

	Sequence& editSeq() {
		return mode == MODE_HOLD ? shadow : live;
	}
	Transport& editTr() {
		return mode == MODE_HOLD ? shadowTr : tr;
	}
	const Sequence& editSeq() const {
		return mode == MODE_HOLD ? shadow : live;
	}

	// Whether a Sequencer Controller expander is attached. Its buttons arrive through the
	// same press()/release(), and MATH switches to the five-operation transform.
	void setExpander(bool attached) {
		panel.expander = attached;
	}

	// After `live` was replaced wholesale (patch load, demo, clear).
	void liveReplaced() {
		tr.rewind(live, false);
		if (mode == MODE_HOLD)
			enterHold();
		panel.normalizeCursors(editSeq());
	}

	void setMode(int m) {
		if (m == mode)
			return;
		int old = mode;
		mode = m;
		panel.mode = m;
		if (m == MODE_HOLD) {
			enterHold();
		}
		else if (old == MODE_HOLD) {
			// Leaving HOLD without COMMIT throws the pending edits away.
			pendingCommit = Q_NONE;
			panel.normalizeCursors(live);
		}
	}

	// --- Panel events ----------------------------------------------------------

	// Undo/redo: puts back an earlier state of the edited sequence (live, or the shadow in
	// HOLD). Play cursors stay where they are when they still fit; edit cursors keep their
	// place where possible.
	void restoreEdited(const Sequence& s) {
		Sequence& target = editSeq();
		target = s;
		Transport& t = editTr();
		for (int i = 0; i < NUM_TRACKS; i++)
			t.playheads[i].validate(target.tracks[i]);
		panel.normalizeCursors(target);
	}

	void press(int b) {
		editGeneration++;
		// LOAD / SAVE ask for a second press ("Abrt" flashes meanwhile); anything else
		// cancels.
		if (snapshotArmed != SNAP_NONE) {
			bool confirm = (snapshotArmed == SNAP_LOAD && b == BUTTON_LOAD) || (snapshotArmed == SNAP_SAVE && b == BUTTON_SAVE);
			int action = snapshotArmed;
			snapshotArmed = SNAP_NONE;
			panel.markChord();
			if (confirm)
				action == SNAP_LOAD ? loadSnapshot() : saveSnapshot();
			return;
		}
		switch (b) {
			case BUTTON_COMMIT:
				panel.markChord();
				pressCommit();
				return;
			case BUTTON_LOAD:
				panel.markChord();
				snapshotArmed = SNAP_LOAD;
				return;
			case BUTTON_SAVE:
				panel.markChord();
				// The blank snapshot cannot be overwritten.
				if (snapshotSlot > 0)
					snapshotArmed = SNAP_SAVE;
				return;
			default:
				panel.press(editSeq(), editTr(), b);
		}
	}

	void release(int b) {
		editGeneration++;
		panel.release(editSeq(), editTr(), b);
	}

	void turnLeft(int d) {
		if (d == 0)
			return;
		editGeneration++;
		if (panel.leftFocus == FOCUS_SNAPSHOT && !panel.mathScreen() && !panel.optionsScreen) {
			panel.markChord();
			snapshotSlot = std::max(0, std::min(snapshotSlot + d, NUM_SNAPSHOTS));
			return;
		}
		panel.turnLeft(editSeq(), editTr(), d);
	}

	void turnRight(int d) {
		if (d == 0)
			return;
		editGeneration++;
		panel.turnRight(editSeq(), d);
	}

	// The RESET button. Holding TRACK, PATTERN or STEP while pressing it waits for the
	// focused track to reach the end of its current track, pattern or step. Returns
	// whether the reset was quantized (so it must not also act as a plain reset).
	bool pressResetButton() {
		int q = heldQuantizer();
		if (q == Q_NONE)
			return false;
		panel.markChord();
		pendingReset = q;
		resetTrack = panel.track;
		return true;
	}

	// --- Audio ------------------------------------------------------------------

	void process(float dt, bool clockEdge, bool resetEdge, bool resetHeld) {
		panel.paused = tr.paused;
		tr.process(live, dt, clockEdge, resetEdge, resetHeld);

		if (pendingCommit != Q_NONE && reached(pendingCommit, commitTrack))
			commit();
		if (pendingReset != Q_NONE && reached(pendingReset, resetTrack)) {
			pendingReset = Q_NONE;
			// The boundary just started the next step; the first step takes its place.
			tr.rewind(live, true);
		}
		if (mode == MODE_FOLLOW)
			panel.follow(live, tr);
		panel.tick(dt);
	}

	float cvA(int t) const { return tr.cvA(live, t); }
	float cvB(int t) const { return tr.cvB(live, t); }
	bool gate(int t) const { return tr.gate(live, t); }

	PanelView view() const {
		PanelView v = panel.view(editSeq());
		v.snapshot = snapshotSlot;
		if (snapshotArmed != SNAP_NONE)
			v.message = panel.blinkPhase() ? "Abrt" : "";
		return v;
	}

	// COMMIT LED: blinks while a quantized commit waits.
	Led commitLed() const {
		return pendingCommit != Q_NONE ? LED_BLINK : LED_OFF;
	}

private:
	void enterHold() {
		shadow = live;
		shadowTr = tr;
		pendingCommit = Q_NONE;
	}

	int heldQuantizer() const {
		if (panel.held[FOCUS_TRACK])
			return Q_TRACK;
		if (panel.held[FOCUS_PATTERN])
			return Q_PATTERN;
		if (panel.held[FOCUS_STEP])
			return Q_STEP;
		return Q_NONE;
	}

	bool reached(int q, int track) const {
		uint8_t e = tr.events[track];
		switch (q) {
			case Q_STEP: return e & Transport::EVENT_STEP;
			case Q_PATTERN: return e & Transport::EVENT_PATTERN;
			case Q_TRACK: return e & Transport::EVENT_TRACK;
			default: return false;
		}
	}

	// COMMIT in HOLD: with TRACK, PATTERN or STEP focused it waits for the focused track
	// to finish its current track, pattern or step; pressed again (or with INDEX or
	// SNAPSHOT focused) it commits at once.
	void pressCommit() {
		if (mode != MODE_HOLD)
			return;
		int q = Q_NONE;
		switch (panel.leftFocus) {
			case FOCUS_TRACK: q = Q_TRACK; break;
			case FOCUS_PATTERN: q = Q_PATTERN; break;
			case FOCUS_STEP: q = Q_STEP; break;
			default: break;
		}
		if (pendingCommit != Q_NONE || q == Q_NONE) {
			commit();
			return;
		}
		pendingCommit = q;
		commitTrack = panel.track;
	}

	void commit() {
		editGeneration++;
		pendingCommit = Q_NONE;
		live = shadow;
		for (int t = 0; t < NUM_TRACKS; t++)
			tr.playheads[t].validate(live.tracks[t]);
	}

	// Loading replaces everything (all four tracks with their tables, options and math)
	// and rewinds every cursor. In HOLD it loads into the shadow, to be cued with COMMIT.
	void loadSnapshot() {
		Sequence& target = editSeq();
		if (snapshotSlot == 0 || !snapshotUsed[snapshotSlot - 1])
			target.clearAll();
		else
			target = snapshots[snapshotSlot - 1];
		if (mode == MODE_HOLD) {
			shadowTr.rewind(shadow, false);
		}
		else {
			tr.rewind(live, false);
		}
		panel.rewindCursors(target);
	}

	void saveSnapshot() {
		if (snapshotSlot == 0)
			return;
		snapshots[snapshotSlot - 1] = editSeq();
		snapshotUsed[snapshotSlot - 1] = true;
	}
};

} // namespace iqs
