// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <utility>
#include <vector>

inline std::vector<std::string> SplitTextBoxLines(const std::string& text) {
	std::vector<std::string> lines;
	size_t start = 0;
	for (;;) {
		const size_t end = text.find('\n', start);
		std::string line = text.substr(start, end == std::string::npos ? end : end - start);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		lines.push_back(std::move(line));
		if (end == std::string::npos)
			break;
		start = end + 1;
	}
	return lines;
}
