#pragma once
#include "plugin.hpp"
#include <functional>

// ---------------------------------------------------------------------------
// Square momentary buttons (8mm), in the three colours the panel uses.

struct SquareButton : app::SvgSwitch {
	SquareButton(const char* colour) {
		momentary = true;
		shadow->opacity = 0.f;
		addFrame(Svg::load(asset::plugin(pluginInstance, string::f("res/components/btn_%s_0.svg", colour))));
		addFrame(Svg::load(asset::plugin(pluginInstance, string::f("res/components/btn_%s_1.svg", colour))));
	}
};
struct BlueButton : SquareButton { BlueButton() : SquareButton("blue") {} };
struct GrayButton : SquareButton { GrayButton() : SquareButton("gray") {} };
struct RedButton : SquareButton { RedButton() : SquareButton("red") {} };

// ---------------------------------------------------------------------------
// Red seven-segment LED display, drawn segment by segment rather than with a font so
// that the hardware's letter shapes (TILt, Abrt, bEFr, 0.C.00 ...) come out exactly.
// Unlit segments are drawn faintly; lit ones go on the light layer so they glow when
// the room is dimmed. Text is right-aligned; a '.' lights the previous digit's point.

namespace seg {
enum : uint8_t { A = 1 << 0, B = 1 << 1, C = 1 << 2, D = 1 << 3, E = 1 << 4, F = 1 << 5, G = 1 << 6 };

inline uint8_t glyph(char ch) {
	switch (ch) {
		case '0': case 'O': return A | B | C | D | E | F;
		case '1': case 'I': return B | C;
		case '2': return A | B | D | E | G;
		case '3': return A | B | C | D | G;
		case '4': return B | C | F | G;
		case '5': case 'S': case 's': return A | C | D | F | G;
		case '6': return A | C | D | E | F | G;
		case '7': return A | B | C;
		case '8': return A | B | C | D | E | F | G;
		case '9': return A | B | C | D | F | G;
		case 'A': case 'a': return A | B | C | E | F | G;
		case 'b': case 'B': return C | D | E | F | G;
		case 'C': return A | D | E | F;
		case 'c': return D | E | G;
		case 'd': case 'D': return B | C | D | E | G;
		case 'E': case 'e': return A | D | E | F | G;
		case 'F': case 'f': return A | E | F | G;
		case 'G': case 'g': return A | C | D | E | F;
		case 'H': return B | C | E | F | G;
		case 'h': return C | E | F | G;
		case 'i': return C;
		case 'J': case 'j': return B | C | D | E;
		case 'L': case 'l': return D | E | F;
		case 'N': return A | B | C | E | F;
		case 'n': return C | E | G;
		case 'o': return C | D | E | G;
		case 'P': case 'p': return A | B | E | F | G;
		case 'r': case 'R': return E | G;
		case 't': case 'T': return D | E | F | G;
		case 'U': return B | C | D | E | F;
		case 'u': return C | D | E;
		case 'y': case 'Y': return B | C | D | F | G;
		case '-': return G;
		case '_': return D;
		default: return 0;
	}
}
} // namespace seg

struct SevenSegDisplay : widget::Widget {
	int digits = 2;
	std::function<std::string()> getText;
	std::string fallback;

	struct Cell {
		uint8_t segs;
		bool dot;
		Cell(uint8_t segs = 0, bool dot = false) : segs(segs), dot(dot) {}
	};

	std::string text() {
		return getText ? getText() : fallback;
	}

	// Lays text out right-aligned into `digits` cells.
	std::vector<Cell> layoutText(const std::string& s) {
		std::vector<Cell> parsed;
		for (char ch : s) {
			if (ch == '.' && !parsed.empty() && !parsed.back().dot)
				parsed.back().dot = true;
			else if (ch == '.')
				parsed.push_back({0, true});
			else
				parsed.push_back({seg::glyph(ch), false});
		}
		std::vector<Cell> cells(digits);
		int n = std::min((int) parsed.size(), digits);
		for (int i = 0; i < n; i++)
			cells[digits - n + i] = parsed[parsed.size() - n + i];
		return cells;
	}

	void drawCells(const DrawArgs& args, const std::vector<Cell>& cells, NVGcolor colour) {
		const float padX = box.size.y * 0.12f;
		const float cellW = (box.size.x - 2 * padX) / digits;
		const float dh = box.size.y * 0.74f;
		const float dw = std::min(cellW * 0.64f, dh * 0.56f);
		const float t = dh * 0.13f;
		const float gap = t * 0.18f;
		const float skew = 0.09f;
		const float top = (box.size.y - dh) / 2;

		nvgFillColor(args.vg, colour);
		for (int i = 0; i < digits; i++) {
			const Cell& cell = cells[i];
			float x0 = padX + cellW * i + (cellW - dw) / 2 - t * 0.3f;
			auto pt = [&](float x, float y) {
				// Lean the digit forward like the hardware's LED modules.
				return Vec(x0 + x + (dh / 2 - y) * skew, top + y);
			};
			auto poly = [&](std::initializer_list<Vec> pts) {
				nvgBeginPath(args.vg);
				bool first = true;
				for (Vec p : pts) {
					if (first)
						nvgMoveTo(args.vg, p.x, p.y);
					else
						nvgLineTo(args.vg, p.x, p.y);
					first = false;
				}
				nvgClosePath(args.vg);
				nvgFill(args.vg);
			};
			auto horiz = [&](float y) {
				float l = t / 2 + gap, r = dw - t / 2 - gap;
				poly({pt(l, y), pt(l + t / 2, y - t / 2), pt(r - t / 2, y - t / 2), pt(r, y), pt(r - t / 2, y + t / 2), pt(l + t / 2, y + t / 2)});
			};
			auto vert = [&](float x, float y0, float y1) {
				float a = y0 + gap, b = y1 - gap;
				poly({pt(x, a), pt(x + t / 2, a + t / 2), pt(x + t / 2, b - t / 2), pt(x, b), pt(x - t / 2, b - t / 2), pt(x - t / 2, a + t / 2)});
			};
			const float yTop = t / 2, yMid = dh / 2, yBot = dh - t / 2;
			const float xL = t / 2, xR = dw - t / 2;
			if (cell.segs & seg::A) horiz(yTop);
			if (cell.segs & seg::B) vert(xR, yTop, yMid);
			if (cell.segs & seg::C) vert(xR, yMid, yBot);
			if (cell.segs & seg::D) horiz(yBot);
			if (cell.segs & seg::E) vert(xL, yMid, yBot);
			if (cell.segs & seg::F) vert(xL, yTop, yMid);
			if (cell.segs & seg::G) horiz(yMid);
			if (cell.dot) {
				Vec p = pt(dw + t * 0.9f, dh - t * 0.5f);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, p.x, p.y, t * 0.55f);
				nvgFill(args.vg);
			}
		}
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 1.5f);
		nvgFillColor(args.vg, nvgRGB(0x1c, 0x08, 0x08));
		nvgFill(args.vg);

		std::vector<Cell> ghost(digits, Cell{0x7f, true});
		drawCells(args, ghost, nvgRGBA(0xff, 0x30, 0x20, 0x14));
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1)
			drawCells(args, layoutText(text()), nvgRGB(0xff, 0x2a, 0x1a));
		Widget::drawLayer(args, layer);
	}
};

// ---------------------------------------------------------------------------
// Endless rotary encoder with 24 detents per turn, like the hardware's encoders.
// It is deliberately not a Param: an encoder has no position, only motion, and a
// param-backed knob would hit its end stop. Each detent calls onTurn(+1 / -1).

struct Encoder : widget::OpaqueWidget {
	static constexpr int DETENTS = 24;
	static constexpr float PX_PER_DETENT = 6.f;

	widget::FramebufferWidget* fb;
	widget::TransformWidget* tw;
	widget::SvgWidget* knob;
	std::function<void(int)> onTurn;
	float dragAccum = 0.f;
	float angle = 0.f;

	Encoder() {
		fb = new widget::FramebufferWidget;
		addChild(fb);
		widget::SvgWidget* bg = new widget::SvgWidget;
		bg->setSvg(Svg::load(asset::system("res/ComponentLibrary/RoundBigBlackKnob_bg.svg")));
		fb->addChild(bg);
		tw = new widget::TransformWidget;
		fb->addChild(tw);
		knob = new widget::SvgWidget;
		knob->setSvg(Svg::load(asset::system("res/ComponentLibrary/RoundBigBlackKnob.svg")));
		tw->addChild(knob);
		box.size = knob->box.size;
		fb->box.size = box.size;
		tw->box.size = box.size;
		bg->box.pos = box.size.minus(bg->box.size).div(2);
	}

	void turn(int d) {
		angle += d * 2.f * M_PI / DETENTS;
		Vec c = box.size.div(2);
		tw->identity();
		tw->translate(c);
		tw->rotate(angle);
		tw->translate(c.neg());
		fb->setDirty();
		if (onTurn)
			onTurn(d);
	}

	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		dragAccum = 0.f;
		if (settings::allowCursorLock)
			APP->window->cursorLock();
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		if (settings::allowCursorLock)
			APP->window->cursorUnlock();
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		// Up or right turns clockwise.
		dragAccum += e.mouseDelta.x - e.mouseDelta.y;
		while (dragAccum >= PX_PER_DETENT) {
			dragAccum -= PX_PER_DETENT;
			turn(+1);
		}
		while (dragAccum <= -PX_PER_DETENT) {
			dragAccum += PX_PER_DETENT;
			turn(-1);
		}
	}

	void onHoverScroll(const HoverScrollEvent& e) override {
		if (e.scrollDelta.y != 0.f) {
			turn(e.scrollDelta.y > 0.f ? +1 : -1);
			e.consume(this);
		}
	}
};

// ---------------------------------------------------------------------------
// Panel legends. Rack's SVG loader drops <text>, so the labels, the bracket lines that
// tie each display to its focus button, and the small graphics are drawn here.
// All coordinates are in millimetres.

struct PanelLabels : widget::Widget {
	struct Label {
		float x, y;
		const char* text;
		float size;
		int align;
	};
	struct Line {
		std::vector<Vec> pts;
	};

	std::vector<Label> labels;
	std::vector<Line> lines;

	void label(float x, float y, const char* text, float size = 2.5f, int align = NVG_ALIGN_CENTER) {
		labels.push_back({x, y, text, size, align});
	}
	void line(std::vector<Vec> pts) {
		lines.push_back({pts});
	}

	void draw(const DrawArgs& args) override {
		NVGcolor ink = nvgRGB(0x2b, 0x2d, 0x30);

		nvgStrokeColor(args.vg, ink);
		nvgStrokeWidth(args.vg, mm2px(0.3f));
		nvgLineCap(args.vg, NVG_ROUND);
		nvgLineJoin(args.vg, NVG_ROUND);
		for (const Line& l : lines) {
			nvgBeginPath(args.vg);
			for (size_t i = 0; i < l.pts.size(); i++) {
				Vec p = mm2px(l.pts[i]);
				if (i == 0)
					nvgMoveTo(args.vg, p.x, p.y);
				else
					nvgLineTo(args.vg, p.x, p.y);
			}
			nvgStroke(args.vg);
		}

		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFillColor(args.vg, ink);
		for (const Label& l : labels) {
			nvgFontSize(args.vg, mm2px(l.size));
			nvgTextAlign(args.vg, l.align | NVG_ALIGN_MIDDLE);
			Vec p = mm2px(Vec(l.x, l.y));
			nvgText(args.vg, p.x, p.y, l.text, NULL);
		}
	}
};
