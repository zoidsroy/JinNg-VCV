#include "plugin.hpp"

// A blank panel with your own text. Width is set by dragging either edge or from the
// context menu; the text, its size, font and direction are in the context menu too.
// The panel is drawn with NanoVG instead of an SVG so it can be any width.

namespace {

const int MIN_HP = 1;
const int MAX_HP = 64;
const float MIN_TEXT_SIZE = 6.f;
const float MAX_TEXT_SIZE = 72.f;

// Same colours as the other JinNg panels (scripts/make_panels.py).
const NVGcolor LIGHT_BG = nvgRGB(0xd8, 0xda, 0xdc);
const NVGcolor LIGHT_FG = nvgRGB(0x2b, 0x2d, 0x30);
const NVGcolor DARK_BG = nvgRGB(0x24, 0x26, 0x2a);
const NVGcolor DARK_FG = nvgRGB(0xc4, 0xc7, 0xcb);

enum Direction {
	HORIZONTAL,
	ROTATED,  // whole text turned 90 degrees, read bottom to top
	STACKED,  // one character under the next, lines as columns from right to left
	NUM_DIRECTIONS
};
const char* const DIRECTION_NAMES[NUM_DIRECTIONS] = {"Horizontal", "Rotated (bottom to top)", "Stacked (vertical)"};

struct FontChoice {
	const char* label;
	const char* file;  // under Rack's res/fonts
};
const FontChoice FONTS[] = {
	{"Nunito Bold", "res/fonts/Nunito-Bold.ttf"},
	{"DejaVu Sans", "res/fonts/DejaVuSans.ttf"},
	{"Share Tech Mono", "res/fonts/ShareTechMono-Regular.ttf"},
	{"DSEG7 (segment display)", "res/fonts/DSEG7ClassicMini-Bold.ttf"},
};
const int NUM_FONTS = sizeof(FONTS) / sizeof(FONTS[0]);

// Returns the NanoVG font for FONTS[index] with CJK fallbacks attached, creating it in
// this context on first use. Rack ships Noto Sans SC/JP; on Windows Microsoft JhengHei
// is tried first so Traditional Chinese gets Taiwanese glyph shapes.
int getFont(NVGcontext* vg, int index) {
	std::string name = string::f("JinNg-Blank-%d", index);
	int font = nvgFindFont(vg, name.c_str());
	if (font >= 0)
		return font;
	font = nvgCreateFont(vg, name.c_str(), asset::system(FONTS[index].file).c_str());
	if (font < 0)
		return -1;

	struct Fallback {
		std::string path;
		int index;
	};
	std::vector<Fallback> fallbacks;
#if defined ARCH_WIN
	fallbacks.push_back({"C:/Windows/Fonts/msjh.ttc", 0});
#endif
	fallbacks.push_back({asset::system("res/fonts/NotoSansSC-Medium.otf"), 0});
	fallbacks.push_back({asset::system("res/fonts/NotoSansJP-Medium.otf"), 0});
	fallbacks.push_back({asset::system("res/fonts/NotoEmoji-Medium.ttf"), 0});
	for (size_t i = 0; i < fallbacks.size(); i++) {
		std::string fbName = string::f("JinNg-Blank-fallback-%d", (int) i);
		int fb = nvgFindFont(vg, fbName.c_str());
		if (fb < 0 && system::isFile(fallbacks[i].path))
			fb = nvgCreateFontAtIndex(vg, fbName.c_str(), fallbacks[i].path.c_str(), fallbacks[i].index);
		if (fb >= 0)
			nvgAddFallbackFontId(vg, font, fb);
	}
	return font;
}

// Splits UTF-8 text into code points, each kept as its own UTF-8 string.
std::vector<std::string> splitCodepoints(const std::string& s) {
	std::vector<std::string> out;
	size_t i = 0;
	while (i < s.size()) {
		unsigned char c = s[i];
		size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 1;
		out.push_back(s.substr(i, len));
		i += len;
	}
	return out;
}

} // namespace

struct Blank : Module {
	int width = 8;
	std::string text = "Jin Ng";
	float textSize = 18.f;
	int direction = HORIZONTAL;
	int font = 0;
	bool screws = true;

	Blank() {
		config(0, 0, 0, 0);
	}

	void onReset() override {
		// Keep the width so a reset doesn't push neighbours around.
		text = "Jin Ng";
		textSize = 18.f;
		direction = HORIZONTAL;
		font = 0;
		screws = true;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "width", json_integer(width));
		json_object_set_new(rootJ, "text", json_string(text.c_str()));
		json_object_set_new(rootJ, "textSize", json_real(textSize));
		json_object_set_new(rootJ, "direction", json_integer(direction));
		json_object_set_new(rootJ, "font", json_integer(font));
		json_object_set_new(rootJ, "screws", json_boolean(screws));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* j = json_object_get(rootJ, "width"))
			width = clamp((int) json_integer_value(j), MIN_HP, MAX_HP);
		if (json_t* j = json_object_get(rootJ, "text"))
			text = json_string_value(j);
		if (json_t* j = json_object_get(rootJ, "textSize"))
			textSize = clamp((float) json_number_value(j), MIN_TEXT_SIZE, MAX_TEXT_SIZE);
		if (json_t* j = json_object_get(rootJ, "direction"))
			direction = clamp((int) json_integer_value(j), 0, NUM_DIRECTIONS - 1);
		if (json_t* j = json_object_get(rootJ, "font"))
			font = clamp((int) json_integer_value(j), 0, NUM_FONTS - 1);
		if (json_t* j = json_object_get(rootJ, "screws"))
			screws = json_boolean_value(j);
	}
};

namespace {

// Draws the background and the text.
struct BlankPanel : Widget {
	Blank* module = nullptr;

	void draw(const DrawArgs& args) override {
		bool dark = settings::preferDarkPanels;
		NVGcolor bg = dark ? DARK_BG : LIGHT_BG;
		NVGcolor fg = dark ? DARK_FG : LIGHT_FG;

		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, bg);
		nvgFill(args.vg);
		// Thin edge so neighbouring blanks don't merge into one.
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f);
		nvgStrokeColor(args.vg, nvgTransRGBAf(fg, 0.25f));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		std::string text = module ? module->text : "Jin Ng";
		if (text.empty())
			return;
		int fontIndex = module ? module->font : 0;
		int font = getFont(args.vg, fontIndex);
		if (font < 0)
			return;
		float size = module ? module->textSize : 18.f;
		int direction = module ? module->direction : HORIZONTAL;

		nvgSave(args.vg);
		nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFontFaceId(args.vg, font);
		nvgFontSize(args.vg, size);
		nvgFillColor(args.vg, fg);
		const float margin = 4.f;

		if (direction == STACKED) {
			drawStacked(args.vg, text, size);
		}
		else {
			// For ROTATED, lay the text out on a box turned 90 degrees.
			bool rotated = direction == ROTATED;
			float w = (rotated ? box.size.y : box.size.x) - 2 * margin;
			nvgTranslate(args.vg, box.size.x / 2, box.size.y / 2);
			if (rotated)
				nvgRotate(args.vg, -M_PI / 2);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgTextLineHeight(args.vg, 1.15f);
			float bounds[4];
			nvgTextBoxBounds(args.vg, -w / 2, 0, w, text.c_str(), NULL, bounds);
			float textH = bounds[3] - bounds[1];
			nvgTextBox(args.vg, -w / 2, -textH / 2, w, text.c_str(), NULL);
		}
		nvgRestore(args.vg);
	}

	// One code point per row; each line of the text becomes a column, the first on the
	// right, as in traditional vertical Chinese.
	void drawStacked(NVGcontext* vg, const std::string& text, float size) {
		std::vector<std::vector<std::string>> columns;
		size_t start = 0;
		while (true) {
			size_t end = text.find('\n', start);
			columns.push_back(splitCodepoints(text.substr(start, end == std::string::npos ? std::string::npos : end - start)));
			if (end == std::string::npos)
				break;
			start = end + 1;
		}
		float step = size * 1.1f;
		float colStep = size * 1.25f;
		float totalW = colStep * (columns.size() - 1);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (size_t c = 0; c < columns.size(); c++) {
			const std::vector<std::string>& chars = columns[c];
			float x = box.size.x / 2 + totalW / 2 - c * colStep;
			float y0 = box.size.y / 2 - step * (chars.size() - 1) / 2;
			for (size_t i = 0; i < chars.size(); i++)
				nvgText(vg, x, y0 + i * step, chars[i].c_str(), NULL);
		}
	}
};

bool setModuleWidth(ModuleWidget* mw, Blank* module, Rect newBox) {
	Rect oldBox = mw->box;
	mw->box = newBox;
	// requestModulePos refuses the new box if it overlaps another module.
	if (!APP->scene->rack->requestModulePos(mw, newBox.pos)) {
		mw->box = oldBox;
		return false;
	}
	module->width = (int) std::round(mw->box.size.x / RACK_GRID_WIDTH);
	return true;
}

// Invisible strip on either edge; drag it to change the width in whole HP.
struct ResizeHandle : OpaqueWidget {
	Blank* module = nullptr;
	bool right = false;
	Vec dragPos;
	Rect originalBox;

	ResizeHandle() {
		box.size = Vec(RACK_GRID_WIDTH / 2, RACK_GRID_HEIGHT);
	}

	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		dragPos = APP->scene->rack->getMousePos();
		originalBox = getAncestorOfType<ModuleWidget>()->box;
	}

	void onDragMove(const DragMoveEvent& e) override {
		ModuleWidget* mw = getAncestorOfType<ModuleWidget>();
		float deltaX = APP->scene->rack->getMousePos().x - dragPos.x;
		Rect newBox = originalBox;
		const float minW = MIN_HP * RACK_GRID_WIDTH;
		const float maxW = MAX_HP * RACK_GRID_WIDTH;
		if (right) {
			newBox.size.x = clamp(std::round((originalBox.size.x + deltaX) / RACK_GRID_WIDTH) * RACK_GRID_WIDTH, minW, maxW);
		}
		else {
			newBox.size.x = clamp(std::round((originalBox.size.x - deltaX) / RACK_GRID_WIDTH) * RACK_GRID_WIDTH, minW, maxW);
			newBox.pos.x = originalBox.pos.x + originalBox.size.x - newBox.size.x;
		}
		if (newBox.size.x != mw->box.size.x)
			setModuleWidth(mw, module, newBox);
	}

	void draw(const DrawArgs& args) override {
		// Show a faint grip only while hovered, so the panel stays blank.
		if (APP->event->hoveredWidget != this && APP->event->draggedWidget != this)
			return;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0x40));
		nvgFill(args.vg);
	}
};

// Text box inside the context menu. Edits apply live; Enter closes the menu,
// Shift+Enter starts a new line.
struct BlankTextField : ui::TextField {
	Blank* module;
	bool focused = false;

	BlankTextField(Blank* module) : module(module) {
		box.size.x = 220;
		box.size.y = 60;
		multiline = true;
		placeholder = "Panel text";
		setText(module->text);
		selectAll();
	}

	void step() override {
		// Take keyboard focus as soon as the menu opens.
		if (!focused) {
			APP->event->setSelectedWidget(this);
			focused = true;
		}
		TextField::step();
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
			if (e.mods & GLFW_MOD_SHIFT) {
				insertText("\n");
			}
			else {
				ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
				if (overlay)
					overlay->requestDelete();
			}
			e.consume(this);
			return;
		}
		TextField::onSelectKey(e);
	}

	void onChange(const ChangeEvent& e) override {
		module->text = getText();
	}
};

struct TextSizeQuantity : Quantity {
	Blank* module;
	TextSizeQuantity(Blank* module) : module(module) {}
	void setValue(float value) override { module->textSize = clamp(value, MIN_TEXT_SIZE, MAX_TEXT_SIZE); }
	float getValue() override { return module->textSize; }
	float getMinValue() override { return MIN_TEXT_SIZE; }
	float getMaxValue() override { return MAX_TEXT_SIZE; }
	float getDefaultValue() override { return 18.f; }
	int getDisplayPrecision() override { return 3; }
	std::string getDisplayValueString() override { return string::f("%.0f", getValue()); }
	std::string getLabel() override { return "Text size"; }
	std::string getUnit() override { return " px"; }
};

struct TextSizeSlider : ui::Slider {
	TextSizeSlider(Blank* module) {
		quantity = new TextSizeQuantity(module);
		box.size.x = 220;
	}
	~TextSizeSlider() { delete quantity; }
};

} // namespace

struct BlankWidget : ModuleWidget {
	Blank* blank;
	BlankPanel* panel;
	ResizeHandle* leftHandle;
	ResizeHandle* rightHandle;
	Widget* screws[4];

	BlankWidget(Blank* module) : blank(module) {
		setModule(module);
		box.size = Vec((module ? module->width : 8) * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		panel = new BlankPanel;
		panel->module = module;
		panel->box.size = box.size;
		addChild(panel);

		for (int i = 0; i < 4; i++) {
			screws[i] = createWidget<ThemedScrew>(Vec());
			addChild(screws[i]);
		}

		leftHandle = new ResizeHandle;
		leftHandle->module = module;
		rightHandle = new ResizeHandle;
		rightHandle->module = module;
		rightHandle->right = true;
		if (module) {
			addChild(leftHandle);
			addChild(rightHandle);
		}
		else {
			delete leftHandle;
			delete rightHandle;
			leftHandle = rightHandle = nullptr;
		}
		layout();
	}

	void layout() {
		panel->box.size = box.size;
		float w = box.size.x;
		bool show = blank ? blank->screws : true;
		// Two screws below 3 HP, four from 3 HP up, like hardware blanks.
		bool four = w >= 3 * RACK_GRID_WIDTH;
		float leftX = four ? RACK_GRID_WIDTH : (w - RACK_GRID_WIDTH) / 2;
		float rightX = w - 2 * RACK_GRID_WIDTH;
		screws[0]->box.pos = Vec(leftX, 0);
		screws[1]->box.pos = Vec(leftX, RACK_GRID_HEIGHT - RACK_GRID_WIDTH);
		screws[2]->box.pos = Vec(rightX, 0);
		screws[3]->box.pos = Vec(rightX, RACK_GRID_HEIGHT - RACK_GRID_WIDTH);
		screws[0]->visible = screws[1]->visible = show;
		screws[2]->visible = screws[3]->visible = show && four;
		if (leftHandle) {
			leftHandle->box.pos = Vec(0, 0);
			rightHandle->box.pos = Vec(w - rightHandle->box.size.x, 0);
		}
	}

	void step() override {
		// Loading a patch or undo can change the width behind our back.
		if (blank) {
			float w = blank->width * RACK_GRID_WIDTH;
			if (box.size.x != w)
				box.size.x = w;
		}
		layout();
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		Blank* m = blank;
		BlankWidget* mw = this;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Text (Shift+Enter for a new line)"));
		menu->addChild(new BlankTextField(m));
		menu->addChild(new TextSizeSlider(m));

		menu->addChild(createIndexSubmenuItem("Direction",
			std::vector<std::string>(DIRECTION_NAMES, DIRECTION_NAMES + NUM_DIRECTIONS),
			[=]() { return m->direction; },
			[=](size_t i) { m->direction = (int) i; }));

		std::vector<std::string> fontLabels;
		for (int i = 0; i < NUM_FONTS; i++)
			fontLabels.push_back(FONTS[i].label);
		menu->addChild(createIndexSubmenuItem("Font", fontLabels,
			[=]() { return m->font; },
			[=](size_t i) { m->font = (int) i; }));

		menu->addChild(createSubmenuItem("Width", string::f("%d HP", m->width), [=](Menu* menu) {
			const int sizes[] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 64};
			for (int hp : sizes) {
				menu->addChild(createCheckMenuItem(string::f("%d HP", hp), "",
					[=]() { return m->width == hp; },
					[=]() {
						Rect newBox = mw->box;
						newBox.size.x = hp * RACK_GRID_WIDTH;
						setModuleWidth(mw, m, newBox);
					}));
			}
		}));
		menu->addChild(createMenuLabel("Or drag the left/right edge of the panel"));

		menu->addChild(createBoolPtrMenuItem("Screws", "", &m->screws));
	}
};

Model* modelBlank = createModel<Blank, BlankWidget>("Blank");
