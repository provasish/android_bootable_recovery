// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace twrp {

struct FlashSpaceInfo {
	uint64_t required = 0;
	uint64_t capacity = 0;
	uint64_t available = 0;  // Includes space released from the old target group.
	uint64_t available_after_reuse = 0;
	uint64_t group_limit = 0;
	bool reuses_booted = false;
	std::vector<std::string> booted_partitions;
	std::string metadata_id;
	std::string error;
};

// Only edits an in-memory builder. Disk writes, dm changes and unmounts belong
// to the caller after review. Shared by preview and the actual installer.
template <typename Builder>
bool PlanFlashLayout(Builder* builder, const std::string& group,
                     const std::string& target_suffix, const std::string& source_suffix,
                     uint64_t group_limit,
                     const std::vector<std::pair<std::string, uint64_t>>& partitions,
                     bool allow_reuse, FlashSpaceInfo* info) {
	info->group_limit = group_limit;
	info->required = 0;
	info->reuses_booted = false;
	info->booted_partitions.clear();
	info->error.clear();
	for (const auto& part : partitions) {
		// liblp sizes are sector aligned; guard the rounding and total too.
		if (part.second == 0 || part.second > std::numeric_limits<uint64_t>::max() - 511) {
			info->error = "Invalid logical partition size";
			return false;
		}
		const uint64_t aligned = (part.second + 511) / 512 * 512;
		if (aligned > std::numeric_limits<uint64_t>::max() - info->required) {
			info->error = "Logical partition sizes overflow";
			return false;
		}
		info->required += aligned;
	}
	if (info->required > group_limit) {
		info->error = "This ROM exceeds its Super group limit";
		return false;
	}
	info->capacity = builder->AllocatableSpace();
	builder->RemoveGroupAndPartitions(group);
	info->available = info->capacity >= builder->UsedSpace() ? info->capacity - builder->UsedSpace() : 0;
	info->available_after_reuse = info->available;
	if (!builder->AddGroup(group, group_limit)) {
		info->error = "Cannot prepare the target Super group";
		return false;
	}
	auto allocate = [&]() {
		for (const auto& part : partitions) {
			auto* p = builder->AddPartition(part.first + target_suffix, group, 0);
			if (!p || !builder->ResizePartition(p, part.second))
				return false;
		}
		return true;
	};
	if (allocate())
		return true;
	if (!allow_reuse) {
		info->error = "ROM does not fit beside the current ROM. Enable current-slot overwrite to review reuse, or turn high-speed flash off";
		return false;
	}
	builder->RemoveGroupAndPartitions(group);
	for (const auto& g : builder->ListGroups()) {
		if (g.size() <= source_suffix.size() ||
		    g.compare(g.size() - source_suffix.size(), source_suffix.size(), source_suffix) != 0)
			continue;
		for (const auto* p : builder->ListPartitionsInGroup(g))
			info->booted_partitions.push_back(p->name());
		builder->RemoveGroupAndPartitions(g);
	}
	info->available_after_reuse = info->capacity >= builder->UsedSpace() ? info->capacity - builder->UsedSpace() : 0;
	if (info->booted_partitions.empty() || !builder->AddGroup(group, group_limit) || !allocate()) {
		info->error = "ROM does not fit even after releasing the current ROM's Super space";
		return false;
	}
	info->reuses_booted = true;
	return true;
}

inline std::string ReviewSize(uint64_t bytes) {
	std::ostringstream out;
	if (bytes >= (1ULL << 30))
		out << std::fixed << std::setprecision(2) << bytes / double(1ULL << 30) << " GiB";
	else
		out << std::fixed << std::setprecision(1) << bytes / double(1ULL << 20) << " MiB";
	return out.str();
}

struct FlashReview {
	std::string file;
	std::string booted_slot;
	std::string active_slot;
	std::string target_slot;
	FlashSpaceInfo space;
	bool has_logical = false;
	bool readback = true;
	bool verify_signature = false;
	bool reflash_recovery = false;
	bool inject_recovery = false;
	bool reboot_after = false;
	bool overwrite_allowed = false;
	std::vector<std::string> firmware;
	std::vector<std::string> shared;
	size_t skipped_firmware = 0;
	std::string binding;  // Includes package identity, payload hashes and live metadata.
};

inline std::string RenderFlashReview(const FlashReview& review) {
	std::ostringstream out;
	out << "File: " << review.file << "\n\n"
	    << "Recovery booted from: " << review.booted_slot << '\n'
	    << "Bootloader active slot: " << review.active_slot << '\n'
	    << "Write to slot: " << review.target_slot << "\n\n";
	if (review.has_logical) {
		out << "Super required: " << ReviewSize(review.space.required) << '\n'
		    << "Available beside current ROM: " << ReviewSize(review.space.available) << '\n'
		    << "Super usable capacity: " << ReviewSize(review.space.capacity) << '\n'
		    << "ROM group limit: " << ReviewSize(review.space.group_limit) << '\n';
		if (review.space.reuses_booted)
			out << "Available after reuse: " << ReviewSize(review.space.available_after_reuse)
			    << "\n\nCURRENT ROM SPACE WILL BE REUSED.\n"
			    << "The current ROM will not boot until installation succeeds.\n";
		else
			out << "Current ROM's Super space will be preserved.\n";
	} else {
		out << "Super: no logical partitions in this payload.\n";
	}
	out << "\nRead-back verification: " << (review.readback ? "ON" : "OFF")
	    << "\nFirmware read-back: always ON when firmware is flashed.\n"
	    << "ZIP signature verification: " << (review.verify_signature ? "ON" : "OFF")
	    << "\nReflash recovery after ROM: " << (review.reflash_recovery ? "ON" : "OFF") << '\n';
	if (review.inject_recovery)
		out << "Inject TWRP after install: ON\n";
	out << "Reboot after install: " << (review.reboot_after ? "ON" : "OFF") << "\n\n";
	if (review.firmware.empty())
		out << "Firmware: none selected (" << review.skipped_firmware << " skipped).\n";
	else {
		out << "Firmware to flash:\n";
		for (const auto& name : review.firmware)
			out << "  " << name << '\n';
	}
	if (!review.shared.empty()) {
		out << "\nShared by both slots (written once):\n";
		for (const auto& name : review.shared)
			out << "  " << name << '\n';
	}
	out << "\nReview complete. No partition layout or images have been changed.\n"
	    << "The package and layout will be checked again before flashing.";
	return out.str();
}

// One-shot consent; viewing a plan alone never authorizes a flash.
struct FlashReviewConsent {
	std::string path;
	std::string binding;
	bool confirmed = false;
	void Clear() { path.clear(); binding.clear(); confirmed = false; }
	bool Confirm(const std::string& package) {
		confirmed = !binding.empty() && path == package;
		return confirmed;
	}
	bool Consume(const std::string& package, const std::string& current_binding) {
		const bool match = confirmed && path == package && binding == current_binding;
		Clear();
		return match;
	}
};

inline void BindReviewField(std::string* binding, const std::string& value) {
	*binding += std::to_string(value.size()) + ":" + value;
}

}  // namespace twrp
