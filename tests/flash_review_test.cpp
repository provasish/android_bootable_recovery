// SPDX-License-Identifier: GPL-3.0-or-later
#include "twinstall/flash_review.h"
#include "gui/textbox_lines.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>

static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
	std::cerr << "Check failed at line " << __LINE__ << ": " #condition "\n"; std::exit(1); } } while (0)

// A bounded, in-memory allocator fixture, not a replacement for device/liblp
// validation. It models independent groups and sector-aligned partition sizes.
struct FakePartition {
	std::string id, group;
	uint64_t size = 0;
	const std::string& name() const { return id; }
};

struct FakeBuilder {
	uint64_t capacity;
	std::map<std::string, uint64_t> groups;
	std::map<std::string, FakePartition> partitions;
	explicit FakeBuilder(uint64_t bytes) : capacity(bytes) {}
	uint64_t AllocatableSpace() const { return capacity; }
	uint64_t UsedSpace() const {
		uint64_t total = 0;
		for (const auto& p : partitions) total += p.second.size;
		return total;
	}
	bool AddGroup(const std::string& name, uint64_t limit) {
		return groups.emplace(name, limit).second;
	}
	FakePartition* AddPartition(const std::string& name, const std::string& group, int) {
		if (!groups.count(group)) return nullptr;
		auto result = partitions.emplace(name, FakePartition{name, group, 0});
		return result.second ? &result.first->second : nullptr;
	}
	bool ResizePartition(FakePartition* p, uint64_t bytes) {
		bytes = (bytes + 511) / 512 * 512;
		uint64_t in_group = 0;
		for (const auto& item : partitions)
			if (item.second.group == p->group) in_group += item.second.size;
		if (UsedSpace() - p->size + bytes > capacity ||
		    in_group - p->size + bytes > groups.at(p->group)) return false;
		p->size = bytes;
		return true;
	}
	void RemoveGroupAndPartitions(const std::string& group) {
		for (auto it = partitions.begin(); it != partitions.end();) {
			if (it->second.group == group) it = partitions.erase(it);
			else ++it;
		}
		groups.erase(group);
	}
	std::vector<std::string> ListGroups() const {
		std::vector<std::string> result;
		for (const auto& g : groups) result.push_back(g.first);
		return result;
	}
	std::vector<const FakePartition*> ListPartitionsInGroup(const std::string& group) const {
		std::vector<const FakePartition*> result;
		for (const auto& p : partitions)
			if (p.second.group == group) result.push_back(&p.second);
		return result;
	}
	void Seed(const std::string& group, const std::string& partition, uint64_t bytes) {
		AddGroup(group, capacity);
		CHECK(ResizePartition(AddPartition(partition, group, 0), bytes));
	}
};

static void TestLayout() {
	using twrp::PlanFlashLayout;
	twrp::FlashSpaceInfo info;
	FakeBuilder source(16384);
	source.Seed("rom_a", "system_a", 4096);
	source.Seed("rom_b", "old_b", 2048);
	source.Seed("unrelated", "keep", 1024);
	for (bool allow : {false, true}) {
		auto builder = source;
		CHECK(PlanFlashLayout(&builder, "rom_b", "_b", "_a", 8192,
		                     {{"system", 6144}}, allow, &info));
		CHECK(!info.reuses_booted);
		CHECK(info.required == 6144);
		CHECK(info.capacity == 16384);
		CHECK(info.available == 11264); // Old target space is reclaimed.
		CHECK(builder.partitions.count("system_a") == 1);
		CHECK(builder.partitions.count("old_b") == 0);
		CHECK(builder.partitions.count("keep") == 1);
	}
	CHECK(source.partitions.count("old_b") == 1); // Preview fixture is a copy.
	FakeBuilder small(8192);
	small.Seed("rom_a", "system_a", 6144);
	auto denied = small;
	CHECK(!PlanFlashLayout(&denied, "rom_b", "_b", "_a", 8192, {{"system", 4096}}, false, &info));
	CHECK(denied.partitions.count("system_a") == 1);
	CHECK(!info.error.empty());
	auto reuse = small;
	CHECK(PlanFlashLayout(&reuse, "rom_b", "_b", "_a", 8192, {{"system", 4096}}, true, &info));
	CHECK(info.reuses_booted);
	CHECK(info.available == 2048);
	CHECK(info.available_after_reuse == 8192);
	CHECK(info.booted_partitions == std::vector<std::string>{"system_a"});
	CHECK(reuse.partitions.count("system_a") == 0);
	CHECK(small.partitions.count("system_a") == 1);
	FakeBuilder impossible(8192);
	impossible.Seed("rom_a", "system_a", 4096);
	impossible.Seed("unrelated", "keep", 4096);
	CHECK(!PlanFlashLayout(&impossible, "rom_b", "_b", "_a", 8192, {{"system", 6144}}, true, &info));
	CHECK(impossible.partitions.count("keep") == 1);
	FakeBuilder a_target(8192);
	a_target.Seed("rom_b", "system_b", 6144);
	CHECK(PlanFlashLayout(&a_target, "rom_a", "_a", "_b", 8192, {{"system", 4096}}, true, &info));
	CHECK(info.booted_partitions == std::vector<std::string>{"system_b"});
	for (uint64_t bytes : {uint64_t(0), std::numeric_limits<uint64_t>::max()}) {
		auto invalid = source;
		CHECK(!PlanFlashLayout(&invalid, "rom_b", "_b", "_a", 8192, {{"system", bytes}}, true, &info));
	}
	auto rounded = source;
	CHECK(!PlanFlashLayout(&rounded, "rom_b", "_b", "_a", 512, {{"system", 513}}, true, &info));
	CHECK(info.required == 1024);
	FakeBuilder enormous(std::numeric_limits<uint64_t>::max());
	CHECK(!PlanFlashLayout(&enormous, "rom_b", "_b", "_a", enormous.capacity,
	                     {{"system", uint64_t(1) << 63}, {"vendor", uint64_t(1) << 63}}, true, &info));
}

static void TestConsentAndText() {
	twrp::FlashReviewConsent consent;
	CHECK(!consent.Confirm("/rom.zip"));
	consent.path = "/rom.zip"; consent.binding = "plan";
	CHECK(!consent.Consume("/rom.zip", "plan")); // Viewing isn't confirmation.
	consent.path = "/rom.zip"; consent.binding = "plan";
	CHECK(consent.Confirm("/rom.zip"));
	CHECK(!consent.Consume("/other.zip", "plan"));
	CHECK(consent.binding.empty());
	for (const auto& changed : {"new-options", "new-slot", "new-layout", "new-payload-hash"}) {
		consent.path = "/rom.zip"; consent.binding = "plan";
		CHECK(consent.Confirm("/rom.zip"));
		CHECK(!consent.Consume("/rom.zip", changed));
	}
	consent.path = "/rom.zip"; consent.binding = "plan";
	CHECK(consent.Confirm("/rom.zip"));
	CHECK(consent.Consume("/rom.zip", "plan"));
	CHECK(!consent.Consume("/rom.zip", "plan")); // Cannot replay a swipe.
	std::string one, two;
	twrp::BindReviewField(&one, "ab"); twrp::BindReviewField(&one, "c");
	twrp::BindReviewField(&two, "a"); twrp::BindReviewField(&two, "bc");
	CHECK(one != two);
	twrp::FlashReview review;
	review.file = "ROM.zip"; review.booted_slot = "A"; review.active_slot = "B"; review.target_slot = "B";
	review.has_logical = true; review.readback = false;
	review.firmware = {"modem", "tz"}; review.shared = {"spunvm"};
	auto text = twrp::RenderFlashReview(review);
	CHECK(text.find("Recovery booted from: A") != std::string::npos);
	CHECK(text.find("Bootloader active slot: B") != std::string::npos);
	CHECK(text.find("Write to slot: B") != std::string::npos);
	CHECK(text.find("Read-back verification: OFF") != std::string::npos);
	CHECK(text.find("ZIP signature verification: OFF") != std::string::npos);
	CHECK(text.find("Reflash recovery after ROM: OFF") != std::string::npos);
	CHECK(text.find("  modem\n  tz") != std::string::npos);
	CHECK(text.find("spunvm") != std::string::npos);
	CHECK(text.find("will be preserved") != std::string::npos);
	CHECK(text.find("WILL BE REUSED") == std::string::npos);
	review.space.reuses_booted = true;
	text = twrp::RenderFlashReview(review);
	CHECK(text.find("WILL BE REUSED") != std::string::npos);
	CHECK(text.find("will be preserved") == std::string::npos);
	review.has_logical = false; review.firmware.clear(); review.skipped_firmware = 22;
	text = twrp::RenderFlashReview(review);
	CHECK(text.find("no logical partitions") != std::string::npos);
	CHECK(text.find("22 skipped") != std::string::npos);
	CHECK(twrp::ReviewSize(uint64_t(1) << 30) == "1.00 GiB");
	CHECK(SplitTextBoxLines("one\n\nthree\n") == (std::vector<std::string>{"one", "", "three", ""}));
	CHECK(SplitTextBoxLines("one\r\ntwo") == (std::vector<std::string>{"one", "two"}));
	CHECK(SplitTextBoxLines("") == (std::vector<std::string>{""}));
}

int main() {
	TestLayout();
	TestConsentAndText();
	std::cout << checks << " flash-review checks passed\n";
}
