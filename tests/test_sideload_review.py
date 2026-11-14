#!/usr/bin/env python3
"""Host check for the ADB sideload high-speed review.

Compiles ReviewSideloadHighSpeed() and its helpers from twrpinstall/twinstall.cpp
and SideloadHighSpeedPrompt() from gui/action.cpp, copied verbatim, against
stubs for DataManager, the page manager, the ZIP and BuildHighSpeedReview().
The install side runs on a thread and is answered like the GUI would.
Run: python3 tests/test_sideload_review.py
"""
import pathlib, subprocess, sys, tempfile

root = pathlib.Path(__file__).resolve().parents[1]
tw = (root / 'twrpinstall/twinstall.cpp').read_text().replace('\r\n', '\n')
act = (root / 'gui/action.cpp').read_text().replace('\r\n', '\n')


def block(src, start_marker, end_marker):
    a = src.index(start_marker)
    b = src.index(end_marker, a)
    return src[a:b]


helpers = block(tw, '// ADB sideload has its own toggle', 'std::string HsSize(')
review = block(tw, '// ADB sideload: the package only exists', '}  // namespace\n\nvoid TW_Set_Sideload')
setter = tw[tw.index('void TW_Set_Sideload_Review_Prompt'):]
setter = setter[:setter.index('\n}\n') + 3]
prompt = block(act, '// Runs on the sideload install thread', 'int GUIAction::adbsideload')

harness = r'''
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include "twinstall/flash_review.h"
#include "twinstall/flash_review_api.h"

struct DataManager {
    static std::mutex m; static std::map<std::string, std::string> v;
    static int GetIntValue(const std::string& k) { std::lock_guard<std::mutex> l(m); return v.count(k) ? std::stoi(v[k]) : 0; }
    static void SetValue(const std::string& k, int x) { std::lock_guard<std::mutex> l(m); v[k] = std::to_string(x); }
    static void SetValue(const std::string& k, const std::string& x) { std::lock_guard<std::mutex> l(m); v[k] = x; }
};
std::mutex DataManager::m; std::map<std::string, std::string> DataManager::v;
std::string g_page = "action_page"; bool g_has_page = true; std::string g_last_fail;
int gui_changePage(std::string p) { if (p != "sideload_review" || g_has_page) g_page = p; return 0; }
struct PageManager { static std::string GetCurrentPage() { return g_page; } };
#define LOGERR(...) fprintf(stderr, __VA_ARGS__)
typedef void* ZipArchiveHandle; struct ZipEntry64 {};
bool g_has_payload = true;
int FindEntry(ZipArchiveHandle, const std::string& n, ZipEntry64*) { return (n == "payload_properties.txt" && g_has_payload) ? 0 : -1; }
void HsSay(const std::string&) {}
void HsFail(const std::string& w) { g_last_fail = w; }
int g_build_calls = 0; std::string g_binding_seq[2] = {"A", "A"}; bool g_valid = true;
bool BuildHighSpeedReview(const std::string&, ZipArchiveHandle, twrp::FlashReview* r, std::string* reason) {
    *r = twrp::FlashReview{}; r->binding = g_binding_seq[g_build_calls++ % 2];
    if (!g_valid) *reason = "does not fit";
    return g_valid;
}
namespace {
''' + helpers + review + '''
}  // namespace
''' + setter + prompt + r'''
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::cerr << "line " << __LINE__ << ": " #c "\n"; fails++; } } while (0)

// Runs the install side on a thread and answers like the GUI would.
static bool run(const std::string& pkg, int answer, bool* reviewed) {
    DataManager::SetValue("tw_sideload_hs_decision", 0);
    g_build_calls = 0; g_page = "action_page";
    bool ok = false;
    std::thread t([&] { ok = ReviewSideloadHighSpeed(pkg, nullptr, reviewed); });
    if (answer) {
        for (int i = 0; i < 200 && g_page != "sideload_review"; i++) usleep(10000);
        CHECK(g_page == "sideload_review");
        DataManager::SetValue("tw_sideload_hs_decision", answer);
    }
    t.join();
    CHECK(g_page == "action_page");
    return ok;
}

int main() {
    const std::string sl = "/sideload/package.zip";
    bool reviewed = true;
    // Install-screen toggle must not affect sideload, and vice versa.
    DataManager::SetValue("tw_high_speed_flash", 1);
    CHECK(!HighSpeedEnabledFor(sl));
    CHECK(HighSpeedEnabledFor("/sdcard/rom.zip"));
    CHECK(ReviewSideloadHighSpeed(sl, nullptr, &reviewed) && !reviewed);  // toggle off: normal flow
    DataManager::SetValue("tw_high_speed_flash", 0);
    DataManager::SetValue("tw_sideload_high_speed", 1);
    CHECK(HighSpeedEnabledFor(sl));
    CHECK(!HighSpeedEnabledFor("/sdcard/rom.zip"));
    // Local zips never prompt here.
    CHECK(ReviewSideloadHighSpeed("/sdcard/rom.zip", nullptr, &reviewed) && !reviewed);
    // No payload: normal flow.
    g_has_payload = false;
    CHECK(ReviewSideloadHighSpeed(sl, nullptr, &reviewed) && !reviewed);
    g_has_payload = true;
    // No GUI hook registered: stop, nothing written.
    CHECK(!ReviewSideloadHighSpeed(sl, nullptr, &reviewed) && !reviewed);
    TW_Set_Sideload_Review_Prompt(SideloadHighSpeedPrompt);
    // Confirm with an unchanged plan.
    CHECK(run(sl, 1, &reviewed) && reviewed);
    CHECK(DataManager::GetIntValue("tw_hs_review_ready") == 0);
    // Cancel.
    CHECK(!run(sl, 2, &reviewed) && !reviewed);
    CHECK(g_last_fail.find("cancelled") != std::string::npos);
    // Plan changed between review and install.
    g_binding_seq[1] = "B";
    CHECK(!run(sl, 1, &reviewed) && !reviewed);
    CHECK(g_last_fail.find("changed") != std::string::npos);
    g_binding_seq[1] = "A";
    // Invalid plan: even a forced "install" answer is refused.
    g_valid = false;
    CHECK(!run(sl, 1, &reviewed) && !reviewed);
    CHECK(g_last_fail.find("does not fit") != std::string::npos);
    g_valid = true;
    // Theme without the page: refuse instead of waiting forever.
    g_has_page = false;
    CHECK(!run(sl, 0, &reviewed) && !reviewed);
    g_has_page = true;
    std::cout << (fails ? "FAILED" : "all sideload review checks passed") << "\n";
    return fails != 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    cpp = pathlib.Path(d) / 't.cpp'
    cpp.write_text(harness)
    exe = pathlib.Path(d) / 't'
    for cc in (['c++'],):
        r = subprocess.run(cc + ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
                                 '-I', str(root / 'twrpinstall/include'), '-I', str(root),
                                 str(cpp), '-o', str(exe)], capture_output=True, text=True)
        if r.returncode:
            print(cc[0], 'compile failed:\n', r.stderr[:3000]); sys.exit(1)
        r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
        print(cc[0] + ':', r.stdout.strip(), r.stderr.strip())
        if r.returncode:
            sys.exit(1)
