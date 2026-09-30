#pragma once
// Messages between the sequencer (IndexedQuadSeq) and the Sequencer Controller expander
// placed directly to its right (docs/SPEC-ER102.md §8).
//
// Rack expander convention: each module owns the buffers for the messages it receives.
// The controller writes into the sequencer's rightExpander.producerMessage; the sequencer
// writes into the controller's leftExpander.producerMessage; Rack flips them after each
// sample. All of the expander's behaviour runs in the sequencer's Engine; the controller
// only forwards its controls and shows what it is told.

#include "core/Panel.hpp"

namespace expander {

// The controller's buttons, in the order the message carries them, with the Engine
// button each one is.
static const int BUTTONS[] = {
	iqs::BUTTON_RESET_TO,
	iqs::BUTTON_TRANSITION,
	iqs::FOCUS_PART,
	iqs::FOCUS_GROUP,
	iqs::BUTTON_DESELECT,
	iqs::BUTTON_INVERT,
	iqs::BUTTON_ROTATE,
	iqs::FOCUS_GROUP_MODIFIER,
	iqs::BUTTON_ARM,
	iqs::BUTTON_PUNCH,
};
static constexpr int NUM_BUTTONS = sizeof(BUTTONS) / sizeof(BUTTONS[0]);

enum Switch {
	SWITCH_TRANSITION,     // 0 last, 1 first, 2 user (CKSSThree: 0 = handle down)
	SWITCH_STORAGE,        // 0 eject, 1 admin, 2 user
	SWITCH_MODIFIER_TYPE,  // 0 low, 1 slope, 2 high
	SWITCH_MODIFIER_CHANNEL, // 0 Z, 1 Y, 2 X
	SWITCH_RECORD_MODE,    // 0 real-time, 1 step, 2 alter
	NUM_SWITCHES
};

enum Input {
	INPUT_SELECT,
	INPUT_ACTIVATE,
	INPUT_X_CV,
	INPUT_X_GATE,
	INPUT_Y_CV,
	INPUT_Y_GATE,
	INPUT_Z_CV,
	INPUT_Z_GATE,
	INPUT_A1,
	INPUT_A2,
	INPUT_AD1,
	INPUT_AD2,
	INPUT_D1,
	INPUT_D2,
	INPUT_PUNCH,
	NUM_INPUTS
};

struct ToSequencer {
	bool buttons[NUM_BUTTONS] = {};
	int switches[NUM_SWITCHES] = {};
	float inputs[NUM_INPUTS] = {};
	bool connected[NUM_INPUTS] = {};
};

enum Light {
	LIGHT_RESET_TO,
	LIGHT_PART_FOCUS,
	LIGHT_ACTIVATE,
	LIGHT_GROUP_MEMBER,
	LIGHT_GROUP_FOCUS,
	LIGHT_MODIFIER_FOCUS,
	LIGHT_ARM,
	LIGHT_REC,
	NUM_LIGHTS
};

struct ToController {
	float lights[NUM_LIGHTS] = {};
	char part[8] = "--";
	char group[8] = "--";
};

} // namespace expander
