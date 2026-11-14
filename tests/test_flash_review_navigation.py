#!/usr/bin/env python3
"""Run the real GUIAction event methods against stock review-page key hooks.

This host harness isolates event dispatch from Android graphics. It reproduces
the page-entry/busy-to-idle/key-release sequence; device UI testing is separate.
Run: python3 tests/test_flash_review_navigation.py
"""
from pathlib import Path
import json
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def method(source, name):
    start = source.index("int GUIAction::" + name + "(")
    brace = source.index("{", start)
    depth = 1
    pos = brace + 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos]


class FlashReviewNavigation(unittest.TestCase):
    def check_theme(self, theme):
        root = ET.parse(ROOT / f"gui/theme/common/{theme}.xml").getroot()
        page = root.find('.//page[@name="flash_review"]')
        self.assertIsNotNone(page)
        hooks = {}
        for action in page.findall("action"):
            touch = action.find("touch")
            if touch is None or "key" not in touch.attrib:
                continue
            key = touch.get("key")
            target = action.find('action[@function="page"]')
            self.assertIsNotNone(target)
            conditions = action.findall("condition") + action.findall("conditions/condition")
            for condition in conditions:
                self.assertEqual(condition.attrib, {"var1": "tw_busy", "var2": "0"})
            hooks[key] = (target.text, bool(conditions))
        self.assertEqual(set(hooks), {"back", "home"})
        self.assertEqual(hooks["back"][0], "flash_confirm")
        self.assertEqual(hooks["home"][0], "main")

        # Keep actual NotifyVarChange/NotifyKey logic, not a reimplementation.
        source = (ROOT / "gui/action.cpp").read_text()
        harness = r'''
#include <map>
#include <string>
#include <vector>
#include <iostream>
struct HardwareKeyboard { void ConsumeKeyRelease(int) {} };
namespace PageManager {
HardwareKeyboard* GetHardwareKeyboard() { static HardwareKeyboard kb; return &kb; }
}
struct GUIObject { void NotifyVarChange(const std::string&, const std::string&) {} };
struct GUIAction : GUIObject {
    std::map<int, bool> mKeys;
    int mActionW = 0;
    int busy = 1;
    bool conditioned;
    std::string target;
    std::vector<std::string> navigations;
    GUIAction(int key, bool condition, const std::string& page)
        : conditioned(condition), target(page) { mKeys[key] = false; }
    bool isConditionValid() const { return conditioned; }
    bool isConditionTrue() const { return !conditioned || busy == 0; }
    bool IsConditionVariable(const std::string& variable) const { return variable == "tw_busy"; }
    void doActions() { navigations.push_back(target); }
    int NotifyVarChange(const std::string&, const std::string&);
    int NotifyKey(int, bool);
};
'''
        harness += method(source, "NotifyVarChange") + "\n" + method(source, "NotifyKey")
        harness += "\nint main() {\n"
        for keycode, key in enumerate(("back", "home"), 1):
            target, condition = hooks[key]
            harness += f"{{ GUIAction action({keycode}, {str(condition).lower()}, {json.dumps(target)});\n"
            harness += r'''
    action.NotifyVarChange("", ""); // Enter page while planning is busy.
    action.busy = 0;
    action.NotifyVarChange("tw_busy", "0"); // operation_end completes planning.
    action.NotifyVarChange("tw_hs_review_ready", "1");
    if (!action.navigations.empty()) {
        std::cerr << "Planning completion navigated without a key press: " << action.target << '\n';
        return 1;
    }
'''
            harness += f"action.NotifyKey({keycode}, true);\n"
            harness += "if (!action.navigations.empty()) return 2;\n"
            harness += f"action.NotifyKey({keycode}, false);\n"
            harness += "if (action.navigations != std::vector<std::string>{action.target}) return 3;\n}\n"
        harness += '}\n'
        with tempfile.TemporaryDirectory() as temp:
            cpp = Path(temp) / "navigation.cpp"
            binary = Path(temp) / "navigation"
            cpp.write_text(harness)
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_portrait(self):
        self.check_theme("portrait")

    def test_landscape(self):
        self.check_theme("landscape")

    def test_watch(self):
        self.check_theme("watch")


if __name__ == "__main__":
    unittest.main(verbosity=2)
