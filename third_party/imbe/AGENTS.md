# Full-rate fixed-point codec source owner

This repository owns the full-rate IMBE C implementation and the existing Open25
C ABI. Classic and Next vendor exact committed snapshots with file hashes.
Refresh both callers and their copied header/runtime files from this owner;
do not maintain independently patched firmware copies.

Keep captured audio, private quality measurements, reference binaries, standards
PDFs, extracted standards text and research notes outside this repository.
Preserve the numerical origins recorded in SOURCES.json and quant-provenance.md.
Project code is MIT; published numerical inputs must retain their actual origin.
Do not describe imported numerical datasets as originally authored project data.

The encoder owns analysis history and its tail-frame requirement. The quantizer
owns pitch/L/K, word widths and unenhanced predictor history. The decoder owns
synthesis phase/noise and enhanced amplitudes. Callers own stream admission,
air framing/FEC, scheduling and audio I/O. Keep derived copies tied to these
owners rather than introducing alternate definitions.

After changing state layout or encoder drain requirements, regenerate
compat/open25/dm32_imbe.h with tools/generate_header.py and check complete
firmware RAM/stack budgets. Run the independent mathematical and sanitizer
checks with tools/test_c.py. DSP changes also require separate cross-codec,
level/clipping, quiet-speech and failure validation. Tests intended to preserve
behavior should retain exact payload/state comparisons; authorized numerical
approximations need explicit quality and performance evidence.

Builds and tests must never flash hardware or publish anything.
