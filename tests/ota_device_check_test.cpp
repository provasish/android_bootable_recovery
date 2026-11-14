// SPDX-License-Identifier: GPL-3.0-or-later
// Host build: use -I twrpinstall/include and compile this file as C++17.
#include "twinstall/ota_device_check.h"

#include <cstdlib>
#include <iostream>

static unsigned checks = 0;

static void Check(const std::string& metadata, bool expected,
                  const std::string& aliases = "RMX3371,RE54E4L1,spartan") {
	std::string reason;
	const bool result = twrp::CheckOtaDevice(metadata, aliases, &reason);
	if (result != expected || (result && !reason.empty()) || (!result && reason.empty())) {
		std::cerr << "Unexpected validation result: " << reason << '\n';
		std::exit(1);
	}
	++checks;
}

int main() {
	Check("ota-type=AB\npre-device=RMX3371\n", true);
	Check("ota-type=AB\npre-device=spartan\n", true);
	Check("ota-type=AB\npre-device=RE54E4L1\n", true);
	Check("ota-type=AB\npre-device=other|spartan\n", true);
	Check("ota-type=AB\r\npre-device= RMX3371 \r\npost-timestamp=1234\r\n", true);
	Check("ota-type=AB\npre-device=other\n", false);
	Check("ota-type=AB\npre-device=RMX3371_evil\n", false);
	Check("ota-type=AB\npre-device=rmx3371\n", false);
	Check("ota-type=AB\npre-device=RMX3371,other\n", true);
	Check("ota-type=AB\npre-device=RMX3371,RE54E4L1,spartan\n", true);
	Check("ota-type=AB\npre-device=other,spartan\n", true);
	Check("ota-type=AB\npre-device=other| RE54E4L1 ,another\n", true);
	Check("ota-type=AB\r\npre-device= other , RMX3371 \r\n", true);
	Check("ota-type=AB\npre-device=other,another\n", false);
	Check("ota-type=AB\npre-device=RMX3371_evil,other\n", false);
	Check("ota-type=AB\npre-device=other|spartan_evil,another\n", false);
	Check("ota-type=AB\npre-device=rmx3371,other\n", false);
	Check("ota-type=AB\npre-device=,,,||,\n", false);
	Check("ota-type=AB\n", false);
	Check("ota-type=AB\npre-device=\n", false);
	Check("pre-device=spartan\n", false);
	Check("ota-type=BLOCK\npre-device=spartan\n", false);
	Check("ota-type=AB\npre-device=spartan\npre-device=other\n", false);
	Check("ota-type=AB\npre-device=spartan\npre-device=spartan\n", false);
	Check("ota-type=AB\nota-type=AB\npre-device=spartan\n", false);
	Check(std::string("ota-type=AB\npre-device=spartan") + '\0' + "other\n", false);
	Check("ota-type=AB\npre-device=spartan\n", false, " , ");
	Check("", false);
	std::cout << checks << " OTA device checks passed\n";
}
