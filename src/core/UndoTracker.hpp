#pragma once
// Decides when panel activity becomes an undo step (host side, UI thread).
//
// The sequence changes on the audio thread; the host's undo history lives on the UI
// thread. Each UI frame the host calls step() with the audio thread's edit generation.
// Once edits have been quiet for QUIET_S the tracker asks for a copy of the edited
// sequence (wantCapture); when the host hands one in, it is compared with the last
// recorded state and, if different, reported as one before/after step. A whole knob
// gesture thus becomes a single undo step, and activity that changed nothing (focus
// changes, cursor moves) records nothing.

#include "Sequence.hpp"
#include <cstdint>
#include <functional>

namespace iqs {

struct UndoTracker {
	static constexpr double QUIET_S = 0.35;

	Sequence base; // the state the next undo step starts from
	uint32_t seenGeneration = 0;
	double lastChange = 0.0;
	bool dirty = false;
	bool wantBaseline = true; // the first capture only sets the base
	bool wantCapture = true;

	// The edited sequence was replaced wholesale (patch load, mode change...): the next
	// capture becomes the new base without recording a step.
	void rebase() {
		wantBaseline = true;
		dirty = false;
		wantCapture = true;
	}

	// One UI frame. `captured` is the audio thread's copy if one arrived since the last
	// frame (nullptr otherwise). `record` receives each new undo step.
	void step(double now, uint32_t generation, const Sequence* captured,
	          const std::function<void(const Sequence& before, const Sequence& after)>& record) {
		if (generation != seenGeneration) {
			seenGeneration = generation;
			lastChange = now;
			if (!wantBaseline)
				dirty = true;
		}
		if (captured) {
			if (wantBaseline) {
				base = *captured;
				wantBaseline = false;
			}
			else if (!captured->sameContent(base)) {
				record(base, *captured);
				base = *captured;
			}
			// Edits that landed after the copy was taken are picked up next time.
			dirty = dirty && generation != capturedGeneration;
		}
		if (dirty && !wantCapture && now - lastChange > QUIET_S) {
			wantCapture = true;
			capturedGeneration = generation;
		}
	}

	// The host restored `s` (undo/redo); it is the new base and not itself an edit.
	void restored(const Sequence& s) {
		base = s;
		dirty = false;
	}

	// Call when the host has handed the capture request to the audio thread.
	void captureRequested() {
		wantCapture = false;
	}

private:
	uint32_t capturedGeneration = 0;
};

} // namespace iqs
