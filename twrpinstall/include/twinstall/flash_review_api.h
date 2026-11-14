// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

bool TW_Prepare_High_Speed_Flash_Review(const std::string& path, std::string* text);
bool TW_Confirm_High_Speed_Flash_Review(const std::string& path);
void TW_Clear_High_Speed_Flash_Review();
