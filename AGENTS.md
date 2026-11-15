# AGENTS.md

Instructions for AI coding agents working in `Open25-DM32`.

## 1. Architecture & Where to Edit

- **Never treat `build/radiofoundry-p25/` as the source of truth.** It is a generated, patched copy created by `tools/build_radiofoundry.py`. Any edits made directly inside `build/radiofoundry-p25/` will be overwritten on the next build.
- **Upstream source**: `research/upstream/radiofoundry` (pinned RadioFoundry checkout; do not modify directly).
- **Patch layers** (edit these to change RadioFoundry core/UI/board behavior):
  - `tools/build_radiofoundry.py`: copies upstream, applies `core/radio/radio.h` / `radio_task.c` / `freertos_board.h` / `beep.c` transforms, copies `src/p25/` + `integration/radiofoundry/` into `core/p25/`, and configures `--classic` build flags.
  - `tools/classic_channels.py`: `prepare(transform)` applies Classic P25 and custom UI/radio feature patches (`settings.h`, `radio_sm.h/.c`, `ui_svc.h`, `ui_svc_radio.c`, `ui_svc_storage.c`, `ui_menus.c`, `ui_screens.c`, `ui_app.c`, etc.).
- **Owned sources**: `src/p25/`, `src/modem/`, `src/codec/`, `src/runtime/`, `integration/radiofoundry/`, `tests/classic/`.
- **Custom feature documentation**: See `CUSTOM_CHANGES.md` for all customizations beyond the fork and their bit allocations.

## 2. Build, Test, and Packaging Commands

Run build and test targets in **WSL / Linux** (`toolchain/bin/csky-elf-` is a Linux ELF toolchain):

```bash
# Run host tests (ASan + UBSan + -Wshadow)
make classic-test

# Build DM-32 Classic firmware (also runs stack check and rejects compiler warnings)
make radiofoundry

# Package firmware .zip for the web Firmware Utility (output path must not exist yet)
python3 tools/prepare_web_firmware.py \
  --image build/radiofoundry-p25/build/dm32-p25-classic/rfoundry_dm32.bin \
  --out artifacts/firmware/<release-name>.zip
```

When working from Windows without WSL access, always verify patch application locally before finishing:

```powershell
python -c "import sys; sys.path.insert(0, 'tools'); import classic_channels; from pathlib import Path; up = Path('research/upstream/radiofoundry');
def check(rel, reps):
    s = (up / rel).read_text(encoding='utf-8')
    for old, new, *rest in reps:
        c = rest[0] if rest else 1
        assert s.count(old) >= c, f'{rel}: missing {old[:60]!r}'
        s = s.replace(old, new, c)
classic_channels.prepare(check); print('ALL PATCHES OK')"
```

## 3. Critical Constraints & Gotchas

- **`sizeof(settings_t) == 392`**: `core/settings/settings.c` has `_Static_assert(sizeof(settings_t) == 392, ...)`. Do **not** add new struct fields to `settings_t`; pack new flags/values into spare bits of existing fields (check `CUSTOM_CHANGES.md` Section 3 before choosing bits).
- **DM-32UV RAM Limit**: Application RAM (`boards/dm32/memory.ld`) spans `0x00010000..0x00043A00` (`211,456` bytes before the 049 codec window), including the `36.5 KiB` FreeRTOS heap in Classic. Enabling heavy features (`DMR_IP_GATEWAY`, `NET_PPP`, `SFR`, `HOTSPOT`, `ZONE_SHARE`, `BCAST_FM`, `DUAL_WATCH`) alongside P25 risks overflowing RAM. Check `_end` vs `0x00043A00` in `build/radiofoundry-p25/build/dm32-p25-classic/rfoundry_dm32.map`.
- **Zero Compiler Warnings**: `tools/build_radiofoundry.py` aborts if `"warning:"` appears anywhere in the build log, and `tests/classic/` compiles host tests with `-Wall -Wextra -Wshadow -fsanitize=address,undefined`.
- **Line Endings**: Keep `.sh` scripts and patch files (e.g., `src/codec/imbe-speed.patch`) with **LF** line endings so WSL builds do not fail.
- **UI/Radio Threading**: UI runs in the UI task; radio state runs in `radio_task`. Settings in `radio_settings_t` must go through `ui_svc_apply()` -> `radio_set_settings()` -> `radio_sm_set_settings()`, and any fields needed by Home/Quick screens (`ui_screens.c`) should be mirrored into `radio_view_t`.
