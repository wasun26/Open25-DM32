/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Open25 contributors
 * Original parameter-mapper code; imported numeric facts are identified in
 * quant-provenance.md and quant_tables.inc. */
#ifndef OPEN25_IMBE_QUANT_H
#define OPEN25_IMBE_QUANT_H
#include "imbe_model.h"
/* Functions returning int use 1 for success, 0 for rejection. Quantization
 * updates the model to decoder-reconstructed amplitudes and advances the
 * predictor exactly once. Invalid frames leave predictor/model unchanged.
 * imbe_voicing_group returns a zero-based group for a zero-based harmonic. */
#endif
