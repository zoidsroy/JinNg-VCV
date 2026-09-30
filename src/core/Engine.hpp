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
	// The expander's TRANSITION switch, in its positions' order (0 = handle down).
	enum Transition { TRANSITION_LAST, TRANSITION_FIRST, TRANSITION_USER };
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
	bool expander = false;

	// Parts (docs/SPEC-ER102.md §3). The panel keeps the focused part; which part plays
	// and which waits is playback state, kept here and mirrored to the panel.
	int playingPart = 1;
	int pendingPart = -1;
	int transition = TRANSITION_FIRST;
	bool loopDone[NUM_TRACKS] = {}; // LAST: which tracks finished a loop since activation
	bool activateHigh = false;

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
		if (attached && !expander && live.partsEmpty()) {
			// First contact: the loops set so far become part 1 rather than vanishing.
			adoptLoopsAsPart(live, 1);
			if (mode == MODE_HOLD)
				adoptLoopsAsPart(shadow, 1);
		}
		if (!attached) {
			tr.stopped = false;
			for (int& r : tr.resetTargets)
				r = -1;
		}
		expander = attached;
		panel.expander = attached;
	}

	// The expander's SELECT and ACTIVATE jacks, every sample. A patched SELECT picks the
	// focused part (0.1V per part); a rising ACTIVATE triggers it, and while ACTIVATE
	// stays high the pending part follows SELECT.
	void setPartInputs(bool selectPatched, float selectVolts, bool activate) {
		if (!expander)
			return;
		panel.selectPatched = selectPatched;
		if (selectPatched)
			panel.focusedPart = std::max(0, std::min((int) std::floor(selectVolts * 10.f), NUM_PARTS - 1));
		if (activate && !activateHigh)
			activatePart();
		else if (activate && pendingPart >= 0)
			pendingPart = panel.focusedPart;
		activateHigh = activate;
		syncPanelParts();
	}

	// TRANSITION button / ACTIVATE: the focused part becomes pending. From STOP, or with
	// the switch on USER, it takes over at once.
	void activatePart() {
		pendingPart = panel.focusedPart;
		for (bool& d : loopDone)
			d = false;
		if (playingPart == STOP_PART)
			startPendingPart(true, false);
		else if (transition == TRANSITION_USER)
			startPendingPart(false, false);
		syncPanelParts();
	}

	// The panel shows (and edits relative to) the playing and pending parts.
	void syncPanelParts() {
		panel.playingPart = playingPart;
		panel.pendingPart = pendingPart;
	}

	// After `live` was replaced wholesale (patch load, demo, clear).
	void liveReplaced() {
		pendingPart = -1;
		syncPanelParts();
		tr.stopped = false;
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
			case BUTTON_TRANSITION:
				panel.markChord();
				if (expander)
					activatePart();
				return;
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
		// RESET goes to the playing part's RESET TO steps.
		for (int t = 0; t < NUM_TRACKS; t++)
			tr.resetTargets[t] = expander && playingPart != STOP_PART ? live.tracks[t].parts[playingPart].resetTo : -1;
		tr.process(live, dt, clockEdge, resetEdge, resetHeld);
		if (expander && pendingPart >= 0)
			checkPartTransition();

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
		v.partPlaying = playingPart;
		v.partPending = pendingPart;
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
	// --- Parts ---------------------------------------------------------------------

	static void adoptLoopsAsPart(Sequence& seq, int part) {
		for (Track& t : seq.tracks) {
			t.parts[part].loopStart = (int16_t) t.loopStart;
			t.parts[part].loopEnd = (int16_t) t.loopEnd;
		}
	}

	// FIRST: the pending part starts as soon as any track finishes its loop (wraps
	// around). LAST: once every track that is playing something has done so at least once
	// since the part was triggered. The switch happens as the next step starts.
	void checkPartTransition() {
		bool any = false, all = true;
		for (int t = 0; t < NUM_TRACKS; t++) {
			const Track& track = live.tracks[t];
			if (Playhead::firstPlayable(track) < 0)
				continue;
			if (tr.events[t] & Transport::EVENT_TRACK) {
				loopDone[t] = true;
				any = true;
			}
			all = all && loopDone[t];
		}
		if (transition == TRANSITION_FIRST ? any : all)
			startPendingPart(true, true);
	}

	// The pending part becomes the playing one: every track takes its loop, and with
	// `reset` the tracks it gives a RESET TO step jump there (sounding now when
	// `immediate`, else on the next clock). Tracks without one play on from where they
	// are ("naked loops").
	void startPendingPart(bool reset, bool immediate) {
		playingPart = pendingPart;
		pendingPart = -1;
		syncPanelParts();
		tr.stopped = playingPart == STOP_PART;
		if (tr.stopped)
			return;
		for (int t = 0; t < NUM_TRACKS; t++) {
			const PartPoints p = live.tracks[t].parts[playingPart];
			live.tracks[t].loopStart = p.loopStart;
			live.tracks[t].loopEnd = p.loopEnd;
			if (mode == MODE_HOLD) {
				shadow.tracks[t].loopStart = p.loopStart;
				shadow.tracks[t].loopEnd = p.loopEnd;
			}
			if (reset && p.resetTo >= 0)
				tr.rewindTrack(live, t, immediate, p.resetTo);
		}
	}

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
