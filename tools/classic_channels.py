# SPDX-License-Identifier: MIT
# Portions Copyright (c) 2026 The RadioFoundry Authors; see LICENSES/RadioFoundry-MIT.txt.
"""Classic channel fields, UI identity and display defaults."""

from pathlib import Path


def prepare(transform):
    # Replace the original splash through the pinned source's existing entry point.
    screens=(Path(__file__).resolve().parents[1]/'research/upstream/radiofoundry/core/ui/ui_screens.c').read_text()
    boot=screens[screens.index('// --- boot splash ---'):screens.index('// --- Home ---')]
    transform('core/settings/settings.c', [
        ('s->brightness = 100;', 's->brightness = 100;\n\ts->theme = 1; // Classic defaults to dark text on white.'),
    ])
    transform('tests/host/fd6818Test.c', [
        ('static void test_rx_tones(void)', '''static void test_tone_to_dmr(void)
{
\tconst fd6818_tone_t tones[] = { FD6818_TONE_CTCSS, FD6818_TONE_DCS, FD6818_TONE_DCS_INV };
\tconst uint16_t values[] = { 1000, 023, 023 };
\tconst uint16_t enabled[] = { 0x9045, 0x8045, 0xA045 };
\tconst fd6818_variant_t *variants[] = { &fd6818_variant_default, &fd6818_variant_b_dmr6943 };
\tfor (unsigned v = 0; v < 2; v++) for (unsigned t = 0; t < sizeof tones / sizeof tones[0]; t++) {
\t\tfd6818_t dev;
\t\treset_chip();fd6818_attach_variant(&dev, bus(), variants[v]);
\t\tfd6818_set_mode(&dev, FD6818_MODE_FM);
\t\tfd6818_set_freq(&dev, 146520000u);
\t\tfd6818_set_rx_tone(&dev, tones[t], values[t]);
\t\tCHECK(chip.regs[FD6818_REG_SUBAUDIO] == enabled[t]);
\t\tuint16_t frequency = chip.regs[FD6818_REG_CTCSS_FREQ], code = chip.regs[FD6818_REG_DCS_CODE];
\t\tchip.regs[0x55] = 0xA55A;chip.regs[0x66] = 0x5AA5;
\t\tfd6818_set_mode(&dev, FD6818_MODE_DMR);
\t\tCHECK(chip.regs[FD6818_REG_SUBAUDIO] == 0);
\t\tCHECK(chip.regs[FD6818_REG_CTCSS_FREQ] == frequency && chip.regs[FD6818_REG_DCS_CODE] == code);
\t\tCHECK(chip.regs[0x55] == 0xA55A && chip.regs[0x66] == 0x5AA5 && dev.freq_10hz == 14652000u);
\t\tfor (unsigned burst = 0; burst < 3; burst++) {
\t\t\tfd6818_dmr_burst(&dev, true, FD6818_BW_DMR);
\t\t\tCHECK(dev.state == FD6818_STATE_TX && !chip.regs[FD6818_REG_SUBAUDIO]);
\t\t\tfd6818_dmr_burst(&dev, false, FD6818_BW_DMR);
\t\t\tCHECK(dev.state == FD6818_STATE_RX && !chip.regs[FD6818_REG_SUBAUDIO]);
\t\t}
\t\tfd6818_set_mode(&dev, FD6818_MODE_FM);
\t\tfd6818_set_rx_tone(&dev, tones[t], values[t]);
\t\tCHECK(chip.regs[FD6818_REG_SUBAUDIO] == enabled[t]);
\t\tCHECK(chip.regs[FD6818_REG_CTCSS_FREQ] == frequency && chip.regs[FD6818_REG_DCS_CODE] == code);
\t\tCHECK(!chip.protocol_errors && !chip.crit_depth);
\t}
}

static void test_rx_tones(void)'''),
        ('\ttest_rx_tones();', '\ttest_rx_tones();\n\ttest_tone_to_dmr();'),
    ])
    transform('drivers/modem/hrc7000/hrc7000.c', [
        ('\t\thrc7000_wr(d, C7_SUBVOICE_COEF, (d->subvoice & ~0xFF00u) | ((uint32_t)dev << 8));', '''\t\t// Subaudio and DTMF share gain: preserve the PL level and signed DCS polarity.
\t\tif (hrc7000_rd(d, C7_FM_MOD) & (FM_MOD_CTCSS | FM_MOD_CDCSS))
\t\t\tdev = (uint8_t)(d->subvoice >> 8);
\t\thrc7000_wr(d, C7_SUBVOICE_COEF, (d->subvoice & ~0xFF00u) | ((uint32_t)dev << 8));'''),
    ])
    transform('tests/host/hrc7000DmrTest.c', [
        ('static void test_subtone(void)', '''static void test_dtmf_with_subtone(void)
{
\tconst hrc7000_tone_t tones[] = { HRC7000_TONE_NONE, HRC7000_TONE_CTCSS,
\t\tHRC7000_TONE_DCS, HRC7000_TONE_DCS_INV };
\tfor (unsigned t = 0; t < sizeof tones / sizeof tones[0]; t++) {
\t\tfor (unsigned wide = 0; wide < 2; wide++) {
\t\t\tsetup();
\t\t\tdev.subaudio_dev = 0x28;
\t\t\tREG(C7_SUBVOICE_COEF) = 0x07D02830u;
\t\t\thrc7000_set_subtone(&dev, true, tones[t], t == 1 ? 1000 : 0023);
\t\t\thrc7000_fm_tx(&dev, wide != 0);
\t\t\tuint32_t saved = REG(C7_SUBVOICE_COEF), mod = REG(C7_FM_MOD);
\t\t\tuint8_t requested = hrc7000_dtmf_dev(146520000u, wide != 0);
\t\t\tuint32_t sounding = t ? saved : (saved & ~0xFF00u) | ((uint32_t)requested << 8);
\t\t\tCHECK(hrc7000_tx_dtmf(&dev, '5', requested));
\t\t\tCHECK(REG(C7_SUBVOICE_COEF) == sounding && REG(C7_FM_MOD) == (mod | 2u));
\t\t\tif (t >= 2) CHECK((REG(C7_SUBVOICE_COEF) & 0x8000u) == 0);
\t\t\tCHECK(hrc7000_tx_dtmf(&dev, '9', requested));
\t\t\tCHECK(REG(C7_SUBVOICE_COEF) == sounding);
\t\t\tCHECK(hrc7000_tx_dtmf(&dev, 0, 0));
\t\t\tCHECK(REG(C7_SUBVOICE_COEF) == saved && REG(C7_FM_MOD) == mod && !dev.tone_on);
\t\t}
\t}
}

static void test_subtone(void)'''),
        ('\ttest_tx_tone();\n\ttest_subtone();', '\ttest_tx_tone();\n\ttest_dtmf_with_subtone();\n\ttest_subtone();'),
    ])
    transform('core/codeplug/codeplug.h', [
        ('typedef struct {\n\tchar name[CODEPLUG_NAME_LEN + 1];\n\tuint32_t rx_hz;', '''#if CONFIG_P25_CHANNELS
enum { RADIO_MODE_P25 = 2 };
#endif
typedef struct {
\tchar name[CODEPLUG_NAME_LEN + 1];
\tuint32_t rx_hz;'''),
        ('\tuint16_t contact;\t// talk target contact index, 0 = none\n\tuint16_t listen_list;\t// RX group list index, 0 = none\n\tuint32_t dmr_id;\t// own DMR ID for this channel, 0 = the radio\'s', '''#if CONFIG_P25_CHANNELS
	uint16_t contact, listen_list;
	uint32_t dmr_id;
	uint16_t p25_tg, p25_nac;
	uint8_t p25_squelch;
	uint32_t p25_id;
	uint16_t p25_tx_nac_override; /* zero inherits RX; otherwise TX NAC + 1 */
#else
	uint16_t contact, listen_list;
	uint32_t dmr_id;
#endif'''),
        ('} channel_t;', '''} channel_t;

static inline bool codeplug_mode_p25(uint8_t mode)
{
#if CONFIG_P25_CHANNELS
	return mode == RADIO_MODE_P25;
#else
	(void)mode;
	return false;
#endif
}
#if CONFIG_P25_CHANNELS
static inline uint16_t codeplug_p25_tx_nac(const channel_t *ch)
{
    if (!ch->p25_tx_nac_override) return ch->p25_nac;
    return (uint16_t)(ch->p25_tx_nac_override - 1u);
}
static inline bool codeplug_p25_valid(const channel_t *ch)
{
	uint16_t tx_nac=codeplug_p25_tx_nac(ch);
	return ch->bw == RADIO_BW_12K5 && ch->p25_nac <= 0xFFFu &&
	       ch->p25_nac != 0xF7Fu && ch->p25_id <= 0xFFFFFEu &&
	       (!ch->p25_tx_nac_override || (tx_nac < 0x1000u && tx_nac != 0xF7Eu && tx_nac != 0xF7Fu)) &&
	       (ch->rx_only || (tx_nac != 0xF7Eu && ch->p25_tg && ch->p25_id));
}
#endif'''),
    ])
    transform('core/codeplug/codeplug.c', [
        ('\tmemset(out, 0, sizeof(*out));\n\tif (!freq(&r[CH_RX]', '\tmemset(out, 0, sizeof(*out));\n#if CONFIG_P25_CHANNELS\n\tif (r[CH_MODE] != 0 && r[CH_MODE] != 1) return false;\n#endif\n\tif (!freq(&r[CH_RX]'),
        ('\tstorage_lock();\n\tbool ok = materialize() && flash_read(channel_addr(number), r, sizeof(r));', '''#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(ch->mode) && !codeplug_p25_valid(ch)) return false;
#endif
\tstorage_lock();
\tbool ok = materialize() && flash_read(channel_addr(number), r, sizeof(r));'''),
    ])
    transform('core/ui/ui_menus.c', [
        ('static const int16_t v_backlight[] = { 5, 10, 30, 60, 120, 0 };',
         'static const int16_t v_led_style[] = { 0, 1, 2 };\nstatic const char *const n_led_style[] = { TRN("Default"), "Hytera", "Motorola" };\nstatic const int16_t v_spotlight[] = { 0, 1, 2 };\nstatic const char *const n_spotlight[] = { TRN("Channel"), "VFO", TRN("Both") };\nstatic const int16_t v_backlight[] = { 5, 10, 30, 60, 120, 0 };'),
        ('\tLIST(TRN("Up/Down on Home"), NULL, UI_SET_UPDOWN_HOME, v_updown, n_updown),',
         '\tLIST(TRN("Up/Down on Home"), NULL, UI_SET_UPDOWN_HOME, v_updown, n_updown),\n\tLIST(TRN("Spotlight"), "find search keypad vfo", UI_SET_SPOTLIGHT, v_spotlight, n_spotlight),'),
        ('\tTOGGLE(TRN("Hint row"), "help", UI_SET_HINTS),',
         '\tTOGGLE(TRN("Hint row"), "help", UI_SET_HINTS),\n\tTOGGLE(TRN("Standby LED"), "status blink indicator", UI_SET_STANDBY_LED),\n\tNUMBER(TRN("Standby interval"), "led blink gap", UI_SET_STANDBY_GAP_S, 1, 30, 1, " s"),\n\tNUMBER(TRN("Standby blink"), "led blink duration", UI_SET_STANDBY_ON_MS, 50, 1000, 50, " ms"),\n\tLIST(TRN("LED style"), "hytera motorola status", UI_SET_LED_STYLE, v_led_style, n_led_style),'),
        ('#if CONFIG_SFR_CWID\n\tLIST(TRN("SFR CW ID"), "single frequency repeater morse callsign identify", UI_SET_SFR_CWID, v_sfrcwid, n_sfrcwid),\n#endif\n};',
         '#if CONFIG_SFR_CWID\n\tLIST(TRN("SFR CW ID"), "single frequency repeater morse callsign identify", UI_SET_SFR_CWID, v_sfrcwid, n_sfrcwid),\n#endif\n\tTOGGLE(TRN("Auto squelch"), "ASQ automatic noise floor", UI_SET_AUTO_SQL),\n\tTOGGLE(TRN("FM Auto Clear"), "fm noise narrow filter", UI_SET_FM_AUTO_CLEAR),\n};'),
        ('\tTOGGLE(TRN("Battery saver"), NULL, UI_SET_BATTERY_SAVER),',
         '\tTOGGLE(TRN("Battery saver"), NULL, UI_SET_BATTERY_SAVER),\n\tTOGGLE(TRN("Auto power save"), "low battery eco", UI_SET_AUTO_POWER_SAVE),'),
        ('\tcase UI_SET_GPS:',
         '\tcase UI_SET_BATTERY_SAVER:\n\t\treturn ui_svc_setting_get(UI_SET_AUTO_POWER_SAVE) ? TR("Auto power save sets this") : NULL;\n\tcase UI_SET_GPS:'),
        ('\tif (s->id == UI_SET_MIC_GAIN && !edit && mic_agc_on())\n\t\tstrcpy(it->value, "AGC");',
         '\tif (s->id == UI_SET_MIC_GAIN && !edit && mic_agc_on())\n\t\tstrcpy(it->value, "AGC");\n\tif (s->id == UI_SET_BATTERY_SAVER && !edit && ui_svc_setting_get(UI_SET_AUTO_POWER_SAVE))\n\t\tstrcpy(it->value, ui_view()->auto_save_active ? "Auto: On" : "Auto: Off");'),
        ('static char t[400];', 'static char t[640];'),
        ('sb_str(&b, "RadioFoundry\\n");', 'sb_str(&b, "Open25-DM32 Classic\\n");\n\tsb_str(&b, "Issues:\\ngithub.com/tylerwatt12/\\nOpen25-DM32/issues\\n");'),
        ('sb_str(&b, "MIT, (c) 2026 The RadioFoundry Authors\\n");',
         'sb_str(&b, "MIT, (c) 2026 Open25-DM32 contributors\\n");\n\tsb_str(&b, "Based on RadioFoundry\\n");\n\tsb_str(&b, "MIT, (c) 2026 The RadioFoundry Authors\\n");'),
        ('ui_toast(i == UI_CP_NEW ? TR("New codeplug started") : TR("Old codeplug kept: edit again"));',
         'ui_toast(i == UI_CP_NEW ? TR("Creating codeplug") : TR("Old codeplug kept: edit again"));'),
        ('static void contacts_item(uint16_t i, ui_item_t *it)', 'static void __attribute__((unused)) contacts_item(uint16_t i, ui_item_t *it)'),
        ('CE_ZONES, CE_GO, CE_SHARE, CE_SAVE, CE_SAVE_NEW, CE_COUNT', '#if CONFIG_P25_CHANNELS\n       CE_P25_TG, CE_P25_NAC, CE_P25_TX_NAC, CE_P25_ID,\n#endif\n       CE_ZONES, CE_GO, CE_SHARE, CE_SAVE, CE_SAVE_NEW, CE_COUNT'),
        ('[CE_SCAN] = CE_FIELD(scan_skip), [CE_ECO] = CE_FIELD(no_eco),', '[CE_SCAN] = CE_FIELD(scan_skip), [CE_ECO] = CE_FIELD(no_eco),\n#if CONFIG_P25_CHANNELS\n\t[CE_P25_TG] = CE_FIELD(p25_tg), [CE_P25_NAC] = CE_FIELD(p25_nac), [CE_P25_TX_NAC] = CE_FIELD(p25_tx_nac_override), [CE_P25_ID] = CE_FIELD(p25_id),\n#endif'),
        ('\t\tif ((fm_only && dmr) || (dmr_only && !dmr) || (r == CE_MODE && !CONFIG_DMR))', '#if CONFIG_P25_CHANNELS\n\t\tbool p25 = codeplug_mode_p25(ced_ch.mode);\n\t\tbool p25_only = r >= CE_P25_TG && r <= CE_P25_ID;\n\t\tif ((p25_only && !p25) || (p25 && (fm_only || dmr_only || r == CE_ECO))) continue;\n#endif\n\t\tif ((fm_only && dmr) || (dmr_only && !dmr) || (r == CE_MODE && !CONFIG_DMR))'),
        ('TRN("Battery saver"), TRN("Zones"), TRN("Tune to this ch"),', 'TRN("Battery saver"),\n#if CONFIG_P25_CHANNELS\n\t\tTRN("Talkgroup"), "RX NAC", "TX NAC", TRN("P25 Radio ID"),\n#endif\n\t\tTRN("Zones"), TRN("Tune to this ch"),'),
        ('sb_str(&wb, ch->mode == RADIO_MODE_DMR ? "DMR" : radio_am_channel(ch) ? "AM" : "FM");', 'sb_str(&wb, codeplug_mode_p25(ch->mode) ? "P25" : ch->mode == RADIO_MODE_DMR ? "DMR" : radio_am_channel(ch) ? "AM" : "FM");'),
        ('sb_str(&b, ch->mode == RADIO_MODE_DMR ? "DMR" : "FM");', 'sb_str(&b, codeplug_mode_p25(ch->mode) ? "P25" : ch->mode == RADIO_MODE_DMR ? "DMR" : "FM");'),
        ('if (editing && (r == CE_RX || r == CE_TX || r == CE_ID)) {', 'if (editing && (r == CE_RX || r == CE_TX || r == CE_ID\n#if CONFIG_P25_CHANNELS\n\t\t|| r == CE_P25_TG || r == CE_P25_ID\n#endif\n\t\t)) {'),
        ('if (r != CE_ID && k == UI_ENTRY_MHZ)', 'if ((r == CE_RX || r == CE_TX) && k == UI_ENTRY_MHZ)'),
        ('\tcase CE_NUMBER:\n\t\tsb_u32(&b, ced_num, 1);', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_TG: sb_u32(&b, ch->p25_tg, 1); break;\n\tcase CE_P25_ID: sb_u32(&b, ch->p25_id, 1); break;\n\tcase CE_P25_NAC:\n\tcase CE_P25_TX_NAC: {\n\t\tunsigned value = r==CE_P25_NAC ? ch->p25_nac : codeplug_p25_tx_nac(ch), cursor = 3;\n#if CONFIG_CHANNEL_EDIT\n\t\tif (editing) { value = (unsigned)ced_val; cursor = ced_len; }\n#endif\n        if (r==CE_P25_TX_NAC && (editing ? cursor==3 : !ch->p25_tx_nac_override)) {\n            sb_str(&b,TR("Same as RX")); break;\n        }\n\t\tsb_chr(&b, \'$\');\n\t\tfor (unsigned digit = 0; digit < 3; digit++) {\n\t\t\tif (digit == cursor) sb_chr(&b, \'[\');\n\t\t\tsb_chr(&b, "0123456789ABCDEF"[(value >> (4 * (2 - digit))) & 15u]);\n\t\t\tif (digit == cursor) sb_chr(&b, \']\');\n\t\t}\n\t\tbreak;\n\t}\n#endif\n\tcase CE_NUMBER:\n\t\tsb_u32(&b, ced_num, 1);'),
        ('\tif (CONFIG_DMR && c->mode == RADIO_MODE_DMR && c->contact == 0)', '#if CONFIG_P25_CHANNELS\n\tif (codeplug_mode_p25(c->mode) && !codeplug_p25_valid(c))\n\t\treturn TR("Check P25 NAC, talkgroup and ID");\n#endif\n\tif (CONFIG_DMR && c->mode == RADIO_MODE_DMR && c->contact == 0)'),
        ('\tcase CE_CC:\n\t\tced_val = (ced_val + dir + 16) % 16;', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_NAC:\n\tcase CE_P25_TX_NAC: {\n        if (ced_len==3) ced_len=0;\n\t\tunsigned shift = 4u * (2u - ced_len);\n\t\tunsigned value = (((unsigned)ced_val >> shift) + (unsigned)dir) & 15u;\n\t\tced_val = (ced_val & ~(15u << shift)) | (value << shift);\n\t\tbreak;\n\t}\n#endif\n\tcase CE_CC:\n\t\tced_val = (ced_val + dir + 16) % 16;'),
        ('\tcase CE_ID:\n\t\tif (ced_len >= CE_DIGITS)', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_NAC:\n\tcase CE_P25_TX_NAC: {\n        if (ced_len==3) ced_len=0;\n\t\tunsigned shift = 4u * (2u - ced_len);\n\t\tced_val = (ced_val & ~(15u << shift)) | ((unsigned)(d - \'0\') << shift);\n\t\tced_len = (uint8_t)((ced_len + 1) % 3);\n\t\tbreak;\n\t}\n\tcase CE_P25_TG:\n\tcase CE_P25_ID:\n\t\tif (v > (ced_edit == CE_P25_TG ? 65535 : 16777214)) {\n\t\t\tui_toast(TR("Value too large")); break;\n\t\t}\n        /* fall through */\n        __attribute__((fallthrough));\n#endif\n\tcase CE_ID:\n\t\tif (ced_len >= CE_DIGITS)'),
        ('\tcase CE_ID:\n\t\tif (ced_len > 0)', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_TG: if (ced_len) c->p25_tg = (uint16_t)ced_val; break;\n\tcase CE_P25_ID: if (ced_len) c->p25_id = (uint32_t)ced_val; break;\n\tcase CE_P25_NAC: c->p25_nac = (uint16_t)ced_val; break;\n    case CE_P25_TX_NAC: c->p25_tx_nac_override=ced_len==3 ? 0 : (uint16_t)(ced_val+1); break;\n#endif\n\tcase CE_ID:\n\t\tif (ced_len > 0)'),
        ('static bool chinfo_key(const input_event_t *ev)\n{\n\tbool press = ev->gesture == GESTURE_PRESS || ev->gesture == GESTURE_LONG;\n\tif (ced_edit == CE_NONE) {\n\t\tif (ev->action == ACT_BACK && ev->gesture == GESTURE_PRESS && ced_num != 0 &&\n\t\t\t\t(ced_new || ced_changed(CE_COUNT))) {\n\t\t\tdiscard_contact = false;\n\t\t\tui_push(SCR_DISCARD, 0);\n\t\t\treturn true;\n\t\t}\n\t\treturn false;\n\t}\n\tint dir = ev->action == ACT_NAV_UP ? 1 : ev->action == ACT_NAV_DOWN ? -1 :\n\t\tev->action == ACT_ENC_STEP ? (ev->value > 0 ? 1 : -1) : 0;\n\tif (dir != 0 && ev->gesture != GESTURE_UP) {', 'static bool chinfo_key(const input_event_t *ev)\n{\n\tbool press = ev->gesture == GESTURE_PRESS || ev->gesture == GESTURE_LONG;\n\tif (ced_edit == CE_NONE) {\n\t\tif (ev->action == ACT_BACK && ev->gesture == GESTURE_PRESS && ced_num != 0 &&\n\t\t\t\t(ced_new || ced_changed(CE_COUNT))) {\n\t\t\tdiscard_contact = false;\n\t\t\tui_push(SCR_DISCARD, 0);\n\t\t\treturn true;\n\t\t}\n\t\treturn false;\n\t}\n\tint dir = ev->action == ACT_NAV_UP ? 1 : ev->action == ACT_NAV_DOWN ? -1 :\n\t\tev->action == ACT_ENC_STEP ? (ev->value > 0 ? 1 : -1) : 0;\n#if CONFIG_P25_CHANNELS\n    if ((ced_edit == CE_P25_NAC || ced_edit == CE_P25_TX_NAC) && dir && ev->gesture != GESTURE_UP && ev->action != ACT_ENC_STEP) {\n        int positions=ced_edit==CE_P25_TX_NAC ? 4 : 3;\n        ced_len = (uint8_t)((ced_len + dir + positions) % positions);\n        ui_dirty(); return true;\n    }\n#endif\n\tif (dir != 0 && ev->gesture != GESTURE_UP) {'),
        ('if (ced_edit != CE_RX && ced_edit != CE_TX && ced_edit != CE_ID)', 'if (ced_edit != CE_RX && ced_edit != CE_TX && ced_edit != CE_ID\n#if CONFIG_P25_CHANNELS\n\t\t\t&& ced_edit != CE_P25_TG && ced_edit != CE_P25_ID\n#endif\n\t\t\t)'),
        ('\tcase CE_MODE: c->mode = c->mode == RADIO_MODE_DMR ? RADIO_MODE_FM : RADIO_MODE_DMR; break;', '\tcase CE_MODE:\n#if CONFIG_P25_CHANNELS\n\t\tif (c->mode == RADIO_MODE_DMR) {\n\t\t\tc->mode = RADIO_MODE_P25; c->p25_nac = 0x293; c->p25_tx_nac_override=0;\n\t\t\tc->p25_tg = 1; c->p25_squelch=1; c->p25_id = 1; c->dmr_id = 1; c->bw = RADIO_BW_12K5;\n\t\t\tc->dmo = c->roam = c->vox = false;\n\t\t} else if (codeplug_mode_p25(c->mode)) {\n\t\t\tc->mode = RADIO_MODE_FM; c->contact = c->listen_list = c->dmr_id = 0;\n\t\t} else\n#endif\n\t\t\tc->mode = c->mode == RADIO_MODE_DMR ? RADIO_MODE_FM : RADIO_MODE_DMR;\n\t\tbreak;\n#if CONFIG_P25_CHANNELS\n\tcase CE_P25_NAC: ced_edit = r; ced_val = c->p25_nac; ced_len = 0; break;\n    case CE_P25_TX_NAC: ced_edit=r; ced_val=codeplug_p25_tx_nac(c); ced_len=c->p25_tx_nac_override ? 0 : 3; break;\n\tcase CE_P25_TG:\n\tcase CE_P25_ID:\n#endif'),
        ('ced_edit == CE_TX_TONE) && ced_len > 0)', 'ced_edit == CE_TX_TONE\n#if CONFIG_P25_CHANNELS\n             || ced_edit == CE_P25_TG || ced_edit == CE_P25_ID\n#endif\n             ) && ced_len > 0)'),
        # Editor-only defaults/confirmation; saved/imported programming is unchanged.
        ('static channel_t ced_orig;', 'static char ced_msg[64];\t// channel editor toast/confirmation text\nstatic channel_t ced_orig;'),
        ('static void ced_rx_band(channel_t *c)', '''static uint32_t ced_tx_hz(const channel_t *c)
{
    return c->dmo || !c->tx_hz ? c->rx_hz : c->tx_hz;
}

static bool ced_outside_ham(const channel_t *c)
{
    uint32_t hz = ced_tx_hz(c);
    return !((hz >= 144000000u && hz <= 148000000u) ||
             (hz >= 420000000u && hz <= 450000000u));
}

static void ced_rx_band(channel_t *c)'''),
        ('\tced_rx_band(&ced_ch);\n}', '\tced_rx_band(&ced_ch);\n\tif (ced_outside_ham(&ced_ch)) ced_ch.rx_only = true;\n}'),
        ('\t\tced_ch.roam = false;\n\t\tchar name', '\t\tced_ch.roam = false;\n\t\tif (ced_outside_ham(&ced_ch)) ced_ch.rx_only = true;\n\t\tchar name'),
        ('static void ced_save(bool as_new)\n{\n\tstatic char msg[32];',
         'static void ced_save(bool as_new)\n{\n\tif (as_new && ced_outside_ham(&ced_ch)) ced_ch.rx_only = true;'),
        ('sb_init(&b, msg, sizeof msg);\n\tsb_str(&b, as_new', 'sb_init(&b, ced_msg, sizeof ced_msg);\n\tsb_str(&b, as_new'),
        ('\tui_toast(msg);\n}\n\n// The tone typed', '\tui_toast(ced_msg);\n}\n\n// The tone typed'),
        ('static void ced_commit(void)\n{\n\tchannel_t *c = &ced_ch;',
         'static void ced_commit(void)\n{\n\tchannel_t *c = &ced_ch;\n\tuint32_t before = ced_tx_hz(c);'),
        ('\tced_edit = CE_NONE;\n}\n\nstatic bool chinfo_key',
         '\tif (ced_tx_hz(c) != before && ced_outside_ham(c)) c->rx_only = true;\n\tced_edit = CE_NONE;\n}\n\nstatic bool chinfo_key'),
        ('\tuint8_t r = ced_rows[i];\n\tswitch (r)', '\tuint8_t r = ced_rows[i];\n\tuint32_t before = ced_tx_hz(c);\n\tswitch (r)'),
        ('case CE_RX_ONLY: c->rx_only = !c->rx_only; break;', '''case CE_RX_ONLY:
        if (!c->rx_only) c->rx_only = true;
        else if (ced_outside_ham(c)) ui_push(SCR_CONFIRM, 1);
        else c->rx_only = false;
        break;'''),
        ('\tui_dirty();\n#else\n\t(void)i;\n\t(void)hold;\n\tchlist_tune',
         '\tif (ced_tx_hz(c) != before && ced_outside_ham(c)) c->rx_only = true;\n\tui_dirty();\n#else\n\t(void)i;\n\t(void)hold;\n\tchlist_tune'),
        ('static void confirm_item(uint16_t i, ui_item_t *it)\n{', '''static void confirm_item(uint16_t i, ui_item_t *it)
{
#if CONFIG_CHANNEL_EDIT
    if (ui_frame()->arg == 1) {
        str_copy(it->label, sizeof it->label, i == 0 ? TR("Keep RX-only") : TR("Enable TX"));
        if (i == 1) it->flags |= UI_ITEM_WARN;
        return;
    }
#endif'''),
        ('static void confirm_select(uint16_t i, bool hold)\n{\n\t(void)hold;', '''static void confirm_select(uint16_t i, bool hold)
{
    (void)hold;
#if CONFIG_CHANNEL_EDIT
    if (ui_frame()->arg == 1) {
        if (i == 1) ced_ch.rx_only = false;
        ui_pop();
        return;
    }
#endif'''),
        ('static const char *confirm_body(void)\n{', '''static const char *confirm_body(void)
{
#if CONFIG_CHANNEL_EDIT
    if (ui_frame()->arg == 1) {
        sbuf_t b;
        sb_init(&b, ced_msg, sizeof ced_msg);
        sb_str(&b, TR("Outside ham bands")); sb_str(&b, "\\nTX: ");
        sb_freq(&b, ced_tx_hz(&ced_ch)); sb_str(&b, " MHz\\n");
        sb_str(&b, TR("PTT may transmit."));
        return ced_msg;
    }
#endif'''),
        ('static const ui_scr_def_t confirm_screen = {', '''static const char *confirm_title(void)
{
#if CONFIG_CHANNEL_EDIT
    if (ui_frame()->arg == 1) return TR("Enable TX?");
#endif
    return TR("Factory Reset");
}

static const ui_scr_def_t confirm_screen = {'''),
        ('.prefix = TRN("Factory Reset"), .hint', '.title = confirm_title, .hint'),
    ])
    transform('core/ui/ui_screens.c', [
        (boot, r'''// --- boot splash ---

// Open25's wordmark and compact numeric mark use the existing display fonts.
void ui_draw_splash(const char *board_name, const char *version, const char *line1, const char *line2)
{
    const ui_layout_t *L=&ui_layout;
    int w=gfx_width(),h=gfx_height(),gap=h>=200?10:2;
    const char *name="Open25-DM32 Classic";
    const font_t *wf=L->large;
    if(gfx_text_width(wf,name)>w-8)wf=L->medium;
    if(gfx_text_width(wf,name)>w-8)wf=L->small;
    if(gfx_text_width(wf,name)>w-8)wf=L->tiny;
    const char *lines[2]={line1?line1:"",line2?line2:""};
    int n_lines=(lines[0][0]!=0)+(lines[1][0]!=0);
    const font_t *lf=L->medium,*ff=L->tiny;
    for(unsigned i=0;i<2;i++)
        if(gfx_text_width(lf,lines[i])>w-4)lf=L->small;
    for(unsigned i=0;i<2;i++)
        if(gfx_text_width(lf,lines[i])>w-4)lf=L->tiny;
    char foot[48];
    static const char *const seps[]={"  "," ",NULL};
    for(unsigned i=0;i<3;i++){
        sbuf_t b;sb_init(&b,foot,sizeof foot);
        if(seps[i]){sb_str(&b,board_name);sb_str(&b,seps[i]);}
        sb_str(&b,version);
        if(gfx_text_width(ff,foot)<=w)break;
    }
    bool tall=h>=160;
    int foot_y=h-ff->h,limit=foot_y-gap-(tall?ff->h+gap:0);
    int text_h=wf->h+(tall?gap+L->small->h:0)+(n_lines?gap+n_lines*lf->h:0);
    int mark_h=L->large->h+16;
    bool mark=tall && limit-text_h>=mark_h+gap;
    int y=(limit-text_h-(mark?mark_h+gap:0))/2;
    if(y<0)y=0;
    gfx_clear(ui_col.bg);
    if(mark){
        int mw=gfx_text_width(L->large,"25")+24;
        color_t blue=gfx_bpp()==1?ui_col.fg:RGB565(0,80,160);
        color_t ink=gfx_bpp()==1?ui_col.bg:RGB565(255,255,255);
        gfx_round_fill((w-mw)/2,y,mw,mark_h,blue);
        gfx_text(L->large,(w-gfx_text_width(L->large,"25"))/2,y+8,"25",ink);
        y+=mark_h+gap;
    }
    gfx_text(wf,(w-gfx_text_width(wf,name))/2,y,name,ui_col.fg);
    y+=wf->h;
    if(tall){
        const char *modes="FM / DMR / P25";
        y+=gap;
        gfx_text(L->small,(w-gfx_text_width(L->small,modes))/2,y,modes,ui_col.dim);
        y+=L->small->h;
    }
    if(n_lines){
        y+=gap;
        for(unsigned i=0;i<2;i++)if(lines[i][0]){
            gfx_text(lf,(w-gfx_text_width(lf,lines[i]))/2,y,lines[i],ui_col.fg);
            y+=lf->h;
        }
    }
    if(tall){
        const char *support="kd8zmm.com/open25-dm32";
        gfx_text(ff,(w-gfx_text_width(ff,support))/2,foot_y-gap-ff->h,support,ui_col.dim);
    }
    gfx_text(ff,(w-gfx_text_width(ff,foot))/2,foot_y,foot,ui_col.dim);
}

'''),
        ('static uint8_t ui_theme;', 'static uint8_t ui_theme=1;'),
        ('TR("Add codec with rfoundry")', 'TR("Use Open25 Flasher v3")'),

        ('return v->ch.mode == RADIO_MODE_DMR ? "DMR" : v->am ? "AM" : "FM";', 'return codeplug_mode_p25(v->ch.mode) ? "P25" : v->ch.mode == RADIO_MODE_DMR ? "DMR" : v->am ? "AM" : "FM";'),
        ('\t\t\tsb_str(&b, "SQ");\n\t\t\tsb_u32(&b, v->squelch, 1);',
         '\t\t\tsb_str(&b, v->auto_sql && v->squelch > 0 ? "ASQ" : "SQ");\n\t\t\tsb_u32(&b, v->squelch, 1);\n\t\t\tbool asq_shift = v->auto_sql && v->squelch > 0 && v->sql_eff > 0 && v->sql_eff != v->squelch;\n\t\t\tif (asq_shift) {\n\t\t\t\tsb_str(&b, v->sql_eff > v->squelch ? ">" : "<");\n\t\t\t\tsb_u32(&b, v->sql_eff, 1);\n\t\t\t}'),
        ('\t\t\tif (x + gfx_text_width(f, buf) <= w)\n\t\t\t\tgfx_text(f, x, y, buf, ui_col.fg);',
         '\t\t\tif (x + gfx_text_width(f, buf) <= w)\n\t\t\t\tgfx_text(f, x, y, buf, asq_shift ? ui_col.warn : ui_col.fg);'),
        ('\tsb_str(&b, "SQ");\n\tsb_u32(&b, v->squelch, 1);',
         '\tsb_str(&b, v->auto_sql && v->squelch > 0 ? "ASQ" : "SQ");\n\tsb_u32(&b, v->squelch, 1);\n\tbool asq_shift = v->auto_sql && v->squelch > 0 && v->sql_eff > 0 && v->sql_eff != v->squelch;\n\tif (asq_shift) {\n\t\tsb_str(&b, v->sql_eff > v->squelch ? ">" : "<");\n\t\tsb_u32(&b, v->sql_eff, 1);\n\t}'),
        ('\tif (x + gfx_text_width(f, buf) <= w)\n\t\tgfx_text(f, x, y, buf, ui_col.fg);',
         '\tif (x + gfx_text_width(f, buf) <= w)\n\t\tgfx_text(f, x, y, buf, asq_shift ? ui_col.warn : ui_col.fg);'),
        ('\tcase QT_SQUELCH:\n\t\tif (v->temp & RADIO_TEMP_SQUELCH)\n\t\t\tsb_str(&b, GLYPH_DOT);\n\t\tsb_u32(&b, v->squelch, 1);',
         '\tcase QT_SQUELCH:\n\t\tif (v->temp & RADIO_TEMP_SQUELCH)\n\t\t\tsb_str(&b, GLYPH_DOT);\n\t\tif (v->auto_sql && v->squelch > 0)\n\t\t\tsb_str(&b, "A");\n\t\tsb_u32(&b, v->squelch, 1);\n\t\tif (v->auto_sql && v->squelch > 0 && v->sql_eff > 0 && v->sql_eff != v->squelch) {\n\t\t\tsb_str(&b, v->sql_eff > v->squelch ? ">" : "<");\n\t\t\tsb_u32(&b, v->sql_eff, 1);\n\t\t}'),
        ('\tbool pw_temp = v->power != channel_power(v);\n\tif (pw_temp)\n\t\tx = gfx_text(f, x, y, GLYPH_DOT, ui_col.warn);\n\tx = gfx_text(f, x, y, ui_power_name(v->power), pw_temp ? ui_col.warn : ui_col.fg) + f->w;',
         '\tbool pw_temp = v->power != channel_power(v);\n\tbool pw_eco = v->auto_save_active && v->power_eff < v->power;\n\tif (pw_temp)\n\t\tx = gfx_text(f, x, y, GLYPH_DOT, ui_col.warn);\n\tsb_init(&b, buf, sizeof buf);\n\tsb_str(&b, ui_power_name(v->power));\n\tif (pw_eco) {\n\t\tsb_chr(&b, \'>\');\n\t\tsb_str(&b, ui_power_name(v->power_eff));\n\t}\n\tx = gfx_text(f, x, y, buf, (pw_temp || pw_eco) ? ui_col.warn : ui_col.fg) + f->w;'),
        ('\tbool pw_temp = v->power != channel_power(v);\n\tif (pw_temp)\n\t\tx = gfx_text(f, x, L->chips_y, GLYPH_DOT, ui_col.warn);\n\tx = gfx_text(f, x, L->chips_y, ui_power_name(v->power), pw_temp ? ui_col.warn : ui_col.fg) + f->w;',
         '\tbool pw_temp = v->power != channel_power(v);\n\tbool pw_eco = v->auto_save_active && v->power_eff < v->power;\n\tif (pw_temp)\n\t\tx = gfx_text(f, x, L->chips_y, GLYPH_DOT, ui_col.warn);\n\tsb_init(&b, buf, sizeof buf);\n\tsb_str(&b, ui_power_name(v->power));\n\tif (pw_eco) {\n\t\tsb_chr(&b, \'>\');\n\t\tsb_str(&b, ui_power_name(v->power_eff));\n\t}\n\tx = gfx_text(f, x, L->chips_y, buf, (pw_temp || pw_eco) ? ui_col.warn : ui_col.fg) + f->w;'),
        ('\tcase QT_POWER:\n\t\tif (v->power != channel_power(v))\n\t\t\tsb_str(&b, GLYPH_DOT);\n\t\tsb_str(&b, ui_power_name(v->power));\n\t\treturn true;',
         '\tcase QT_POWER:\n\t\tif (v->power != channel_power(v))\n\t\t\tsb_str(&b, GLYPH_DOT);\n\t\tsb_str(&b, ui_power_name(v->power));\n\t\tif (v->auto_save_active && v->power_eff < v->power) {\n\t\t\tsb_chr(&b, \'>\');\n\t\t\tsb_str(&b, ui_power_name(v->power_eff));\n\t\t}\n\t\treturn true;'),
    ])
    transform('core/ui/ui_svc.h', [
        ('\tUI_SET_LANGUAGE,\t// the UI\'s language, i18n.h lang_value: "en" English, 0 never picked (Settings > Display; settings_t.lang)\n\tUI_SET_COUNT',
         '\tUI_SET_LANGUAGE,\t// the UI\'s language, i18n.h lang_value: "en" English, 0 never picked (Settings > Display; settings_t.lang)\n\tUI_SET_STANDBY_LED, UI_SET_STANDBY_GAP_S, UI_SET_STANDBY_ON_MS, UI_SET_LED_STYLE, UI_SET_AUTO_SQL, UI_SET_SPOTLIGHT,\n\tUI_SET_AUTO_POWER_SAVE, UI_SET_FM_AUTO_CLEAR,\n\tUI_SET_COUNT'),
        ('} ui_event_kind_t;', '\tUI_EV_CODEPLUG_STARTED,\t// Start new and its held edit finished successfully\n} ui_event_kind_t;'),
    ])
    transform('core/ui/ui_app.c', [
        ('static bool has_quick_key, has_context_key;',
         'static bool has_quick_key, has_context_key;\nstatic uint8_t spotlight_mode;'),
        ('\tcase UI_EV_CODEPLUG_FOREIGN:', '\tcase UI_EV_CODEPLUG_STARTED:\n\t\tui_toast(TR("New codeplug started"));\n\t\tbreak;\n\tcase UI_EV_CODEPLUG_FOREIGN:'),
        ('\tif (d >= 0 && (press || ev->gesture == GESTURE_LONG)) {\n\t\tif (view->vfo) {\n\t\t\tentry_last = now;\n\t\t\tentry_digit((char)(\'0\' + d));\n\t\t} else {\n\t\t\tchar q[2] = { (char)(\'0\' + d), 0 };\n\t\t\tui_push(SCR_FIND, 0);\n\t\t\tui_find_set(q);\n\t\t}\n\t\treturn true;\n\t}',
         '\tif (d >= 0 && (press || ev->gesture == GESTURE_LONG)) {\n\t\tbool use_find = view->vfo ? (spotlight_mode == 1 || spotlight_mode == 2) : (spotlight_mode == 0 || spotlight_mode == 2);\n\t\tif (use_find) {\n\t\t\tchar q[2] = { (char)(\'0\' + d), 0 };\n\t\t\tui_push(SCR_FIND, 0);\n\t\t\tui_find_set(q);\n\t\t} else if (view->vfo) {\n\t\t\tentry_last = now;\n\t\t\tentry_digit((char)(\'0\' + d));\n\t\t}\n\t\treturn true;\n\t}'),
        ('\t\t\t\t\t  UI_SET_UPDOWN_HOME, UI_SET_LOW_BATTERY,',
         '\t\t\t\t\t  UI_SET_UPDOWN_HOME, UI_SET_SPOTLIGHT, UI_SET_LOW_BATTERY,'),
        ('\tcase UI_SET_UPDOWN_HOME: ui_st.updown = (uint8_t)(v >= 0 && v <= 2 ? v : 0); break;',
         '\tcase UI_SET_UPDOWN_HOME: ui_st.updown = (uint8_t)(v >= 0 && v <= 2 ? v : 0); break;\n\tcase UI_SET_SPOTLIGHT: spotlight_mode = (uint8_t)(v >= 0 && v <= 2 ? v : 0); break;'),
        ('\tif (view->vfo) {\n\t\tui_toast(TR("VFO settings save automatically"));',
         '\tif (view->vfo) {\n\t\tif (view->temp & RADIO_TEMP_SQUELCH) {\n\t\t\tui_svc_setting_set(UI_SET_SQUELCH, view->squelch);\n\t\t\tradio_req_clear_temporary();\n\t\t\tui_toast(TR("Squelch saved"));\n\t\t\treturn true;\n\t\t}\n\t\tui_toast(TR("VFO settings save automatically"));'),
        ('\tdefault:\t// value tiles: squelch, brightness\n\t\tui_st.quick_edit = true;\n\t\tbreak;',
         '\tcase QT_SQUELCH:\n\t\tui_st.quick_edit = true;\n\t\tui_toast(TR("Up/Dn 0-9  # Auto SQL"));\n\t\tbreak;\n\tdefault:\t// value tiles: brightness\n\t\tui_st.quick_edit = true;\n\t\tbreak;'),
        ('\tcase ACT_CONTEXT:\n\t\tif (press && ui_st.quick_edit && kind == QT_TONE) {',
         '\tcase ACT_CONTEXT:\n\t\tif (press && kind == QT_SQUELCH) {\n\t\t\tbool next = !ui_svc_setting_get(UI_SET_AUTO_SQL);\n\t\t\tif (ui_svc_setting_set(UI_SET_AUTO_SQL, next)) {\n\t\t\t\tui_svc_apply(UI_SET_AUTO_SQL, next);\n\t\t\t\tui_toast(next ? TR("Auto SQL on") : TR("Auto SQL off"));\n\t\t\t}\n\t\t\tdirty = true;\n\t\t} else if (press && ui_st.quick_edit && kind == QT_TONE) {'),
    ])
    transform('core/ui/ui_svc_storage.c', [
        ('static void save_failed_text(const char *text)\n{',
         'static bool cp_write_failed;\t// result of the held edit, owned by the storage job\n\nstatic void save_failed_text(const char *text)\n{\n\tcp_write_failed = true;'),
        ('\tvoid (*fn)(void *ctx) = cp_jobs[job.held - 1u];', '''\tvoid (*fn)(void *ctx) = cp_jobs[job.held - 1u];
\t// Only the explicit Start new choice may replace another firmware's settings/journal.
\t// The settings owner attaches its live preferences through its existing claim path.
\tif ((uintptr_t)ctx == UI_CP_NEW && storage_region() == STORAGE_REGION_FOREIGN && storage_claim_request())
\t\tsettings_poll(hal_millis());'''),
        ('\t\tjob.held = 0;\n\t\tfn(NULL);', '''\t\tjob.held = 0;
\t\tcp_write_failed = false;
\t\tfn(NULL);
\t\tif (!cp_write_failed) {
\t\t\tui_event_t ev = { .kind = UI_EV_CODEPLUG_STARTED };
\t\t\tui_notify(&ev);
\t\t}'''),
        ('\tcase UI_SET_TALK_PERMIT:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.talk_permit;',
         '\tcase UI_SET_TALK_PERMIT:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.talk_permit;\n\tcase UI_SET_STANDBY_LED:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.standby_led;\n\tcase UI_SET_STANDBY_GAP_S:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.standby_gap_s;\n\tcase UI_SET_STANDBY_ON_MS:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.standby_on_ms;\n\tcase UI_SET_LED_STYLE:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.led_style;\n\tcase UI_SET_AUTO_SQL:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.auto_sql;\n\tcase UI_SET_AUTO_POWER_SAVE:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.auto_power_save;\n\tcase UI_SET_FM_AUTO_CLEAR:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.fm_auto_clear;'),
        ('\tcase UI_SET_UPDOWN_HOME:\n\t\tsettings_read(&st);\n\t\treturn settings_updown_home(&st);',
         '\tcase UI_SET_UPDOWN_HOME:\n\t\tsettings_read(&st);\n\t\treturn settings_updown_home(&st);\n\tcase UI_SET_SPOTLIGHT:\n\t\tsettings_read(&st);\n\t\treturn (st.updown_home >> 2) & 0x03u;'),
        ('\tcase UI_SET_UPDOWN_HOME:\n\tcase UI_SET_LOW_BATTERY:',
         '\tcase UI_SET_UPDOWN_HOME:\n\tcase UI_SET_SPOTLIGHT:\n\tcase UI_SET_LOW_BATTERY:'),
        ('\t\t} else if (id == UI_SET_UPDOWN_HOME) {\n\t\t\tst->updown_home = (uint8_t)((st->updown_home & ~SETTINGS_SMALL_MASK) | clamp16(value, 0, 2));',
         '\t\t} else if (id == UI_SET_UPDOWN_HOME) {\n\t\t\tst->updown_home = (uint8_t)((st->updown_home & ~SETTINGS_SMALL_MASK) | clamp16(value, 0, 2));\n\t\t} else if (id == UI_SET_SPOTLIGHT) {\n\t\t\tst->updown_home = (uint8_t)((st->updown_home & ~0x0Cu) | ((clamp16(value, 0, 2) & 0x03u) << 2));'),
        ('\tcase UI_SET_REPLY_S:\n\tcase UI_SET_TALK_PERMIT:',
         '\tcase UI_SET_REPLY_S:\n\tcase UI_SET_TALK_PERMIT:\n\tcase UI_SET_STANDBY_LED:\n\tcase UI_SET_STANDBY_GAP_S:\n\tcase UI_SET_STANDBY_ON_MS:\n\tcase UI_SET_LED_STYLE:\n\tcase UI_SET_AUTO_SQL:\n\tcase UI_SET_AUTO_POWER_SAVE:\n\tcase UI_SET_FM_AUTO_CLEAR:'),
    ])
    transform('core/radio/radio_sm.h', [
        ('\tuint32_t batt_ms;\t\t// last battery reading',
         '\tuint32_t batt_ms;\t\t// last battery reading\n\tuint32_t led_now_ms;\t\t// latest poll timestamp for standby LED blink\n\tuint16_t sql_floor_q8;\t\t// adaptive FM noise floor (Q8, 0 = not seeded)\n\tuint8_t sql_settle;\t\t// polls after tune before sampling noise floor\n\tbool fm_narrow_active;\t\t// fm_auto_clear narrowed FM BW to 12.5k'),
        ('void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms);', '''void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms);
uint8_t radio_sm_tx_check(const radio_sm_t *sm);
uint8_t radio_sm_effective_power(const radio_sm_t *sm);
#if CONFIG_P25_CHANNELS
void radio_sm_native_maintenance(radio_sm_t *sm, uint32_t now_ms);
#endif'''),
    ])
    transform('core/radio/radio_sm.c', [
        ('#include "config.h"', '#include "config.h"\n#if CONFIG_P25_CHANNELS\n#include "p25/p25_lab.h"\n#endif'),
        ('\tshared = ambeVoiceStreaming();\n#endif', '\tshared = ambeVoiceStreaming();\n#endif\n#if CONFIG_P25_CHANNELS\n\tshared = shared || p25_lab_busy();\n#endif', 2),
        ('static uint8_t tx_check(const radio_sm_t *sm)', 'uint8_t radio_sm_tx_check(const radio_sm_t *sm)'),
        ('tx_check(sm)', 'radio_sm_tx_check(sm)', 4),
        ('\tcase RREQ_PTT:\n#if CONFIG_FM_TX_TONES', '\tcase RREQ_PTT:\n\t\t/* P25 PTT is handled by the native owner before this state machine. */\n\t\tif (codeplug_mode_p25(sm->view.ch.mode)) break;\n#if CONFIG_FM_TX_TONES'),
        ('case RREQ_MODE:\n\t\tif (busy ||', 'case RREQ_MODE:\n\t\tif (codeplug_mode_p25(sm->view.ch.mode)) break;\n\t\tif (busy ||'),
        ('codeplug_channel(sm->zone.channels[pos], &ch) && !ch.scan_skip;', 'codeplug_channel(sm->zone.channels[pos], &ch) && !ch.scan_skip && !codeplug_mode_p25(ch.mode);'),
        ('static void scan_request(radio_sm_t *sm, const radio_req_t *req, uint32_t now_ms)\n{', 'static void scan_request(radio_sm_t *sm, const radio_req_t *req, uint32_t now_ms)\n{\n\t/* P25 currently supports selected-channel receive, not scan dwell. */\n\tif (codeplug_mode_p25(sm->view.ch.mode)) return;'),
        ('\tcase RSCAN_DUAL: {\n', '\tcase RSCAN_DUAL: {\n\t\tchannel_t other;\n\t\tif (req->arg2 >= sm->zone.count || !codeplug_channel(sm->zone.channels[req->arg2], &other) || codeplug_mode_p25(other.mode)) break;\n'),
        ('if (ch->mode == RADIO_MODE_DMR ? !CONFIG_VFO_DMR : !CONFIG_VFO_FM)', 'if (codeplug_mode_p25(ch->mode) || (ch->mode == RADIO_MODE_DMR ? !CONFIG_VFO_DMR : !CONFIG_VFO_FM))'),
        ('if (ch->mode == RADIO_MODE_DMR ? !CONFIG_DMR : !CONFIG_FM)', 'if (codeplug_mode_p25(ch->mode) ? !CONFIG_P25_NATIVE : (ch->mode == RADIO_MODE_DMR ? !CONFIG_DMR : !CONFIG_FM))'),
        ('\tif (ch->tx_hz == 0) {', '''#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(ch->mode) && !codeplug_p25_valid(ch)) return TXDENY_MODE;
#endif
\tif (ch->tx_hz == 0) {'''),
        ('\tif (!board_in_band(ch->tx_hz, true)) {', '''\tif (!board_in_band(ch->tx_hz, true)
#if CONFIG_P25_CHANNELS
\t    || (codeplug_mode_p25(ch->mode) &&
\t        !((ch->tx_hz > 144000000u && ch->tx_hz < 148000000u) ||
\t          (ch->tx_hz > 420000000u && ch->tx_hz < 450000000u)))
#endif
\t) {'''),
        ('\tif (sm->view.ch.mode != RADIO_MODE_FM || sm->set.bcl == RADIO_BCL_OFF) {', '''#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(sm->view.ch.mode))
\t\treturn sm->set.bcl != RADIO_BCL_OFF && sm->view.state == RS_RX;
#endif
\tif (sm->view.ch.mode != RADIO_MODE_FM || sm->set.bcl == RADIO_BCL_OFF) {'''),
        ('\tbool green = open;', '''\tbool green = open;
#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(sm->view.ch.mode)) { open = false; green = sm->view.state == RS_RX; }
#endif'''),
        ('\tif (sm->green_out != (int8_t)green) {\n\t\tsm->green_out = (int8_t)green;\n\t\tboard.power->led(LED_GREEN, green);\n\t}', '''\tbool led_green = green;
\tbool led_red = tx;
\tif (!tx && sm->set.led_style != 0) {
\t\tbool rx_other = false;
#if CONFIG_DMR
\t\trx_other = !fm && sm->view.state == RS_RX && sm->view.dmr.state == RDMR_RX_OTHER;
#endif
\t\tif (sm->set.led_style == 2) {
\t\t\tif (green && !sm->view.monitor) {
\t\t\t\tled_green = (sm->led_now_ms % 400u) < 200u;
\t\t\t} else if (sm->view.monitor || rx_other) {
\t\t\t\tled_green = true;
\t\t\t\tled_red = true;
\t\t\t}
\t\t} else if (sm->set.led_style == 1) {
\t\t\tif (sm->view.monitor || rx_other) {
\t\t\t\tled_green = true;
\t\t\t\tled_red = true;
\t\t\t}
\t\t}
\t}
\tuint32_t gap_ms = (uint32_t)(sm->set.standby_gap_s ? sm->set.standby_gap_s : 5u) * 1000u;
\tuint32_t on_ms = (uint32_t)(sm->set.standby_on_ms ? sm->set.standby_on_ms : 100u);
\tif (sm->set.auto_power_save && sm->view.battery >= RADIO_BATT_LOW) {
\t\tgap_ms *= 2u;
\t\tif (on_ms > 50u) on_ms = 50u;
\t}
\tif (on_ms >= gap_ms) on_ms = gap_ms - 1u;
\tbool standby_blink = !tx && !green && !open && !led_red && !sm->hotspot && !sm->sfr_rpt &&
\t\tsm->set.standby_led && sm->view.state == RS_IDLE &&
\t\t(sm->led_now_ms % gap_ms) >= (gap_ms - on_ms);
#if CONFIG_DUAL_WATCH
\tif (sm->rx2_on) standby_blink = false;
#endif
\tif (standby_blink) led_green = true;
\tif (sm->green_out != (int8_t)led_green) {
\t\tsm->green_out = (int8_t)led_green;
\t\tboard.power->led(LED_GREEN, led_green);
\t}'''),
        ('\tif (sm->red_out != (int8_t)tx) {\n\t\tsm->red_out = (int8_t)tx;\n\t\tboard.power->led(LED_RED, tx);\n\t}',
         '\tif (sm->red_out != (int8_t)led_red) {\n\t\tsm->red_out = (int8_t)led_red;\n\t\tboard.power->led(LED_RED, led_red);\n\t}'),
        ('\tsm->view.leds = (uint8_t)((green ? RADIO_LED_GREEN : 0u) | (tx ? RADIO_LED_RED : 0u));',
         '\tsm->view.leds = (uint8_t)((led_green ? RADIO_LED_GREEN : 0u) | (led_red ? RADIO_LED_RED : 0u));'),
        ('\ts->talk_permit = true;',
         '\ts->talk_permit = true;\n\ts->standby_gap_s = 5;\n\ts->standby_on_ms = 100;'),
        ('\tif (s->beep_level > 100) {',
         '\tif (s->standby_gap_s == 0) s->standby_gap_s = 5;\n\telse if (s->standby_gap_s > 30) s->standby_gap_s = 30;\n\tif (s->standby_on_ms == 0) s->standby_on_ms = 100;\n\telse if (s->standby_on_ms < 50) s->standby_on_ms = 50;\n\telse if (s->standby_on_ms > 1000) s->standby_on_ms = 1000;\n\tif (s->led_style > 2) s->led_style = 0;\n\tif (s->beep_level > 100) {'),
        ('static uint8_t channel_power(const channel_t *ch)\n{\n\tif (ch->power == CODEPLUG_POWER_DEFAULT || ch->power >= board.power_count) {\n\t\treturn default_power();\n\t}\n\treturn ch->power;\n}',
         '''static uint8_t channel_power(const channel_t *ch)
{
\tif (ch->power == CODEPLUG_POWER_DEFAULT || ch->power >= board.power_count) {
\t\treturn default_power();
\t}
\treturn ch->power;
}

uint8_t radio_sm_effective_power(const radio_sm_t *sm)
{
\tuint8_t p = sm->view.power;
\tif (sm->set.auto_power_save && sm->view.battery >= RADIO_BATT_LOW) {
\t\tuint8_t cap = board.power_count > 3u ? 1u : 0u;
\t\tif (p > cap) p = cap;
\t}
\treturn p;
}

static void auto_save_sync(radio_sm_t *sm)
{
\tbool auto_save = sm->set.auto_power_save && sm->view.battery >= RADIO_BATT_LOW;
\tuint8_t eff_p = radio_sm_effective_power(sm);
\tif (auto_save != sm->view.auto_save_active || eff_p != sm->view.power_eff) {
\t\tsm->view.auto_save_active = auto_save;
\t\tsm->view.power_eff = eff_p;
\t\tchanged(sm);
\t}
}'''),
        ('if (!board.radio->tx(ch->tx_hz, (radio_bw_t)ch->bw, sm->view.power, &none)) {',
         'if (!board.radio->tx(ch->tx_hz, (radio_bw_t)ch->bw, radio_sm_effective_power(sm), &none)) {', 3),
        ('if (!board.radio->tx(ch->tx_hz, (radio_bw_t)ch->bw, sm->view.power, &ch->tx_tone)) {',
         'if (!board.radio->tx(ch->tx_hz, (radio_bw_t)ch->bw, radio_sm_effective_power(sm), &ch->tx_tone)) {'),
        ('\tsm->view.power = (uint8_t)p;',
         '\tsm->view.power = (uint8_t)p;\n\t\tsm->view.power_eff = radio_sm_effective_power(sm);'),
        ('\tsm->set = *s;',
         '\tif (s->auto_sql != sm->set.auto_sql) { sm->sql_floor_q8 = 0; sm->sql_settle = 0; }\n\tsm->set = *s;\n\tsm->view.auto_sql = sm->set.auto_sql;\n\tauto_save_sync(sm);'),
        ('\tsm->view.tot_s = sm->set.tot_s;',
         '\tsm->view.tot_s = sm->set.tot_s;\n\tsm->view.auto_sql = sm->set.auto_sql;\n\tauto_save_sync(sm);'),
        ('\ts->bcl = st->rs_bcl;\n\ts->roger_beep = (st->rs_flags & SETTINGS_RS_ROGER_BEEP) != 0;\n\ts->tx_inhibit = (st->rs_flags & SETTINGS_RS_TX_INHIBIT) != 0;\n\ts->scan_priority = (st->rs_flags & SETTINGS_RS_SCAN_PRIORITY) != 0;\n\ts->scan_beep = (st->rs_flags & SETTINGS_RS_SCAN_BEEP) != 0;\n\ts->key_beep = (st->rs_flags & SETTINGS_RS_KEY_BEEP) != 0;\n\ts->talk_permit = (st->rs_flags & SETTINGS_RS_NO_TALK_PERMIT) == 0;\n\ts->scan_resume = st->rs_scan_resume;',
         '\ts->bcl = st->rs_bcl & 0x03u;\n\ts->roger_beep = (st->rs_flags & SETTINGS_RS_ROGER_BEEP) != 0;\n\ts->tx_inhibit = (st->rs_flags & SETTINGS_RS_TX_INHIBIT) != 0;\n\ts->scan_priority = (st->rs_flags & SETTINGS_RS_SCAN_PRIORITY) != 0;\n\ts->scan_beep = (st->rs_flags & SETTINGS_RS_SCAN_BEEP) != 0;\n\ts->key_beep = (st->rs_flags & SETTINGS_RS_KEY_BEEP) != 0;\n\ts->talk_permit = (st->rs_flags & SETTINGS_RS_NO_TALK_PERMIT) == 0;\n\ts->standby_led = (st->rs_flags & SETTINGS_RS_STANDBY_LED) != 0;\n\ts->auto_sql = (st->rs_flags & SETTINGS_RS_AUTO_SQL) != 0;\n\ts->standby_gap_s = ((st->rs_bcl >> 2) & 0x1Fu) ? (uint8_t)((st->rs_bcl >> 2) & 0x1Fu) : 5u;\n\ts->scan_resume = st->rs_scan_resume & 0x03u;\n\ts->standby_on_ms = ((st->rs_scan_resume >> 2) & 0x1Fu) ? (uint16_t)(((st->rs_scan_resume >> 2) & 0x1Fu) * 50u) : 100u;'),
        ('\ts->rx_save = st->rs_rx_save;',
         '\ts->rx_save = st->rs_rx_save & 0x03u;\n\ts->led_style = (st->rs_rx_save >> 2) & 0x03u;\n\ts->auto_power_save = (st->unused_dmr_id & SETTINGS_CUSTOM_AUTO_SAVE) != 0;\n\ts->fm_auto_clear = (st->unused_dmr_id & SETTINGS_CUSTOM_FM_CLEAR) != 0;'),
        ('\tst->rs_bcl = s->bcl;\n\tst->rs_flags = (uint8_t)((s->roger_beep ? SETTINGS_RS_ROGER_BEEP : 0) | (s->tx_inhibit ? SETTINGS_RS_TX_INHIBIT : 0) |\n\t\t\t\t (s->scan_priority ? SETTINGS_RS_SCAN_PRIORITY : 0) |\n\t\t\t\t (s->scan_beep ? SETTINGS_RS_SCAN_BEEP : 0) | (s->key_beep ? SETTINGS_RS_KEY_BEEP : 0) |\n\t\t\t\t (s->talk_permit ? 0 : SETTINGS_RS_NO_TALK_PERMIT));\n\tst->rs_scan_resume = s->scan_resume;',
         '\tuint8_t gap = s->standby_gap_s <= 30u ? s->standby_gap_s : 30u;\n\tuint8_t on50 = (uint8_t)(s->standby_on_ms / 50u);\n\tif (on50 > 20u) on50 = 20u;\n\tst->rs_bcl = (uint8_t)((s->bcl & 0x03u) | ((gap & 0x1Fu) << 2));\n\tst->rs_flags = (uint8_t)((s->roger_beep ? SETTINGS_RS_ROGER_BEEP : 0) | (s->tx_inhibit ? SETTINGS_RS_TX_INHIBIT : 0) |\n\t\t\t\t (s->scan_priority ? SETTINGS_RS_SCAN_PRIORITY : 0) |\n\t\t\t\t (s->scan_beep ? SETTINGS_RS_SCAN_BEEP : 0) | (s->key_beep ? SETTINGS_RS_KEY_BEEP : 0) |\n\t\t\t\t (s->talk_permit ? 0 : SETTINGS_RS_NO_TALK_PERMIT) |\n\t\t\t\t (s->standby_led ? SETTINGS_RS_STANDBY_LED : 0) |\n\t\t\t\t (s->auto_sql ? SETTINGS_RS_AUTO_SQL : 0));\n\tst->rs_scan_resume = (uint8_t)((s->scan_resume & 0x03u) | ((on50 & 0x1Fu) << 2));'),
        ('\tst->rs_rx_save = s->rx_save;',
         '\tst->rs_rx_save = (uint8_t)((s->rx_save & 0x03u) | ((s->led_style & 0x03u) << 2));\n\tst->unused_dmr_id = (st->unused_dmr_id & ~(SETTINGS_CUSTOM_AUTO_SAVE | SETTINGS_CUSTOM_FM_CLEAR)) | (s->auto_power_save ? SETTINGS_CUSTOM_AUTO_SAVE : 0u) | (s->fm_auto_clear ? SETTINGS_CUSTOM_FM_CLEAR : 0u);'),
        ('\tmemset(sm, 0, sizeof(*sm));',
         '\tmemset(sm, 0, sizeof(*sm));\n\tsm->led_now_ms = now_ms;'),
        ('\tsm->retune_pending = sm->retune_due = false;\t// this tune is the one they wait for\n#if CONFIG_SFR_CWID\n\tcw_cancel(sm);\t\t// the repeater\'s ID: the board is set up for the channel below\n#endif\n\tend_reception(sm, now_ms);',
         '\tsm->retune_pending = sm->retune_due = false;\t// this tune is the one they wait for\n#if CONFIG_SFR_CWID\n\tcw_cancel(sm);\t\t// the repeater\'s ID: the board is set up for the channel below\n#endif\n\tend_reception(sm, now_ms);\n\tsm->sql_floor_q8 = 0;\n\tsm->sql_settle = 3;\n\tsm->fm_narrow_active = false;'),
        ('static bool save_allowed(const radio_sm_t *sm, uint32_t now_ms)\n{\n\tif (sm->set.rx_save == 0 ||',
         'static uint8_t effective_rx_save(const radio_sm_t *sm)\n{\n\tif (sm->set.auto_power_save) {\n\t\treturn sm->view.battery >= RADIO_BATT_LOW ? (sm->set.rx_save != 0 ? sm->set.rx_save : 1u) : 0u;\n\t}\n\treturn sm->set.rx_save;\n}\n\nstatic bool save_allowed(const radio_sm_t *sm, uint32_t now_ms)\n{\n\tif (effective_rx_save(sm) == 0 ||'),
        ('uint32_t sleep_ms = (uint32_t)RADIO_SAVE_SLEEP_MS << (sm->set.rx_save - 1u);',
         'uint32_t sleep_ms = (uint32_t)RADIO_SAVE_SLEEP_MS << (effective_rx_save(sm) - 1u);'),
        ('static bool carrier_heard(const radio_sm_t *sm, uint8_t noise)\n{\n\tuint8_t sq = sm->view.squelch != 0 ? sm->view.squelch : RADIO_SQUELCH_DEFAULT;\n\treturn noise < radio_sm_squelch_open_below(sq);\n}',
         '''static uint8_t squelch_open_below(const radio_sm_t *sm, uint8_t sq)
{
\tif (sq == 0) {
\t\treturn 255;
\t}
\tif (sm->set.auto_sql && sm->sql_floor_q8 != 0 && !sm->view.bcast && !scan_on(&sm->scan)) {
\t\tuint16_t floor = (uint16_t)((sm->sql_floor_q8 + 128u) >> 8);
\t\tuint16_t sq_c = sq > RADIO_SQUELCH_MAX ? RADIO_SQUELCH_MAX : sq;
\t\tuint16_t margin = sm->view.am ? (uint16_t)(3u + (sq_c >> 1)) : (uint16_t)(8u + sq_c);
\t\tuint16_t min_t = sm->view.am ? 40u : 16u;
\t\tuint16_t max_t = sm->view.am ? 64u : 120u;
\t\tuint16_t thresh = floor > margin ? (uint16_t)(floor - margin) : min_t;
\t\tif (thresh < min_t) thresh = min_t;
\t\tif (thresh > max_t) thresh = max_t;
\t\treturn (uint8_t)thresh;
\t}
\treturn radio_sm_squelch_open_below(sq);
}

static void auto_sql_update(radio_sm_t *sm, uint8_t noise, uint8_t sq)
{
\tif (!sm->set.auto_sql || sq == 0 || sm->view.bcast ||
\t    scan_on(&sm->scan) || sm->view.state != RS_IDLE || sm->view.monitor) {
\t\treturn;
\t}
\tif (sm->sql_settle > 0) {
\t\tsm->sql_settle--;
\t\treturn;
\t}
\tuint16_t n_q8 = (uint16_t)noise << 8;
\tif (sm->sql_floor_q8 == 0) {
\t\tif (noise >= radio_sm_squelch_open_below(sq)) {
\t\t\tsm->sql_floor_q8 = n_q8;
\t\t}
\t\treturn;
\t}
\tuint8_t open_below = squelch_open_below(sm, sq);
\tif (n_q8 >= sm->sql_floor_q8) {
\t\tsm->sql_floor_q8 = (uint16_t)(sm->sql_floor_q8 + ((n_q8 - sm->sql_floor_q8) >> 5));
\t} else if (noise >= open_below) {
\t\tsm->sql_floor_q8 = (uint16_t)(sm->sql_floor_q8 - ((sm->sql_floor_q8 - n_q8) >> 4));
\t}
}

static bool carrier_heard(const radio_sm_t *sm, uint8_t noise)
{
\tuint8_t sq = sm->view.squelch != 0 ? sm->view.squelch : RADIO_SQUELCH_DEFAULT;
\treturn noise < squelch_open_below(sm, sq);
}

static uint8_t squelch_eff_level(const radio_sm_t *sm, uint8_t sq)
{
\tif (sq == 0 || !sm->set.auto_sql || sm->sql_floor_q8 == 0 || sm->view.bcast || scan_on(&sm->scan)) {
\t\treturn sq;
\t}
\tuint8_t thresh = squelch_open_below(sm, sq);
\tuint8_t best = 1u;
\tuint8_t best_diff = 255u;
\tfor (uint8_t lv = 1u; lv <= RADIO_SQUELCH_MAX; lv++) {
\t\tuint8_t ref = radio_sm_squelch_open_below(lv);
\t\tuint8_t diff = ref > thresh ? (uint8_t)(ref - thresh) : (uint8_t)(thresh - ref);
\t\tif (diff < best_diff) {
\t\t\tbest_diff = diff;
\t\t\tbest = lv;
\t\t}
\t}
\treturn best;
}'''),
        ('\tuint8_t open_below = radio_sm_squelch_open_below(sq);',
         '\tif (!dual_peeking(sm)) auto_sql_update(sm, st.noise, sq);\n\tuint8_t eff_sq = squelch_eff_level(sm, sq);\n\tif (eff_sq != sm->view.sql_eff) { sm->view.sql_eff = eff_sq; changed(sm); }\n\tif (sm->set.fm_auto_clear && sm->view.ch.mode == RADIO_MODE_FM && !sm->view.am && !sm->view.bcast && sm->view.ch.bw == RADIO_BW_25K && sm->view.state == RS_RX && !sm->ptt && !scan_on(&sm->scan)) {\n\t\tif (!sm->fm_narrow_active && st.noise >= 46u) {\n\t\t\tsm->fm_narrow_active = true;\n\t\t\tif (board.radio->fm_bw != NULL) board.radio->fm_bw(RADIO_BW_12K5);\n\t\t} else if (sm->fm_narrow_active && st.noise < 38u) {\n\t\t\tsm->fm_narrow_active = false;\n\t\t\tif (board.radio->fm_bw != NULL) board.radio->fm_bw(RADIO_BW_25K);\n\t\t}\n\t} else if (sm->fm_narrow_active) {\n\t\tsm->fm_narrow_active = false;\n\t\tif (board.radio->fm_bw != NULL) board.radio->fm_bw(sm->view.ch.bw);\n\t}\n\tuint8_t open_below = squelch_open_below(sm, sq);'),
        ('\tbattery_poll(sm, now_ms);',
         '\tbattery_poll(sm, now_ms);\n\tauto_save_sync(sm);'),
        ('void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms)', '''#if CONFIG_P25_CHANNELS
void radio_sm_native_maintenance(radio_sm_t *sm, uint32_t now_ms)
{
\tsm->led_now_ms = now_ms;
\tfollow_volume(sm);
\tbattery_poll(sm, now_ms);
\tauto_save_sync(sm);
\tif (sm->view.state == RS_TX) tx_timer(sm, now_ms);
\toutputs(sm);
#if CONFIG_BEEP
\tbeep_service(p25_lab_busy());
#endif
}
#endif
void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms)'''),
        ('void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms)\n{\n\tradio_status_t st;',
         'void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms)\n{\n\tradio_status_t st;\n\tsm->led_now_ms = now_ms;'),
        ('\tif (sm->dmr_running) {\n\t\treturn;\t\t// the state follows the call stack (radio_sm_tick), and the stack keeps its own TX time-out\n\t}',
         '\tif (sm->dmr_running) {\n\t\toutputs(sm);\n\t\treturn;\t\t// the state follows the call stack (radio_sm_tick), and the stack keeps its own TX time-out\n\t}'),
    ])
    transform('core/settings/settings.h', [
        ('#define SETTINGS_RS_NO_TALK_PERMIT\t0x20u\t// the talk permit tone off (0 = on, as before)',
         '#define SETTINGS_RS_NO_TALK_PERMIT\t0x20u\t// the talk permit tone off (0 = on, as before)\n#define SETTINGS_RS_STANDBY_LED\t\t0x40u\t// periodic green LED blink while idle/standby\n#define SETTINGS_RS_AUTO_SQL\t\t0x80u\t// adaptive FM squelch from channel noise floor\n#define SETTINGS_CUSTOM_AUTO_SAVE\t0x01u\t// unused_dmr_id b0: auto rx_save on low batt\n#define SETTINGS_CUSTOM_FM_CLEAR\t0x02u\t// unused_dmr_id b1: narrow FM BW on noisy RX'),
    ])
    transform('core/ui/ui_svc_radio.c', [
        ('\tcase UI_SET_TALK_PERMIT:\n\t\trs.talk_permit = value != 0;\n\t\tbreak;',
         '\tcase UI_SET_TALK_PERMIT:\n\t\trs.talk_permit = value != 0;\n\t\tbreak;\n\tcase UI_SET_STANDBY_LED:\n\t\trs.standby_led = value != 0;\n\t\tbreak;\n\tcase UI_SET_STANDBY_GAP_S:\n\t\trs.standby_gap_s = (uint8_t)(value < 1 ? 1 : value > 30 ? 30 : value);\n\t\tbreak;\n\tcase UI_SET_STANDBY_ON_MS:\n\t\trs.standby_on_ms = (uint16_t)(value < 50 ? 50 : value > 1000 ? 1000 : value);\n\t\tbreak;\n\tcase UI_SET_LED_STYLE:\n\t\trs.led_style = (uint8_t)(value < 0 || value > 2 ? 0 : value);\n\t\tbreak;\n\tcase UI_SET_AUTO_SQL:\n\t\trs.auto_sql = value != 0;\n\t\tbreak;\n\tcase UI_SET_AUTO_POWER_SAVE:\n\t\trs.auto_power_save = value != 0;\n\t\tbreak;\n\tcase UI_SET_FM_AUTO_CLEAR:\n\t\trs.fm_auto_clear = value != 0;\n\t\tbreak;'),
    ])
    transform('core/debuglink/debuglink_view.c', [
        ('v->ch.mode == RADIO_MODE_DMR ? "dmr" : "fm"', 'codeplug_mode_p25(v->ch.mode) ? "p25" : v->ch.mode == RADIO_MODE_DMR ? "dmr" : "fm"'),
        ('c->mode == RADIO_MODE_DMR ? "dmr" : "fm"', 'codeplug_mode_p25(c->mode) ? "p25" : c->mode == RADIO_MODE_DMR ? "dmr" : "fm"'),
    ])
