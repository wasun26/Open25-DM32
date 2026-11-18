# P25 CAI guidance

- Read README.md and SOURCES.md before changing protocol behavior.
- Keep algorithm and receive state ownership in this library. Applications own
  codec, RF, configuration, call policy, registration and session state.
- Preserve all original license notices. Keep external reference programs,
  standards PDFs, captures, test outputs, firmware, keys and backups outside Git.
- For every new wire layout, field or equation, verify the source PDF and record
  its exact designation/revision, clause, PDF page, printed page, document ID
  and corpus-relative PDF path in SOURCES.md.
- Keep the Open25 binding limited to the existing conventional C caller and ABI.
  Extend existing components directly; add a layer only for a concrete unmet
  caller, device, protocol or persisted-format requirement.
- The checked-in Golay table is derived from tools/generate_golay.py. Regenerate
  it and run --check after changes; do not maintain an independent table.
- Run meaningful host tests with sanitizers and cross compilation for changes
  to algorithms, public interfaces or bounded buffer processing.
- Keep local validation records outside the source tree. Do not publish, upload,
  flash hardware or modify a release checkout as part of routine library work.
