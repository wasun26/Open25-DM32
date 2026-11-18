# Full-rate IMBE for Open25

A fixed-point C implementation of the P25 Phase 1 full-rate voice codec. The
implementation uses the mathematical description in TIA-102.BABA-A, with the
numerical origins and implementation choices recorded in
[quant-provenance.md](quant-provenance.md) and [SOURCES.json](SOURCES.json).

The caller-facing interface is
[compat/open25/dm32_imbe.h](compat/open25/dm32_imbe.h). Each call processes
160 signed 16-bit PCM samples at 8 kHz: one 20 ms voice frame. Payloads use eight
words with widths `{12,12,12,12,11,11,11,7}`; word 7 includes the sync bit at bit 0.
The pack/unpack functions concatenate those 88 bits MSB-first into 11 bytes.
The caller owns air framing, error correction and received-frame admission.

A context belongs to one serialized half-duplex voice session. Initialize it
before use and keep context, PCM, parameter and packed-byte buffers separate.
Changing encode/decode direction resets the new direction. Copy every context
storage byte when cloning a session; phase, noise and predictor history are
instance-owned. The implementation uses caller-owned storage, with no runtime
allocator or mutable global DSP workspace.

The encoder retains analysis look-ahead. After the last real PCM frame of a
nonempty transmission, encode `DM32_IMBE_ENCODER_FLUSH_FRAMES` zero PCM frames
and retain their payloads before closing the encoded stream. The generated
header derives this value from the encoder's authoritative drain requirement.

## Building and checking

Compile the six files in `src/` as C11, using the same compiler ABI and target
options as the application. The CK803S configuration uses little-endian ABI v2,
low registers and integer arithmetic. `build-options.json` records the selected
build settings. The CK803S preset uses `-O2` and places the sine function and
1 KiB sine table in `.ramfunc*` and `.ramdata*`; target linker scripts must
map both to startup-copied RAM sections. Other code stays in flash.
The source requires two's-complement fixed-width integers and
arithmetic signed right shifts, checked at compilation where used.

Run the standalone host checks with a C compiler and Python 3:

```sh
python3 tools/test_c.py --out build/tests --sanitize
```

These tests use mathematical double-precision references, synthesized signals,
worked bit-layout facts, state/guard checks and hostile input. They require no
speech recordings, standards PDF or external codec implementation. Mathematical
reference tolerances exercise the fixed-point arithmetic; they do not establish
subjective speech quality, hardware timing or radio interoperability.

Regenerate the opaque context capacity and encoder flush constant after relevant
layout/contract changes, passing the built host shared library:

```sh
python3 tools/generate_header.py build/host/libopen25_imbe.dylib
```

`runtime/memory.c` is the shared freestanding memory implementation. Build it with
`-O2 -fno-builtin -fno-tree-loop-distribute-patterns -fno-lto`; aligned word accesses
use the GCC/Clang `may_alias` extension, while unaligned accesses and tails use
bytes. Firmware integrations own their complete memory and stack checks.

This repository is the authoritative source owner. Classic and Next refresh exact
committed snapshots and copied bindings together, then rerun their complete
integration checks.

## License and source origins

Project-authored implementation, integration and tests use the MIT license in
[LICENSE](LICENSE). Published quantizer/allocation data and the pitch FIR retain
specific standards origins; formula-generated windows and mathematical tables
are identified separately. The numerical datasets are not claimed as originally
authored project data.

Pavel Yazev's GPL codec and ISC mbelib may be used as separate host-only
interoperability references. They are not runtime dependencies of this source
snapshot. The included standalone tests do not require them. MIT covers the project code; numerical origins are identified separately.
