#include "plugin.hpp"

// Stage 0 skeleton: I/O layout only. The sequencer engine lands in stage 1 (see docs/SPEC.md).
// For now every track's GATE mirrors the clock so the build can be sanity-checked in Rack.

static constexpr int NUM_TRACKS = 4;

struct IndexedQuadSeq : Module {
	enum ParamId {
		PAUSE_PARAM,
		RESET_PARAM,
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
		PAUSE_LIGHT,
		LIGHTS_LEN
	};

	dsp::SchmittTrigger pauseTrigger;
	bool paused = false;

	IndexedQuadSeq() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(PAUSE_PARAM, "Pause");
		configButton(RESET_PARAM, "Reset");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		for (int t = 0; t < NUM_TRACKS; t++) {
			configOutput(CV_A_OUTPUT + t, string::f("Track %d CV-A", t + 1));
			configOutput(CV_B_OUTPUT + t, string::f("Track %d CV-B", t + 1));
			configOutput(GATE_OUTPUT + t, string::f("Track %d gate", t + 1));
		}
	}

	void process(const ProcessArgs& args) override {
		if (pauseTrigger.process(params[PAUSE_PARAM].getValue()))
			paused = !paused;
		lights[PAUSE_LIGHT].setBrightness(paused);

		bool clockHigh = !paused && inputs[CLOCK_INPUT].getVoltage() >= 2.5f;
		for (int t = 0; t < NUM_TRACKS; t++) {
			outputs[CV_A_OUTPUT + t].setVoltage(0.f);
			outputs[CV_B_OUTPUT + t].setVoltage(0.f);
			outputs[GATE_OUTPUT + t].setVoltage(clockHigh ? 10.f : 0.f);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "paused", json_boolean(paused));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* pausedJ = json_object_get(rootJ, "paused");
		if (pausedJ)
			paused = json_boolean_value(pausedJ);
	}
};

struct IndexedQuadSeqWidget : ModuleWidget {
	IndexedQuadSeqWidget(IndexedQuadSeq* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/IndexedQuadSeq.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Clock / reset column on the left of the jack row.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.0, 100.0)), module, IndexedQuadSeq::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.0, 115.0)), module, IndexedQuadSeq::RESET_INPUT));
		addParam(createLightParamCentered<VCVLightBezel<>>(mm2px(Vec(24.0, 100.0)), module, IndexedQuadSeq::PAUSE_PARAM, IndexedQuadSeq::PAUSE_LIGHT));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(24.0, 115.0)), module, IndexedQuadSeq::RESET_PARAM));

		// Four tracks, each a column of CV-A / CV-B / GATE.
		for (int t = 0; t < NUM_TRACKS; t++) {
			float x = 42.0f + t * 24.0f;
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x - 7.0f, 100.0)), module, IndexedQuadSeq::CV_A_OUTPUT + t));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x + 7.0f, 100.0)), module, IndexedQuadSeq::CV_B_OUTPUT + t));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x, 115.0)), module, IndexedQuadSeq::GATE_OUTPUT + t));
		}
	}
};

Model* modelIndexedQuadSeq = createModel<IndexedQuadSeq, IndexedQuadSeqWidget>("IndexedQuadSeq");
