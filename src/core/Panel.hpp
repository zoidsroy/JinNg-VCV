#pragma once
// Front-panel behaviour (docs/SPEC.md §4): focus, the two encoders, and the buttons that
// edit a sequence. It receives press/release/turn events and edits the sequence it is
// handed directly, so it must run on the audio thread, which is the only place sequences
// change.
//
// The panel edits whatever sequence the host passes in; in HOLD mode that is a shadow
// copy (see Engine). COMMIT, LOAD/SAVE and RESET are handled by the Engine too.

#include "Editor.hpp"
#include "Groups.hpp"
#include "Math.hpp"
#include "Transform.hpp"
#include "Transport.hpp"
#include "VoltageTables.hpp"

namespace iqs {

// Focus targets. INDEX..SNAPSHOT (and the expander's PART and GROUP) are steered by the
// left encoder, the others by the right.
enum Focus {
	FOCUS_INDEX,
	FOCUS_TRACK,
	FOCUS_PATTERN,
	FOCUS_STEP,
	FOCUS_SNAPSHOT,
	FOCUS_VOLTAGE,
	FOCUS_CV_A,
	FOCUS_CV_B,
	FOCUS_DURATION,
	FOCUS_GATE,
	// On the expander.
	FOCUS_PART,
	FOCUS_GROUP,
	FOCUS_GROUP_MODIFIER,
	FOCUS_LEN
};
// The sequencer's own focus buttons (the expander adds the rest).
static constexpr int NUM_SEQUENCER_FOCUS = FOCUS_PART;

inline bool isLeftFocus(int f) {
	return f < FOCUS_VOLTAGE || f == FOCUS_PART || f == FOCUS_GROUP;
}

// Panel buttons. The focus buttons share their Focus value.
enum Button {
	BUTTON_INSERT = FOCUS_LEN,
	BUTTON_DELETE,
	BUTTON_MATH,
	BUTTON_COPY,
	BUTTON_LOAD,
	BUTTON_SAVE,
	BUTTON_LOOP_START,
	BUTTON_LOOP_END,
	BUTTON_SMOOTH,
	BUTTON_COMMIT,
	// On the expander.
	BUTTON_RESET_TO,
	BUTTON_TRANSITION,
	BUTTON_DESELECT,
	BUTTON_INVERT,
	BUTTON_ROTATE,
	BUTTON_ARM,
	BUTTON_PUNCH,
	BUTTON_LEN
};

enum Mode { MODE_EDIT, MODE_FOLLOW, MODE_HOLD };

enum InsertMode { INSERT_AFTER, INSERT_SPLIT, INSERT_BEFORE };

enum Table { TABLE_A, TABLE_B, TABLE_REF };

// The expander's GROUP MODIFIERS type switch, in its positions' order (0 = handle down).
enum ModifierType { MODIFIER_LOW, MODIFIER_SLOPE, MODIFIER_HIGH };

// LED state for indicators that can blink.
enum Led { LED_OFF, LED_ON, LED_BLINK };

// Seven-segment bits (a, g, d) for the part overview on the VOLTAGE display.
static constexpr uint8_t SEG_TOP = 1 << 0;
static constexpr uint8_t SEG_BOTTOM = 1 << 3;
static constexpr uint8_t SEG_MIDDLE = 1 << 6;

struct Clipboard {
	enum Kind { NONE, STEPS, PATTERNS, TRACK, TABLE, PART, GROUP };
	Kind kind = NONE;
	std::vector<Step> steps;
	std::vector<Pattern> patterns;
	Track track;
	VoltageTable table;
	std::array<PartPoints, NUM_TRACKS> part;
	std::vector<uint8_t> selection; // a group's members on one track, step by step

	Clipboard() {
		steps.reserve(MAX_TOTAL_STEPS);
		patterns.reserve(MAX_PATTERNS);
		selection.reserve(MAX_TOTAL_STEPS);
	}
	void clear() {
		kind = NONE;
		steps.clear();
		patterns.clear();
		selection.clear();
	}
};

// Where the edit cursor is on one track. `step` is a flat step index, or -1 when the
// cursor sits on an empty pattern (or the track has no patterns at all).
struct EditCursor {
	int pattern = -1;
	int step = -1;
};

// Everything the displays need, copied out for the UI thread. `message` points to a
// string literal (or is null), so the struct can be copied without allocating.
struct PanelView {
	int leftFocus = FOCUS_TRACK;
	int rightFocus = FOCUS_CV_A;
	int track = 0;
	int pattern = -1;       // 0-based, -1 when the track has no patterns
	int stepInPattern = -1; // 0-based, -1 when the cursor has no step
	Step step;
	int index = 0;
	float voltage = 0.f;
	bool noteDisplay = true; // VOLTAGE as a note (Nt) or a number (Nr)
	int grain = GRAIN_FINE;  // voltage editing granularity
	const char* message = nullptr;
	bool optionsScreen = false;
	TrackOptions options;
	bool mathScreen = false;
	int mathRow = MATH_CV_A;
	std::array<MathOp, MATH_PARAMS> math;
	bool expander = false;        // the MATH screen shows the five-operation transform
	TransformSet transform;
	int transformOp[MATH_PARAMS]; // the operation shown for each parameter
	int snapshot = 0; // filled in by the Engine: 0 is the blank snapshot "--"
	// Parts (expander). The overview is one segment mask per track: top bar RESET TO,
	// middle LOOP START, bottom LOOP END set.
	int partFocused = 1;
	int partPlaying = 1;
	int partPending = -1;
	uint8_t partOverview[NUM_TRACKS] = {};
	bool blink = false; // the blink phase, for displays that blink
	// Groups (expander).
	int groupFocused = 0;
	bool groupHasMembers = false;
	int groupCount = 0; // members on the selected track
	bool groupMember = false; // the cursor's step is in the focused group
	bool euclid = false; // choosing a Euclidean mask E(euclidN, euclidM)
	int euclidN = 0;
	int euclidM = 0;
	bool slopeScreen = false; // editing a channel's slopes
	float slopes[MATH_PARAMS] = {};
	int slopeParam = MATH_CV_A;
	// Recording (expander): the real-time configuration screen.
	bool recordConfig = false;
	bool recordCvATrigger = true, recordCvBTrigger = false;
	int recordDurationGrid = 1, recordGateGrid = 1;
};

struct Panel {
	static constexpr float MESSAGE_S = 1.f;
	static constexpr float BLINK_S = 0.5f;

	int leftFocus = FOCUS_TRACK;
	int rightFocus = FOCUS_CV_A;
	int track = 0;
	int table = TABLE_A;
	// Set by the host every sample.
	int mode = MODE_EDIT;
	bool paused = false;
	bool expander = false; // a SEQ-101ext is attached

	EditCursor cursors[NUM_TRACKS];
	Clipboard clip;

	bool held[BUTTON_LEN] = {};
	// A press on an already focused button acts on release, and only if nothing else
	// happened meanwhile, so the button can also be held as a modifier.
	bool focusPressPending[FOCUS_LEN] = {};
	int insertMode = INSERT_AFTER;
	bool copyDragged = false;
	int copyAnchor = -1;
	bool deleteArmed = false;
	bool optionsScreen = false;
	bool mathPinned = false;
	int mathRow = MATH_CV_A;
	// With the expander: which of the five operations each parameter's display shows.
	int transformOp[MATH_PARAMS] = {OP_ADD, OP_ADD, OP_ADD, OP_ADD};
	// INVERT acts on release unless it was held as a modifier (for ROTATE or MATH).
	bool invertPending = false;
	// Parts (expander). The Engine owns playing/pending and mirrors them here.
	int focusedPart = 1;
	int playingPart = 1;
	int pendingPart = -1;
	bool selectPatched = false; // the SELECT jack picks the part, not the LEFT knob
	// Groups (expander).
	int focusedGroup = 0;
	bool euclidActive = false;
	int euclidN = 0, euclidM = 1, euclidFirst = 0, euclidLast = -1;
	bool modifierMode = false; // the GROUP MODIFIERS screen
	int modifierType = MODIFIER_SLOPE; // set by the host from the switches
	int modifierChannel = 0;           // 0 X, 1 Y, 2 Z
	Rng rng;
	int browseIndex = 0; // the voltage table entry INDEX points at
	int refTable = 0;    // the reference table picked while TABLE is on REF
	RefTables ownRefs;
	RefTables* refs = &ownRefs; // the host points this at the shared user tables
	const char* message = nullptr;
	float messageTime = 0.f;
	float time = 0.f;

	// --- Events --------------------------------------------------------------

	void press(Sequence& seq, Transport& tr, int b) {
		if (b < 0 || b >= BUTTON_LEN)
			return;
		markChord();
		held[b] = true;

		// Clearing a track asks for a second DELETE; any other button aborts.
		if (deleteArmed && b != BUTTON_DELETE) {
			deleteArmed = false;
			flash("Abrt");
			// The aborting press is used up; its release must not act either.
			held[b] = false;
			return;
		}

		if (b == BUTTON_MATH) {
			if (mathPinned)
				applyMathToFocus(seq);
			return;
		}
		if (euclidActive) {
			pressInEuclid(seq, b);
			held[b] = false;
			return;
		}
		if (b == FOCUS_GROUP_MODIFIER) {
			toggleModifierMode();
			return;
		}
		if (mathScreen()) {
			if (b == BUTTON_INVERT && expander) {
				// Inverting the transform being edited.
				TransformSet& tf = mathTarget(seq);
				tf = invertedAll(tf);
				held[b] = false;
				return;
			}
			pressInMathScreen(seq, b);
			return;
		}
		if (b < FOCUS_LEN) {
			pressFocus(seq, b);
			return;
		}
		// The track options screen only takes focus buttons and the encoders.
		if (optionsScreen)
			return;
		if (b == BUTTON_INVERT) {
			invertPending = true;
			return;
		}
		if (b == BUTTON_ROTATE) {
			// With INVERT held it shifts backward instead.
			rotoinvert(seq, held[BUTTON_INVERT] ? ROTO_SHIFT_BACK : ROTO_SHIFT_FORWARD);
			return;
		}
		if (b == BUTTON_RESET_TO) {
			toggleResetTo(seq);
			return;
		}
		if (b == BUTTON_DELETE && leftFocus == FOCUS_PART) {
			clearPart(seq);
			return;
		}
		if (b == BUTTON_DELETE && leftFocus == FOCUS_GROUP) {
			for (Step& st : seq.tracks[track].steps)
				setInGroup(st, focusedGroup, false);
			return;
		}
		if (b == BUTTON_DESELECT) {
			pressDeselect(seq);
			return;
		}
		if ((b == BUTTON_INSERT || b == BUTTON_DELETE) && refuseEdit()) {
			held[b] = false;
			return;
		}
		switch (b) {
			case BUTTON_INSERT:
				insertMode = INSERT_AFTER;
				break;
			case BUTTON_DELETE:
				doDelete(seq, tr);
				break;
			case BUTTON_COPY:
				copyDragged = false;
				copyAnchor = copyPosition();
				break;
			case BUTTON_LOOP_START:
				toggleLoop(seq, true);
				break;
			case BUTTON_LOOP_END:
				toggleLoop(seq, false);
				break;
			case BUTTON_SMOOTH:
				toggleSmooth(seq);
				break;
			default:
				break;
		}
	}

	void release(Sequence& seq, Transport& tr, int b) {
		if (b < 0 || b >= BUTTON_LEN || !held[b])
			return;
		held[b] = false;
		if (b < FOCUS_LEN) {
			if (focusPressPending[b]) {
				focusPressPending[b] = false;
				focusPress(seq, b);
			}
			return;
		}
		if (b == BUTTON_MATH) {
			// Releasing MATH applies the transform, unless its edit screen is pinned.
			if (!mathPinned)
				applyMathToFocus(seq);
			return;
		}
		if (b == BUTTON_INVERT) {
			if (invertPending && !optionsScreen && !mathScreen())
				rotoinvert(seq, ROTO_REVERSE);
			invertPending = false;
			return;
		}
		if (optionsScreen || mathScreen())
			return;
		switch (b) {
			case BUTTON_INSERT:
				doInsert(seq, tr);
				break;
			case BUTTON_COPY:
				doCopy(seq);
				break;
			default:
				break;
		}
	}

	void turnLeft(Sequence& seq, Transport& tr, int d) {
		if (d == 0)
			return;
		markChord();
		if (euclidActive) {
			euclidN = std::max(0, std::min(euclidN + d, euclidM));
			return;
		}
		if (modifierMode && modifierType == MODIFIER_SLOPE) {
			focusedGroup = std::max(0, std::min(focusedGroup + d, NUM_GROUPS - 1));
			return;
		}
		if (mathScreen() || editingModifierTransform()) {
			if (expander) {
				// The LEFT knob picks which operation of the focused parameter to edit.
				int param = rightFocus - FOCUS_CV_A;
				if (param >= 0 && param < MATH_PARAMS)
					transformOp[param] = ((transformOp[param] + d) % OP_LEN + OP_LEN) % OP_LEN;
			}
			else {
				cycleMathType(seq.tracks[track].math[mathRow], d);
			}
			return;
		}
		if (optionsScreen && leftFocus == FOCUS_STEP) {
			TrackOptions& o = seq.tracks[track].options;
			o.clockMul = adjustRatio(o.clockMul, d);
			return;
		}
		// Holding PART: the cursor jumps between the track's landmarks.
		if (expander && held[FOCUS_PART]) {
			quickNavigate(seq, d);
			scrub(tr);
			return;
		}
		switch (leftFocus) {
			case FOCUS_GROUP:
				focusedGroup = std::max(0, std::min(focusedGroup + d, NUM_GROUPS - 1));
				return;
			case FOCUS_PART:
				if (selectPatched)
					flash("PLUG");
				else
					focusedPart = std::max(0, std::min(focusedPart + d, NUM_PARTS - 1));
				return;
			case FOCUS_TRACK:
				track = std::max(0, std::min(track + d, NUM_TRACKS - 1));
				if (mode == MODE_FOLLOW)
					follow(seq, tr);
				break;
			case FOCUS_PATTERN:
				movePattern(seq, d);
				scrub(tr);
				break;
			case FOCUS_STEP:
				moveStep(seq, d);
				scrub(tr);
				break;
			case FOCUS_INDEX:
				browseIndex = std::max(0, std::min(browseIndex + d, TABLE_SIZE - 1));
				return;
			default:
				return;
		}
		if (held[BUTTON_COPY])
			copyDragged = true;
	}

	void turnRight(Sequence& seq, int d) {
		if (d == 0)
			return;
		markChord();
		// Holding INSERT picks where the insertion goes: turning counter-clockwise walks
		// AFtr -> SPLt -> bEFr.
		if (held[BUTTON_INSERT]) {
			insertMode = std::max((int) INSERT_AFTER, std::min(insertMode - d, (int) INSERT_BEFORE));
			return;
		}
		Track& t = seq.tracks[track];
		if (euclidActive) {
			euclidM = std::max(1, std::min(euclidM + d, MAX_EUCLID));
			euclidN = std::min(euclidN, euclidM);
			return;
		}
		if (modifierMode) {
			int param = rightFocus - FOCUS_CV_A;
			if (param < 0 || param >= MATH_PARAMS)
				return;
			Group& g = seq.groups[focusedGroup];
			if (modifierType == MODIFIER_SLOPE) {
				float& k = g.slope[modifierChannel][param];
				k = adjustSlope(k, d);
			}
			else {
				adjustTransform(modifierTarget(seq)[param], transformOp[param], d);
			}
			return;
		}
		if (mathScreen()) {
			int param = rightFocus - FOCUS_CV_A;
			if (param < 0 || param >= MATH_PARAMS)
				return;
			if (expander)
				adjustTransform(mathTarget(seq)[param], transformOp[param], d);
			else
				adjustMathOperand(t.math[param], d);
			return;
		}
		if (optionsScreen) {
			editOption(t.options, rightFocus, d);
			return;
		}
		// With TABLE on REF, the right encoder picks the reference table when INDEX or
		// VOLTAGE has focus.
		if (table == TABLE_REF && (leftFocus == FOCUS_INDEX || rightFocus == FOCUS_VOLTAGE)) {
			refTable = std::max(0, std::min(refTable + d, NUM_REF_TABLES - 1));
			flash(refTableName(refTable));
			return;
		}
		if (rightFocus == FOCUS_VOLTAGE) {
			editVoltage(seq, d);
			return;
		}
		int pos = cursor().step;
		if (pos < 0 || refuseEdit())
			return;
		if (rightFocus == FOCUS_DURATION && held[FOCUS_DURATION]) {
			edit::transferDuration(t, pos, d);
			return;
		}
		Step& s = t.steps[pos];
		switch (rightFocus) {
			case FOCUS_CV_A: s.cvA = adjust(s.cvA, d); break;
			case FOCUS_CV_B: s.cvB = adjust(s.cvB, d); break;
			case FOCUS_DURATION: s.duration = adjust(s.duration, d); break;
			case FOCUS_GATE: s.gate = adjust(s.gate, d); break;
			default: break;
		}
	}

	// A held focus button was used as a modifier (another button or an encoder moved
	// while it was down), so its focus-press action is cancelled.
	void markChord() {
		for (int f = 0; f < FOCUS_LEN; f++) {
			if (held[f])
				focusPressPending[f] = false;
		}
		if (held[BUTTON_INVERT])
			invertPending = false;
	}

	void tick(float dt) {
		time += dt;
		if (message) {
			messageTime -= dt;
			if (messageTime <= 0.f)
				message = nullptr;
		}
	}

	// FOLLOW mode: the cursor of the selected track tracks its play cursor.
	void follow(const Sequence& seq, const Transport& tr) {
		const Track& t = seq.tracks[track];
		const Playhead& ph = tr.playheads[track];
		EditCursor& c = cursor();
		if (ph.step < t.numSteps() && t.numSteps() > 0) {
			if (c.step != ph.step)
				setCursorStep(t, ph.step);
		}
		else {
			normalize(t, c);
		}
	}

	// Call after the sequence was replaced wholesale (patch load, demo, clear).
	void normalizeCursors(const Sequence& seq) {
		for (int t = 0; t < NUM_TRACKS; t++)
			normalize(seq.tracks[t], cursors[t]);
	}

	// Moves every cursor back to the first step (loading a snapshot does this).
	void rewindCursors(const Sequence& seq) {
		for (int t = 0; t < NUM_TRACKS; t++) {
			cursors[t] = EditCursor();
			cursors[t].pattern = 0;
			normalize(seq.tracks[t], cursors[t]);
		}
	}

	void flash(const char* msg) {
		message = msg;
		messageTime = MESSAGE_S;
	}

	bool mathScreen() const {
		return held[BUTTON_MATH] || mathPinned;
	}

	// --- Output --------------------------------------------------------------

	bool blinkPhase() const {
		return std::fmod(time, BLINK_S) < BLINK_S / 2;
	}

	Led focusLed(int f) const {
		// A filled clipboard blinks the display that says what it holds.
		if ((clip.kind == Clipboard::STEPS && f == FOCUS_STEP) ||
		    (clip.kind == Clipboard::PATTERNS && f == FOCUS_PATTERN) ||
		    (clip.kind == Clipboard::TRACK && f == FOCUS_TRACK) ||
		    (clip.kind == Clipboard::TABLE && f == FOCUS_INDEX) ||
		    (clip.kind == Clipboard::PART && f == FOCUS_PART) ||
		    (clip.kind == Clipboard::GROUP && f == FOCUS_GROUP))
			return LED_BLINK;
		if (mathScreen() && isLeftFocus(f))
			return f == FOCUS_TRACK + mathRow ? LED_ON : LED_OFF;
		// The PART LED blinks while a part is waiting to play.
		if (f == FOCUS_PART && pendingPart >= 0)
			return LED_BLINK;
		// GROUP and GROUP MODIFIERS blink while their screens are up.
		if (f == FOCUS_GROUP && (euclidActive || modifierMode))
			return LED_BLINK;
		if (f == FOCUS_GROUP_MODIFIER)
			return modifierMode ? LED_BLINK : LED_OFF;
		return (f == leftFocus || f == rightFocus) ? LED_ON : LED_OFF;
	}

	bool copyLed() const {
		return clip.kind != Clipboard::NONE;
	}

	// The lower VOLTAGE LED shows the editing granularity: off fine, on coarse, blinking
	// super coarse.
	Led grainLed(const Sequence& seq) const {
		int g = seq.tracks[track].voltageGrain;
		return g == GRAIN_FINE ? LED_OFF : (g == GRAIN_COARSE ? LED_ON : LED_BLINK);
	}

	// Lit when the cursor's step is smoothed on the selected table, whether by its own
	// flag, its pattern's or the track's.
	bool smoothLed(const Sequence& seq) const {
		if (table == TABLE_REF)
			return false;
		bool b = table == TABLE_B;
		const Track& t = seq.tracks[track];
		const EditCursor& c = cursors[track];
		if (b ? t.smoothB : t.smoothA)
			return true;
		if (c.pattern >= 0 && (b ? t.patterns[c.pattern].smoothB : t.patterns[c.pattern].smoothA))
			return true;
		return c.step >= 0 && (b ? t.steps[c.step].smoothB : t.steps[c.step].smoothA);
	}

	// Loop LEDs: on when the cursor's step is the loop point. With PATTERN focused, on
	// when the loop point is the pattern's boundary and blinking when it falls inside.
	Led loopLed(const Sequence& seq, bool start) const {
		const Track& t = seq.tracks[track];
		int point = loopPoint(t, start);
		const EditCursor& c = cursors[track];
		if (point < 0)
			return LED_OFF;
		if (leftFocus == FOCUS_PATTERN && c.pattern >= 0) {
			int first = edit::patternStart(t, c.pattern);
			int end = edit::patternEnd(t, c.pattern);
			if (point < first || point >= end)
				return LED_OFF;
			return point == (start ? first : end - 1) ? LED_ON : LED_BLINK;
		}
		return point == c.step ? LED_ON : LED_OFF;
	}

	// With the expander the LOOP buttons and LEDs work on the focused part's loop.
	int loopPoint(const Track& t, bool start) const {
		if (expander) {
			const PartPoints& p = t.parts[focusedPart];
			return start ? p.loopStart : p.loopEnd;
		}
		return start ? t.loopStart : t.loopEnd;
	}

	// The red LED by GROUP: the cursor's step is in the focused group.
	bool groupMemberLed(const Sequence& seq) const {
		const EditCursor& c = cursors[track];
		return expander && c.step >= 0 && inGroup(seq.tracks[track].steps[c.step], focusedGroup);
	}

	// RESET TO LED: the cursor's step is the focused part's reset step.
	bool resetToLed(const Sequence& seq) const {
		const EditCursor& c = cursors[track];
		return expander && c.step >= 0 && c.step == seq.tracks[track].parts[focusedPart].resetTo;
	}

	PanelView view(const Sequence& seq) const {
		PanelView v;
		v.leftFocus = leftFocus;
		v.rightFocus = rightFocus;
		v.track = track;
		const Track& t = seq.tracks[track];
		const EditCursor& c = cursors[track];
		v.pattern = c.pattern;
		if (c.step >= 0 && c.step < t.numSteps()) {
			v.step = t.steps[c.step];
			v.stepInPattern = c.step - edit::patternStart(t, c.pattern);
		}
		v.index = activeIndex(seq);
		v.voltage = activeTable(seq)[v.index];
		v.noteDisplay = table == TABLE_B ? t.options.noteDisplayB : t.options.noteDisplayA;
		v.grain = t.voltageGrain;
		v.message = currentMessage();
		v.optionsScreen = optionsScreen;
		v.options = t.options;
		v.mathScreen = mathScreen();
		v.mathRow = mathRow;
		v.math = t.math;
		v.expander = expander;
		v.blink = blinkPhase();
		v.groupFocused = focusedGroup;
		v.groupHasMembers = seq.groupHasMembers(focusedGroup);
		v.groupCount = countMembers(t, focusedGroup);
		v.groupMember = groupMemberLed(seq);
		v.euclid = euclidActive;
		v.euclidN = euclidN;
		v.euclidM = euclidM;
		v.partFocused = focusedPart;
		v.partPlaying = playingPart;
		v.partPending = pendingPart;
		for (int i = 0; i < NUM_TRACKS; i++) {
			const PartPoints& p = seq.tracks[i].parts[focusedPart];
			v.partOverview[i] = (uint8_t) ((p.resetTo >= 0 ? SEG_TOP : 0) | (p.loopStart >= 0 ? SEG_MIDDLE : 0) |
			                               (p.loopEnd >= 0 ? SEG_BOTTOM : 0));
		}
		v.transform = mathScreen() ? mathTargetConst(seq) : t.transform;
		for (int i = 0; i < MATH_PARAMS; i++)
			v.transformOp[i] = transformOp[i];
		// The GROUP MODIFIERS screen: the HIGH/LOW transforms show like the MATH screen;
		// slopes get their own.
		if (editingModifierTransform()) {
			v.mathScreen = true;
			v.transform = modifierMode ? modifierTargetConst(seq) : v.transform;
		}
		if (modifierMode && modifierType == MODIFIER_SLOPE) {
			v.slopeScreen = true;
			for (int i = 0; i < MATH_PARAMS; i++)
				v.slopes[i] = seq.groups[focusedGroup].slope[modifierChannel][i];
			v.slopeParam = std::max(0, std::min(rightFocus - FOCUS_CV_A, MATH_PARAMS - 1));
		}
		return v;
	}

	const char* currentMessage() const {
		if (mathScreen()) {
			// "PIN" invites pinning the edit screen; once pinned, "dOnE" says how to leave.
			if (!blinkPhase())
				return "";
			return mathPinned ? "dOnE" : "PIN";
		}
		if (held[BUTTON_INSERT] && (leftFocus == FOCUS_STEP || leftFocus == FOCUS_PATTERN)) {
			static const char* const words[3] = {"AFtr", "SPLt", "bEFr"};
			return words[insertMode];
		}
		if (deleteArmed)
			return "CLr";
		return message;
	}

private:
	EditCursor& cursor() {
		return cursors[track];
	}

	static uint8_t adjust(uint8_t v, int d) {
		return (uint8_t) std::max(0, std::min((int) v + d, MAX_VALUE));
	}

	// In FOLLOW mode the sequence can only be changed while paused; otherwise the edit is
	// refused with "TILt" (manual, The Modes).
	bool refuseEdit() {
		if (mode != MODE_FOLLOW || paused)
			return false;
		flash("TILt");
		return true;
	}

	static void normalize(const Track& t, EditCursor& c) {
		int np = edit::numPatterns(t);
		if (np == 0) {
			c = EditCursor();
			return;
		}
		c.pattern = std::max(0, std::min(c.pattern, np - 1));
		int first = edit::patternStart(t, c.pattern);
		int end = edit::patternEnd(t, c.pattern);
		if (first == end)
			c.step = -1;
		else if (c.step < first || c.step >= end)
			c.step = first;
	}

	void setCursorStep(const Track& t, int pos) {
		EditCursor& c = cursor();
		int p, s;
		t.locate(pos, p, s);
		c.pattern = p;
		c.step = pos;
	}

	// FOLLOW mode: moving the cursor moves the play cursor with it.
	void scrub(Transport& tr) {
		if (mode != MODE_FOLLOW || cursor().step < 0)
			return;
		Playhead& ph = tr.playheads[track];
		ph.step = cursor().step;
		ph.pulse = 0;
	}

	void pressFocus(Sequence& seq, int f) {
		int& focus = isLeftFocus(f) ? leftFocus : rightFocus;
		if (focus == f) {
			focusPressPending[f] = true;
		}
		else {
			focus = f;
			// Focusing INDEX starts browsing at the entry the cursor's step uses.
			if (f == FOCUS_INDEX && table != TABLE_REF && cursor().step >= 0) {
				const Step& s = seq.tracks[track].steps[cursor().step];
				browseIndex = table == TABLE_B ? s.cvB : s.cvA;
			}
		}
		// On the options screen the buttons beside the two-state options cycle them.
		if (optionsScreen && (f == FOCUS_CV_A || f == FOCUS_CV_B || f == FOCUS_GATE))
			editOption(seq.tracks[track].options, f, 1);
	}

	// The action of pressing an already focused button, run on its release.
	void focusPress(Sequence& seq, int f) {
		// TRACK opens and closes the track options screen.
		if (f == FOCUS_TRACK) {
			optionsScreen = !optionsScreen;
			return;
		}
		if (optionsScreen)
			return;
		// VOLTAGE cycles fine -> coarse -> super coarse.
		if (f == FOCUS_VOLTAGE) {
			uint8_t& g = seq.tracks[track].voltageGrain;
			g = (uint8_t) ((g + 1) % GRAIN_LEN);
		}
		// GATE toggles ratchet on the cursor's step.
		if (f == FOCUS_GATE) {
			int pos = cursor().step;
			if (pos >= 0 && !refuseEdit()) {
				Step& s = seq.tracks[track].steps[pos];
				s.ratchet = !s.ratchet;
			}
		}
	}

	static uint8_t adjustRatio(uint8_t v, int d) {
		return (uint8_t) std::max(1, std::min((int) v + d, MAX_VALUE));
	}

	// Track options screen (spec §4.8): CV-A/CV-B note or number display, DURATION the
	// clock divider, GATE gate or trigger output. (STEP, the multiplier, is on the left.)
	static void editOption(TrackOptions& o, int f, int d) {
		switch (f) {
			case FOCUS_CV_A: o.noteDisplayA = !o.noteDisplayA; break;
			case FOCUS_CV_B: o.noteDisplayB = !o.noteDisplayB; break;
			case FOCUS_DURATION: o.clockDiv = adjustRatio(o.clockDiv, d); break;
			case FOCUS_GATE: o.triggerMode = !o.triggerMode; break;
			default: break;
		}
	}

	// SMOOTH toggles smoothing for the focused step, pattern or track, on the output the
	// TABLE switch selects (A or B; nothing in REF).
	void toggleSmooth(Sequence& seq) {
		if (table == TABLE_REF)
			return;
		bool b = table == TABLE_B;
		Track& t = seq.tracks[track];
		const EditCursor& c = cursor();
		auto flip = [b](bool& a, bool& bb) {
			bool& flag = b ? bb : a;
			flag = !flag;
		};
		if (leftFocus == FOCUS_TRACK)
			flip(t.smoothA, t.smoothB);
		else if (leftFocus == FOCUS_PATTERN && c.pattern >= 0)
			flip(t.patterns[c.pattern].smoothA, t.patterns[c.pattern].smoothB);
		else if (c.step >= 0)
			flip(t.steps[c.step].smoothA, t.steps[c.step].smoothB);
	}

	// STEP moves through the whole track; the PATTERN display follows along.
	void moveStep(const Sequence& seq, int d) {
		const Track& t = seq.tracks[track];
		EditCursor& c = cursor();
		int n = t.numSteps();
		if (n == 0)
			return;
		int target;
		if (c.step >= 0) {
			target = c.step + d;
		}
		else {
			// On an empty pattern: step to its neighbours.
			int at = c.pattern >= 0 ? edit::patternStart(t, c.pattern) : 0;
			target = d > 0 ? at + d - 1 : at + d;
		}
		setCursorStep(t, std::max(0, std::min(target, n - 1)));
	}

	// PATTERN jumps a pattern at a time, landing on its first step going forward and on
	// its last step going backward.
	void movePattern(const Sequence& seq, int d) {
		const Track& t = seq.tracks[track];
		EditCursor& c = cursor();
		int np = edit::numPatterns(t);
		if (np == 0)
			return;
		c.pattern = std::max(0, std::min(c.pattern + d, np - 1));
		int first = edit::patternStart(t, c.pattern);
		int end = edit::patternEnd(t, c.pattern);
		c.step = first == end ? -1 : (d > 0 ? first : end - 1);
	}

	// --- MATH ------------------------------------------------------------------

	// While MATH is held (or its screen pinned) the left displays show each parameter's
	// operation and the right ones its operand. Left focus buttons pick a parameter's row
	// and cycle its operation; the right encoder sets the focused parameter's operand.
	void pressInMathScreen(Sequence& seq, int b) {
		Track& t = seq.tracks[track];
		if (b == FOCUS_VOLTAGE) {
			// VOLTAGE pins the edit screen so MATH can be let go; pressed again it leaves
			// without applying.
			mathPinned = !mathPinned;
			return;
		}
		if (b == BUTTON_DELETE) {
			if (expander)
				mathTarget(seq) = TransformSet();
			else
				t.math = std::array<MathOp, MATH_PARAMS>();
			return;
		}
		if (b >= FOCUS_TRACK && b <= FOCUS_SNAPSHOT) {
			mathRow = b - FOCUS_TRACK;
			if (expander)
				rightFocus = FOCUS_CV_A + mathRow; // the row's parameter
			else
				cycleMathType(t.math[mathRow], 1);
			return;
		}
		if (b >= FOCUS_CV_A && b <= FOCUS_GATE)
			rightFocus = b;
	}

	// Transforms the focused step, pattern or whole track.
	// With the expander the five-operation transform is applied, inverted while INVERT
	// is held.
	void applyMathToFocus(Sequence& seq) {
		Track& t = seq.tracks[track];
		// With GROUP focused the group's transform goes to its members, on every track.
		if (expander && leftFocus == FOCUS_GROUP) {
			const Group& g = seq.groups[focusedGroup];
			TransformSet tf = held[BUTTON_INVERT] ? invertedAll(g.transform) : g.transform;
			if (held[BUTTON_INVERT])
				invertPending = false;
			for (Track& tr : seq.tracks) {
				for (int i = 0; i < tr.numSteps(); i++) {
					if (inGroup(tr.steps[i], focusedGroup))
						applyTransforms(tr, tf, i, i, rng);
				}
			}
			return;
		}
		int first, last;
		if (!focusRange(t, first, last)) {
			const EditCursor& c = cursor();
			if (c.step < 0)
				return;
			first = last = c.step;
		}
		if (expander) {
			if (held[BUTTON_INVERT]) {
				invertPending = false;
				applyTransforms(t, invertedAll(t.transform), first, last, rng);
			}
			else {
				applyTransforms(t, t.transform, first, last, rng);
			}
		}
		else {
			applyMath(t, first, last, rng);
		}
	}

	// The steps the focused PATTERN or TRACK covers; false for any other focus.
	bool focusRange(const Track& t, int& first, int& last) {
		const EditCursor& c = cursor();
		if (leftFocus == FOCUS_TRACK && t.numSteps() > 0) {
			first = 0;
			last = t.numSteps() - 1;
			return true;
		}
		if (leftFocus == FOCUS_PATTERN && c.pattern >= 0 && t.patterns[c.pattern].length > 0) {
			first = edit::patternStart(t, c.pattern);
			last = edit::patternEnd(t, c.pattern) - 1;
			return true;
		}
		return false;
	}

	// --- Rotoinversion (expander) ------------------------------------------------

	enum Roto { ROTO_REVERSE, ROTO_SHIFT_FORWARD, ROTO_SHIFT_BACK };

	// INVERT / ROTATE with PATTERN or TRACK focused rearrange the focused right-hand
	// parameter of those steps (spec ER-102 §6).
	void rotoinvert(Sequence& seq, int op) {
		Track& t = seq.tracks[track];
		// With GROUP focused it is the selection that is inverted or shifted (this track).
		if (leftFocus == FOCUS_GROUP) {
			if (op == ROTO_REVERSE)
				invertMembership(t, focusedGroup);
			else
				rotateMembership(t, focusedGroup, op == ROTO_SHIFT_FORWARD);
			return;
		}
		int param = rightFocus - FOCUS_CV_A;
		int first, last;
		if (param < 0 || param >= MATH_PARAMS || !focusRange(t, first, last))
			return;
		if (refuseEdit())
			return;
		if (op == ROTO_REVERSE)
			edit::reverseParam(t, first, last, param);
		else
			edit::rotateParam(t, first, last, param, op == ROTO_SHIFT_FORWARD);
	}

	// --- INSERT ----------------------------------------------------------------

	void doInsert(Sequence& seq, Transport& tr) {
		Playhead& ph = tr.playheads[track];
		bool ok = true;
		if (leftFocus == FOCUS_STEP)
			ok = clip.kind == Clipboard::STEPS ? pasteSteps(seq, ph) : insertStep(seq, ph);
		else if (leftFocus == FOCUS_PATTERN)
			ok = clip.kind == Clipboard::PATTERNS ? pastePatterns(seq, ph) : insertPattern(seq, ph);
		else if (leftFocus == FOCUS_TRACK && clip.kind == Clipboard::TRACK) {
			ok = edit::replaceTrack(seq, track, clip.track, ph);
			normalize(seq.tracks[track], cursor());
		}
		else if (leftFocus == FOCUS_INDEX && clip.kind == Clipboard::TABLE) {
			pasteTable(seq);
			return;
		}
		else if (leftFocus == FOCUS_PART && clip.kind == Clipboard::PART) {
			pastePart(seq);
			return;
		}
		else if (leftFocus == FOCUS_GROUP && clip.kind == Clipboard::GROUP) {
			// Pasting a selection adds it to the focused group's (a union).
			Track& t = seq.tracks[track];
			for (int i = 0; i < t.numSteps() && i < (int) clip.selection.size(); i++) {
				if (clip.selection[i])
					setInGroup(t.steps[i], focusedGroup, true);
			}
			return;
		}
		if (!ok)
			flash("FULL");
	}

	bool ensurePattern(Sequence& seq, Playhead& ph) {
		EditCursor& c = cursor();
		if (c.pattern >= 0)
			return true;
		if (!edit::insertEmptyPattern(seq, track, 0, ph))
			return false;
		c.pattern = 0;
		c.step = -1;
		return true;
	}

	// Where a new step goes: after/before the cursor's step, or at the start of the
	// cursor's pattern when it is empty.
	int insertPosition(const Track& t, bool before) {
		const EditCursor& c = cursor();
		if (c.step < 0)
			return edit::patternStart(t, c.pattern);
		return before ? c.step : c.step + 1;
	}

	bool insertStep(Sequence& seq, Playhead& ph) {
		Track& t = seq.tracks[track];
		EditCursor& c = cursor();
		if (insertMode == INSERT_SPLIT)
			return c.step < 0 || edit::splitStep(seq, track, c.step, ph);
		if (!ensurePattern(seq, ph))
			return false;
		// A new step copies the one under the cursor (manual, Quick Start step 8), or
		// takes the defaults when there is none (spec §2).
		Step s = c.step >= 0 ? t.steps[c.step] : t.defaultStep();
		int pos = insertPosition(t, insertMode == INSERT_BEFORE);
		if (!edit::insertSteps(seq, track, c.pattern, pos, &s, 1, ph))
			return false;
		setCursorStep(t, pos);
		return true;
	}

	bool pasteSteps(Sequence& seq, Playhead& ph) {
		Track& t = seq.tracks[track];
		if (!ensurePattern(seq, ph))
			return false;
		EditCursor& c = cursor();
		bool before = insertMode == INSERT_BEFORE && c.step >= 0;
		int pos = insertPosition(t, before);
		int n = (int) clip.steps.size();
		if (!edit::insertSteps(seq, track, c.pattern, pos, clip.steps.data(), n, ph))
			return false;
		// After (or into an empty pattern): land on the last pasted step so repeated
		// pastes chain. Before: stay on the step the paste went in front of.
		setCursorStep(t, before ? pos + n : pos + n - 1);
		return true;
	}

	bool insertPattern(Sequence& seq, Playhead& ph) {
		Track& t = seq.tracks[track];
		EditCursor& c = cursor();
		if (insertMode == INSERT_SPLIT) {
			if (c.pattern < 0)
				return true;
			int k = c.step >= 0 ? c.step - edit::patternStart(t, c.pattern) : 0;
			return edit::splitPattern(seq, track, c.pattern, k);
		}
		int at = c.pattern < 0 ? 0 : (insertMode == INSERT_BEFORE ? c.pattern : c.pattern + 1);
		if (!edit::insertEmptyPattern(seq, track, at, ph))
			return false;
		c.pattern = at;
		c.step = -1;
		return true;
	}

	bool pastePatterns(Sequence& seq, Playhead& ph) {
		Track& t = seq.tracks[track];
		EditCursor& c = cursor();
		bool before = insertMode == INSERT_BEFORE;
		int at = c.pattern < 0 ? 0 : (before ? c.pattern : c.pattern + 1);
		int count = (int) clip.patterns.size();
		if (!edit::insertPatterns(seq, track, at, clip.patterns.data(), count, clip.steps.data(), ph))
			return false;
		c.pattern = before ? at + count : at + count - 1;
		c.step = -1;
		normalize(t, c);
		return true;
	}

	// --- DELETE ----------------------------------------------------------------

	void doDelete(Sequence& seq, Transport& tr) {
		Playhead& ph = tr.playheads[track];
		Track& t = seq.tracks[track];
		EditCursor& c = cursor();
		if (leftFocus == FOCUS_TRACK) {
			if (!deleteArmed) {
				deleteArmed = true;
				return;
			}
			deleteArmed = false;
			seq.clearTrack(track);
			ph.validate(t);
			c = EditCursor();
			return;
		}
		if (leftFocus == FOCUS_STEP && c.step >= 0) {
			int pos = c.step;
			int p = c.pattern;
			edit::removeStep(seq, track, pos, ph);
			// Stay in the same pattern: on the step that moved up, else the one before.
			int end = edit::patternEnd(t, p);
			int first = edit::patternStart(t, p);
			c.step = pos < end ? pos : (end > first ? end - 1 : -1);
		}
		else if (leftFocus == FOCUS_PATTERN && c.pattern >= 0) {
			edit::removePattern(seq, track, c.pattern, ph);
			c.step = -1;
			normalize(t, c);
		}
	}

	// --- Voltage tables --------------------------------------------------------

	// The table the TABLE switch selects: the track's A or B, or the chosen reference.
	const VoltageTable& activeTable(const Sequence& seq) const {
		const Track& t = seq.tracks[track];
		if (table == TABLE_REF)
			return refs->get(refTable);
		return table == TABLE_B ? t.tableB : t.tableA;
	}

	// The entry INDEX and VOLTAGE show: the browsed one while INDEX has focus (and always
	// for reference tables), otherwise the one the cursor's step uses.
	int activeIndex(const Sequence& seq) const {
		const EditCursor& c = cursors[track];
		const Track& t = seq.tracks[track];
		if (leftFocus == FOCUS_INDEX || table == TABLE_REF || c.step < 0 || c.step >= t.numSteps())
			return browseIndex;
		const Step& s = t.steps[c.step];
		return table == TABLE_B ? s.cvB : s.cvA;
	}

	// The right encoder on VOLTAGE edits that entry of the track's table; while VOLTAGE
	// is held it changes the granularity instead. Reference tables are not edited entry by
	// entry (copy a track table onto a user table instead).
	void editVoltage(Sequence& seq, int d) {
		Track& t = seq.tracks[track];
		if (held[FOCUS_VOLTAGE]) {
			t.voltageGrain = (uint8_t) std::max(0, std::min(t.voltageGrain + d, GRAIN_LEN - 1));
			return;
		}
		if (table == TABLE_REF)
			return;
		bool b = table == TABLE_B;
		VoltageTable& tbl = b ? t.tableB : t.tableA;
		int i = activeIndex(seq);
		bool note = b ? t.options.noteDisplayB : t.options.noteDisplayA;
		tbl.volts[i] = nudgeVoltage(tbl.volts[i], d, t.voltageGrain, note);
	}

	// INSERT with INDEX focused overwrites the selected table with the copied one. Built-in
	// reference tables are read-only.
	void pasteTable(Sequence& seq) {
		Track& t = seq.tracks[track];
		if (table == TABLE_REF) {
			if (!refs->writable(refTable)) {
				flash("Err");
				return;
			}
			refs->user[refTable - NUM_BUILTIN_TABLES] = clip.table;
		}
		else {
			(table == TABLE_B ? t.tableB : t.tableA) = clip.table;
		}
	}

	// --- COPY ------------------------------------------------------------------

	// The cursor position a copy range is measured in: a step, or a pattern.
	int copyPosition() const {
		const EditCursor& c = cursors[track];
		return leftFocus == FOCUS_PATTERN ? c.pattern : c.step;
	}

	void doCopy(const Sequence& seq) {
		const Track& t = seq.tracks[track];
		int here = copyPosition();
		if (!copyDragged && clip.kind != Clipboard::NONE) {
			clip.clear();
			return;
		}
		// With GROUP focused, COPY takes the group's selection on this track.
		if (leftFocus == FOCUS_GROUP) {
			clip.clear();
			for (const Step& st : t.steps)
				clip.selection.push_back(inGroup(st, focusedGroup));
			clip.kind = Clipboard::GROUP;
			return;
		}
		// With PART focused, COPY takes the part's step assignments on every track.
		if (leftFocus == FOCUS_PART) {
			clip.clear();
			for (int i = 0; i < NUM_TRACKS; i++)
				clip.part[i] = seq.tracks[i].parts[focusedPart];
			clip.kind = Clipboard::PART;
			return;
		}
		// With INDEX focused, COPY takes the whole voltage table the TABLE switch shows.
		if (leftFocus == FOCUS_INDEX) {
			clip.clear();
			clip.table = activeTable(seq);
			clip.kind = Clipboard::TABLE;
			return;
		}
		int from = copyDragged ? std::min(copyAnchor, here) : here;
		int to = copyDragged ? std::max(copyAnchor, here) : here;
		clip.clear();

		if (leftFocus == FOCUS_TRACK) {
			clip.track = t;
			clip.kind = Clipboard::TRACK;
		}
		else if (leftFocus == FOCUS_STEP && from >= 0) {
			clip.steps.assign(t.steps.begin() + from, t.steps.begin() + to + 1);
			clip.kind = Clipboard::STEPS;
		}
		else if (leftFocus == FOCUS_PATTERN && from >= 0) {
			int first = edit::patternStart(t, from);
			int end = edit::patternEnd(t, to);
			clip.steps.assign(t.steps.begin() + first, t.steps.begin() + end);
			clip.patterns.assign(t.patterns.begin() + from, t.patterns.begin() + to + 1);
			clip.kind = Clipboard::PATTERNS;
		}
	}

	// --- LOOP ------------------------------------------------------------------

	// Sets the loop point at the cursor, or clears it if it is already there. With
	// PATTERN focused the target is the pattern's first (START) or last (END) step.
	void toggleLoop(Sequence& seq, bool start) {
		Track& t = seq.tracks[track];
		const EditCursor& c = cursor();
		int target = c.step;
		if (leftFocus == FOCUS_PATTERN && c.pattern >= 0) {
			int first = edit::patternStart(t, c.pattern);
			int end = edit::patternEnd(t, c.pattern);
			target = first == end ? -1 : (start ? first : end - 1);
		}
		if (target < 0)
			return;
		if (!expander) {
			int& point = start ? t.loopStart : t.loopEnd;
			point = point == target ? -1 : target;
			return;
		}
		if (focusedPart == STOP_PART)
			return;
		PartPoints& p = t.parts[focusedPart];
		int16_t& point = start ? p.loopStart : p.loopEnd;
		point = (int16_t) (point == target ? -1 : target);
		followPlayingPart(t);
	}

	// --- Parts (expander) ----------------------------------------------------------

	// Edits to the playing part's loop are heard at once.
	void followPlayingPart(Track& t) {
		if (focusedPart != playingPart)
			return;
		t.loopStart = t.parts[focusedPart].loopStart;
		t.loopEnd = t.parts[focusedPart].loopEnd;
	}

	// RESET TO sets (or clears) the focused part's reset step on this track.
	void toggleResetTo(Sequence& seq) {
		int step = cursor().step;
		if (!expander || focusedPart == STOP_PART || step < 0)
			return;
		int16_t& point = seq.tracks[track].parts[focusedPart].resetTo;
		point = (int16_t) (point == step ? -1 : step);
	}

	void clearPart(Sequence& seq) {
		if (focusedPart == STOP_PART)
			return;
		for (int i = 0; i < NUM_TRACKS; i++) {
			seq.tracks[i].parts[focusedPart] = PartPoints();
			followPlayingPart(seq.tracks[i]);
		}
	}

	void pastePart(Sequence& seq) {
		if (focusedPart == STOP_PART)
			return;
		for (int i = 0; i < NUM_TRACKS; i++) {
			Track& t = seq.tracks[i];
			PartPoints p = clip.part[i];
			// A part copied from a longer track may point past the end of this one.
			auto fit = [&](int16_t v) { return (int16_t) (v < t.numSteps() ? v : -1); };
			p.resetTo = fit(p.resetTo);
			p.loopStart = fit(p.loopStart);
			p.loopEnd = fit(p.loopEnd);
			t.parts[focusedPart] = p;
			followPlayingPart(t);
		}
	}

	// --- Groups (expander) --------------------------------------------------------

	// What MATH edits and applies: the focused group's transform with GROUP focused,
	// otherwise the track's.
	TransformSet& mathTarget(Sequence& seq) {
		if (leftFocus == FOCUS_GROUP)
			return seq.groups[focusedGroup].transform;
		return seq.tracks[track].transform;
	}
	const TransformSet& mathTargetConst(const Sequence& seq) const {
		if (leftFocus == FOCUS_GROUP)
			return seq.groups[focusedGroup].transform;
		return seq.tracks[track].transform;
	}

	// The GROUP MODIFIERS screen with the switch on HIGH or LOW edits that channel's
	// non-destructive transform (with the MATH screen's controls, but nothing to apply).
	bool editingModifierTransform() const {
		return modifierMode && modifierType != MODIFIER_SLOPE;
	}
	TransformSet& modifierTarget(Sequence& seq) {
		Group& g = seq.groups[focusedGroup];
		return modifierType == MODIFIER_HIGH ? g.high[modifierChannel] : g.low[modifierChannel];
	}
	const TransformSet& modifierTargetConst(const Sequence& seq) const {
		const Group& g = seq.groups[focusedGroup];
		return modifierType == MODIFIER_HIGH ? g.high[modifierChannel] : g.low[modifierChannel];
	}

	void toggleModifierMode() {
		modifierMode = !modifierMode;
		if (modifierMode) {
			leftFocus = FOCUS_GROUP;
			if (rightFocus < FOCUS_CV_A || rightFocus > FOCUS_GATE)
				rightFocus = FOCUS_CV_A;
		}
	}

	// (DE)SELECT: with PATTERN or TRACK focused, start choosing a Euclidean mask; on a
	// step, add it to or take it out of the focused group.
	void pressDeselect(Sequence& seq) {
		Track& t = seq.tracks[track];
		const EditCursor& c = cursor();
		if ((leftFocus == FOCUS_PATTERN || leftFocus == FOCUS_TRACK) && c.pattern >= 0) {
			int first, last;
			if (!focusRange(t, first, last))
				return;
			int length = std::max(1, std::min((int) t.patterns[c.pattern].length, MAX_EUCLID));
			euclidActive = true;
			euclidFirst = first;
			euclidLast = last;
			euclidN = euclidM = length; // E(L, L): everything
			return;
		}
		if (c.step >= 0) {
			Step& st = t.steps[c.step];
			setInGroup(st, focusedGroup, !inGroup(st, focusedGroup));
		}
	}

	// While choosing a mask: LEFT sets N, RIGHT sets M, DELETE makes it E(0, M), INDEX
	// flips between E(0, M) and E(M, M), and (DE)SELECT applies it.
	void pressInEuclid(Sequence& seq, int b) {
		if (b == BUTTON_DESELECT) {
			applyEuclid(seq.tracks[track], euclidFirst, euclidLast, focusedGroup, euclidN, euclidM);
			euclidActive = false;
		}
		else if (b == BUTTON_DELETE) {
			euclidN = 0;
		}
		else if (b == FOCUS_INDEX) {
			euclidN = euclidN == 0 ? euclidM : 0;
		}
	}

	// Holding PART and turning LEFT walks: first step, RESET TO, LOOP START, LOOP END,
	// last step (skipping whatever the focused part has not set).
	void quickNavigate(const Sequence& seq, int d) {
		const Track& t = seq.tracks[track];
		int n = t.numSteps();
		if (n == 0)
			return;
		const PartPoints& p = t.parts[focusedPart];
		int stops[5];
		int count = 0;
		const int candidates[5] = {0, p.resetTo, p.loopStart, p.loopEnd, n - 1};
		for (int v : candidates) {
			if (v >= 0 && v < n && (count == 0 || stops[count - 1] != v))
				stops[count++] = v;
		}
		int at = 0;
		for (int i = 0; i < count; i++) {
			if (stops[i] == cursor().step)
				at = i;
		}
		at = std::max(0, std::min(at + d, count - 1));
		setCursorStep(t, stops[at]);
	}
};

} // namespace iqs
