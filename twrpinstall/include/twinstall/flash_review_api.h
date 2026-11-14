// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

bool TW_Prepare_High_Speed_Flash_Review(const std::string& path, std::string* text);
bool TW_Confirm_High_Speed_Flash_Review(const std::string& path);
void TW_Clear_High_Speed_Flash_Review();

// ADB sideload review: shows |text| and blocks the install thread until the
// user confirms (true) or cancels (false). |ready| false = cancel only.
typedef bool (*TW_Sideload_Review_Prompt)(const std::string& text, bool ready);
void TW_Set_Sideload_Review_Prompt(TW_Sideload_Review_Prompt prompt);
