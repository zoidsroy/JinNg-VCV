#include "plugin.hpp"
#include "Expander.hpp"
#include "ui/Components.hpp"
#include <cstdio>

// The Sequencer Controller expander (docs/SPEC-ER102.md). It has no behaviour of its own:
// placed directly to the right of an Indexed Quad Sequencer, it forwards its buttons,
// switches and jacks to the sequencer's Engine and shows the lights and displays the
// sequencer sends back. On its own the STORAGE "error" LED lights, like the hardware
// without a card.

struct SequencerController : Module {
	enum ParamId {
		ENUMS(BUTTON_PARAM, expander::NUM_BUTTONS),
		ENUMS(SWITCH_PARAM, expander::NUM_SWITCHES),
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(JACK_INPUT, expander::NUM_INPUTS),
		INPUTS_LEN
	};
	enum LightId {
		ENUMS(STATUS_LIGHT, expander::NUM_LIGHTS),
		IO_LIGHT,
		ERROR_LIGHT,
		LIGHTS_LEN
	};

	// Buffers for what the sequencer sends us (Rack's double buffering).
	expander::ToController fromSequencer[2];

	// For the displays (UI thread).
	char partText[8] = "";
	char groupText[8] = "";

	SequencerController() {
		config(PARAMS_LEN, INPUTS_LEN, 0, LIGHTS_LEN);
		static const char* const buttonNames[expander::NUM_BUTTONS] = {
			"Reset to", "Transition", "Part focus", "Group focus", "(De)select",
			"Invert", "Rotate", "Group modifiers focus", "Arm", "Punch in/out",
		};
		for (int i = 0; i < expander::NUM_BUTTONS; i++)
			configButton(BUTTON_PARAM + i, buttonNames[i]);
		configSwitch(SWITCH_PARAM + expander::SWITCH_TRANSITION, 0.f, 2.f, 1.f, "Transition", {"Last", "First", "User"});
		configSwitch(SWITCH_PARAM + expander::SWITCH_STORAGE, 0.f, 2.f, 2.f, "Storage", {"Eject", "Admin", "User"});
		configSwitch(SWITCH_PARAM + expander::SWITCH_MODIFIER_TYPE, 0.f, 2.f, 1.f, "Group modifier", {"Low", "Slope", "High"});
		configSwitch(SWITCH_PARAM + expander::SWITCH_MODIFIER_CHANNEL, 0.f, 2.f, 2.f, "Modulation channel", {"Z", "Y", "X"});
		configSwitch(SWITCH_PARAM + expander::SWITCH_RECORD_MODE, 0.f, 2.f, 1.f, "Record mode", {"Real-time", "Step", "Alter"});

		static const char* const inputNames[expander::NUM_INPUTS] = {
			"Part select", "Part activate", "Mod X CV", "Mod X gate", "Mod Y CV", "Mod Y gate",
			"Mod Z CV", "Mod Z gate", "A-1 (CV-A)", "A-2 (CV-B)", "AD-1 (gate)", "AD-2 (duration)",
			"D-1 (insert)", "D-2 (delete)", "Punch in/out",
		};
		for (int i = 0; i < expander::NUM_INPUTS; i++)
			configInput(JACK_INPUT + i, inputNames[i]);

		leftExpander.producerMessage = &fromSequencer[0];
		leftExpander.consumerMessage = &fromSequencer[1];
	}

	bool attached() {
		return leftExpander.module && leftExpander.module->model == modelIndexedQuadSeq;
	}

	void process(const ProcessArgs& args) override {
		if (!attached()) {
			for (int i = 0; i < expander::NUM_LIGHTS; i++)
				lights[STATUS_LIGHT + i].setBrightness(0.f);
			lights[IO_LIGHT].setBrightness(0.f);
			lights[ERROR_LIGHT].setBrightness(1.f);
			partText[0] = groupText[0] = '\0';
			return;
		}

		// Send our controls to the sequencer.
		Module* seq = leftExpander.module;
		expander::ToSequencer* out = (expander::ToSequencer*) seq->rightExpander.producerMessage;
		if (out) {
			for (int i = 0; i < expander::NUM_BUTTONS; i++)
				out->buttons[i] = params[BUTTON_PARAM + i].getValue() > 0.f;
			for (int i = 0; i < expander::NUM_SWITCHES; i++)
				out->switches[i] = (int) params[SWITCH_PARAM + i].getValue();
			for (int i = 0; i < expander::NUM_INPUTS; i++) {
				out->inputs[i] = inputs[JACK_INPUT + i].getVoltage();
				out->connected[i] = inputs[JACK_INPUT + i].isConnected();
			}
			seq->rightExpander.requestMessageFlip();
		}

		// Show what the sequencer sent.
		const expander::ToController* in = (const expander::ToController*) leftExpander.consumerMessage;
		for (int i = 0; i < expander::NUM_LIGHTS; i++)
			lights[STATUS_LIGHT + i].setBrightness(in->lights[i]);
		lights[IO_LIGHT].setBrightness(0.f);
		lights[ERROR_LIGHT].setBrightness(0.f);
		std::snprintf(partText, sizeof(partText), "%s", in->part);
		std::snprintf(groupText, sizeof(groupText), "%s", in->group);
	}
};

// ---------------------------------------------------------------------------
// Layout, in mm, measured off the hardware panel figure (14HP).

namespace clayout {
static constexpr float DISPLAY_X = 18.8f;
static constexpr float DISPLAY_W = 14.2f;
static constexpr float DISPLAY_H = 7.9f;
static constexpr float LED_X = 28.8f;
static constexpr float BUTTON_X = 35.8f;
static constexpr float COL_1 = 8.5f;
static constexpr float JACK_1 = 49.4f;
static constexpr float JACK_2 = 63.1f;

static constexpr float ROW_TOP = 17.4f;
static constexpr float ROW_PART = 33.2f;
static constexpr float MOD_ROWS[3] = {48.9f, 64.8f, 80.5f};
static constexpr float ROW_GROUP = 48.9f;
static constexpr float ROW_SELECT = 64.6f;
static constexpr float ROW_MODIFIER = 80.5f;
static constexpr float REC_ROW_1 = 96.8f;
static constexpr float REC_ROW_2 = 112.7f;
} // namespace clayout

struct SequencerControllerWidget : ModuleWidget {
	SequencerController* ctrl;

	SequencerControllerWidget(SequencerController* module) : ctrl(module) {
		using namespace clayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SequencerController.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addLabels();

		auto button = [&](ParamWidget* w) { addParam(w); };
		auto blue = [&](float x, float y, int b) {
			button(createParamCentered<BlueButton>(mm2px(Vec(x, y)), module, SequencerController::BUTTON_PARAM + b));
		};
		auto gray = [&](float x, float y, int b) {
			button(createParamCentered<GrayButton>(mm2px(Vec(x, y)), module, SequencerController::BUTTON_PARAM + b));
		};
		auto light = [&](float x, float y, int l) {
			addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(x, y)), module, l));
		};
		auto jack = [&](float x, float y, int i) {
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, y)), module, SequencerController::JACK_INPUT + i));
		};
		auto sw = [&](float x, float y, int s) {
			addParam(createParamCentered<CKSSThree>(mm2px(Vec(x, y)), module, SequencerController::SWITCH_PARAM + s));
		};
		auto display = [&](float y, const char* fallback) {
			SevenSegDisplay* d = new SevenSegDisplay;
			d->box.size = mm2px(Vec(DISPLAY_W, DISPLAY_H));
			d->box.pos = mm2px(Vec(DISPLAY_X, y)).minus(d->box.size.div(2));
			d->digits = 2;
			d->fallback = fallback;
			addChild(d);
			return d;
		};
		using SC = SequencerController;

		// --- Parts and storage.
		blue(COL_1, ROW_TOP, 0); // RESET TO
		light(16.2f, ROW_TOP, SC::STATUS_LIGHT + expander::LIGHT_RESET_TO);
		sw(22.5f, ROW_TOP, expander::SWITCH_TRANSITION);
		blue(BUTTON_X, ROW_TOP, 1); // TRANSITION
		sw(47.7f, ROW_TOP, expander::SWITCH_STORAGE);
		light(65.6f, 14.2f, SC::IO_LIGHT);
		light(65.6f, 20.4f, SC::ERROR_LIGHT);

		light(7.3f, ROW_PART, SC::STATUS_LIGHT + expander::LIGHT_ACTIVATE);
		SevenSegDisplay* part = display(ROW_PART, "6.");
		part->getText = [module]() { return module ? std::string(module->partText) : std::string("6."); };
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(LED_X, ROW_PART)), module, SC::STATUS_LIGHT + expander::LIGHT_PART_FOCUS));
		gray(BUTTON_X, ROW_PART, 2); // PART focus
		jack(JACK_1, ROW_PART, expander::INPUT_SELECT);
		jack(JACK_2, ROW_PART, expander::INPUT_ACTIVATE);

		// --- Groups.
		light(7.3f, ROW_GROUP, SC::STATUS_LIGHT + expander::LIGHT_GROUP_MEMBER);
		SevenSegDisplay* group = display(ROW_GROUP, "1");
		group->getText = [module]() { return module ? std::string(module->groupText) : std::string("1"); };
		light(LED_X, ROW_GROUP, SC::STATUS_LIGHT + expander::LIGHT_GROUP_FOCUS);
		gray(BUTTON_X, ROW_GROUP, 3); // GROUP focus
		blue(COL_1, ROW_SELECT, 4);   // (DE)SELECT
		blue(22.2f, ROW_SELECT, 5);   // INVERT
		blue(BUTTON_X, ROW_SELECT, 6); // ROTATE
		sw(COL_1, ROW_MODIFIER, expander::SWITCH_MODIFIER_TYPE);
		sw(19.4f, ROW_MODIFIER, expander::SWITCH_MODIFIER_CHANNEL);
		light(LED_X, ROW_MODIFIER, SC::STATUS_LIGHT + expander::LIGHT_MODIFIER_FOCUS);
		gray(BUTTON_X, ROW_MODIFIER, 7); // GROUP MODIFIERS focus
		const int modCv[3] = {expander::INPUT_X_CV, expander::INPUT_Y_CV, expander::INPUT_Z_CV};
		for (int c = 0; c < 3; c++) {
			jack(JACK_1, MOD_ROWS[c], modCv[c]);
			jack(JACK_2, MOD_ROWS[c], modCv[c] + 1);
		}

		// --- Recording.
		light(COL_1, 89.3f, SC::STATUS_LIGHT + expander::LIGHT_ARM);
		button(createParamCentered<RedButton>(mm2px(Vec(COL_1, REC_ROW_1)), module, SC::BUTTON_PARAM + 8)); // ARM
		jack(22.2f, REC_ROW_1, expander::INPUT_A1);
		jack(BUTTON_X, REC_ROW_1, expander::INPUT_A2);
		jack(JACK_1, REC_ROW_1, expander::INPUT_AD1);
		jack(JACK_2, REC_ROW_1, expander::INPUT_PUNCH);
		sw(COL_1, REC_ROW_2, expander::SWITCH_RECORD_MODE);
		jack(22.2f, REC_ROW_2, expander::INPUT_D1);
		jack(BUTTON_X, REC_ROW_2, expander::INPUT_D2);
		jack(JACK_1, REC_ROW_2, expander::INPUT_AD2);
		light(JACK_2, 104.7f, SC::STATUS_LIGHT + expander::LIGHT_REC);
		button(createParamCentered<RedButton>(mm2px(Vec(JACK_2, REC_ROW_2)), module, SC::BUTTON_PARAM + 9)); // PUNCH
	}

	void addLabels() {
		using namespace clayout;
		PanelLabels* p = new PanelLabels;
		p->box.size = box.size;
		const float big = 2.6f, small = 2.0f, tiny = 1.7f;

		p->label(35.56f, 4.3f, "SEQUENCER CONTROLLER", 3.2f);
		p->label(35.56f, 124.2f, "JIN NG", 2.2f);

		// Parts.
		p->label(COL_1, 11.9f, "RESET TO", small);
		p->line({Vec(COL_1 + 4.0f, ROW_TOP), Vec(16.2f - 1.4f, ROW_TOP)});
		p->label(29.0f, 11.9f, "TRANSITION", small);
		p->line({Vec(35.5f, 11.9f), Vec(BUTTON_X, 11.9f), Vec(BUTTON_X, ROW_TOP - 4.0f)});
		p->label(25.4f, 14.9f, "user", tiny, NVG_ALIGN_LEFT);
		p->label(25.4f, 17.4f, "first", tiny, NVG_ALIGN_LEFT);
		p->label(25.4f, 19.9f, "last", tiny, NVG_ALIGN_LEFT);
		p->label(45.0f, 11.9f, "STORAGE", small, NVG_ALIGN_LEFT);
		p->label(50.6f, 14.9f, "user", tiny, NVG_ALIGN_LEFT);
		p->label(50.6f, 17.4f, "admin", tiny, NVG_ALIGN_LEFT);
		p->label(50.6f, 19.9f, "eject", tiny, NVG_ALIGN_LEFT);
		p->label(65.6f, 11.4f, "I/O", tiny);
		p->label(65.6f, 17.6f, "error", tiny);
		p->label(DISPLAY_X - DISPLAY_W / 2, 27.4f, "PART", big, NVG_ALIGN_LEFT);
		p->line({Vec(DISPLAY_X - DISPLAY_W / 2 + 7.6f, 27.4f), Vec(BUTTON_X, 27.4f), Vec(BUTTON_X, ROW_PART - 4.0f)});
		p->label(JACK_1, 27.4f, "SELECT", small);
		p->label(JACK_2, 27.4f, "ACTIVATE", small);

		// Groups.
		p->label(DISPLAY_X - DISPLAY_W / 2, 43.2f, "GROUP", big, NVG_ALIGN_LEFT);
		p->line({Vec(DISPLAY_X - DISPLAY_W / 2 + 9.6f, 43.2f), Vec(BUTTON_X, 43.2f), Vec(BUTTON_X, ROW_GROUP - 4.0f)});
		p->line({Vec(7.3f, ROW_GROUP + 1.5f), Vec(7.3f, 57.6f)});
		p->label(COL_1, 59.1f, "(DE)SELECT", small);
		p->label(22.2f, 59.1f, "INVERT", small);
		p->label(BUTTON_X, 59.1f, "ROTATE", small);
		p->label(3.9f, 74.8f, "GROUP MODIFIERS", big, NVG_ALIGN_LEFT);
		p->line({Vec(30.6f, 74.8f), Vec(BUTTON_X, 74.8f), Vec(BUTTON_X, ROW_MODIFIER - 4.0f)});
		p->label(11.2f, 78.1f, "high", tiny, NVG_ALIGN_LEFT);
		p->label(11.2f, 80.5f, "slope", tiny, NVG_ALIGN_LEFT);
		p->label(11.2f, 82.9f, "low", tiny, NVG_ALIGN_LEFT);
		p->label(22.2f, 78.1f, "X", tiny, NVG_ALIGN_LEFT);
		p->label(22.2f, 80.5f, "Y", tiny, NVG_ALIGN_LEFT);
		p->label(22.2f, 82.9f, "Z", tiny, NVG_ALIGN_LEFT);
		static const char* const channels[3] = {"X", "Y", "Z"};
		for (int c = 0; c < 3; c++) {
			float y = MOD_ROWS[c];
			p->label(JACK_1, y - 5.7f, "CV", small);
			p->label(JACK_2, y - 5.7f, "GATE", small);
			p->label(56.3f, y - 2.6f, "mod", tiny);
			p->label(56.3f, y + 0.3f, channels[c], 2.4f);
			p->line({Vec(54.8f, y - 1.1f), Vec(57.8f, y - 1.1f), Vec(57.8f, y + 1.7f), Vec(54.8f, y + 1.7f), Vec(54.8f, y - 1.1f)});
		}

		// Recording.
		p->label(COL_1, 92.1f, "ARM", small);
		p->label(22.2f, 88.8f, "A-1", small);
		p->label(22.2f, 91.2f, "(cv-a)", small);
		p->label(BUTTON_X, 88.8f, "A-2", small);
		p->label(BUTTON_X, 91.2f, "(cv-b)", small);
		p->label(JACK_1, 88.8f, "AD-1", small);
		p->label(JACK_1, 91.2f, "(gate)", small);
		p->label(JACK_2, 88.5f, "PUNCH", small);
		p->label(JACK_2, 90.9f, "IN/OUT", small);
		p->label(COL_1, 103.4f, "RECORD", small);
		p->label(COL_1, 105.7f, "MODE", small);
		p->label(11.2f, 110.1f, "alter", tiny, NVG_ALIGN_LEFT);
		p->label(11.2f, 112.6f, "step", tiny, NVG_ALIGN_LEFT);
		p->label(11.2f, 115.1f, "real-time", tiny, NVG_ALIGN_LEFT);
		p->label(22.2f, 104.5f, "D-1", small);
		p->label(22.2f, 107.0f, "(insert)", small);
		p->label(BUTTON_X, 104.5f, "D-2", small);
		p->label(BUTTON_X, 107.0f, "(delete)", small);
		p->label(JACK_1, 104.5f, "AD-2", small);
		p->label(JACK_1, 107.0f, "(duration)", small);
		p->label(59.0f, 104.7f, "REC", small, NVG_ALIGN_RIGHT);
		p->line({Vec(JACK_2, REC_ROW_1 + 4.2f), Vec(JACK_2, 103.2f)});
		p->line({Vec(JACK_2, 106.2f), Vec(JACK_2, REC_ROW_2 - 4.0f)});

		addChild(p);
	}
};

Model* modelSequencerController = createModel<SequencerController, SequencerControllerWidget>("SequencerController");
