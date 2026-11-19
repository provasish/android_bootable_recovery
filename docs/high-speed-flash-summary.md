# High-speed flash review

Install a local full A/B ROM ZIP with High-speed flash enabled:

1. Select the ZIP and the desired install options.
2. Tap **Review Flash**.
3. Read the scrollable summary. Use **Back to Options** to make changes.
4. **Swipe to Install** appears only after a successful review.

The device check accepts both standard pipe-separated and OEM comma-separated
`pre-device` lists. Aliases are trimmed and compared as complete, case-sensitive
tokens; a similar prefix or substring is not a match.

The summary shows the recovery's booted slot, the bootloader's active slot,
the actual target slot, sector-aligned logical image requirements, usable
Super capacity, available space after reclaiming the old target group, and
whether the booted ROM's Super space must be reused. It lists selected firmware
and any physical partitions shared by both slots, plus read-back, ZIP
signature, recovery-reflash, injection and reboot options as applicable.

Like update_engine, the whole target slot is replaced: every Super group
ending in the target suffix is dropped with its partitions, even when it
carries another ROM's name (e.g. `realme_sm8250_mainline_dynpart_b` while
the new ROM declares `qti_dynamic_partitions_b`). The summary then says
which group is replaced. Their dm mappings are released before the new table
is written. Groups of the booted slot are only touched with the overwrite
opt-in.

Space is computed by running the installer's actual layout algorithm against
an in-memory liblp builder, including the overwrite opt-in. The layout routine
returns before unmounting partitions, changing dm mappings or writing metadata
in preview mode. Preview never cancels snapshot updates. With virtual A/B,
Metadata must already be mounted so its update state can be read; active
snapshot updates block review until resolved in Android.

The installer verifies the ZIP signature/digest according to the selected
settings, then checks the review again before installer dispatch. Consent is
one-shot and bound to the package path and file identity, payload partition
names/sizes/hashes, the booted/active slots, install options, resolved physical
targets and the source Super geometry/partition/extent/group/device tables.
A changed or unavailable plan aborts and requires review again. This is a
compatibility and stale-plan check; the selected ZIP signature policy still
controls package authenticity. It does not prevent another privileged process
from modifying block devices concurrently with recovery.

A reviewed install cannot fall back to update_engine because its partition or
firmware choices could differ from the displayed plan. Automatic recovery
reflash runs only after a successful install; the pre-existing ADB sideload
reinjection helper is still called on both success and failure. Normal flashing
and OpenRecoveryScript retain their existing entry points; ADB sideload has
its own reviewed flow (below). Review currently requires one
ROM ZIP in the queue. Turn high-speed flashing off for a normal ZIP batch or
for a package that requires the normal installer.

Portrait, landscape and watch stock themes include the review page. Textboxes
now split expanded multiline values into logical lines before word wrapping
and rebuild their contents only when the expanded text changes. The summary's
detailed body is currently English; button labels use normal theme resources.

## ADB sideload

Advanced → ADB Sideload has its own **High-speed flash** toggle
(`tw_sideload_high_speed`) with the same verify/firmware/overwrite options.
The Install screen's toggle no longer carries over to a sideload.

The package only exists once `adb sideload` starts serving it, so the review
happens inside the install while adb waits:

1. Signature/digest checks run as usual, then the same plan is built and shown
   on the **Review Sideload Flash** page.
2. **Swipe to Install** continues; **Cancel Install**, Back or Home stops the
   install before anything is written. A plan that can't run shows only Cancel.
3. On confirm, the plan is built again and must match what was shown, then
   it installs as a reviewed plan: no fallback to update_engine.

The page is an overlay over the sideload progress page and only sets
`tw_sideload_hs_decision`; the waiting install thread reads it and closes the
overlay (re-entering action_page would re-run its `%tw_action%` entry action).
When it opens, the page sets `tw_sideload_review_shown`; a theme without a
`sideload_review` page makes the install stop instead of waiting. Sideload
never reboots on its own, so the review always shows "Reboot after install:
OFF". The payload is read over USB through the sideload FUSE file, so the
transfer usually limits the speed; installing from storage is faster.

## Validation

Host tests cover layout allocation, reclaiming inactive-slot space, preserving
the booted group, opt-in booted-space reuse, both target slots, out-of-space
failures, sector rounding, size overflow, one-shot consent and stale bindings,
summary contents and multiline textbox splitting. The fixture uses a bounded
in-memory allocator; real liblp fragmentation and Android integration still
require a recovery build and device tests.

```bash
c++ -std=c++17 -Wall -Wextra -Werror \
  -I bootable/recovery -I bootable/recovery/twrpinstall/include \
  bootable/recovery/tests/flash_review_test.cpp -o /tmp/flash-review-test
/tmp/flash-review-test
```

The Android build also exposes the `twrp_flash_review_test` host test target.
Run the `twrp_ota_device_check_test` host test target for OTA metadata checks.
`tests/run_host_tests.sh` runs all four host tests in one command; it needs a
C++ compiler (set `CXX` to pick one). The two Python harnesses stay manual by
design: they read the real sources at runtime, which cannot be staged reliably
in a `python_test_host` sandbox.

`python3 tests/test_flash_review_navigation.py` runs the actual GUIAction
variable/key dispatch methods in a host harness against each stock review
page. It checks that planning completion does not navigate away, and that
Back/Home navigate only after a key press/release; it also checks that every
theme's `sideload_review` page only sets the decision variable.
`python3 tests/test_sideload_review.py` compiles the sideload review code
from `twinstall.cpp` and `action.cpp` against stubs and runs confirm,
cancel, changed-plan, invalid-plan, missing-page and toggle-separation cases.
Keep the review page's
key-only actions free of variable conditions: GUIAction also executes
conditional actions when their watched variable changes. The on-screen
navigation buttons and install slider retain their busy-state conditions.

On device, first open a summary and return to options without flashing. Check
the displayed slots against recovery boot properties and bootctl, and compare
the Super figures with lpdump. Test firmware off/on, read-back off/on, a
package that fits beside the current ROM, one that requires explicit reuse,
and one that does not fit. A rejected review must offer no install swipe.
Changing an option after returning should require a new review. Use existing
partition backups before actual ROM-install tests, and verify normal local
install and sideload behavior separately.
