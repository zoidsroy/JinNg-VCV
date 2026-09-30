// Test runner. Built and run by `make test`; no Rack required.

#include "testing.hpp"

int main() {
	for (const testing::TestCase& t : testing::registry()) {
		int before = testing::counters().failures;
		t.fn();
		std::printf("%s %s\n", testing::counters().failures == before ? "ok  " : "FAIL", t.name);
	}
	std::printf("\n%d checks, %d failures\n", testing::counters().checks, testing::counters().failures);
	return testing::counters().failures == 0 ? 0 : 1;
}
