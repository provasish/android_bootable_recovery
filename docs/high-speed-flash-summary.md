# High-speed flash review

Install a local full A/B ROM ZIP with High-speed flash enabled:

1. Select the ZIP and the desired install options.
2. Tap **Review Flash**.
3. Read the scrollable summary. Use **Back to Options** to make changes.
4. **Swipe to Install** appears only after a successful review.

The summary shows the recovery's booted slot, the bootloader's active slot,
the actual target slot, sector-aligned logical image requirements, usable
Super capacity, available space after reclaiming the old target group, and
whether the booted ROM's Super space must be reused. It lists selected firmware
and any physical partitions shared by both slots, plus read-back, ZIP
signature, recovery-reflash, injection and reboot options as applicable.

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
reflash/reinjection runs only after a successful install. Normal flashing,
ADB sideload and OpenRecoveryScript retain their existing entry points; the
new review screen is for local GUI installs. Review currently requires one
ROM ZIP in the queue. Turn high-speed flashing off for a normal ZIP batch or
for a package that requires the normal installer.

Portrait, landscape and watch stock themes include the review page. Textboxes
now split expanded multiline values into logical lines before word wrapping
and rebuild their contents only when the expanded text changes. The summary's
detailed body is currently English; button labels use normal theme resources.

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
Run the existing `twrp_ota_device_check_test` for OTA metadata checks.

On device, first open a summary and return to options without flashing. Check
the displayed slots against recovery boot properties and bootctl, and compare
the Super figures with lpdump. Test firmware off/on, read-back off/on, a
package that fits beside the current ROM, one that requires explicit reuse,
and one that does not fit. A rejected review must offer no install swipe.
Changing an option after returning should require a new review. Use existing
partition backups before actual ROM-install tests, and verify normal local
install and sideload behavior separately.
