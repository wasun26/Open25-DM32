/* SPDX-License-Identifier: MIT */
/* Exercise the real VERSION handler and build feature owner, without radio I/O. */
#include <assert.h>
#include <stdio.h>
#include "debuglink/debuglink.c"

const board_t board = { .name = "DM-32UV" };
uint32_t hal_millis(void) { return 0; }

int main(void)
{
    uint8_t out[DL_REPLY_MAX + 1];
    size_t n = cmd_version(out, DL_REPLY_MAX);
    assert(n > 1 && n <= DL_REPLY_MAX && out[0] == DL_ST_OK);
    out[n] = '\0';
    const char *text = (const char *)out + 1;
    const char *features = strstr(text, "\nfeatures=");
    assert(features != NULL);
    features += strlen("\nfeatures=");
    const char *end = strchr(features, '\n');
    assert(end != NULL);
    unsigned p25 = 0;
    for (const char *p = features; p < end;) {
        const char *next = memchr(p, ',', (size_t)(end - p));
        if (next == NULL) next = end;
        if (next - p == 3 && memcmp(p, "P25", 3) == 0) p25++;
        p = next + 1;
    }
    assert(p25 == (CONFIG_P25_CHANNELS ? 1u : 0u));
    assert((strstr(text, "\ncodeplug=rfkv1\n") != NULL) == !!CONFIG_P25_CHANNELS);
    assert((strstr(text, "\ncodeplug_chain=1\n") != NULL) == !!CONFIG_P25_CHANNELS);
    assert(strstr(text, "P25_TRUNK") == NULL);
    assert(fwrite(out + 1, 1, n - 1, stdout) == n - 1);
    return 0;
}
