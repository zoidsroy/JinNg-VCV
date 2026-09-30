// Tests for UndoTracker (src/core/UndoTracker.hpp) driving a real Engine, with the
// audio/UI handshake simulated: a requested capture is delivered on the next frame.

#include "Engine.hpp"
#include "UndoTracker.hpp"

#include "testing.hpp"

#include <vector>

using namespace iqs;

struct UndoRig {
	Engine e;
	UndoTracker u;
	double now = 0.0;
	bool requested = false;
	std::vector<std::pair<Sequence, Sequence>> steps; // what would go to Rack's history

	explicit UndoRig(const Sequence& s) {
		e.live = s;
		e.liveReplaced();
		frames(2); // the initial capture becomes the base
	}
	// One UI frame of 1/60 s; the audio thread answers a request made last frame.
	void frame() {
		now += 1.0 / 60;
		Sequence copy;
		const Sequence* got = nullptr;
		if (requested) {
			copy = e.editSeq();
			got = &copy;
			requested = false;
		}
		u.step(now, e.editGeneration, got, [this](const Sequence& before, const Sequence& after) {
			steps.push_back(std::make_pair(before, after));
		});
		if (u.wantCapture && !requested) {
			requested = true;
			u.captureRequested();
		}
	}
	void frames(int n) {
		for (int i = 0; i < n; i++)
			frame();
	}
	void tap(int b) {
		e.press(b);
		e.release(b);
	}
	// Ctrl+Z on the most recent step.
	void undoLast() {
		const Sequence& before = steps.back().first;
		e.restoreEdited(before);
		u.restored(before);
	}
};

static Sequence sample() {
	return build({{10, 1, 1}, {20, 1, 1}});
}

TEST(undo_first_capture_is_only_a_baseline) {
	UndoRig r(sample());
	r.frames(60);
	CHECK_EQ((int) r.steps.size(), 0);
	CHECK(r.u.base.sameContent(r.e.live));
}

TEST(undo_a_knob_gesture_is_one_step) {
	UndoRig r(sample());
	r.tap(FOCUS_CV_A);
	for (int i = 0; i < 5; i++) {
		r.e.turnRight(1);
		r.frames(3); // turning, with short pauses under the quiet time
	}
	CHECK_EQ((int) r.steps.size(), 0); // not yet: the gesture may continue
	r.frames(40);
	CHECK_EQ((int) r.steps.size(), 1);
	CHECK_EQ((int) r.steps[0].first.tracks[0].steps[0].cvA, 10);
	CHECK_EQ((int) r.steps[0].second.tracks[0].steps[0].cvA, 15);
}

TEST(undo_ignores_activity_that_changes_nothing) {
	UndoRig r(sample());
	r.tap(FOCUS_STEP);
	r.e.turnLeft(1);
	r.tap(FOCUS_GATE);
	r.frames(60);
	CHECK_EQ((int) r.steps.size(), 0);
}

TEST(undo_restore_is_not_recorded_as_a_new_step) {
	UndoRig r(sample());
	r.tap(FOCUS_STEP);
	r.tap(BUTTON_DELETE);
	r.frames(60);
	CHECK_EQ((int) r.steps.size(), 1);
	CHECK_EQ(r.e.live.tracks[0].numSteps(), 1);
	r.undoLast();
	r.frames(60);
	CHECK_EQ(r.e.live.tracks[0].numSteps(), 2);
	CHECK_EQ((int) r.steps.size(), 1);
	// A new edit after the undo starts from the restored state.
	r.tap(FOCUS_CV_B);
	r.e.turnRight(3);
	r.frames(60);
	CHECK_EQ((int) r.steps.size(), 2);
	CHECK_EQ(r.steps[1].first.tracks[0].numSteps(), 2);
}

TEST(undo_edits_during_a_capture_are_not_lost) {
	UndoRig r(sample());
	r.tap(FOCUS_CV_A);
	r.e.turnRight(1);
	r.frames(22); // quiet long enough: a capture is requested this frame...
	r.e.turnRight(1); // ...and another edit lands before it is answered
	r.frames(60);
	CHECK(!r.steps.empty());
	CHECK_EQ((int) r.steps.back().second.tracks[0].steps[0].cvA, 12);
	CHECK(r.u.base.sameContent(r.e.live));
}

TEST(undo_rebase_skips_wholesale_replacements) {
	UndoRig r(sample());
	r.e.live.tracks[0].steps[0].cvA = 99; // e.g. a patch load
	r.u.rebase();
	r.frames(60);
	CHECK_EQ((int) r.steps.size(), 0);
	CHECK(r.u.base.sameContent(r.e.live));
}
