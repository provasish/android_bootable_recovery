// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace twrp {

// Arms a Super-layout rollback for a high-speed install. The constructor is
// called before the target slot's metadata is committed and the destructor
// calls Sink::Rollback() unless Keep() was called.
//
// Keep() is the point where the target slot's content stops being the old
// ROM's: once the extractor may have written anything, restoring the previous
// table would describe extents that no longer hold that data, so the new table
// is kept instead.
//
// Sink must provide Rollback() and Clear().
template <typename Sink>
class SuperRollbackGuard {
public:
	explicit SuperRollbackGuard(Sink& sink) : sink_(sink) {}
	~SuperRollbackGuard() {
		if (!kept_)
			sink_.Rollback();
	}

	SuperRollbackGuard(const SuperRollbackGuard&) = delete;
	SuperRollbackGuard& operator=(const SuperRollbackGuard&) = delete;

	void Keep() {
		if (kept_)
			return;
		kept_ = true;
		sink_.Clear();
	}

private:
	Sink& sink_;
	bool kept_ = false;
};

}  // namespace twrp
