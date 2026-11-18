/* SPDX-License-Identifier: MIT
 * Freestanding memory runtime. Build with -fno-builtin and
 * -fno-tree-loop-distribute-patterns to prevent compiler libc recursion.
 * GCC/Clang may_alias permits aligned word access to arbitrary object storage.
 * Unaligned or differently aligned source/destination pairs use byte access.
 */
#include <stddef.h>
#include <stdint.h>
typedef uint32_t memory_word __attribute__((__may_alias__));

static inline void copy_forward(unsigned char *out, const unsigned char *in, size_t count)
{
    if ((((uintptr_t)out ^ (uintptr_t)in) & 3u) == 0u) {
        while (count != 0u && ((uintptr_t)out & 3u) != 0u) {
            *out++ = *in++;
            --count;
        }
        if (count >= 4u) {
        memory_word *words_out = (memory_word *)out;
        const memory_word *words_in = (const memory_word *)in;
        while (count >= 16u) {
            memory_word a = words_in[0], b = words_in[1];
            memory_word c = words_in[2], d = words_in[3];
            words_out[0] = a; words_out[1] = b;
            words_out[2] = c; words_out[3] = d;
            words_in += 4; words_out += 4; count -= 16u;
        }
        while (count >= 4u) {
            *words_out++ = *words_in++;
            count -= 4u;
        }
        out = (unsigned char *)words_out;
        in = (const unsigned char *)words_in;
        }
    }
    while (count != 0u) { *out++ = *in++; --count; }
}

void *memcpy(void *restrict destination, const void *restrict source, size_t count)
{
    copy_forward(destination, source, count);
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    if (out == in || count == 0u) return destination;
    if ((uintptr_t)out < (uintptr_t)in) {
        copy_forward(out, in, count);
    } else {
        out += count; in += count;
        if ((((uintptr_t)out ^ (uintptr_t)in) & 3u) == 0u) {
            while (count != 0u && ((uintptr_t)out & 3u) != 0u) {
                *--out = *--in;
                --count;
            }
            if (count >= 4u) {
            memory_word *words_out = (memory_word *)out;
            const memory_word *words_in = (const memory_word *)in;
            while (count >= 16u) {
                memory_word a = words_in[-1], b = words_in[-2];
                memory_word c = words_in[-3], d = words_in[-4];
                words_out[-1] = a; words_out[-2] = b;
                words_out[-3] = c; words_out[-4] = d;
                words_in -= 4; words_out -= 4; count -= 16u;
            }
            while (count >= 4u) { *--words_out = *--words_in; count -= 4u; }
            out = (unsigned char *)words_out;
            in = (const unsigned char *)words_in;
            }
        }
        while (count != 0u) { *--out = *--in; --count; }
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *out = destination;
    const unsigned char byte = (unsigned char)value;
    while (count != 0u && ((uintptr_t)out & 3u) != 0u) {
        *out++ = byte; --count;
    }
    if (count >= 4u) {
    memory_word *words = (memory_word *)out;
    const memory_word word = (memory_word)byte * UINT32_C(0x01010101);
    while (count >= 16u) {
        words[0] = word; words[1] = word; words[2] = word; words[3] = word;
        words += 4; count -= 16u;
    }
    while (count >= 4u) { *words++ = word; count -= 4u; }
    out = (unsigned char *)words;
    }
    while (count != 0u) { *out++ = byte; --count; }
    return destination;
}
