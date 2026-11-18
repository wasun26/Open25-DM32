# RadioFoundry codeplug components

Public MIT source selected from RadioFoundry commit
9e1f7209cd00c2575f18c72746ed4dbb1f482f67. Original hashes and paths are in
UPSTREAM.json. Original copyright notices remain in each file.

Local changes implement the existing RFKV continuation design using unused
combined-image sectors, preserve explicit versus inherited TX NAC, and project
canonical mode records through Classic's existing channel interface. The format
uses the upstream RFKV v1 headers, entries and registered mode kinds; it does not
add a second container. Continuation roots use E000 and banked pages AF000–CEFFF.
The header's offset is interpreted in the same combined image used by CPS.
Ordinary records and the DMR ID database retain their locations.

Channel P25 parameters retain the upstream five-byte prefix. An append-only
tail adds a direct group target (flag1 at byte5; u16LE at byte6), preserving
Classic's independent P25 talkgroups without allocating DMR contacts. Other mode
parameter bytes remain owned by their modules. This candidate is not a claim
that upstream has accepted the additions. Existing A5 records are converted
once through the journal; no release file or hardware is modified by building.
