/* SPDX-License-Identifier: MIT
 * TIA-102.BAAA-B 4.6-4.9, 7.5. See SOURCES.md for source locations.
 * Decoder design: Peterson syndrome equations and finite-field elimination;
 * this is an implementation choice, not software supplied by TIA. */
#include "internal.h"
#include "golay_errors.inc"

static uint64_t binary_remainder(uint64_t value, uint64_t generator, unsigned degree)
{
    for (unsigned bit = 64; bit-- > degree;)
        if ((value >> bit) & 1) value ^= generator << (bit - degree);
    return value;
}

int p25cai_golay_encode(uint16_t data, unsigned bits, uint32_t *word)
{
    if (!word || (bits != 18 && bits != 23 && bits != 24) ||
        data >= (bits == 18 ? 64 : 4096)) return P25CAI_INVALID;
    uint32_t value = (uint32_t)data << 11;
    value |= (uint32_t)binary_remainder(value, 0xc75, 11);
    if (bits != 23) value = (value << 1) | (cai_weight(value) & 1);
    *word = value;
    return 0;
}

int p25cai_golay_decode(uint32_t word, unsigned bits, uint16_t *data)
{
    if (!data || (bits != 18 && bits != 23 && bits != 24) ||
        (word >> bits)) return P25CAI_INVALID;
    uint32_t base = bits == 23 ? word : word >> 1;
    uint32_t corrected = base ^ golay_errors[binary_remainder(base, 0xc75, 11)];
    uint16_t message = (uint16_t)(corrected >> 11);
    uint32_t encoded;
    if (p25cai_golay_encode(message, bits, &encoded)) return P25CAI_UNCORRECTABLE;
    unsigned distance = cai_weight(encoded ^ word);
    if (distance > 3) return P25CAI_UNCORRECTABLE;
    *data = message;
    return (int)distance;
}

static unsigned hamming_column(unsigned data_bit, unsigned bits)
{
    /* Table 4's shortened column selection is not the leading six columns
     * of the full code. These numbers specify functional parity equations. */
    static const uint8_t short_columns[6] = {12, 3, 7, 11, 13, 14};
    if (bits == 10) return short_columns[data_bit];
    unsigned found = 0;
    for (unsigned column = 1; column < 16; ++column)
        if ((column & (column - 1)) != 0 && found++ == data_bit) return column;
    return 0;
}

int p25cai_hamming_encode(uint16_t data, unsigned bits, uint16_t *word)
{
    if (!word || (bits != 10 && bits != 15) || (data >> (bits - 4)))
        return P25CAI_INVALID;
    unsigned parity = 0;
    for (unsigned bit = 0; bit < bits - 4; ++bit)
        if ((data >> bit) & 1) parity ^= hamming_column(bit, bits);
    *word = (uint16_t)((data << 4) | parity);
    return 0;
}

int p25cai_hamming_decode(uint16_t word, unsigned bits, uint16_t *data)
{
    uint16_t encoded;
    if (!data || (bits != 10 && bits != 15) || (word >> bits))
        return P25CAI_INVALID;
    int result = p25cai_hamming_encode(word >> 4, bits, &encoded);
    if (result < 0) return result;
    unsigned syndrome = word ^ encoded;
    if (!syndrome) { *data = word >> 4; return 0; }
    for (unsigned bit = 0; bit < bits; ++bit) {
        unsigned column = bit < 4 ? 1u << bit : hamming_column(bit - 4, bits);
        if (syndrome == column) {
            *data = (uint16_t)((word ^ (1u << bit)) >> 4);
            return 1;
        }
    }
    return P25CAI_UNCORRECTABLE;
}

int p25cai_lsd_encode(uint8_t data, uint16_t *word)
{
    if (!word) return P25CAI_INVALID;
    uint16_t value = (uint16_t)data << 8;
    *word = value | (uint16_t)binary_remainder(value, 0x139, 8);
    return 0;
}

int p25cai_lsd_decode(uint16_t word, uint8_t *data)
{
    if (!data) return P25CAI_INVALID;
    /* Exhaustive nearest-codeword search is bounded to 256 tiny words. */
    for (unsigned message = 0; message < 256; ++message) {
        uint16_t encoded;
        p25cai_lsd_encode((uint8_t)message, &encoded);
        unsigned distance = cai_weight(word ^ encoded);
        if (distance <= 2) { *data = (uint8_t)message; return (int)distance; }
    }
    return P25CAI_UNCORRECTABLE;
}

typedef struct { uint8_t powers[126], logarithms[64]; } field;

static void field_init(field *f)
{
    unsigned value = 1;
    memset(f, 0, sizeof *f);
    for (unsigned power = 0; power < 63; ++power) {
        f->powers[power] = f->powers[power + 63] = (uint8_t)value;
        f->logarithms[value] = (uint8_t)power;
        value <<= 1;
        if (value & 64) value ^= 0x43;
    }
}

static uint8_t product(const field *f, uint8_t a, uint8_t b)
{
    return a && b ? f->powers[f->logarithms[a] + f->logarithms[b]] : 0;
}

/* Reduce a square augmented system to identity; reject singular systems.
 * Callers bound dimension to 11, and entries to valid GF(64) elements. */
static int solve(const field *f, uint8_t matrix[11][12], unsigned dimension)
{
    for (unsigned column = 0; column < dimension; ++column) {
        unsigned pivot = column;
        while (pivot < dimension && !matrix[pivot][column]) ++pivot;
        if (pivot == dimension) return 0;
        if (pivot != column)
            for (unsigned j = column; j <= dimension; ++j) {
                uint8_t saved = matrix[pivot][j];
                matrix[pivot][j] = matrix[column][j];
                matrix[column][j] = saved;
            }
        uint8_t inverse = f->powers[63 - f->logarithms[matrix[column][column]]];
        for (unsigned j = column; j <= dimension; ++j)
            matrix[column][j] = product(f, matrix[column][j], inverse);
        for (unsigned row = 0; row < dimension; ++row) {
            if (row == column) continue;
            uint8_t scale = matrix[row][column];
            for (unsigned j = column; j <= dimension; ++j)
                matrix[row][j] ^= product(f, scale, matrix[column][j]);
        }
    }
    return 1;
}

static void syndromes(const field *f, const uint8_t *word, unsigned n,
                      unsigned count, uint8_t *out)
{
    for (unsigned root = 1; root <= count; ++root) {
        uint8_t value = 0;
        for (unsigned position = 0; position < n; ++position)
            value = product(f, value, f->powers[root]) ^ word[position];
        out[root - 1] = value;
    }
}

static int all_zero(const uint8_t *values, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) if (values[i]) return 0;
    return 1;
}

/* Shared mathematical solver for symbol errors and binary BCH errors.
 * Try the possible error count, solve the syndrome recurrence, locate roots,
 * solve their magnitudes, then require all check equations to hold. */
static int correct(const uint8_t *word, unsigned n, unsigned checks,
                    int binary, uint8_t *out)
{
    field f;
    uint8_t s[22];
    field_init(&f);
    syndromes(&f, word, n, checks, s);
    if (all_zero(s, checks)) { memcpy(out, word, n); return 0; }
    for (unsigned errors = 1; errors <= checks / 2; ++errors) {
        uint8_t equations[11][12] = {{0}}, lambda[12] = {1};
        for (unsigned r = 0; r < errors; ++r) {
            for (unsigned c = 0; c < errors; ++c)
                equations[r][c] = s[errors + r - c - 1];
            equations[r][errors] = s[errors + r];
        }
        if (!solve(&f, equations, errors)) continue;
        for (unsigned i = 0; i < errors; ++i) lambda[i + 1] = equations[i][errors];
        int consistent = 1;
        for (unsigned k = errors; k < checks; ++k) {
            uint8_t value = s[k];
            for (unsigned i = 1; i <= errors; ++i)
                value ^= product(&f, lambda[i], s[k - i]);
            if (value) { consistent = 0; break; }
        }
        if (!consistent) continue;
        uint8_t positions[11], locations[11];
        unsigned found = 0;
        for (unsigned pos = 0; pos < n; ++pos) {
            unsigned exponent = n - 1 - pos;
            uint8_t inverse = f.powers[(63 - exponent) % 63];
            uint8_t value = lambda[errors];
            for (unsigned i = errors; i > 0; --i)
                value = product(&f, value, inverse) ^ lambda[i - 1];
            if (!value) {
                if (found == errors) { ++found; break; }
                positions[found] = (uint8_t)pos;
                locations[found++] = f.powers[exponent];
            }
        }
        if (found != errors) continue;
        memset(equations, 0, sizeof equations);
        for (unsigned c = 0; c < errors; ++c) {
            uint8_t power = locations[c];
            for (unsigned r = 0; r < errors; ++r) {
                equations[r][c] = power;
                power = product(&f, power, locations[c]);
            }
        }
        for (unsigned r = 0; r < errors; ++r) equations[r][errors] = s[r];
        if (!solve(&f, equations, errors)) continue;
        uint8_t candidate[63];
        memcpy(candidate, word, n);
        for (unsigned i = 0; i < errors; ++i) {
            uint8_t magnitude = equations[i][errors];
            if (!magnitude || (binary && magnitude != 1)) { consistent = 0; break; }
            candidate[positions[i]] ^= magnitude;
        }
        if (!consistent) continue;
        uint8_t check[22];
        syndromes(&f, candidate, n, checks, check);
        if (!all_zero(check, checks)) continue;
        memcpy(out, candidate, n);
        return (int)errors;
    }
    return P25CAI_UNCORRECTABLE;
}

static int rs_parameters(unsigned n, unsigned k)
{
    return n <= 63 && k > 0 && n > k &&
        (n-k == 8 || n-k == 12 || n-k == 16);
}

int p25cai_rs_encode(const uint8_t *data, unsigned n, unsigned k, uint8_t *word)
{
    if (!data || !word || !rs_parameters(n, k)) return P25CAI_INVALID;
    for (unsigned i = 0; i < k; ++i) if (data[i] >= 64) return P25CAI_INVALID;
    field f;
    field_init(&f);
    unsigned parity = n - k;
    /* Coefficients in ascending powers; multiply by each specified root. */
    uint8_t generator[17] = {1};
    for (unsigned degree = 0; degree < parity; ++degree) {
        generator[degree + 1] = generator[degree];
        for (unsigned i = degree; i > 0; --i)
            generator[i] = generator[i - 1] ^ product(&f, generator[i], f.powers[degree + 1]);
        generator[0] = product(&f, generator[0], f.powers[degree + 1]);
    }
    uint8_t encoded[63] = {0}, division[63] = {0};
    memcpy(encoded, data, k);
    memcpy(division, data, k);
    for (unsigned start = 0; start < k; ++start) {
        uint8_t scale = division[start];
        for (unsigned j = 0; j <= parity; ++j)
            division[start + j] ^= product(&f, scale, generator[parity - j]);
    }
    memcpy(encoded + k, division + k, parity);
    memcpy(word, encoded, n);
    return 0;
}

int p25cai_rs_decode(uint8_t *word, unsigned n, unsigned k)
{
    if (!word || !rs_parameters(n, k)) return P25CAI_INVALID;
    for (unsigned i = 0; i < n; ++i) if (word[i] >= 64) return P25CAI_INVALID;
    uint8_t decoded[63];
    int result = correct(word, n, n - k, 0, decoded);
    if (result >= 0) memcpy(word, decoded, n);
    return result;
}

static int duid_valid(uint8_t duid)
{
    return duid == 0 || duid == 3 || duid == 5 || duid == 7 || duid == 10 || duid == 12 || duid == 15;
}

int p25cai_nid_encode(uint16_t nac, uint8_t duid, uint64_t *word)
{
    if (!word || nac > 0xfff || duid > 15) return P25CAI_INVALID;
    uint64_t information = ((uint64_t)nac << 4) | duid;
    uint64_t value = information << 47;
    value |= binary_remainder(value, UINT64_C(06331141367235453), 47);
    *word = (value << 1) | ((duid ^ (duid >> 1)) & 1);
    return 0;
}

int p25cai_nid_decode(uint64_t word, uint16_t *nac, uint8_t *duid)
{
    if (!nac || !duid) return P25CAI_INVALID;
    uint8_t binary[63], decoded[63];
    for (unsigned i = 0; i < 63; ++i) binary[i] = (uint8_t)((word >> (63 - i)) & 1);
    int result = correct(binary, 63, 22, 1, decoded);
    if (result < 0) return result;
    uint16_t information = 0;
    for (unsigned i = 0; i < 16; ++i) information = (uint16_t)((information << 1) | decoded[i]);
    uint8_t type = information & 15;
    if (!duid_valid(type)) return P25CAI_UNSUPPORTED;
    uint64_t encoded;
    p25cai_nid_encode(information >> 4, type, &encoded);
    unsigned distance = cai_weight(word ^ encoded);
    if (distance > 11) return P25CAI_UNCORRECTABLE;
    *nac = information >> 4;
    *duid = type;
    return (int)distance;
}
