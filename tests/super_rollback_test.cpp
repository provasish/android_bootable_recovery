// SPDX-License-Identifier: GPL-3.0-or-later
// Host build: use -I twrpinstall/include and compile this file as C++17.
#include "twinstall/super_rollback.h"

#include <iostream>
#include <string>

namespace {

struct FakeSink {
	int rollbacks = 0;
	int clears = 0;
	void Rollback() { ++rollbacks; }
	void Clear() { ++clears; }
};

int failures = 0;

void Expect(bool condition, const char* what) {
	if (!condition) {
		std::cerr << "FAILED: " << what << '\n';
		++failures;
	}
}

// Mirrors TryHighSpeedAbInstall: the guard is armed early and only disarmed
// when the extractor may have written something.
int InstallThatStopsEarly(bool wrote_anything) {
	FakeSink sink;
	{
		twrp::SuperRollbackGuard<FakeSink> guard(sink);
		if (wrote_anything)
			guard.Keep();
		Expect(sink.rollbacks == 0,
		       "rollback must not run while the install is still running");
	}
	return sink.rollbacks;
}

}  // namespace

int main() {
	// A failure before the extractor starts puts the saved layout back.
	Expect(InstallThatStopsEarly(false) == 1, "early failure must roll back once");

	// Once images may have been written, the new table is kept.
	Expect(InstallThatStopsEarly(true) == 0, "writes must keep the new layout");

	// Keep() clears the saved state and is idempotent.
	{
		FakeSink sink;
		{
			twrp::SuperRollbackGuard<FakeSink> guard(sink);
			guard.Keep();
			guard.Keep();
		}
		Expect(sink.clears == 1, "Keep() must clear exactly once");
		Expect(sink.rollbacks == 0, "Keep() must disarm the rollback");
	}

	// Without Keep(), the destructor is the only caller.
	{
		FakeSink sink;
		{
			twrp::SuperRollbackGuard<FakeSink> guard(sink);
		}
		Expect(sink.rollbacks == 1, "destructor must roll back exactly once");
		Expect(sink.clears == 0, "a rollback leaves the clearing to the sink");
	}

	if (failures != 0)
		return 1;
	std::cout << "6 Super rollback checks passed\n";
	return 0;
}
