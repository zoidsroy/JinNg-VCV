// Tests for voltage tables: the reference tables, voltage editing granularity, and the
// panel's INDEX / VOLTAGE / reference-table handling (manual, Voltage Tables).

#include "Panel.hpp"
#include "Transport.hpp"
#include "VoltageTables.hpp"

#include "testing.hpp"

#include <cmath>

using namespace iqs;

static bool near(float a, float b, float tol = 1e-4f) {
	return std::fabs(a - b) <= tol;
}

struct TableRig {
	Sequence seq;
	Transport tr;
	Panel panel;
	RefTables refs;

	explicit TableRig(const Sequence& s) : seq(s) {
		panel.refs = &refs;
		tr.rewind(seq, false);
		panel.normalizeCursors(seq);
	}
	void tap(int b) {
		panel.press(seq, tr, b);
		panel.release(seq, tr, b);
	}
	void left(int d) { panel.turnLeft(seq, tr, d); }
	void right(int d) { panel.turnRight(seq, d); }
	PanelView view() const { return panel.view(seq); }
	std::string message() const {
		const char* m = view().message;
		return m ? m : "(none)";
	}
};

// --- Reference tables ----------------------------------------------------------

TEST(builtin_tables_match_their_descriptions) {
	CHECK(near(builtinTable(0)[12], 1.f));           // 12ET: 12 per volt
	CHECK(near(builtinTable(1)[24], 1.f));           // 24ET: 24 per volt
	CHECK(near(builtinTable(2)[22], 1.f));           // 22JT: 22 per octave
	CHECK(near(builtinTable(2)[13], std::log2(1.5f))); // the just fifth
	CHECK(near(builtinTable(3)[9], 1.f));            // BLUE: 9 notes per octave
	CHECK(near(builtinTable(3)[2], 3 / 12.f));       // flat third
	CHECK(near(builtinTable(4)[5], 1.f));            // PEnt: major pentatonic from 0
	CHECK(near(builtinTable(4)[50], 0.f));           // minor pentatonic from 50
	CHECK(near(builtinTable(4)[51], 3 / 12.f));
	CHECK(near(builtinTable(5)[10], 1.f));           // L-8: 100mV steps
	CHECK(near(builtinTable(5)[99], 8.f));
	CHECK(near(builtinTable(6)[0], 0.f));            // E-8: 0 .. 8V
	CHECK(near(builtinTable(6)[99], 8.f, 1e-3f));
	CHECK(near(builtinTable(7)[1], 0.002f));         // LE-8: 2mV steps up to 0.1V
	CHECK(near(builtinTable(7)[50], 0.1f));
	CHECK(near(builtinTable(7)[99], 0.8f, 1e-3f));
	// Everything but PEnt (two scales back to back) rises monotonically, within range.
	for (int t = 0; t < NUM_BUILTIN_TABLES; t++) {
		const VoltageTable& tbl = builtinTable(t);
		for (int i = 0; i < TABLE_SIZE; i++) {
			CHECK(tbl[i] >= 0.f && tbl[i] <= MAX_VOLTAGE);
			if (t != 4 && i > 0 && tbl[i] < tbl[i - 1])
				CHECK(!"table not monotonic");
		}
	}
	CHECK_EQ(std::string(refTableName(0)), std::string("12Et"));
	CHECK_EQ(std::string(refTableName(15)), std::string("USr8"));
}

TEST(user_tables_start_as_12et_and_only_they_are_writable) {
	RefTables r;
	CHECK(near(r.get(8)[12], 1.f));
	CHECK(!r.writable(7));
	CHECK(r.writable(8));
	CHECK(r.writable(15));
}

// --- Granularity ---------------------------------------------------------------

TEST(voltage_nudges_in_number_display) {
	CHECK(near(nudgeVoltage(1.f, 1, GRAIN_FINE, false), 1.002f));
	CHECK(near(nudgeVoltage(1.f, -3, GRAIN_FINE, false), 0.994f));
	CHECK(near(nudgeVoltage(1.f, 1, GRAIN_COARSE, false), 1.1f));
	CHECK(near(nudgeVoltage(1.f, 1, GRAIN_SUPER_COARSE, false), 2.f));
	CHECK(near(nudgeVoltage(8.1f, 1, GRAIN_COARSE, false), MAX_VOLTAGE));
	CHECK(near(nudgeVoltage(0.05f, -1, GRAIN_COARSE, false), 0.f));
}

TEST(voltage_nudges_in_note_display) {
	CHECK(near(nudgeVoltage(1.f, 1, GRAIN_FINE, true), 1.f + 1 / 600.f));
	CHECK(near(nudgeVoltage(1.f, 1, GRAIN_COARSE, true), 1.f + 1 / 12.f));
	// Off the semitone grid, a coarse step goes to the next / previous chromatic note.
	CHECK(near(nudgeVoltage(1.03f, 1, GRAIN_COARSE, true), 1.f + 1 / 12.f));
	CHECK(near(nudgeVoltage(1.03f, -1, GRAIN_COARSE, true), 1.f));
	// An octave keeps the offset.
	CHECK(near(nudgeVoltage(1.03f, 1, GRAIN_SUPER_COARSE, true), 2.03f));
}

// --- Panel: browsing and editing ------------------------------------------------

TEST(index_browses_the_selected_table) {
	Sequence seq = build({{24, 1, 1}});
	seq.tracks[0].steps[0].cvB = 36;
	TableRig r(seq);
	// Without INDEX focused, INDEX/VOLTAGE follow the cursor's step.
	CHECK_EQ(r.view().index, 24);
	r.tap(FOCUS_INDEX); // browsing starts at the step's entry
	CHECK_EQ(r.view().index, 24);
	r.left(3);
	CHECK_EQ(r.view().index, 27);
	CHECK(near(r.view().voltage, 27 / 12.f));
	r.left(-100);
	CHECK_EQ(r.view().index, 0);
	r.panel.table = TABLE_B;
	r.tap(FOCUS_STEP);
	r.tap(FOCUS_INDEX);
	CHECK_EQ(r.view().index, 36);
}

TEST(editing_a_table_entry_retunes_every_step_using_it) {
	// Manual, "Global control with voltage tables": change one entry, all steps follow.
	Sequence seq = build({{12, 1, 1}, {20, 1, 1}, {12, 1, 1}});
	TableRig r(seq);
	r.tap(FOCUS_VOLTAGE);
	r.tap(FOCUS_VOLTAGE); // focus press: coarse
	CHECK_EQ(r.view().grain, (int) GRAIN_COARSE);
	r.right(2);           // the cursor's step uses entry 12: up two semitones
	CHECK(near(r.seq.tracks[0].tableA[12], 1.f + 2 / 12.f));
	CHECK(near(r.seq.tracks[0].tableA[20], 20 / 12.f));
	Playhead& ph = r.tr.playheads[0];
	ph.step = 2;
	ph.pulse = 0;
	CHECK(near(r.tr.cvA(r.seq, 0), 1.f + 2 / 12.f));
	// The steps themselves still point at entry 12.
	CHECK_EQ((int) r.seq.tracks[0].steps[2].cvA, 12);
}

TEST(granularity_cycles_and_can_be_dialled_while_held) {
	TableRig r(build({{12, 1, 1}}));
	r.tap(FOCUS_VOLTAGE);
	CHECK_EQ(r.view().grain, (int) GRAIN_FINE);
	CHECK_EQ((int) r.panel.grainLed(r.seq), (int) LED_OFF);
	r.tap(FOCUS_VOLTAGE);
	r.tap(FOCUS_VOLTAGE);
	CHECK_EQ(r.view().grain, (int) GRAIN_SUPER_COARSE);
	CHECK_EQ((int) r.panel.grainLed(r.seq), (int) LED_BLINK);
	r.tap(FOCUS_VOLTAGE);
	CHECK_EQ(r.view().grain, (int) GRAIN_FINE);
	// Holding VOLTAGE and turning picks the granularity; the voltage is left alone.
	r.panel.press(r.seq, r.tr, FOCUS_VOLTAGE); // held as a modifier: no focus-press cycle
	r.right(-5);
	CHECK_EQ(r.view().grain, (int) GRAIN_FINE);
	r.right(1);
	CHECK_EQ(r.view().grain, (int) GRAIN_COARSE);
	r.panel.release(r.seq, r.tr, FOCUS_VOLTAGE);
	CHECK(near(r.seq.tracks[0].tableA[12], 1.f));
	// Number display: fine is 2mV.
	r.seq.tracks[0].options.noteDisplayA = false;
	r.tap(FOCUS_VOLTAGE); // coarse -> super coarse
	r.tap(FOCUS_VOLTAGE); // -> fine
	r.right(5);
	CHECK(near(r.seq.tracks[0].tableA[12], 1.01f));
}

// --- Panel: reference tables and copying ----------------------------------------

TEST(copy_a_reference_table_onto_a_track_table) {
	TableRig r(build({{9, 1, 1}}));
	r.panel.table = TABLE_REF;
	r.tap(FOCUS_INDEX);
	r.right(3); // 12Et -> bLUE
	CHECK_EQ(r.message(), std::string("bLUE"));
	r.left(9);
	CHECK(near(r.view().voltage, 1.f)); // an octave up in a 9-note scale
	r.tap(BUTTON_COPY);
	CHECK_EQ((int) r.panel.focusLed(FOCUS_INDEX), (int) LED_BLINK);
	r.panel.table = TABLE_A;
	r.tap(BUTTON_INSERT);
	CHECK(near(r.seq.tracks[0].tableA[9], 1.f));
	CHECK(near(r.seq.tracks[0].tableA[2], 3 / 12.f));
	CHECK(near(r.seq.tracks[0].tableB[9], 9 / 12.f)); // B untouched
	r.panel.table = TABLE_B;
	r.tap(BUTTON_INSERT); // paste again onto B
	CHECK(near(r.seq.tracks[0].tableB[9], 1.f));
	r.tap(BUTTON_COPY);   // leave copy mode
	CHECK(!r.panel.copyLed());
}

TEST(save_a_track_table_as_a_user_table) {
	Sequence seq = build({{12, 1, 1}});
	seq.tracks[0].tableA.volts[12] = 1.234f;
	TableRig r(seq);
	r.tap(FOCUS_INDEX);
	r.tap(BUTTON_COPY);
	r.panel.table = TABLE_REF;
	r.tap(BUTTON_INSERT); // onto 12Et: built-ins are read-only
	CHECK_EQ(r.message(), std::string("Err"));
	CHECK(near(builtinTable(0)[12], 1.f));
	r.right(8);           // USr1
	CHECK_EQ(r.message(), std::string("USr1"));
	r.tap(BUTTON_INSERT);
	CHECK(near(r.refs.user[0][12], 1.234f));
	// And back onto another track.
	r.panel.table = TABLE_A;
	r.tap(FOCUS_TRACK);
	r.tap(BUTTON_COPY);   // clears the table clipboard (INDEX no longer focused)
	r.tap(FOCUS_INDEX);
	r.panel.table = TABLE_REF;
	r.tap(BUTTON_COPY);   // copy USr1
	r.panel.table = TABLE_A;
	r.tap(FOCUS_TRACK);
	r.left(1);
	r.tap(FOCUS_INDEX);
	r.tap(BUTTON_INSERT);
	CHECK(near(r.seq.tracks[1].tableA[12], 1.234f));
}

TEST(reference_tables_are_not_edited_entry_by_entry) {
	TableRig r(build({{12, 1, 1}}));
	r.panel.table = TABLE_REF;
	r.tap(FOCUS_VOLTAGE);
	r.right(8); // picks USr1, does not edit it
	CHECK(near(r.refs.user[0][0], 0.f));
	CHECK_EQ(r.panel.refTable, 8);
}
