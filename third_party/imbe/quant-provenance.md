# Numerical provenance and implementation choices

The implementation follows the full-rate mathematical model in TIA-102.BABA-A-2014.
MIT covers the project code; published numerical inputs and formula-generated
quantities are identified separately below.

Primary reference: [TIA-102.BABA-A, Project 25 Vocoder Description](https://connect.tiaonline.org/viewdocument/tia-102baba-a-vocoder-descriptio).

## Published numerical inputs

| Runtime data | Reference | Representation |
| --- | --- | --- |
| `encoder.c`: `pitch_fir` | Annex D, printed page 83 | Eleven coefficients store the symmetric half of the 21-tap FIR, rounded to Q15. |
| `quant_tables.inc`: `gain_q11` | Annex E, page 84 | All 64 gain reconstruction levels, rounded to Q11. |
| `quant_tables.inc`: `gain_alloc_nibbles` | Annex F, pages 85–88 | Five gain-coefficient allocations for each L=9…56, packed into 120 bytes. |
| `quant.c`: gain step representation | Annex F, pages 85–88; Table 3, page 28 | All 240 published steps equal five coefficient scales times the ten decimal step factors; this checked identity compresses the dataset. |
| `quant_tables.inc`: `hoc_alloc_nibbles` | Annex G, pages 89–101 | All 1,272 higher-order DCT allocations, packed into 636 bytes. |
| `quant.c`: `step_centi`, `hoc_sigma_milli` | Tables 3–4, page 28 | Decimal factors and coefficient standard deviations, represented as integers. |

These imported functional datasets are distinguished from the project's C
expressions. Uniform steps retain the published decimal precision in units of
1e-5: encoding uses `floor(Q11_value*3125/(64*step_numerator))`; midpoint
reconstruction uses `round(step_numerator*(2*code-2^B+1)*32/3125)`.

## Formula-generated quantities

- Pitch, frequency, harmonic count and voicing groups follow equations 45–51.
  Pitch codes 0…207 are admitted; 208…255 remain reserved. No pitch table is
  imported. Frequency is cycles/sample Q32 and logs are raw-amplitude log2 Q11.
- The six residual block lengths are the unique nondecreasing integer partition
  defined by equations 58–59/65–66. The formula was checked against Annex J,
  page 105; no duplicate block-length lookup array is maintained.
- `encoder.c` uses mathematically generated Kaiser windows: 301 samples with
  beta 5.25 for coarse pitch and 221 samples with beta 6 for spectral analysis.
  The coarse window is stored squared. Its sums, fourth-power ratio and the
  refinement-window response are generated from those same definitions.
- `fft.c` uses quarter-wave values `round(32768*sin(pi*k/128))`, k=0…64. FFT,
  real-transform merging and inverse-transform scaling are mathematical
  expressions rather than imported reference-vocoder kernels.
- DCT cosine phases are generated from the DCT definition. `math.c` stores
  `round(2048*log2(1+i/64))` and `round(16384*2^(i/64))`, i=0…64.
- The synthesis window is represented by the linear ramps and plateau of
  Annex I. The unvoiced normalization scalar is derived from equation 121,
  the generated analysis window and that synthesis window.
- Noise recurrence and initialization use equations 117/141 and Annex A.
  Each decoder owns its sequence, overlap history and oscillator phases.

The analyzer includes documented estimator choices, including quantized-pitch
refinement and continuity decisions. These can change valid encoded bits while
preserving the parameter meanings. Clean-frame synthesis retains its own
fixed-point approximations and amplitude limits. Quantizer predictor history
stores unenhanced reconstructed parameters; synthesis enhancement does not
become a second predictor owner.

## Explicit interpretation of inconsistent examples

- Equation 69 prints a residual-block length inside the gain inverse DCT. The
  implementation uses the six-point mathematical inverse of equation 61.
- In the conditional L=16 layout, Table 10 repeats b16 bit 1, already assigned
  in Table 9. Section 7.1's exhaustive descending bit-plane traversal instead
  places b16 bit 0 at u7 bit 5. Every information bit is retained exactly once.
- Section 10's example pitch of 35.125 samples produces code 31 and L=15 under
  equations 45/47, although the example text states L=16. Tests use the tables
  as a conditional L=16 layout with a valid L=16 pitch code, rather than changing
  the exact pitch-to-L mapping to fit the inconsistent text.
- Annex F's L=23 transformed gain coefficient G5 step is 0.058000. This value
  is retained even where a separate implementation's corresponding value differs.

## Test and dependency boundary

The standalone host tests contain synthesized signals, mathematical oracles and
functional bit-layout facts. Optional external interoperability tests can use
Pavel Yazev's GPL-3.0-or-later codec and ISC mbelib under their own terms. Those external implementations are used only for host-side comparisons; the
standalone tests build the project C source directly.
