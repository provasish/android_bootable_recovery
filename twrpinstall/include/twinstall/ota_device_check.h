// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <set>
#include <sstream>
#include <string>

namespace twrp {

inline std::string TrimOtaValue(const std::string& value) {
	const auto first = value.find_first_not_of(" \t\r");
	if (first == std::string::npos)
		return {};
	return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

// Match complete aliases, never a substring or a value supplied by the ROM
// currently installed on /system. Standard OTA pre-device lists use '|'; some
// packages use ',' like TARGET_OTA_ASSERT_DEVICE. Accept either separator while
// still matching complete aliases. Keep this independent of Android for tests.
inline bool CheckOtaDevice(const std::string& metadata, const std::string& aliases,
                           std::string* reason) {
	std::set<std::string> allowed;
	std::istringstream alias_stream(aliases);
	std::string token;
	while (std::getline(alias_stream, token, ',')) {
		token = TrimOtaValue(token);
		if (!token.empty())
			allowed.insert(token);
	}
	if (allowed.empty()) {
		*reason = "recovery has no OTA device aliases configured";
		return false;
	}
	if (metadata.find('\0') != std::string::npos) {
		*reason = "OTA metadata contains a NUL byte";
		return false;
	}
	std::istringstream lines(metadata);
	std::string line, devices, ota_type;
	bool have_devices = false, have_type = false;
	while (std::getline(lines, line)) {
		const auto equal = line.find('=');
		if (equal == std::string::npos)
			continue;
		const auto key = TrimOtaValue(line.substr(0, equal));
		const auto value = TrimOtaValue(line.substr(equal + 1));
		if (key == "pre-device") {
			if (have_devices) {
				*reason = "duplicate pre-device in OTA metadata";
				return false;
			}
			have_devices = true;
			devices = value;
		} else if (key == "ota-type") {
			if (have_type) {
				*reason = "duplicate ota-type in OTA metadata";
				return false;
			}
			have_type = true;
			ota_type = value;
		}
	}
	if (!have_type || ota_type != "AB") {
		*reason = "OTA metadata does not declare ota-type=AB";
		return false;
	}
	if (!have_devices || devices.empty()) {
		*reason = "OTA metadata has no pre-device";
		return false;
	}
	std::string device_list = devices;
	for (char& separator : device_list)
		if (separator == ',')
			separator = '|';
	std::istringstream device_stream(device_list);
	while (std::getline(device_stream, token, '|')) {
		if (allowed.count(TrimOtaValue(token))) {
			reason->clear();
			return true;
		}
	}
	*reason = "OTA targets '" + devices + "', recovery accepts '" + aliases + "'";
	return false;
}

}  // namespace twrp
