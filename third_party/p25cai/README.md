# P25 CAI

Portable C99 Phase 1 FDMA air-interface processing. [MIT license](LICENSE).

## Interfaces

`include/p25cai.h` provides:

- HDU, LDU1, LDU2, TDU and TDULC framing and decoding.
- BCH NID, shortened Reed-Solomon, Golay, Hamming and low-speed cyclic codes.
- Voice-parameter channel coding, whitening and interleaving.
- Rate 1/2 and 3/4 trellis block coding with hard-bit Viterbi decoding.
- Unconfirmed and full-fragment confirmed packet assembly with header,
  block and packet CRC validation.
- Single/double/triple TSBKs and standard/alternative multiblock control units.
- Voice LC/header fields, FDMA bandplans, channel grants, site broadcasts,
  abbreviated requests/responses and authentication message fields.
- AACE-A AM1/AM3 key derivation and AM2/AM4 response calculation.
- Voice and control dibit-stream receivers, polarity normalization and NAC filters.
- Qualified channel-status observations and explicit voice-stream discontinuities.

Arrays are MSB first in transmission order. Integer codewords put the first
transmitted bit in the highest used bit. Invalid input and failed payload
checks return negative errors; outputs are committed only on success.
Check the per-field quality flags after a successful voice-frame decode.
Bounded error correction can miscorrect damage beyond its correction radius.

The voice boundary is eight parameter words: four 12-bit, three 11-bit and one
unshifted 7-bit word. The caller supplies voice sync-bit alternation and its
codec. The library has no PCM codec, RF driver, operating-system calls, heap,
mutable global state or persistent key storage.

Applications own admission, call/audio policy, RF access, configuration,
learned-plan storage, registration, retries and session lifetime. Frequency
resolution and decoded messages do not authorize a transmission. Packet-data
selective retries and enhanced addressing are handled outside the complete
packet helper; its unsupported formats return an error. Control receivers can
be finished at a physical end-of-burst when trailing null fill was omitted.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DP25CAI_SANITIZE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires CMake 3.16 and a C99 compiler. Python 3.8+ verifies the generated
Golay table. It is not a runtime dependency. Set `BUILD_TESTING=OFF` for
library-only cross compilation. Sanitizers default to OFF and are intended
for host Clang/GCC builds.

Link target `p25cai` and include `p25cai.h`. Direct-build integrations compile
`src/*.c` with `include/` on their header search path; `src/internal.h` and
`src/golay_errors.inc` stay beside those sources. Provide the target's normal
`memcpy`, `memset` and compiler-generated integer runtime helpers.

`p25cai_open25` supplies the conventional Open25-DM32 `p25.h` C interface and
structure layouts. Its conversion is limited to that existing caller; coding,
framing and receive state remain owned by `p25cai`. Set `P25CAI_OPEN25=OFF`
when that interface is not needed. A codec-linked firmware combination must
follow the codec's license.

## Sources and generated data

[SOURCES.md](SOURCES.md) maps protocol behavior to exact standard editions,
clauses, diagrams and pages. Source publications are not bundled.

```sh
python3 tools/generate_golay.py
python3 tools/generate_golay.py --check
```

The generator owns the mathematical Golay syndrome table. Regenerate after
changing its derivation; never edit the generated table directly. Keep any
integration pinned to a specific revision of this repository.
