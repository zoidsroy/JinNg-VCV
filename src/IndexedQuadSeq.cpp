#include "plugin.hpp"
#include "Expander.hpp"
#include "Serialize.hpp"
#include "core/Engine.hpp"
#include "core/Midi.hpp"
#include "core/NoteFormat.hpp"
#include "core/Transform.hpp"
#include "core/UndoTracker.hpp"
#include "ui/Components.hpp"
#include <atomic>
#include <fstream>
#include <iterator>
#include <osdialog.h>

// The sequencer itself is iqs::Engine (src/core), which has no Rack dependency. This
// module only turns params and jacks into Engine events and reads its outputs back.
// All panel input is handled on the audio thread (buttons are params, encoder turns
// arrive through atomics), so the audio thread is the only writer of the sequence and
// no locking is needed.

using iqs::NUM_TRACKS;
using iqs::FOCUS_INDEX;
using iqs::FOCUS_TRACK;
using iqs::FOCUS_PATTERN;
using iqs::FOCUS_STEP;
using iqs::FOCUS_SNAPSHOT;
using iqs::FOCUS_VOLTAGE;
using iqs::FOCUS_CV_A;
using iqs::FOCUS_CV_B;
using iqs::FOCUS_DURATION;
using iqs::FOCUS_GATE;
using iqs::FOCUS_LEN;
using iqs::FOCUS_PART;
using iqs::NUM_SEQUENCER_FOCUS;

static const char* const FOCUS_NAMES[NUM_SEQUENCER_FOCUS] = {
	"Index", "Track", "Pattern", "Step", "Snapshot",
	"Voltage", "CV-A", "CV-B", "Duration", "Gate",
};

struct IndexedQuadSeq : Module {
	enum ParamId {
		ENUMS(FOCUS_PARAM, NUM_SEQUENCER_FOCUS),
		TABLE_PARAM,
		MODE_PARAM,
		SMOOTH_PARAM,
		INSERT_PARAM,
		DELETE_PARAM,
		MATH_PARAM,
		COPY_PARAM,
		LOAD_PARAM,
		SAVE_PARAM,
		LOOP_START_PARAM,
		LOOP_END_PARAM,
		PAUSE_PARAM,
		RESET_PARAM,
		COMMIT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(CV_A_OUTPUT, NUM_TRACKS),
		ENUMS(CV_B_OUTPUT, NUM_TRACKS),
		ENUMS(GATE_OUTPUT, NUM_TRACKS),
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(FOCUS_LIGHT, NUM_SEQUENCER_FOCUS),
		VOLTAGE_GRAIN_LIGHT,
		SMOOTH_LIGHT,
		COPY_LIGHT,
		LOOP_START_LIGHT,
		LOOP_END_LIGHT,
		PAUSE_LIGHT,
		COMMIT_LIGHT,
		LIGHTS_LEN
	};

	// Switch positions, matching CKSSThree's frames (0 = handle down).
	enum TablePos { TABLE_POS_B, TABLE_POS_REF, TABLE_POS_A };
	enum ModePos { MODE_POS_FOLLOW, MODE_POS_EDIT, MODE_POS_HOLD };

	iqs::Engine engine;

	// This panel's buttons: the Engine button and the param behind each.
	static constexpr int NUM_OWN_BUTTONS = NUM_SEQUENCER_FOCUS + 10;
	int ownButtons[NUM_OWN_BUTTONS];
	int ownParams[NUM_OWN_BUTTONS];
	// Which Engine buttons are down, from this panel or the expander.
	bool buttonDown[iqs::BUTTON_LEN] = {};

	// Buffers for what a Sequencer Controller on the right sends us.
	expander::ToSequencer fromController[2];

	dsp::BooleanTrigger pauseTrigger;
	bool resetButtonDown = false;
	bool resetButtonQuantized = false;
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::ClockDivider displayDivider;

	// Written by the UI thread (encoder widgets, context menu), drained by process().
	std::atomic<int> leftTurns{0};
	std::atomic<int> rightTurns{0};
	std::atomic<bool> loadDemoRequested{false};
	// MIDI import: the UI thread builds the sequence here, the audio thread swaps it in.
	iqs::Sequence importBuffer;
	std::atomic<bool> importRequested{false};

	// Published by process() for the displays.
	iqs::PanelView view;

	// --- Undo (Ctrl+Z) ---------------------------------------------------------
	// The sequence only changes on the audio thread, but Rack's history lives on the UI
	// thread. iqs::UndoTracker (UI thread) decides when to take a copy of the edited
	// sequence and whether it is a new undo step. Copies travel audio -> UI through
	// captureRequested -> captured -> captureReady, and undo/redo states travel back
	// through restoreBuffer -> restoreRequested.
	std::atomic<uint32_t> editGenerationPub{0};
	std::atomic<bool> captureRequested{false};
	std::atomic<bool> captureReady{false};
	iqs::Sequence captured;
	std::atomic<bool> restoreRequested{false};
	iqs::Sequence restoreBuffer;
	// Set when the edited sequence was replaced wholesale (patch load, initialize, mode
	// change): the next capture only becomes the new baseline, not an undo step. Rack
	// records its own undo steps for initialize and preset loads.
	std::atomic<bool> baselineRequested{true};
	int lastMode = iqs::MODE_EDIT;

	// UI thread only.
	iqs::UndoTracker undo;

	void uiStep();
	void requestRestore(const iqs::Sequence& s);
	void importMidi();

	IndexedQuadSeq() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int f = 0; f < NUM_SEQUENCER_FOCUS; f++)
			configButton(FOCUS_PARAM + f, string::f("%s focus", FOCUS_NAMES[f]));
		configSwitch(TABLE_PARAM, 0.f, 2.f, TABLE_POS_A, "Table", {"B", "Reference", "A"});
		configSwitch(MODE_PARAM, 0.f, 2.f, MODE_POS_EDIT, "Mode", {"Follow", "Edit", "Hold"});
		configButton(SMOOTH_PARAM, "Smooth");
		configButton(INSERT_PARAM, "Insert");
		configButton(DELETE_PARAM, "Delete");
		configButton(MATH_PARAM, "Math");
		configButton(COPY_PARAM, "Copy");
		configButton(LOAD_PARAM, "Load snapshot");
		configButton(SAVE_PARAM, "Save snapshot");
		configButton(LOOP_START_PARAM, "Loop start");
		configButton(LOOP_END_PARAM, "Loop end");
		configButton(PAUSE_PARAM, "Pause");
		configButton(RESET_PARAM, "Reset (hold TRACK, PATTERN or STEP to quantize)");
		configButton(COMMIT_PARAM, "Commit");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		for (int t = 0; t < NUM_TRACKS; t++) {
			configOutput(CV_A_OUTPUT + t, string::f("Track %d CV-A", t + 1));
			configOutput(CV_B_OUTPUT + t, string::f("Track %d CV-B", t + 1));
			configOutput(GATE_OUTPUT + t, string::f("Track %d gate", t + 1));
		}

		int n = 0;
		auto own = [&](int button, int param) {
			ownButtons[n] = button;
			ownParams[n] = param;
			n++;
		};
		for (int f = 0; f < NUM_SEQUENCER_FOCUS; f++)
			own(f, FOCUS_PARAM + f);
		own(iqs::BUTTON_INSERT, INSERT_PARAM);
		own(iqs::BUTTON_DELETE, DELETE_PARAM);
		own(iqs::BUTTON_MATH, MATH_PARAM);
		own(iqs::BUTTON_COPY, COPY_PARAM);
		own(iqs::BUTTON_LOAD, LOAD_PARAM);
		own(iqs::BUTTON_SAVE, SAVE_PARAM);
		own(iqs::BUTTON_LOOP_START, LOOP_START_PARAM);
		own(iqs::BUTTON_LOOP_END, LOOP_END_PARAM);
		own(iqs::BUTTON_SMOOTH, SMOOTH_PARAM);
		own(iqs::BUTTON_COMMIT, COMMIT_PARAM);

		rightExpander.producerMessage = &fromController[0];
		rightExpander.consumerMessage = &fromController[1];

		displayDivider.setDivision(512);
		engine.panel.refs = &gRefTables;
	}

	// Initialize: a blank sequencer, snapshots included.
	void onReset() override {
		engine.live.clearAll();
		engine.snapshots.clear();
		engine.snapshotSlot = 1;
		iqs::Panel& p = engine.panel;
		p.leftFocus = FOCUS_TRACK;
		p.rightFocus = FOCUS_CV_A;
		p.track = 0;
		p.clip.clear();
		engine.tr.paused = false;
		engine.liveReplaced();
		baselineRequested = true;
	}

	// "Load example sequence": a short four-track example to play with.
	void loadDemo() {
		iqs::Sequence& seq = engine.live;
		seq.clearAll();
		auto step = [](int cvA, int cvB, int duration, int gate) {
			iqs::Step s;
			s.cvA = (uint8_t) cvA;
			s.cvB = (uint8_t) cvB;
			s.duration = (uint8_t) duration;
			s.gate = (uint8_t) gate;
			return s;
		};
		// Track 1: C major then F major arpeggio, one pattern each, 4 pulses per note.
		const int arpC[4] = {24, 28, 31, 36};
		const int arpF[4] = {29, 33, 36, 41};
		for (int i = 0; i < 4; i++)
			seq.appendStep(0, step(arpC[i], 60 - i * 10, 4, 2));
		seq.appendPattern(0);
		for (int i = 0; i < 4; i++)
			seq.appendStep(0, step(arpF[i], 60 - i * 10, 4, 2));
		// Track 2: bass, legato half notes.
		seq.appendStep(1, step(12, 48, 16, 16));
		seq.appendStep(1, step(17, 48, 16, 16));
		// Track 3: syncopated rhythm, with a zero-length step that is skipped.
		seq.appendStep(2, step(36, 96, 3, 1));
		seq.appendStep(2, step(36, 40, 3, 1));
		seq.appendStep(2, step(99, 99, 0, 5));
		seq.appendStep(2, step(36, 70, 2, 1));
		// Track 4: loops steps 2-3 after playing step 1 once.
		seq.appendStep(3, step(48, 24, 8, 1));
		seq.appendStep(3, step(43, 24, 4, 2));
		seq.appendStep(3, step(45, 24, 4, 2));
		seq.tracks[3].loopStart = 1;
		seq.tracks[3].loopEnd = 2;
		engine.liveReplaced();
		// Loading the demo is itself undoable, so it is not a baseline.
		engine.editGeneration++;
	}

	float ledBrightness(iqs::Led led) {
		if (led == iqs::LED_BLINK)
			return engine.panel.blinkPhase() ? 1.f : 0.f;
		return led == iqs::LED_ON ? 1.f : 0.f;
	}

	void process(const ProcessArgs& args) override {
		if (loadDemoRequested.exchange(false))
			loadDemo();
		if (importRequested) {
			engine.live = importBuffer;
			engine.liveReplaced();
			engine.editGeneration++; // undoable
			importRequested = false;
		}
		if (restoreRequested) {
			engine.restoreEdited(restoreBuffer);
			restoreRequested = false;
		}

		// --- Switches.
		switch ((int) params[TABLE_PARAM].getValue()) {
			case TABLE_POS_B: engine.panel.table = iqs::TABLE_B; break;
			case TABLE_POS_REF: engine.panel.table = iqs::TABLE_REF; break;
			default: engine.panel.table = iqs::TABLE_A; break;
		}
		switch ((int) params[MODE_PARAM].getValue()) {
			case MODE_POS_FOLLOW: engine.setMode(iqs::MODE_FOLLOW); break;
			case MODE_POS_HOLD: engine.setMode(iqs::MODE_HOLD); break;
			default: engine.setMode(iqs::MODE_EDIT); break;
		}
		// Undo steps belong to one edited sequence; switching between live and the HOLD
		// shadow starts afresh.
		if (engine.mode != lastMode) {
			lastMode = engine.mode;
			baselineRequested = true;
		}

		// --- Buttons and encoders, ours and the expander's.
		bool attached = rightExpander.module && rightExpander.module->model == modelSequencerController;
		engine.setExpander(attached);
		const expander::ToSequencer* ctrl = attached ? (const expander::ToSequencer*) rightExpander.consumerMessage : nullptr;
		auto button = [&](int b, bool down) {
			if (down && !buttonDown[b])
				engine.press(b);
			else if (!down && buttonDown[b])
				engine.release(b);
			buttonDown[b] = down;
		};
		for (int i = 0; i < NUM_OWN_BUTTONS; i++)
			button(ownButtons[i], params[ownParams[i]].getValue() > 0.f);
		// Unplugging the expander lets go of anything held on it.
		for (int i = 0; i < expander::NUM_BUTTONS; i++)
			button(expander::BUTTONS[i], ctrl && ctrl->buttons[i]);
		if (ctrl) {
			engine.transition = clamp(ctrl->switches[expander::SWITCH_TRANSITION], 0, 2);
			engine.setPartInputs(ctrl->connected[expander::INPUT_SELECT], ctrl->inputs[expander::INPUT_SELECT],
			                     ctrl->inputs[expander::INPUT_ACTIVATE] >= 1.5f);
			// The modulation bus: an unpatched gate reads low, an unpatched CV 0V.
			iqs::ModBus bus;
			const int cvIn[3] = {expander::INPUT_X_CV, expander::INPUT_Y_CV, expander::INPUT_Z_CV};
			for (int c = 0; c < 3; c++) {
				bus.cv[c] = ctrl->inputs[cvIn[c]];
				bus.gate[c] = ctrl->connected[cvIn[c] + 1] && ctrl->inputs[cvIn[c] + 1] > 1.5f;
			}
			// Switch positions: type 0 low / 1 slope / 2 high; channel 0 Z / 1 Y / 2 X.
			engine.setModulation(bus, clamp(ctrl->switches[expander::SWITCH_MODIFIER_TYPE], 0, 2),
			                     2 - clamp(ctrl->switches[expander::SWITCH_MODIFIER_CHANNEL], 0, 2));
			// Recording inputs; the digital ones (D-1, D-2, PUNCH) trigger above 1.5V.
			iqs::RecordInputs rec;
			rec.a1 = ctrl->inputs[expander::INPUT_A1];
			rec.a2 = ctrl->inputs[expander::INPUT_A2];
			rec.ad1 = ctrl->inputs[expander::INPUT_AD1];
			rec.ad2 = ctrl->inputs[expander::INPUT_AD2];
			rec.a1Patched = ctrl->connected[expander::INPUT_A1];
			rec.a2Patched = ctrl->connected[expander::INPUT_A2];
			rec.ad1Patched = ctrl->connected[expander::INPUT_AD1];
			rec.ad2Patched = ctrl->connected[expander::INPUT_AD2];
			rec.d1 = ctrl->inputs[expander::INPUT_D1] > 1.5f;
			rec.d2 = ctrl->inputs[expander::INPUT_D2] > 1.5f;
			rec.punchGate = ctrl->connected[expander::INPUT_PUNCH] && ctrl->inputs[expander::INPUT_PUNCH] > 1.5f;
			// Switch positions: 0 real-time, 1 step, 2 alter.
			engine.setRecordInputs(rec, clamp(ctrl->switches[expander::SWITCH_RECORD_MODE], 0, 2));
		}
		engine.turnLeft(leftTurns.exchange(0));
		engine.turnRight(rightTurns.exchange(0));

		if (pauseTrigger.process(params[PAUSE_PARAM].getValue() > 0.f))
			engine.tr.paused = !engine.tr.paused;

		// --- RESET. The button is a plain reset unless a TRACK/PATTERN/STEP focus button
		// is held, which quantizes it; held high (input or plain button press) it keeps
		// the sequencer parked.
		bool resetButton = params[RESET_PARAM].getValue() > 0.f;
		bool resetEdge = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f);
		if (resetButton && !resetButtonDown) {
			resetButtonQuantized = engine.pressResetButton();
			resetEdge |= !resetButtonQuantized;
		}
		resetButtonDown = resetButton;
		bool resetHeld = resetTrigger.isHigh() || (resetButton && !resetButtonQuantized);
		bool clockEdge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f);

		engine.process(args.sampleTime, clockEdge, resetEdge, resetHeld);

		for (int t = 0; t < NUM_TRACKS; t++) {
			outputs[CV_A_OUTPUT + t].setVoltage(engine.cvA(t));
			outputs[CV_B_OUTPUT + t].setVoltage(engine.cvB(t));
			outputs[GATE_OUTPUT + t].setVoltage(engine.gate(t) ? 10.f : 0.f);
		}

		// --- Lights and displays.
		if (displayDivider.process()) {
			const iqs::Panel& p = engine.panel;
			const iqs::Sequence& shown = engine.editSeq();
			for (int f = 0; f < NUM_SEQUENCER_FOCUS; f++)
				lights[FOCUS_LIGHT + f].setBrightness(ledBrightness(engine.focusLed(f)));
			lights[COPY_LIGHT].setBrightness(p.copyLed());
			lights[SMOOTH_LIGHT].setBrightness(p.smoothLed(shown));
			lights[VOLTAGE_GRAIN_LIGHT].setBrightness(ledBrightness(p.grainLed(shown)));
			lights[LOOP_START_LIGHT].setBrightness(ledBrightness(p.loopLed(shown, true)));
			lights[LOOP_END_LIGHT].setBrightness(ledBrightness(p.loopLed(shown, false)));
			lights[PAUSE_LIGHT].setBrightness(engine.tr.paused);
			lights[COMMIT_LIGHT].setBrightness(ledBrightness(engine.commitLed()));
			view = engine.view();
			if (attached)
				sendToController(ctrl);
		}

		editGenerationPub = engine.editGeneration;
		if (captureRequested && !captureReady) {
			captured = engine.editSeq();
			captureRequested = false;
			captureReady = true;
		}
	}

	static int validFocus(int f, bool left, int fallback) {
		return (f >= 0 && f < FOCUS_LEN && iqs::isLeftFocus(f) == left) ? f : fallback;
	}

	// Lights and displays of the expander, sent at display rate.
	void sendToController(const expander::ToSequencer* ctrl) {
		expander::ToController* out = (expander::ToController*) rightExpander.module->leftExpander.producerMessage;
		if (!out)
			return;
		const iqs::Panel& p = engine.panel;
		for (float& l : out->lights)
			l = 0.f;
		out->lights[expander::LIGHT_PART_FOCUS] = ledBrightness(engine.focusLed(iqs::FOCUS_PART));
		out->lights[expander::LIGHT_GROUP_FOCUS] = ledBrightness(engine.focusLed(iqs::FOCUS_GROUP));
		out->lights[expander::LIGHT_GROUP_MEMBER] = p.groupMemberLed(engine.editSeq());
		out->lights[expander::LIGHT_MODIFIER_FOCUS] = ledBrightness(engine.focusLed(iqs::FOCUS_GROUP_MODIFIER));
		// ARM: the selected track is armed (blinking while the configuration screen is up).
		// REC: recording is under way.
		const iqs::Recorder& rec = engine.recorder;
		out->lights[expander::LIGHT_ARM] =
			rec.configScreen ? (p.blinkPhase() ? 1.f : 0.f) : (float) rec.armed[p.track];
		out->lights[expander::LIGHT_REC] = rec.recording(engine.tr.paused);
		out->lights[expander::LIGHT_ACTIVATE] = ctrl->inputs[expander::INPUT_ACTIVATE] >= 1.5f;
		out->lights[expander::LIGHT_RESET_TO] = p.resetToLed(engine.editSeq());
		// The focused part, with a dot when it is the one playing.
		std::snprintf(out->part, sizeof(out->part), "%d%s", p.focusedPart, p.focusedPart == engine.playingPart ? "." : "");
		// The focused group (1-16), with a dot when it has members.
		std::snprintf(out->group, sizeof(out->group), "%d%s", p.focusedGroup + 1,
		              engine.editSeq().groupHasMembers(p.focusedGroup) ? "." : "");
		rightExpander.module->leftExpander.requestMessageFlip();
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "sequence", iqs::sequenceToJson(engine.live));
		// Saved slots only; templates have negative slot numbers (see Snapshots.hpp).
		json_t* snapshotsJ = json_array();
		for (int slot = -iqs::NUM_TEMPLATES; slot <= iqs::NUM_REGULAR_SLOTS; slot++) {
			const iqs::Sequence* saved = engine.snapshots.get(slot);
			if (!saved)
				continue;
			json_t* snapJ = json_object();
			json_object_set_new(snapJ, "slot", json_integer(slot));
			json_object_set_new(snapJ, "sequence", iqs::sequenceToJson(*saved));
			json_array_append_new(snapshotsJ, snapJ);
		}
		json_object_set_new(rootJ, "snapshots", snapshotsJ);
		json_object_set_new(rootJ, "snapshotSlot", json_integer(engine.snapshotSlot));
		json_object_set_new(rootJ, "resetMode", json_integer(engine.tr.resetMode));
		json_object_set_new(rootJ, "paused", json_boolean(engine.tr.paused));
		json_object_set_new(rootJ, "leftFocus", json_integer(engine.panel.leftFocus));
		json_object_set_new(rootJ, "rightFocus", json_integer(engine.panel.rightFocus));
		json_object_set_new(rootJ, "selectedTrack", json_integer(engine.panel.track));
		json_object_set_new(rootJ, "focusedPart", json_integer(engine.panel.focusedPart));
		json_object_set_new(rootJ, "playingPart", json_integer(engine.playingPart));
		const iqs::RealtimeConfig& rc = engine.recorder.config;
		json_t* recJ = json_object();
		json_object_set_new(recJ, "cvATrigger", json_boolean(rc.cvATrigger));
		json_object_set_new(recJ, "cvBTrigger", json_boolean(rc.cvBTrigger));
		json_object_set_new(recJ, "durationGrid", json_integer(rc.durationGrid));
		json_object_set_new(recJ, "gateGrid", json_integer(rc.gateGrid));
		json_object_set_new(recJ, "focus", json_integer(rc.focus));
		json_object_set_new(rootJ, "recording", recJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		iqs::sequenceFromJson(engine.live, json_object_get(rootJ, "sequence"));
		engine.snapshots.clear();
		json_t* snapshotsJ = json_object_get(rootJ, "snapshots");
		iqs::Sequence loaded;
		for (size_t i = 0; i < json_array_size(snapshotsJ); i++) {
			json_t* snapJ = json_array_get(snapshotsJ, i);
			int slot = (int) json_integer_value(json_object_get(snapJ, "slot"));
			if (!iqs::SnapshotStore::storable(slot))
				continue;
			iqs::sequenceFromJson(loaded, json_object_get(snapJ, "sequence"));
			engine.snapshots.save(slot, loaded);
		}
		if (json_t* j = json_object_get(rootJ, "snapshotSlot"))
			engine.snapshotSlot = clamp((int) json_integer_value(j), -iqs::NUM_TEMPLATES, iqs::NUM_REGULAR_SLOTS);
		if (json_t* j = json_object_get(rootJ, "resetMode"))
			engine.tr.resetMode = clamp((int) json_integer_value(j), 0, (int) iqs::RESET_STARTS_FIRST_STEP);
		if (json_t* j = json_object_get(rootJ, "paused"))
			engine.tr.paused = json_boolean_value(j);
		if (json_t* j = json_object_get(rootJ, "leftFocus"))
			engine.panel.leftFocus = validFocus((int) json_integer_value(j), true, FOCUS_TRACK);
		if (json_t* j = json_object_get(rootJ, "rightFocus"))
			engine.panel.rightFocus = validFocus((int) json_integer_value(j), false, FOCUS_CV_A);
		if (json_t* j = json_object_get(rootJ, "selectedTrack"))
			engine.panel.track = clamp((int) json_integer_value(j), 0, NUM_TRACKS - 1);
		engine.liveReplaced();
		if (json_t* recJ = json_object_get(rootJ, "recording")) {
			iqs::RealtimeConfig& rc = engine.recorder.config;
			rc.cvATrigger = json_boolean_value(json_object_get(recJ, "cvATrigger"));
			rc.cvBTrigger = json_boolean_value(json_object_get(recJ, "cvBTrigger"));
			rc.durationGrid = clamp((int) json_integer_value(json_object_get(recJ, "durationGrid")), 1, iqs::MAX_VALUE);
			rc.gateGrid = clamp((int) json_integer_value(json_object_get(recJ, "gateGrid")), 1, iqs::MAX_VALUE);
			int focus = (int) json_integer_value(json_object_get(recJ, "focus"));
			rc.focus = (focus == FOCUS_PATTERN || focus == FOCUS_STEP) ? focus : FOCUS_TRACK;
		}
		if (json_t* j = json_object_get(rootJ, "focusedPart"))
			engine.panel.focusedPart = clamp((int) json_integer_value(j), 0, iqs::NUM_PARTS - 1);
		if (json_t* j = json_object_get(rootJ, "playingPart")) {
			engine.playingPart = clamp((int) json_integer_value(j), 0, iqs::NUM_PARTS - 1);
			engine.tr.stopped = engine.playingPart == iqs::STOP_PART;
			engine.syncPanelParts();
		}
		baselineRequested = true;
	}
};

// --- Undo ------------------------------------------------------------------------

// One undo step: the edited sequence before and after a burst of panel activity.
struct SequenceEditAction : history::ModuleAction {
	iqs::Sequence before;
	iqs::Sequence after;

	SequenceEditAction() {
		name = "edit sequence";
	}
	void apply(const iqs::Sequence& s) {
		IndexedQuadSeq* m = dynamic_cast<IndexedQuadSeq*>(APP->engine->getModule(moduleId));
		if (m)
			m->requestRestore(s);
	}
	void undo() override {
		apply(before);
	}
	void redo() override {
		apply(after);
	}
};

void IndexedQuadSeq::uiStep() {
	if (baselineRequested.exchange(false))
		undo.rebase();
	bool ready = captureReady;
	int64_t moduleId = id;
	undo.step(system::getTime(), editGenerationPub, ready ? &captured : nullptr,
		[moduleId](const iqs::Sequence& before, const iqs::Sequence& after) {
			SequenceEditAction* a = new SequenceEditAction;
			a->moduleId = moduleId;
			a->before = before;
			a->after = after;
			APP->history->push(a);
		});
	if (ready)
		captureReady = false;
	if (undo.wantCapture && !captureRequested && !captureReady) {
		captureRequested = true;
		undo.captureRequested();
	}
}

// Import MIDI file (UI thread): channels 1-4 become tracks 1-4 and replace everything
// (see src/core/Midi.hpp). Undoable.
void IndexedQuadSeq::importMidi() {
	osdialog_filters* filters = osdialog_filters_parse("MIDI files:mid,midi,smf");
	char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
	osdialog_filters_free(filters);
	if (!path)
		return;
	std::ifstream file(path, std::ios::binary);
	std::free(path);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	iqs::MidiSong song;
	std::string error;
	if (!iqs::parseMidi(bytes.data(), bytes.size(), song, error)) {
		osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, ("Could not import: " + error + ".").c_str());
		return;
	}
	if (importRequested)
		return; // the previous import has not been applied yet
	bool truncated = false;
	if (!iqs::midiToSequence(song, importBuffer, iqs::MidiImportOptions(), truncated)) {
		osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "No notes on MIDI channels 1-4.");
		return;
	}
	importRequested = true;
	if (truncated)
		osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, "The file was longer than 2000 steps; the rest was left out.");
}

void IndexedQuadSeq::requestRestore(const iqs::Sequence& s) {
	restoreBuffer = s;
	restoreRequested = true;
	undo.restored(s);
}

// ---------------------------------------------------------------------------
// Layout, in mm. Measured off the hardware's 26HP panel so the controls sit where a
// hardware user expects them.

namespace layout {
// Rows of the display grid.
static constexpr float ROW_TOP = 17.0f;
static const float ROWS[4] = {33.0f, 48.9f, 64.9f, 80.8f};
static constexpr float JACK_ROW_1 = 97.8f;
static constexpr float JACK_ROW_2 = 113.4f;

// Left display column (INDEX, TRACK, PATTERN, STEP, SNAPSHOT).
static constexpr float L_DISPLAY_X = 49.1f;
static constexpr float L_LED_X = 59.0f;
static constexpr float L_BUTTON_X = 66.6f;
// Right display column (VOLTAGE, CV-A, CV-B, DURATION, GATE).
static constexpr float R_BUTTON_X = 80.5f;
static constexpr float R_LED_X = 87.4f;
static constexpr float R_DISPLAY_X = 97.9f;
static constexpr float VOLTAGE_DISPLAY_X = 105.0f;

static constexpr float DISPLAY_W = 14.2f;
static constexpr float VOLTAGE_DISPLAY_W = 28.2f;
static constexpr float DISPLAY_H = 7.9f;

// Blue function buttons down the left edge.
static constexpr float FN_COL_1 = 12.4f;
static constexpr float FN_COL_2 = 27.6f;

static constexpr float LOOP_LED_X = 111.1f;
static constexpr float LOOP_BUTTON_X = 119.4f;

static const float TRACK_JACK_X[2][3] = {
	{39.4f, 52.3f, 65.5f},
	{78.8f, 91.8f, 105.0f},
};
} // namespace layout

struct IndexedQuadSeqWidget : ModuleWidget {
	IndexedQuadSeq* seq;

	SevenSegDisplay* addDisplay(float cx, float cy, float w, int digits, std::string fallback,
	                           std::function<std::string()> getText = nullptr) {
		SevenSegDisplay* d = new SevenSegDisplay;
		d->box.size = mm2px(Vec(w, layout::DISPLAY_H));
		d->box.pos = mm2px(Vec(cx, cy)).minus(d->box.size.div(2));
		d->digits = digits;
		d->fallback = fallback;
		d->getText = getText;
		addChild(d);
		return d;
	}

	// Text for a display, computed from the view the engine last published. In the
	// module browser (no module) the display shows `fallback`.
	std::function<std::string()> fromView(std::string fallback, std::function<std::string(const iqs::PanelView&)> text) {
		IndexedQuadSeq* m = seq;
		return [m, fallback, text]() {
			if (!m)
				return fallback;
			iqs::PanelView v = m->view;
			return text(v);
		};
	}

	// A display in the left column below INDEX. Row r shows `normal` usually, the MATH
	// operation code of the r-th step parameter on the MATH screen, and `option` (if any)
	// on the track options screen.
	std::function<std::string()> leftRow(int r, std::string fallback, std::function<std::string(const iqs::PanelView&)> normal,
	                                     std::function<std::string(const iqs::PanelView&)> option = nullptr) {
		return fromView(fallback, [r, normal, option](const iqs::PanelView& v) {
			if (v.mathScreen && v.expander)
				return std::string(iqs::transformCode(v.transform[r], v.transformOp[r]));
			if (v.mathScreen)
				return std::string(iqs::mathCode(v.math[r]));
			if (v.optionsScreen && option)
				return option(v);
			return normal(v);
		});
	}

	// A display in the right column: a field of the cursor's step ("--" without one), the
	// MATH operand on the MATH screen, or a track option on the options screen.
	std::function<std::string()> rightRow(int r, std::function<std::string(const iqs::Step&)> field,
	                                      std::function<std::string(const iqs::TrackOptions&)> option) {
		return fromView("--", [r, field, option](const iqs::PanelView& v) {
			// The real-time recording configuration: CV-A/CV-B trigger new steps (tr) or not
			// (--), then the DURATION and GATE quantization grids.
			if (v.recordConfig) {
				switch (r) {
					case iqs::MATH_CV_A: return std::string(v.recordCvATrigger ? "tr" : "--");
					case iqs::MATH_CV_B: return std::string(v.recordCvBTrigger ? "tr" : "--");
					case iqs::MATH_DURATION: return std::to_string(v.recordDurationGrid);
					default: return std::to_string(v.recordGateGrid);
				}
			}
			if (v.slopeScreen)
				return iqs::formatSlopeShort(v.slopes[r]);
			if (v.mathScreen && v.expander)
				return std::to_string(iqs::transformValue(v.transform[r], v.transformOp[r]));
			if (v.mathScreen)
				return std::to_string(std::abs((int) v.math[r].operand));
			if (v.optionsScreen)
				return option(v.options);
			return v.stepInPattern >= 0 ? field(v.step) : std::string("--");
		});
	}

	void step() override {
		if (seq)
			seq->uiStep();
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		IndexedQuadSeq* m = seq;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Reset behaviour",
			{"Next clock plays the first step", "First step sounds at the reset"},
			[m]() { return m->engine.tr.resetMode; },
			[m](int mode) { m->engine.tr.resetMode = mode; }));
		menu->addChild(createMenuItem("Import MIDI file...", "", [m]() { m->importMidi(); }));
		menu->addChild(createMenuItem("Load example sequence", "", [m]() { m->loadDemoRequested = true; }));
	}

	void addFocus(int focus, float buttonX, float ledX, float y) {
		addParam(createParamCentered<GrayButton>(mm2px(Vec(buttonX, y)), seq, IndexedQuadSeq::FOCUS_PARAM + focus));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(ledX, y)), seq, IndexedQuadSeq::FOCUS_LIGHT + focus));
	}

	void addEncoder(Vec posMm, std::atomic<int> IndexedQuadSeq::*turns) {
		Encoder* e = new Encoder;
		e->box.pos = mm2px(posMm).minus(e->box.size.div(2));
		IndexedQuadSeq* m = seq;
		e->onTurn = [m, turns](int d) {
			if (m)
				(m->*turns) += d;
		};
		addChild(e);
	}

	IndexedQuadSeqWidget(IndexedQuadSeq* module) : seq(module) {
		using namespace layout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/IndexedQuadSeq.svg"),
		                      asset::plugin(pluginInstance, "res/IndexedQuadSeq-dark.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// --- Top strip: TABLE switch, SMOOTH, INDEX, VOLTAGE.
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(14.0f, ROW_TOP)), module, IndexedQuadSeq::TABLE_PARAM));
		addParam(createParamCentered<BlueButton>(mm2px(Vec(FN_COL_2, ROW_TOP)), module, IndexedQuadSeq::SMOOTH_PARAM));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(37.2f, ROW_TOP)), module, IndexedQuadSeq::SMOOTH_LIGHT));

		// INDEX and VOLTAGE show the cursor step's entry in the table the TABLE switch picks;
		// VOLTAGE also shows the panel's messages (AFtr, FULL, TILt, Abrt ...).
		// With PART focused, INDEX blinks the pending part (if any).
		addDisplay(L_DISPLAY_X, ROW_TOP, DISPLAY_W, 2, "0", fromView("0", [](const iqs::PanelView& v) {
			if (v.euclid)
				return std::to_string(v.euclidN);
			if (v.leftFocus == FOCUS_PART)
				return v.partPending >= 0 && v.blink ? std::to_string(v.partPending) : std::string("");
			if (v.leftFocus == iqs::FOCUS_GROUP && !v.mathScreen && !v.slopeScreen)
				return std::string("nS");
			return std::to_string(v.index);
		}));
		addFocus(FOCUS_INDEX, L_BUTTON_X, L_LED_X, ROW_TOP);

		// VOLTAGE has two LEDs: its focus LED above, the editing granularity below (off fine,
		// on coarse, blinking super coarse).
		addParam(createParamCentered<GrayButton>(mm2px(Vec(R_BUTTON_X, ROW_TOP)), module, IndexedQuadSeq::FOCUS_PARAM + FOCUS_VOLTAGE));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(R_LED_X, ROW_TOP - 2.3f)), module, IndexedQuadSeq::FOCUS_LIGHT + FOCUS_VOLTAGE));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(R_LED_X, ROW_TOP + 2.3f)), module, IndexedQuadSeq::VOLTAGE_GRAIN_LIGHT));
		addDisplay(VOLTAGE_DISPLAY_X, ROW_TOP, VOLTAGE_DISPLAY_W, 4, "0.C.00", fromView("0.C.00", [](const iqs::PanelView& v) {
			if (v.message)
				return std::string(v.message);
			// Choosing a Euclidean mask: "3" on INDEX and "Eu.8" here read as E(3, 8).
			if (v.euclid)
				return std::string("Eu.") + std::to_string(v.euclidM);
			if (v.slopeScreen)
				return iqs::formatSlope(v.slopes[v.slopeParam]);
			// With GROUP focused: how many of this track's steps are in the group.
			if (v.leftFocus == iqs::FOCUS_GROUP && !v.mathScreen)
				return std::to_string(v.groupCount);
			// With PART focused: the focused part at a glance, one digit per track with
			// bars for RESET TO (top), LOOP START (middle) and LOOP END (bottom).
			if (v.leftFocus == FOCUS_PART) {
				std::string bars;
				for (int t = 0; t < NUM_TRACKS; t++)
					bars += (char) (0x80 | v.partOverview[t]);
				return bars;
			}
			return v.noteDisplay ? iqs::formatNote(v.voltage) : iqs::formatVoltage(v.voltage);
		}));

		// --- Display grid.
		const int leftFocus[4] = {FOCUS_TRACK, FOCUS_PATTERN, FOCUS_STEP, FOCUS_SNAPSHOT};
		const int rightFocus[4] = {FOCUS_CV_A, FOCUS_CV_B, FOCUS_DURATION, FOCUS_GATE};
		for (int r = 0; r < 4; r++) {
			addFocus(leftFocus[r], L_BUTTON_X, L_LED_X, ROWS[r]);
			addFocus(rightFocus[r], R_BUTTON_X, R_LED_X, ROWS[r]);
		}
		// Tracks, patterns and steps count from 1, as in the manual.
		addDisplay(L_DISPLAY_X, ROWS[0], DISPLAY_W, 2, "1", leftRow(0, "1", [](const iqs::PanelView& v) {
			return std::to_string(v.track + 1);
		}));
		addDisplay(L_DISPLAY_X, ROWS[1], DISPLAY_W, 2, "--", leftRow(1, "--", [](const iqs::PanelView& v) {
			return v.pattern >= 0 ? std::to_string(v.pattern + 1) : std::string("--");
		}));
		// On the track options screen STEP shows the clock multiplier.
		addDisplay(L_DISPLAY_X, ROWS[2], DISPLAY_W, 2, "--", leftRow(2, "--", [](const iqs::PanelView& v) {
			return v.stepInPattern >= 0 ? std::to_string(v.stepInPattern + 1) : std::string("--");
		}, [](const iqs::PanelView& v) {
			return std::to_string(v.options.clockMul);
		}));
		addDisplay(L_DISPLAY_X, ROWS[3], DISPLAY_W, 2, "1", leftRow(3, "1", [](const iqs::PanelView& v) {
			return iqs::SnapshotStore::name(v.snapshot, v.expander);
		}));
		// On the track options screen the right column shows CV-A/CV-B note (Nt) or number
		// (Nr) display, the clock divider, and gate (Gt) or trigger (tr) output.
		addDisplay(R_DISPLAY_X, ROWS[0], DISPLAY_W, 2, "--", rightRow(iqs::MATH_CV_A,
			[](const iqs::Step& s) { return std::to_string(s.cvA); },
			[](const iqs::TrackOptions& o) { return std::string(o.noteDisplayA ? "Nt" : "Nr"); }));
		addDisplay(R_DISPLAY_X, ROWS[1], DISPLAY_W, 2, "--", rightRow(iqs::MATH_CV_B,
			[](const iqs::Step& s) { return std::to_string(s.cvB); },
			[](const iqs::TrackOptions& o) { return std::string(o.noteDisplayB ? "Nt" : "Nr"); }));
		addDisplay(R_DISPLAY_X, ROWS[2], DISPLAY_W, 2, "--", rightRow(iqs::MATH_DURATION,
			[](const iqs::Step& s) { return std::to_string(s.duration); },
			[](const iqs::TrackOptions& o) { return std::to_string(o.clockDiv); }));
		// GATE lights both decimal points when the step ratchets.
		addDisplay(R_DISPLAY_X, ROWS[3], DISPLAY_W, 2, "--", rightRow(iqs::MATH_GATE,
			[](const iqs::Step& s) {
				std::string digits = string::f("%2d", (int) s.gate);
				if (!s.ratchet)
					return digits;
				return std::string(1, digits[0]) + "." + digits[1] + ".";
			},
			[](const iqs::TrackOptions& o) { return std::string(o.triggerMode ? "tr" : "Gt"); }));

		// --- Encoders.
		addEncoder(Vec(27.2f, 39.5f), &IndexedQuadSeq::leftTurns);
		addEncoder(Vec(118.6f, 39.5f), &IndexedQuadSeq::rightTurns);

		// --- Function buttons.
		addParam(createParamCentered<BlueButton>(mm2px(Vec(FN_COL_1, ROWS[0])), module, IndexedQuadSeq::INSERT_PARAM));
		addParam(createParamCentered<BlueButton>(mm2px(Vec(FN_COL_1, ROWS[1])), module, IndexedQuadSeq::DELETE_PARAM));
		addParam(createParamCentered<BlueButton>(mm2px(Vec(FN_COL_1, ROWS[2])), module, IndexedQuadSeq::MATH_PARAM));
		addParam(createParamCentered<BlueButton>(mm2px(Vec(FN_COL_2, ROWS[2])), module, IndexedQuadSeq::COPY_PARAM));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(36.2f, ROWS[2])), module, IndexedQuadSeq::COPY_LIGHT));
		addParam(createParamCentered<BlueButton>(mm2px(Vec(FN_COL_1, ROWS[3])), module, IndexedQuadSeq::LOAD_PARAM));
		addParam(createParamCentered<BlueButton>(mm2px(Vec(FN_COL_2, ROWS[3])), module, IndexedQuadSeq::SAVE_PARAM));

		// --- Loop.
		addParam(createParamCentered<BlueButton>(mm2px(Vec(LOOP_BUTTON_X, ROWS[2])), module, IndexedQuadSeq::LOOP_START_PARAM));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(LOOP_LED_X, ROWS[2])), module, IndexedQuadSeq::LOOP_START_LIGHT));
		addParam(createParamCentered<BlueButton>(mm2px(Vec(LOOP_BUTTON_X, ROWS[3])), module, IndexedQuadSeq::LOOP_END_PARAM));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(LOOP_LED_X, ROWS[3])), module, IndexedQuadSeq::LOOP_END_LIGHT));

		// --- Jack section.
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(FN_COL_1, 89.4f)), module, IndexedQuadSeq::PAUSE_LIGHT));
		addParam(createParamCentered<RedButton>(mm2px(Vec(FN_COL_1, JACK_ROW_1)), module, IndexedQuadSeq::PAUSE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(26.0f, JACK_ROW_1)), module, IndexedQuadSeq::CLOCK_INPUT));
		addParam(createParamCentered<RedButton>(mm2px(Vec(FN_COL_1, JACK_ROW_2)), module, IndexedQuadSeq::RESET_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(26.0f, JACK_ROW_2)), module, IndexedQuadSeq::RESET_INPUT));

		for (int t = 0; t < NUM_TRACKS; t++) {
			float y = t < 2 ? JACK_ROW_1 : JACK_ROW_2;
			const float* xs = TRACK_JACK_X[t % 2];
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xs[0], y)), module, IndexedQuadSeq::CV_A_OUTPUT + t));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xs[1], y)), module, IndexedQuadSeq::CV_B_OUTPUT + t));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xs[2], y)), module, IndexedQuadSeq::GATE_OUTPUT + t));
		}

		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(LOOP_BUTTON_X, 89.4f)), module, IndexedQuadSeq::COMMIT_LIGHT));
		addParam(createParamCentered<RedButton>(mm2px(Vec(LOOP_BUTTON_X, JACK_ROW_1)), module, IndexedQuadSeq::COMMIT_PARAM));
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(116.4f, 114.0f)), module, IndexedQuadSeq::MODE_PARAM));
	}
};

Model* modelIndexedQuadSeq = createModel<IndexedQuadSeq, IndexedQuadSeqWidget>("IndexedQuadSeq");
