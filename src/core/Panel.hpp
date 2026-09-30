#pragma once
// Front-panel behaviour (docs/SPEC.md §4): focus, the two encoders, and the edit
// buttons. It receives press/release/turn events and edits the sequence directly, so
// it must run on the audio thread, which is the only place the sequence changes.
//
// Covers EDIT-mode editing, SMOOTH and the track options screen. The MODE switch, MATH,
// snapshots and voltage table editing come in later stages.

#include "Editor.hpp"
#include "Transport.hpp"

namespace iqs {

// Focus targets. The first five are steered by the left encoder, the rest by the right.
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
	FOCUS_LEN
};
static constexpr int NUM_LEFT_FOCUS = FOCUS_VOLTAGE;

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
	BUTTON_LEN
};

enum InsertMode { INSERT_AFTER, INSERT_SPLIT, INSERT_BEFORE };

enum Table { TABLE_A, TABLE_B, TABLE_REF };

// LED state for indicators that can blink.
enum Led { LED_OFF, LED_ON, LED_BLINK };

struct Clipboard {
	enum Kind { NONE, STEPS, PATTERNS, TRACK };
	Kind kind = NONE;
	std::vector<Step> steps;
	std::vector<Pattern> patterns;
	Track track;

	Clipboard() {
		steps.reserve(MAX_TOTAL_STEPS);
		patterns.reserve(MAX_PATTERNS);
	}
	void clear() {
		kind = NONE;
		steps.clear();
		patterns.clear();
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
	int pattern = -1;      // 0-based, -1 when the track has no patterns
	int stepInPattern = -1; // 0-based, -1 when the cursor has no step
	Step step;
	int index = 0;
	float voltage = 0.f;
	bool noteDisplay = true; // VOLTAGE as a note (Nt) or a number (Nr)
	const char* message = nullptr;
	bool optionsScreen = false;
	TrackOptions options;
};

struct Panel {
	static constexpr float MESSAGE_S = 1.f;
	static constexpr float BLINK_S = 0.5f;

	int leftFocus = FOCUS_TRACK;
	int rightFocus = FOCUS_CV_A;
	int track = 0;
	int table = TABLE_A;
	EditCursor cursors[NUM_TRACKS];
	Clipboard clip;

	bool held[BUTTON_LEN] = {};
	int insertMode = INSERT_AFTER;
	bool copyDragged = false;
	int copyAnchor = -1;
	bool deleteArmed = false;
	bool optionsScreen = false;
	const char* message = nullptr;
	float messageTime = 0.f;
	float time = 0.f;

	// --- Events --------------------------------------------------------------

	void press(Sequence& seq, Transport& tr, int b) {
		if (b < 0 || b >= BUTTON_LEN)
			return;
		held[b] = true;

		// Clearing a track asks for a second DELETE; any other button aborts.
		if (deleteArmed && b != BUTTON_DELETE) {
			deleteArmed = false;
			flash("Abrt");
			// The aborting press is used up; its release must not act either.
			held[b] = false;
			return;
		}

		if (b < FOCUS_LEN) {
			pressFocus(seq, b);
			return;
		}
		// The track options screen only takes focus buttons and the encoders.
		if (optionsScreen)
			return;
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
		if (optionsScreen)
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

	void turnLeft(Sequence& seq, int d) {
		if (d == 0)
			return;
		if (optionsScreen && leftFocus == FOCUS_STEP) {
			TrackOptions& o = seq.tracks[track].options;
			o.clockMul = adjustRatio(o.clockMul, d);
			return;
		}
		switch (leftFocus) {
			case FOCUS_TRACK:
				track = std::max(0, std::min(track + d, NUM_TRACKS - 1));
				break;
			case FOCUS_PATTERN:
				movePattern(seq, d);
				break;
			case FOCUS_STEP:
				moveStep(seq, d);
				break;
			default:
				return;
		}
		if (held[BUTTON_COPY])
			copyDragged = true;
	}

	void turnRight(Sequence& seq, int d) {
		if (d == 0)
			return;
		// Holding INSERT picks where the insertion goes: turning counter-clockwise walks
		// AFtr -> SPLt -> bEFr.
		if (held[BUTTON_INSERT]) {
			insertMode = std::max((int) INSERT_AFTER, std::min(insertMode - d, (int) INSERT_BEFORE));
			return;
		}
		Track& t = seq.tracks[track];
		if (optionsScreen) {
			editOption(t.options, rightFocus, d);
			return;
		}
		int pos = cursor().step;
		if (pos < 0)
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

	void tick(float dt) {
		time += dt;
		if (message) {
			messageTime -= dt;
			if (messageTime <= 0.f)
				message = nullptr;
		}
	}

	// Call after the sequence was replaced wholesale (patch load, demo, clear).
	void normalizeCursors(const Sequence& seq) {
		for (int t = 0; t < NUM_TRACKS; t++)
			normalize(seq.tracks[t], cursors[t]);
	}

	// --- Output --------------------------------------------------------------

	bool blinkPhase() const {
		return std::fmod(time, BLINK_S) < BLINK_S / 2;
	}

	Led focusLed(int f) const {
		// A filled clipboard blinks the display that says what it holds.
		if ((clip.kind == Clipboard::STEPS && f == FOCUS_STEP) ||
		    (clip.kind == Clipboard::PATTERNS && f == FOCUS_PATTERN) ||
		    (clip.kind == Clipboard::TRACK && f == FOCUS_TRACK))
			return LED_BLINK;
		return (f == leftFocus || f == rightFocus) ? LED_ON : LED_OFF;
	}

	bool copyLed() const {
		return clip.kind != Clipboard::NONE;
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
		int point = start ? t.loopStart : t.loopEnd;
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

	PanelView view(const Sequence& seq) const {
		PanelView v;
		v.leftFocus = leftFocus;
		v.rightFocus = rightFocus;
		v.track = track;
		const Track& t = seq.tracks[track];
		const EditCursor& c = cursors[track];
		v.pattern = c.pattern;
		if (c.step >= 0) {
			v.step = t.steps[c.step];
			v.stepInPattern = c.step - edit::patternStart(t, c.pattern);
			bool useB = table == TABLE_B;
			v.index = useB ? v.step.cvB : v.step.cvA;
			v.voltage = useB ? t.tableB[v.index] : t.tableA[v.index];
		}
		else {
			v.voltage = t.tableA[0];
		}
		v.noteDisplay = table == TABLE_B ? t.options.noteDisplayB : t.options.noteDisplayA;
		v.message = currentMessage();
		v.optionsScreen = optionsScreen;
		v.options = t.options;
		return v;
	}

	const char* currentMessage() const {
		if (held[BUTTON_INSERT]) {
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

	void flash(const char* msg) {
		message = msg;
		messageTime = MESSAGE_S;
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

	void pressFocus(Sequence& seq, int f) {
		int& focus = f < NUM_LEFT_FOCUS ? leftFocus : rightFocus;
		bool focusPress = focus == f;
		focus = f;

		// A focus press on TRACK opens and closes the track options screen.
		if (focusPress && f == FOCUS_TRACK) {
			optionsScreen = !optionsScreen;
			return;
		}
		if (optionsScreen) {
			// On the options screen the buttons beside the two-state options cycle them.
			if (f == FOCUS_CV_A || f == FOCUS_CV_B || f == FOCUS_GATE)
				editOption(seq.tracks[track].options, f, 1);
			return;
		}

		if (!focusPress)
			return;
		if (f == FOCUS_GATE) {
			int pos = cursor().step;
			if (pos >= 0) {
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
		int& point = start ? t.loopStart : t.loopEnd;
		point = point == target ? -1 : target;
	}
};

} // namespace iqs
