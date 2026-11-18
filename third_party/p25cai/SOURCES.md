# Protocol references

The tables identify the editions used for this implementation. PDF pages are
one based and include front matter; printed pages use the publication's labels.
Each document key resolves to the exact document ID and corpus-relative path
below. Source publications and reference software are not bundled.

## Documents

| Key | Publication | Document ID |
| --- | --- | --- |
| BAAA | TIA-102.BAAA-B, FDMA Common Air Interface, June 2017 | `p25_d1460d9ae0713992` |
| AABF | TIA-102.AABF-D, Link Control Word Formats and Messages, April 2015 | `p25_9dee2110f368c422` |
| BAAC | TIA-102.BAAC-D, Common Air Interface Reserved Values, June 2017 | `p25_51ef5661ee2ec6ac` |
| AABB | TIA-102.AABB-B, Trunking Control Channel Formats, April 2011 | `p25_3b736a9a242d0479` |
| AABC | TIA-102.AABC-E, Trunking Control Channel Messages, April 2019 | `p25_89a3b878eee70a14` |
| AACE | TIA-102.AACE-A, Authentication, April 2011 | `p25_c1c4e0fac4666363` |
| AES | FIPS PUB 197, Advanced Encryption Standard, November 26, 2001 | `fips197-2001` |

Corpus-relative paths and SHA-256 identities:

- BAAA: `documents/tia-library/8.15_TIA-102.BAAA-B_Final Published Document_2017_06_22.pdf`;
  `d1460d9ae07139928ba969d17a9db33823a758a4aa0dc52ee541a5ed49c89469`.
- AABF: `documents/tia-library/8.10_TIA-102.AABF-D_Final Published Document_2015_04_06.pdf`;
  `9dee2110f368c422db05b917393d5add3564fc46595ca344e24ccaae128fad12`.
- BAAC: `documents/tia-library/8.15_TIA-102.BAAC-D_Final Published Document_2017_06_22.pdf`;
  `51ef5661ee2ec6ac31f2f0dc589e76dd80a66a619c8298521ffe942e6dbdc93c`.
- AABB: `documents/tia-library/TIA-102.AABB-B Final for Publication.pdf`;
  `3b736a9a242d04797ad10130e0fa074e6f9b9ca28b61f232a5d391783d0cfd63`.
- AABC: `documents/tia-library/8.10_TIA-102-AABC-E_Final Published Document_2019_04_231.pdf`;
  `89a3b878eee70a1414bebc18b214dba4e1bb5b18e4c7efd5c1e58ee5b195b45d`.
- AACE: `documents/tia-library/TIA-102.AACE-A Final for Publication.pdf`;
  `c1c4e0fac4666363dc5bc746ba4e92432def36156dd4ee1382c9f36b4806decf`.
- AES: [NIST publication PDF](https://csrc.nist.gov/files/pubs/fips/197/final/docs/fips-197.pdf);
  `89c6da9e6cb81ffb7c115752b789ff3dc205acb2b13d6a36ff55acc79e87f61e`.

## Coding and framing

| Behavior | Document and clause/table | PDF / printed pages | Implementation |
| --- | --- | --- | --- |
| MSB-first transmission | BAAA 4.1.1, Annex A.1 | 25 / 11; 64–65 / 50–51 | `internal.h`, `frame.c` |
| HDU fields and concatenated coding | BAAA 4.2, Figure 11 | 26 / 12 | `frame.c`, `fields.c` |
| Voice word widths, protection and PN recurrence | BAAA 4.3, Figure 12 | 27–28 / 13–14 | `voice.c` |
| Voice permutation | BAAA 4.3.1, Table 1 | 28–29 / 14–15 | `voice.c` |
| ES, LC and TDULC | BAAA 4.4–4.5.1, Figures 13–16 | 30–31 / 16–17 | `frame.c` |
| Low-speed cyclic polynomial | BAAA 4.6, Table 2 | 31–32 / 17–18 | `fec.c` |
| Golay extension and shortening | BAAA 4.7, Table 3 | 32–33 / 18–19 | `fec.c`, `generate_golay.py` |
| Hamming parity relationships | BAAA 4.8, Table 4 | 33 / 19 | `fec.c` |
| GF(64), RS roots and shortening | BAAA 4.9, Tables 5–9 | 34–36 / 20–22 | `fec.c` |
| Trellis state outputs and termination | BAAA 6–6.1, Tables 11–12 | 50–51 / 36–37 | `trellis.c` |
| Trellis interleave and constellation | BAAA 6.1–6.2, Tables 13–14; 8.2, Table 20 | 52 / 38; 60 / 46 | `trellis.c` |
| Voice-unit placement and padding | BAAA 7.2–7.2.3, Figures 29–34 | 53–56 / 39–42 | `frame.c` |
| Frame sync and status cadence | BAAA 7.3–7.4, Tables 15–16 | 57 / 43 | `frame.c`, `blocks.c` |
| NID layout, BCH polynomial and P bit | BAAA 7.5–7.5.2, Tables 17–19 | 57–59 / 43–45 | `fec.c` |
| Explicit/implicit and protected LC | AABF 7.1–7.2 | 17–19 / 6–8 | `fields.c` |
| Group and individual LC layouts | AABF 7.3.1, 7.3.3 | 20–21 / 9–10 | `fields.c` |
| NAC and standard MFIDs | BAAC 2.1–2.3 | 14–15 / 4–5 | field helpers |
| Clear MI, KID and ALGID | BAAC 2.6–2.8 | 16 / 6 | `fields.c` |

## Packet and control transport

| Behavior | Document and clause/figure | PDF / printed pages | Implementation |
| --- | --- | --- | --- |
| Header CRC16 | BAAA 5.2 | 38 / 24 | `blocks.c` |
| Packet CRC32 and padding | BAAA 5.3–5.3.3 | 38–39 / 24–25 | `blocks.c`, `packet.c` |
| Confirmed header | BAAA 5.4.1, Figure 18 | 40 / 26 | `packet.c` |
| Confirmed serial and CRC9 | BAAA 5.4.2, Figure 19 | 41–42 / 27–28 | `blocks.c`, `packet.c` |
| Unconfirmed header and blocks | BAAA 5.6, Figures 23–24 | 45–46 / 31–32 | `packet.c` |
| PDU status and final padding | BAAA 7.2.4, Figure 35 | 56 / 42 | `packet.c`, `control.c` |
| Control DUIDs and block coding | AABB 4.1–4.4 | 16–17 / 8–9 | `control.c` |
| One to three TSBKs, LB and CRC | AABB 5–5.1 | 18–21 / 10–13 | `control.c` |
| Optional final null fill | AABB 5 | 20 / 12 | control decode/receiver finish |
| Multiblock sizes and CRC | AABB 6 | 28–32 / 20–24 | `control.c` |
| Standard/alternative headers and SAP | AABB 6.1, Figures 6.1-1/6.1-2 | 30–31 / 22–23 | `control.c` |

## Abbreviated message fields

All rows in this table use AABC. Directions are ISP (subscriber to system) and
OSP (system to subscriber). The semantic helpers process the indicated fields;
applications own procedure state and message admission.

| Fields / opcodes (hex) | Clause | PDF / printed pages |
| --- | --- | --- |
| Base, bandwidth, channels, spacing | 2.3.5–2.3.10 | 28–31 / 9–12 |
| FDMA offset units/sign and explicit-TX sentinels | 2.3.35–2.3.36 | 41–42 / 22–23 |
| ISP group request 00 | 4.1.1 | 48 / 29 |
| ISP individual request 04 | 4.1.2 | 49 / 30 |
| ISP answer response 05 | 4.1.3 | 51 / 32 |
| OSP group grant 00 | 4.2.1 | 58 / 39 |
| OSP group updates 02/03 | 4.2.2–4.2.3 | 60–61 / 41–42 |
| OSP answer request 05 | 4.2.4 | 62 / 43 |
| OSP individual grant/update 04/06 | 4.2.5, 4.2.8 | 64 / 45; 70 / 51 |
| ISP ACK 20 | 6.1.1 | 94 / 75 |
| ISP alert 1f | 6.1.4 | 99 / 80 |
| ISP cancel 23 | 6.1.5 | 101 / 82 |
| ISP emergency 27 | 6.1.6 | 103 / 84 |
| ISP affiliation 28 | 6.1.9 | 108 / 89 |
| ISP message 1c | 6.1.11 | 111 / 92 |
| ISP status query 1a | 6.1.13 | 114 / 95 |
| ISP status response/update 19/18 | 6.1.14–6.1.15 | 116 / 97; 118 / 99 |
| ISP registration/deregistration 2c/2b | 6.1.16–6.1.17 | 120–121 / 101–102 |
| ISP location 2d | 6.1.18 | 122 / 103 |
| ISP authentication response 38 | 6.1.23 | 136 / 117 |
| OSP ACK 20 | 6.2.1 | 150–151 / 131–132 |
| OSP alert 1f | 6.2.4 | 157 / 138 |
| OSP deny 27 | 6.2.5 | 160 / 141 |
| OSP affiliation 28 | 6.2.8 | 165 / 146 |
| OSP general FDMA plan 3d | 6.2.9 | 168 / 149 |
| OSP message 1c | 6.2.10 | 170 / 151 |
| OSP network 3b | 6.2.11 | 173 / 154 |
| OSP queued 21 | 6.2.14 | 178 / 159 |
| OSP RFSS 3a | 6.2.15 | 180 / 161 |
| OSP status query/update 1a/18 | 6.2.17–6.2.18 | 183 / 164; 186 / 167 |
| OSP registration command/response 2d/2c | 6.2.20–6.2.21 | 192–193 / 173–174 |
| OSP location response 2b | 6.2.23 | 196 / 177 |
| OSP VHF/UHF FDMA plan 34 | 6.2.29 | 209–210 / 190–191 |
| AMBT authentication demand 31 | 6.2.30 | 211–213 / 192–194 |
| OSP authentication response 32 | 6.2.31 | 214 / 195 |

These fields are owned by `src/messages.c`. `src/blocks.c` owns checksums;
`src/control.c` owns transport and stream state. The caller owns learned plans.

## Authentication calculations

| Behavior | Document and clause | PDF / printed pages |
| --- | --- | --- |
| AM1/AM3 zero expansion, complement and AES key derivation | AACE 6.1, 6.3 | 40–41 / 32–33; 43–44 / 35–36 |
| AM2/AM4 challenge expansion and response truncation | AACE 6.2, 6.4 | 42 / 34; 45 / 37 |
| Numerical AM1–AM4 examples | AACE 6.6 | 47 / 39 |
| GF byte arithmetic | AES 4 | 14–18 / 10–14 |
| Substitution inverse and affine map | AES 5.1.1 | 19–20 / 15–16 |
| ShiftRows and MixColumns | AES 5.1.2–5.1.3 | 21–22 / 17–18 |
| AddRoundKey and key expansion | AES 5.1.4–5.2 | 22–24 / 18–20 |

`src/auth.c` implements forward AES-128 for these calculations. The application
owns provisioning, challenge admission and session lifetime.

## Implementation and test conventions

Binary redundancy uses polynomial division. RS and BCH decoding solve syndrome
recurrences and error magnitudes by finite-field elimination. Golay correction
masks are generated by enumerating errors of weight at most three; the Python
generator is authoritative and `--check` verifies its checked-in result.
Trellis decoding uses hard-bit Viterbi with initial/final state zero, rejects
equal-cost paths and applies the caller's distance limit. These decoder choices
are implementation policies rather than prescribed decoding algorithms.

Tests check basis relationships, polynomial roots, the voice bit arrangement,
published numerical authentication values, complete units, boundary sizes,
error correction, rejected CRCs, polarity and receiver recovery. Synthetic
inputs keep test data local to this source tree. The Open25 binding is limited
to the existing conventional C caller and its ABI; it delegates coding,
framing and receive state to this library.

## Qualified receiver observations

The existing Open25-DM32 Next caller requires status observations from the
voice stream without a second framing state machine. The receiver publishes
normalized status at the existing BAAA 7.4/Table16 cadence (PDF57 / printed43,
BAAA document/path above) only after NID and NAC admission. A new sync or
explicit gap invalidates that observation. The caller owns its age and RF
admission; no status value grants transmission. Existing fields keep their
offsets; the status tail extends receiver storage by eight bytes.

The public RS helper also admits the pre-existing Next shortening domain
(`n<=63`, parity8/12/16). It uses the same GF(64), roots and mathematical
shortening already recorded above; fixed wire layouts remain unchanged.
