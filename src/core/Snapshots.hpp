#pragma once
// Snapshot slots (spec §4.7, SPEC-ER102 §7).
//
// Slot numbers: 0 is the blank snapshot "--"; 1..117 are regular slots; -9..-1 are the
// expander's templates t1..t9, listed before the blank one. The sequencer alone offers
// slots 1..16 ("1".."16"); with the expander attached all 127 are available and regular
// slots are named A1..q9 (13 letters that read clearly on seven segments, skipping I, O,
// K and M). The first 16 regular slots are the same storage either way, so snapshots
// saved without the expander show up as A1..b7 with it.
//
// A slot only takes memory once something is saved in it.

#include "Sequence.hpp"
#include <array>
#include <memory>
#include <string>

namespace iqs {

static constexpr int NUM_TEMPLATES = 9;
static constexpr int NUM_REGULAR_SLOTS = 117;
static constexpr int NUM_STANDALONE_SLOTS = 16;

struct SnapshotStore {
	std::array<std::unique_ptr<Sequence>, NUM_TEMPLATES + NUM_REGULAR_SLOTS> slots;

	static int minSlot(bool expander) {
		return expander ? -NUM_TEMPLATES : 0;
	}
	static int maxSlot(bool expander) {
		return expander ? NUM_REGULAR_SLOTS : NUM_STANDALONE_SLOTS;
	}
	static bool storable(int slot) {
		return slot != 0 && slot >= -NUM_TEMPLATES && slot <= NUM_REGULAR_SLOTS;
	}
	static int key(int slot) {
		return slot > 0 ? slot - 1 : NUM_REGULAR_SLOTS + (slot + NUM_TEMPLATES);
	}

	bool saved(int slot) const {
		return storable(slot) && slots[key(slot)] != nullptr;
	}
	const Sequence* get(int slot) const {
		return saved(slot) ? slots[key(slot)].get() : nullptr;
	}
	// Saving into a slot for the first time allocates it (a rare, user-triggered event).
	void save(int slot, const Sequence& s) {
		if (!storable(slot))
			return;
		std::unique_ptr<Sequence>& p = slots[key(slot)];
		if (p)
			*p = s;
		else
			p.reset(new Sequence(s));
	}
	void clear() {
		for (std::unique_ptr<Sequence>& p : slots)
			p.reset();
	}

	// What the SNAPSHOT display shows for a slot.
	static std::string name(int slot, bool expander) {
		if (slot == 0)
			return "--";
		if (slot < 0)
			return "t" + std::to_string(slot + NUM_TEMPLATES + 1);
		if (!expander)
			return std::to_string(slot);
		static const char LETTERS[] = "AbCdEFGHJLnPq";
		return std::string(1, LETTERS[(slot - 1) / 9]) + std::to_string((slot - 1) % 9 + 1);
	}
};

} // namespace iqs
