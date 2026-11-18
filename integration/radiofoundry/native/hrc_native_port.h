/* SPDX-License-Identifier: MIT */
#ifndef HRC_NATIVE_PORT_H
#define HRC_NATIVE_PORT_H
#include "hrc_native_chip.h"
/* Derivative-only reservation: APRS is disabled, leaving timer 4 available.
 * Keep upstream's PCM refill on timer 6 and the board's PA delay on timer 3. */
enum { HN_GUARD_TIMER = 4 };
int hn_port_on(hn_chip *, nt_test *);
void hn_port_quiet(void);
void hn_port_restore(void);
#endif
