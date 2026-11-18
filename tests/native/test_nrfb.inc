// SPDX-License-Identifier: MIT
// Portions Copyright (c) 2026 The RadioFoundry Authors.
// Lock/detection cases selected from RadioFoundry 9e1f7209; board cases below
// exercise the generated Open25 board helpers against that same fake RF bus.
// The lock poll (0x0C bit 9) and set-up B's two DMR word sets: the newer DM-32UV RF section's differs from the DM-1801
// Pro's only in 0x47 / 0x48 (hardware-dm32.md 8.10)
static void test_setup_b(void)
{
	fd6818_t dev;
	reset_chip();
	fd6818_attach_variant(&dev, bus(), &fd6818_variant_b_dmr6943);
	chip.regs[0x0C] = 0x0280;
	CHECK(fd6818_pll_locked(&dev, 0));
	chip.regs[0x0C] = 0x0080;
	CHECK(!fd6818_pll_locked(&dev, 3));
	chip.regs[0x0C] = 0x0200;
	CHECK(fd6818_pll_locked(&dev, 3));

	fd6818_set_mode(&dev, FD6818_MODE_DMR);
	CHECK(chip.regs[0x47] == 0x6943 && chip.regs[0x48] == 0xB2FD);
	fd6818_attach_variant(&dev, bus(), &fd6818_variant_b);
	fd6818_set_mode(&dev, FD6818_MODE_DMR);
	CHECK(chip.regs[0x47] == 0x0140 && chip.regs[0x48] == 0xB2F5);
	const fd6818_variant_t *a = &fd6818_variant_b, *b = &fd6818_variant_b_dmr6943;
	CHECK(a->init == b->init && a->fm == b->fm && a->rx == b->rx && a->tx == b->tx && a->burst_rx == b->burst_rx &&
		a->burst_tx == b->burst_tx && a->bw_dmr == b->bw_dmr && a->dmr_n > 0 && b->dmr_n > 0 && a->dmr != b->dmr);
	CHECK(chip.protocol_errors == 0);
}

// fd6818_detect against a section that locks on some set-ups only, after some reads of 0x0C
static fd6818_t det_dev;
static const fd6818_variant_t *det_locks[2];	// the set-ups that lock this section
static const fd6818_variant_t *det_last;
static unsigned det_reads, det_slow;		// reads since the set-up changed; reads before det_locks[0] locks
static const uint16_t *det_seq;			// set: 0x0C reads in turn (a bit that flickers)

static uint16_t det_css(void)
{
	if (det_seq) {
		return *det_seq++;
	}
	if (det_dev.variant != det_last) {
		det_last = det_dev.variant;
		det_reads = 0;
	}
	det_reads++;
	bool ok = (det_dev.variant == det_locks[0] && det_reads > det_slow) || det_dev.variant == det_locks[1];
	return ok ? FD6818_CSS_PLL_LOCK : 0;
}

static uint8_t detect(const fd6818_variant_t *l0, const fd6818_variant_t *l1, unsigned slow)
{
	static const fd6818_variant_t *const v[] = { &fd6818_variant_default, &fd6818_variant_b_dmr6943 };
	reset_chip();
	chip.css = det_css;
	det_locks[0] = l0;
	det_locks[1] = l1;
	det_last = NULL;
	det_slow = slow;
	uint8_t i = fd6818_detect(&det_dev, bus(), v, 2, 145000000u, 20);
	CHECK(chip.protocol_errors == 0);
	return i;
}

static void test_detect(void)
{
	const fd6818_variant_t *a = &fd6818_variant_default, *b = &fd6818_variant_b_dmr6943;
	// Both lock the classic section, so the default is taken; only B locks the newer one; neither: none, left on A
	CHECK(detect(a, b, 0) == 0 && det_dev.variant == a);
	CHECK(detect(b, NULL, 0) == 1 && det_dev.variant == b);
	CHECK(detect(NULL, NULL, 0) == 2 && det_dev.variant == a && chip.regs[FD6818_REG_STATE] != FD6818_STATE_TX);
	// The default locks only after the first wait: B locks too, so A gets a longer try and is taken
	CHECK(detect(a, b, 25) == 0 && det_dev.variant == a);
	// Slower than the longer try as well: taken as B
	CHECK(detect(a, b, 70) == 1 && det_dev.variant == b);
	// A lock bit set once while settling is no lock
	static const uint16_t flicker[] = { FD6818_CSS_PLL_LOCK, 0, 0, 0, 0 };
	reset_chip();
	chip.css = det_css;
	det_seq = flicker;
	fd6818_attach_variant(&det_dev, bus(), &fd6818_variant_default);
	CHECK(!fd6818_pll_locked(&det_dev, 2));
	static const uint16_t settle[] = { FD6818_CSS_PLL_LOCK, 0, FD6818_CSS_PLL_LOCK, FD6818_CSS_PLL_LOCK,
					   FD6818_CSS_PLL_LOCK };
	det_seq = settle;
	CHECK(fd6818_pll_locked(&det_dev, 2));
	det_seq = NULL;
	chip.css = NULL;
}


static fd6818_t fd;
static rfbus_3w_t fd_bus;
static bool rf_b, rf_unlocked, dmr_keyed;
static uint32_t tx_hz, rx_hz;
static unsigned pa_on_calls, pa_off_calls, lna_calls;
static uint32_t modem;
#define CONFIG_FIRSTBOOT 0
#define hrc7000_wr(m,r,v) ((void)(r), *(m)=(v))
#define hrc7000_rd(m,r) ((void)(r), *(m))
#define C7_RF_MODE 0
static void pa_off(void) { pa_off_calls++; }
static void pa_key(void) { pa_on_calls++; }
static void lna(uint32_t hz, bool on) { (void)hz; (void)on; lna_calls++; }
static uint32_t synth_hz(uint32_t hz) { return hz; }
#include "board_nrfb.inc"

static uint16_t board_css(void)
{
    return fd.variant == det_locks[0] || fd.variant == det_locks[1] ? FD6818_CSS_PLL_LOCK : 0;
}

static void test_board_detect(void)
{
    const fd6818_variant_t *a = &fd6818_variant_default, *b = &fd6818_variant_b_dmr6943;
    for (unsigned variant=0; variant<3; variant++) {
        reset_chip();
        rfbus_3w_attach(&fd_bus, &pins);
        chip.css=board_css;
        det_locks[0]=variant==0 ? a : variant==1 ? b : NULL;
        det_locks[1]=variant==0 ? b : NULL;
        modem=0x12348000u;
        rf_detect();
        CHECK(rf_b == (variant==1) && rf_unlocked == (variant==2));
        CHECK(fd.variant == (variant==1 ? b : a));
        CHECK(fd.state == FD6818_STATE_IDLE);
        CHECK(modem == (variant==1 ? 0x12340000u : 0x12348000u));
        CHECK(chip.protocol_errors == 0);
    }
}

static void test_board_routes(void)
{
    const fd6818_variant_t *v[] = { &fd6818_variant_default, &fd6818_variant_b_dmr6943 };
    const fd6818_tone_t tones[] = { FD6818_TONE_NONE, FD6818_TONE_CTCSS, FD6818_TONE_DCS, FD6818_TONE_DCS_INV };
    for (unsigned variant=0; variant<2; variant++) {
        reset_chip();
        rfbus_3w_attach(&fd_bus, &pins);
        fd6818_attach_variant(&fd, &fd_bus.bus, v[variant]);
        rf_b=variant!=0;rf_unlocked=false;
        CHECK(fm_bw_reg(RADIO_BW_25K) == (rf_b ? 0x3028 : FD6818_BW_FM_25K));
        CHECK(fm_bw_reg(RADIO_BW_12K5) == (rf_b ? 0x3048 : FD6818_BW_FM_12K5));
        for (unsigned t=0; t<sizeof(tones)/sizeof(tones[0]); t++) {
            chip.n=0;chip.regs[0x2E]=0x1111;chip.regs[0x2F]=0x2222;
            fd_rx_subtone(tones[t], t==1 ? 885 : 23);
            CHECK(chip.regs[0x2E] == (rf_b ? t==1 ? 0x8608 : 0x9608 : 0x1111));
            CHECK(chip.regs[0x2F] == (rf_b ? t==1 ? 0x1890 : 0x9890 : 0x2222));
        }
        fd6818_set_mode(&fd, FD6818_MODE_DMR);
        CHECK(chip.regs[0x47] == (rf_b ? 0x6943 : 0x0140));
        tx_hz=146550000u;rx_hz=145550000u;
        pa_on_calls=pa_off_calls=lna_calls=0;
        chip.n=0;
        dmr_rf_switch(true);
        CHECK(fd.state == FD6818_STATE_TX && fd.freq_10hz == tx_hz/10);
        if (rf_b) CHECK(chip.regs[0x50] == 0x3B18);
        CHECK(lna_calls == 1);
        dmr_keyed=true;dmr_pa_on();
        CHECK(pa_on_calls == 1);
        dmr_rf_switch(false);
        CHECK(fd.state == FD6818_STATE_RX && fd.freq_10hz == rx_hz/10);
        CHECK(pa_off_calls == 1 && lna_calls == 2);
        // Losing every supported lock must inhibit both the burst and its PA.
        rf_unlocked=true;unsigned writes=fd_bus.bus.writes;
        dmr_rf_switch(true);dmr_pa_on();
        CHECK(fd_bus.bus.writes == writes && pa_on_calls == 1 && lna_calls == 2);
        dmr_rf_switch(false);
        CHECK(fd.state == FD6818_STATE_RX && pa_off_calls == 2);
        CHECK(chip.protocol_errors == 0);
    }
}

// The normal FM frontend can leave the tone modulator enabled before a cold
// P25/DMR entry. The DM32 setup must clear it even without the native FM helper.
static void test_setup_b_handoff(void)
{
    const fd6818_tone_t tones[] = { FD6818_TONE_NONE, FD6818_TONE_CTCSS, FD6818_TONE_DCS, FD6818_TONE_DCS_INV };
    fd6818_t dev;
    for (unsigned t=0; t<sizeof(tones)/sizeof(tones[0]); t++) {
        reset_chip();
        fd6818_attach_variant(&dev, bus(), &fd6818_variant_b_dmr6943);
        fd6818_set_mode(&dev, FD6818_MODE_FM);
        fd6818_set_rx_tone(&dev, tones[t], t==1 ? 885 : 23);
        CHECK((chip.regs[FD6818_REG_SUBAUDIO] != 0) == (t != 0));
        fd6818_set_mode(&dev, FD6818_MODE_DMR);
        CHECK(chip.regs[FD6818_REG_SUBAUDIO] == 0);
        CHECK(chip.regs[0x47] == 0x6943 && chip.regs[0x48] == 0xB2FD);
    }
}

static void test_setup_b_sleep(void)
{
    fd6818_t dev;
    reset_chip();
    fd6818_attach_variant(&dev, bus(), &fd6818_variant_b_dmr6943);
    fd6818_rx_sleep(&dev, true, FD6818_MODE_FM);
    CHECK(dev.state == FD6818_STATE_IDLE && chip.regs[0x37] == 0x9D17);
    fd6818_rx_sleep(&dev, false, FD6818_MODE_FM);
    CHECK(chip.regs[0x37] == 0x9D1F);
    fd6818_rx_sleep(&dev, true, FD6818_MODE_DMR);
    CHECK(chip.regs[0x37] == 0x9D00);
    fd6818_rx_sleep(&dev, false, FD6818_MODE_DMR);
    CHECK(chip.regs[0x37] == 0x9D1F);
    CHECK(chip.protocol_errors == 0);
}

static void test_nrfb(void)
{
    test_setup_b();
    test_detect();
    test_setup_b_sleep();
    test_setup_b_handoff();
    test_board_detect();
    test_board_routes();
}
