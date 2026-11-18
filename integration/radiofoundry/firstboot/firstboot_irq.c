/* SPDX-License-Identifier: MIT */
#include "config.h"
#if CONFIG_FIRSTBOOT
#include "platform.h"
#include "firstboot/firstboot.h"
uint32_t firstboot_irq_save(void) { return hrc_irq_save(); }
void firstboot_irq_restore(uint32_t irq) { hrc_irq_restore(irq); }
#endif
