#include "plugin.hpp"
#include "Serialize.hpp"
#include "core/NoteFormat.hpp"
#include "core/Panel.hpp"
#include "core/Sequence.hpp"
#include "core/Transport.hpp"
#include "ui/Components.hpp"
#include <atomic>

// The panel edits the sequence in EDIT mode (src/core/Panel.hpp) while the engine plays
// it; smoothing, ratchet, trigger mode and clock div/mul are in src/core/Transport.hpp. All panel input is handled on the audio thread (buttons are params,
// encoder turns arrive through atomics), so the audio thread is the only writer of the
// sequence and no locking is needed.

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
using iqs::NUM_LEFT_FOCUS;

static const char* const FOCUS_NAMES[FOCUS_LEN] = {
	"Index", "Track", "Pattern", "Step", "Snapshot",
	"Voltage", "CV-A", "CV-B", "Duration", "Gate",
};

struct IndexedQuadSeq : Module {
	enum ParamId {
		ENUMS(FOCUS_PARAM, FOCUS_LEN),
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
		ENUMS(FOCUS_LIGHT, FOCUS_LEN),
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
	enum ModePos { MODE_FOLLOW, MODE_EDIT, MODE_HOLD };

	iqs::Sequence seq;
	iqs::Transport transport;
	iqs::Panel panel;

	// The param behind each iqs::Button.
	int buttonParams[iqs::BUTTON_LEN];
	bool buttonDown[iqs::BUTTON_LEN] = {};

	dsp::BooleanTrigger pauseTrigger;
	dsp::BooleanTrigger resetButtonTrigger;
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::ClockDivider displayDivider;

	// Written by the UI thread (encoder widgets, context menu), drained by process().
	std::atomic<int> leftTurns{0};
	std::atomic<int> rightTurns{0};
	std::atomic<bool> loadDemoRequested{false};
	std::atomic<bool> clearRequested{false};

	// Published by process() for the displays.
	iqs::PanelView view;

	IndexedQuadSeq() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int f = 0; f < FOCUS_LEN; f++)
			configButton(FOCUS_PARAM + f, string::f("%s focus", FOCUS_NAMES[f]));
		configSwitch(TABLE_PARAM, 0.f, 2.f, TABLE_POS_A, "Table", {"B", "Reference", "A"});
		configSwitch(MODE_PARAM, 0.f, 2.f, MODE_EDIT, "Mode", {"Follow", "Edit", "Hold"});
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
		configButton(RESET_PARAM, "Reset");
		configButton(COMMIT_PARAM, "Commit");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		for (int t = 0; t < NUM_TRACKS; t++) {
			configOutput(CV_A_OUTPUT + t, string::f("Track %d CV-A", t + 1));
			configOutput(CV_B_OUTPUT + t, string::f("Track %d CV-B", t + 1));
			configOutput(GATE_OUTPUT + t, string::f("Track %d gate", t + 1));
		}

		for (int f = 0; f < FOCUS_LEN; f++)
			buttonParams[f] = FOCUS_PARAM + f;
		buttonParams[iqs::BUTTON_INSERT] = INSERT_PARAM;
		buttonParams[iqs::BUTTON_DELETE] = DELETE_PARAM;
		buttonParams[iqs::BUTTON_MATH] = MATH_PARAM;
		buttonParams[iqs::BUTTON_COPY] = COPY_PARAM;
		buttonParams[iqs::BUTTON_LOAD] = LOAD_PARAM;
		buttonParams[iqs::BUTTON_SAVE] = SAVE_PARAM;
		buttonParams[iqs::BUTTON_LOOP_START] = LOOP_START_PARAM;
		buttonParams[iqs::BUTTON_LOOP_END] = LOOP_END_PARAM;
		buttonParams[iqs::BUTTON_SMOOTH] = SMOOTH_PARAM;
		buttonParams[iqs::BUTTON_COMMIT] = COMMIT_PARAM;

		displayDivider.setDivision(512);
		panel.refs = &gRefTables;
	}

	void onReset() override {
		for (int t = 0; t < NUM_TRACKS; t++)
			seq.clearTrack(t);
		panel.leftFocus = FOCUS_TRACK;
		panel.rightFocus = FOCUS_CV_A;
		panel.track = 0;
		panel.clip.clear();
		transport.paused = false;
		sequenceReplaced();
	}

	// After the whole sequence was swapped out (load, demo, clear).
	void sequenceReplaced() {
		transport.rewind(seq, false);
		panel.normalizeCursors(seq);
	}

	// A short four-track example to play with.
	void loadDemo() {
		for (int t = 0; t < NUM_TRACKS; t++)
			seq.clearTrack(t);
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
		sequenceReplaced();
	}

	float ledBrightness(iqs::Led led) {
		if (led == iqs::LED_BLINK)
			return panel.blinkPhase() ? 1.f : 0.f;
		return led == iqs::LED_ON ? 1.f : 0.f;
	}

	void process(const ProcessArgs& args) override {
		if (clearRequested.exchange(false))
			onReset();
		if (loadDemoRequested.exchange(false))
			loadDemo();

		// --- Panel input.
		switch ((int) params[TABLE_PARAM].getValue()) {
			case TABLE_POS_B: panel.table = iqs::TABLE_B; break;
			case TABLE_POS_REF: panel.table = iqs::TABLE_REF; break;
			default: panel.table = iqs::TABLE_A; break;
		}
		for (int b = 0; b < iqs::BUTTON_LEN; b++) {
			bool down = params[buttonParams[b]].getValue() > 0.f;
			if (down && !buttonDown[b])
				panel.press(seq, transport, b);
			else if (!down && buttonDown[b])
				panel.release(seq, transport, b);
			buttonDown[b] = down;
		}
		panel.turnLeft(seq, leftTurns.exchange(0));
		panel.turnRight(seq, rightTurns.exchange(0));
		panel.tick(args.sampleTime);

		if (pauseTrigger.process(params[PAUSE_PARAM].getValue() > 0.f))
			transport.paused = !transport.paused;

		// --- Transport. RESET (input or button) held high keeps the sequencer parked.
		bool resetButton = params[RESET_PARAM].getValue() > 0.f;
		bool resetEdge = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f);
		resetEdge |= resetButtonTrigger.process(resetButton);
		bool resetHeld = resetTrigger.isHigh() || resetButton;
		bool clockEdge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f);
		transport.process(seq, args.sampleTime, clockEdge, resetEdge, resetHeld);

		for (int t = 0; t < NUM_TRACKS; t++) {
			outputs[CV_A_OUTPUT + t].setVoltage(transport.cvA(seq, t));
			outputs[CV_B_OUTPUT + t].setVoltage(transport.cvB(seq, t));
			outputs[GATE_OUTPUT + t].setVoltage(transport.gate(seq, t) ? 10.f : 0.f);
		}

		// --- Lights and displays.
		if (displayDivider.process()) {
			for (int f = 0; f < FOCUS_LEN; f++)
				lights[FOCUS_LIGHT + f].setBrightness(ledBrightness(panel.focusLed(f)));
			lights[COPY_LIGHT].setBrightness(panel.copyLed());
			lights[SMOOTH_LIGHT].setBrightness(panel.smoothLed(seq));
			lights[VOLTAGE_GRAIN_LIGHT].setBrightness(ledBrightness(panel.grainLed(seq)));
			lights[LOOP_START_LIGHT].setBrightness(ledBrightness(panel.loopLed(seq, true)));
			lights[LOOP_END_LIGHT].setBrightness(ledBrightness(panel.loopLed(seq, false)));
			lights[PAUSE_LIGHT].setBrightness(transport.paused);
			view = panel.view(seq);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "sequence", iqs::sequenceToJson(seq));
		json_object_set_new(rootJ, "resetMode", json_integer(transport.resetMode));
		json_object_set_new(rootJ, "paused", json_boolean(transport.paused));
		json_object_set_new(rootJ, "leftFocus", json_integer(panel.leftFocus));
		json_object_set_new(rootJ, "rightFocus", json_integer(panel.rightFocus));
		json_object_set_new(rootJ, "selectedTrack", json_integer(panel.track));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		iqs::sequenceFromJson(seq, json_object_get(rootJ, "sequence"));
		if (json_t* j = json_object_get(rootJ, "resetMode"))
			transport.resetMode = clamp((int) json_integer_value(j), 0, (int) iqs::RESET_STARTS_FIRST_STEP);
		if (json_t* j = json_object_get(rootJ, "paused"))
			transport.paused = json_boolean_value(j);
		if (json_t* j = json_object_get(rootJ, "leftFocus"))
			panel.leftFocus = clamp((int) json_integer_value(j), 0, NUM_LEFT_FOCUS - 1);
		if (json_t* j = json_object_get(rootJ, "rightFocus"))
			panel.rightFocus = clamp((int) json_integer_value(j), NUM_LEFT_FOCUS, FOCUS_LEN - 1);
		if (json_t* j = json_object_get(rootJ, "selectedTrack"))
			panel.track = clamp((int) json_integer_value(j), 0, NUM_TRACKS - 1);
		sequenceReplaced();
	}
};

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

	// A step field for a 2-digit display ("--" when the cursor has no step), or the track
	// option shown in its place on the options screen.
	std::function<std::string()> stepField(std::function<int(const iqs::Step&)> field,
	                                       std::function<std::string(const iqs::TrackOptions&)> option) {
		return fromView("--", [field, option](const iqs::PanelView& v) {
			if (v.optionsScreen)
				return option(v.options);
			return v.stepInPattern >= 0 ? std::to_string(field(v.step)) : std::string("--");
		});
	}

	void appendContextMenu(Menu* menu) override {
		IndexedQuadSeq* m = seq;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Reset behaviour",
			{"Next clock plays the first step", "First step sounds at the reset"},
			[m]() { return m->transport.resetMode; },
			[m](int mode) { m->transport.resetMode = mode; }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Testing"));
		menu->addChild(createMenuItem("Load demo sequence", "", [m]() { m->loadDemoRequested = true; }));
		menu->addChild(createMenuItem("Clear all tracks", "", [m]() { m->clearRequested = true; }));
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
		setPanel(createPanel(asset::plugin(pluginInstance, "res/IndexedQuadSeq.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addLabels();

		// --- Top strip: TABLE switch, SMOOTH, INDEX, VOLTAGE.
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(14.0f, ROW_TOP)), module, IndexedQuadSeq::TABLE_PARAM));
		addParam(createParamCentered<BlueButton>(mm2px(Vec(FN_COL_2, ROW_TOP)), module, IndexedQuadSeq::SMOOTH_PARAM));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(37.2f, ROW_TOP)), module, IndexedQuadSeq::SMOOTH_LIGHT));

		// INDEX and VOLTAGE show the cursor step's entry in the table the TABLE switch picks;
		// VOLTAGE also shows the panel's messages (AFtr, FULL, ...).
		addDisplay(L_DISPLAY_X, ROW_TOP, DISPLAY_W, 2, "0", fromView("0", [](const iqs::PanelView& v) {
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
		addDisplay(L_DISPLAY_X, ROWS[0], DISPLAY_W, 2, "1", fromView("1", [](const iqs::PanelView& v) {
			return std::to_string(v.track + 1);
		}));
		addDisplay(L_DISPLAY_X, ROWS[1], DISPLAY_W, 2, "--", fromView("--", [](const iqs::PanelView& v) {
			return v.pattern >= 0 ? std::to_string(v.pattern + 1) : std::string("--");
		}));
		// On the track options screen STEP shows the clock multiplier.
		addDisplay(L_DISPLAY_X, ROWS[2], DISPLAY_W, 2, "--", fromView("--", [](const iqs::PanelView& v) {
			if (v.optionsScreen)
				return std::to_string(v.options.clockMul);
			return v.stepInPattern >= 0 ? std::to_string(v.stepInPattern + 1) : std::string("--");
		}));
		addDisplay(L_DISPLAY_X, ROWS[3], DISPLAY_W, 2, "1");
		// On the track options screen the right column shows CV-A/CV-B note (Nt) or number
		// (Nr) display, the clock divider, and gate (Gt) or trigger (tr) output.
		addDisplay(R_DISPLAY_X, ROWS[0], DISPLAY_W, 2, "--", stepField([](const iqs::Step& s) { return (int) s.cvA; },
			[](const iqs::TrackOptions& o) { return std::string(o.noteDisplayA ? "Nt" : "Nr"); }));
		addDisplay(R_DISPLAY_X, ROWS[1], DISPLAY_W, 2, "--", stepField([](const iqs::Step& s) { return (int) s.cvB; },
			[](const iqs::TrackOptions& o) { return std::string(o.noteDisplayB ? "Nt" : "Nr"); }));
		addDisplay(R_DISPLAY_X, ROWS[2], DISPLAY_W, 2, "--", stepField([](const iqs::Step& s) { return (int) s.duration; },
			[](const iqs::TrackOptions& o) { return std::to_string(o.clockDiv); }));
		// GATE lights both decimal points when the step ratchets.
		addDisplay(R_DISPLAY_X, ROWS[3], DISPLAY_W, 2, "--", fromView("--", [](const iqs::PanelView& v) {
			if (v.optionsScreen)
				return std::string(v.options.triggerMode ? "tr" : "Gt");
			if (v.stepInPattern < 0)
				return std::string("--");
			std::string digits = string::f("%2d", (int) v.step.gate);
			if (!v.step.ratchet)
				return digits;
			return std::string(1, digits[0]) + "." + digits[1] + ".";
		}));

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

	void addLabels() {
		using namespace layout;
		PanelLabels* p = new PanelLabels;
		p->box.size = box.size;

		const float big = 2.6f, small = 2.0f, tiny = 1.7f;
		const float labelDy = 6.2f;   // label baseline above a display row
		const float btnTop = 4.0f;    // half a button

		p->label(66.04f, 4.3f, "INDEXED QUAD SEQUENCER", 3.2f);
		p->label(66.04f, 124.2f, "JIN NG", 2.2f);

		// TABLE switch legend.
		p->label(14.0f, 10.3f, "TABLE", small);
		p->label(10.4f, ROW_TOP - 3.2f, "A", tiny, NVG_ALIGN_RIGHT);
		p->label(10.4f, ROW_TOP, "ref", tiny, NVG_ALIGN_RIGHT);
		p->label(10.4f, ROW_TOP + 3.2f, "B", tiny, NVG_ALIGN_RIGHT);
		p->label(37.2f, ROW_TOP - 3.4f, "smooth", tiny);
		// Step-to-ramp icon above SMOOTH.
		p->line({Vec(23.4f, 11.6f), Vec(24.6f, 11.6f), Vec(24.6f, 10.0f), Vec(25.8f, 10.0f)});
		p->line({Vec(26.4f, 10.8f), Vec(27.6f, 10.8f)});
		p->line({Vec(27.2f, 10.4f), Vec(27.6f, 10.8f), Vec(27.2f, 11.2f)});
		p->line({Vec(28.2f, 11.6f), Vec(29.4f, 11.6f), Vec(30.8f, 10.0f), Vec(31.8f, 10.0f)});

		// Display captions with the bracket down to their focus button.
		auto leftCaption = [&](float row, const char* text, float textW) {
			float y = row - labelDy;
			float x0 = L_DISPLAY_X - DISPLAY_W / 2;
			p->label(x0, y, text, big, NVG_ALIGN_LEFT);
			p->line({Vec(x0 + textW + 1.0f, y), Vec(L_BUTTON_X, y), Vec(L_BUTTON_X, row - btnTop)});
		};
		auto rightCaption = [&](float row, const char* text, float textW, float displayW, float displayX) {
			float y = row - labelDy;
			float x1 = displayX + displayW / 2;
			p->label(x1, y, text, big, NVG_ALIGN_RIGHT);
			p->line({Vec(x1 - textW - 1.0f, y), Vec(R_BUTTON_X, y), Vec(R_BUTTON_X, row - btnTop)});
		};
		leftCaption(ROW_TOP, "INDEX", 9.0f);
		rightCaption(ROW_TOP, "VOLTAGE", 13.2f, VOLTAGE_DISPLAY_W, VOLTAGE_DISPLAY_X);
		p->label(122.6f, ROW_TOP + 0.4f, "V", 5.0f, NVG_ALIGN_LEFT);

		const char* leftNames[4] = {"TRACK", "PATTERN", "STEP", "SNAPSHOT"};
		const float leftW[4] = {9.6f, 12.4f, 7.2f, 15.2f};
		const char* rightNames[4] = {"CV-A", "CV-B", "DURATION", "GATE"};
		const float rightW[4] = {7.2f, 7.2f, 15.0f, 7.6f};
		for (int r = 0; r < 4; r++) {
			leftCaption(ROWS[r], leftNames[r], leftW[r]);
			rightCaption(ROWS[r], rightNames[r], rightW[r], DISPLAY_W, R_DISPLAY_X);
		}

		// Function button captions.
		auto above = [&](float x, float row, const char* text) {
			p->label(x, row - labelDy, text, big);
		};
		above(FN_COL_1, ROWS[0], "INSERT");
		above(FN_COL_1, ROWS[1], "DELETE");
		above(FN_COL_1, ROWS[2], "MATH");
		above(FN_COL_2, ROWS[2], "COPY");
		above(FN_COL_1, ROWS[3], "LOAD");
		above(FN_COL_2, ROWS[3], "SAVE");
		p->label(36.2f, ROWS[2] - 3.2f, "press", tiny);
		p->label(36.2f, ROWS[2] + 3.4f, "INSERT", tiny);

		// Loop bracket.
		const float loopY = ROWS[2] - 11.4f, loopX = 117.4f;
		p->label(loopX, loopY, "LOOP", big);
		p->line({Vec(LOOP_LED_X - 2.0f, loopY + 1.6f), Vec(LOOP_LED_X - 2.0f, loopY), Vec(loopX - 5.0f, loopY)});
		p->line({Vec(loopX + 5.0f, loopY), Vec(126.6f, loopY), Vec(126.6f, loopY + 1.6f)});
		p->label(LOOP_BUTTON_X, ROWS[2] - 5.6f, "START", small);
		p->label(LOOP_BUTTON_X, ROWS[3] - 5.6f, "END", small);
		p->line({Vec(LOOP_LED_X - 1.3f, ROWS[2] - 4.0f), Vec(LOOP_LED_X + 1.3f, ROWS[2] - 4.0f)});
		p->line({Vec(LOOP_LED_X, ROWS[2] - 4.0f), Vec(LOOP_LED_X, ROWS[3] + 4.0f)});
		p->line({Vec(LOOP_LED_X - 1.3f, ROWS[3] + 4.0f), Vec(LOOP_LED_X + 1.3f, ROWS[3] + 4.0f)});
		p->line({Vec(LOOP_LED_X - 0.8f, (ROWS[2] + ROWS[3]) / 2 - 0.8f), Vec(LOOP_LED_X, (ROWS[2] + ROWS[3]) / 2 + 0.4f), Vec(LOOP_LED_X + 0.8f, (ROWS[2] + ROWS[3]) / 2 - 0.8f)});

		// Jack section.
		p->label(FN_COL_1, 92.4f, "PAUSE", tiny);
		p->label(26.0f, 91.2f, "CLOCK", big);
		p->label(26.0f, 106.8f, "RESET", big);
		p->line({Vec(FN_COL_1 + 4.0f, JACK_ROW_1), Vec(26.0f - 4.2f, JACK_ROW_1)});
		p->line({Vec(FN_COL_1 + 4.0f, JACK_ROW_2), Vec(26.0f - 4.2f, JACK_ROW_2)});

		for (int t = 0; t < NUM_TRACKS; t++) {
			float y = t < 2 ? JACK_ROW_1 : JACK_ROW_2;
			const float* xs = TRACK_JACK_X[t % 2];
			static const char* const names[NUM_TRACKS] = {"TRACK 1", "TRACK 2", "TRACK 3", "TRACK 4"};
			float yTitle = y - 8.9f;
			p->label(xs[1], yTitle, names[t], small);
			p->line({Vec(xs[0] - 3.0f, yTitle + 1.2f), Vec(xs[0] - 3.0f, yTitle), Vec(xs[1] - 6.0f, yTitle)});
			p->line({Vec(xs[1] + 6.0f, yTitle), Vec(xs[2] + 3.0f, yTitle), Vec(xs[2] + 3.0f, yTitle + 1.2f)});
			p->label(xs[0], y - 6.4f, "CV-A", tiny);
			p->label(xs[1], y - 6.4f, "CV-B", tiny);
			p->label(xs[2], y - 6.4f, "GATE", tiny);
		}

		p->label(LOOP_BUTTON_X, 92.4f, "COMMIT", tiny);
		p->label(LOOP_BUTTON_X, 106.8f, "MODE", big);
		p->label(119.6f, 110.8f, "hold", tiny, NVG_ALIGN_LEFT);
		p->label(119.6f, 114.0f, "edit", tiny, NVG_ALIGN_LEFT);
		p->label(119.6f, 117.2f, "follow", tiny, NVG_ALIGN_LEFT);

		addChild(p);
	}
};

Model* modelIndexedQuadSeq = createModel<IndexedQuadSeq, IndexedQuadSeqWidget>("IndexedQuadSeq");
