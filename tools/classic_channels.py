# SPDX-License-Identifier: GPL-3.0-or-later
"""Conventional P25 fields in RadioFoundry's existing channel store/editor."""


def prepare(transform):
    transform('drivers/rf/fd6818/fd6818.c', [
        ('static const regval_t dmr_table[] = {', '''static const regval_t dmr_table[] = {
\t{ 0x51, 0x0000 }, // FM tone detection also enables subaudio TX; digital modulation comes from the C7000.'''),
    ])
    transform('tests/host/fd6818Test.c', [
        ('static void test_rx_tones(void)', '''static void test_tone_to_dmr(void)
{
\tconst fd6818_tone_t tones[] = { FD6818_TONE_CTCSS, FD6818_TONE_DCS, FD6818_TONE_DCS_INV };
\tconst uint16_t values[] = { 1000, 023, 023 };
\tconst uint16_t enabled[] = { 0x9045, 0x8045, 0xA045 };
\tfor (unsigned t = 0; t < sizeof tones / sizeof tones[0]; t++) {
\t\tfd6818_t dev;
\t\treset_chip();fd6818_attach(&dev, bus());
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
\tunion {
\t\tstruct { uint16_t contact, listen_list; uint32_t dmr_id; };
\t\tstruct { uint16_t p25_tg, p25_nac; uint32_t p25_id; };
\t};
#else
\tuint16_t contact, listen_list;
\tuint32_t dmr_id;
#endif'''),
        ('} channel_t;', '''} channel_t;

static inline bool codeplug_mode_p25(uint8_t mode)
{
#if CONFIG_P25_CHANNELS
\treturn mode == RADIO_MODE_P25;
#else
\t(void)mode;
\treturn false;
#endif
}
#if CONFIG_P25_CHANNELS
static inline bool codeplug_p25_valid(const channel_t *ch)
{
\treturn ch->bw == RADIO_BW_12K5 && ch->p25_nac <= 0xFFFu &&
\t       ch->p25_nac != 0xF7Fu && ch->p25_id <= 9999999u &&
\t       (ch->rx_only || (ch->p25_nac != 0xF7Eu && ch->p25_tg && ch->p25_id));
}
#endif'''),
    ])
    transform('core/codeplug/codeplug.c', [
        ('\tmemset(out, 0, sizeof(*out));\n\tif (!freq(&r[CH_RX]', '\tmemset(out, 0, sizeof(*out));\n#if CONFIG_P25_CHANNELS\n\tif (r[CH_MODE] != 0 && r[CH_MODE] != 1 && r[CH_MODE] != 0xA5u) return false;\n#endif\n\tif (!freq(&r[CH_RX]'),
        ('\tout->alias_tx = (uint8_t)', '''#if CONFIG_P25_CHANNELS
\tif (r[CH_MODE] == 0xA5u) {
\t\tout->mode = RADIO_MODE_P25;
\t\tout->p25_nac = (uint16_t)(((r[CH_CC] & 15u) << 8) | r[CH_RX_GROUP]);
\t\tout->dmo = out->roam = out->vox = false;
\t\treturn (r[CH_CC] & 0xF0u) == 0xD0u && codeplug_p25_valid(out);
\t}
#endif
\tout->alias_tx = (uint8_t)'''),
        ('\tr[CH_MODE] = ch->mode == RADIO_MODE_DMR ? 1 : 0;', '\tr[CH_MODE] = codeplug_mode_p25(ch->mode) ? 0xA5u : ch->mode == RADIO_MODE_DMR ? 1 : 0;'),
        ('\tr[CH_CC] = ch->color_code & 0x0Fu;', '''\tr[CH_CC] = ch->color_code & 0x0Fu;
#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(ch->mode))
\t\tr[CH_CC] = (uint8_t)(0xD0u | (ch->p25_nac >> 8));
#endif'''),
        ('\tr[CH_RX_GROUP] = (uint8_t)(ch->listen_list <= 0xFFu ? ch->listen_list : 0);', '''\tr[CH_RX_GROUP] = (uint8_t)(ch->listen_list <= 0xFFu ? ch->listen_list : 0);
#if CONFIG_P25_CHANNELS
\tif (codeplug_mode_p25(ch->mode)) r[CH_RX_GROUP] = (uint8_t)ch->p25_nac;
#endif'''),
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
         '#if CONFIG_SFR_CWID\n\tLIST(TRN("SFR CW ID"), "single frequency repeater morse callsign identify", UI_SET_SFR_CWID, v_sfrcwid, n_sfrcwid),\n#endif\n\tTOGGLE(TRN("Auto squelch"), "ASQ automatic noise floor", UI_SET_AUTO_SQL),\n};'),
        ('ui_toast(i == UI_CP_NEW ? TR("New codeplug started") : TR("Old codeplug kept: edit again"));',
         'ui_toast(i == UI_CP_NEW ? TR("Creating codeplug") : TR("Old codeplug kept: edit again"));'),
        ('static void contacts_item(uint16_t i, ui_item_t *it)', 'static void __attribute__((unused)) contacts_item(uint16_t i, ui_item_t *it)'),
        ('CE_ZONES, CE_GO, CE_SHARE, CE_SAVE, CE_SAVE_NEW, CE_COUNT', '#if CONFIG_P25_CHANNELS\n       CE_P25_TG, CE_P25_NAC, CE_P25_ID,\n#endif\n       CE_ZONES, CE_GO, CE_SHARE, CE_SAVE, CE_SAVE_NEW, CE_COUNT'),
        ('[CE_SCAN] = CE_FIELD(scan_skip), [CE_ECO] = CE_FIELD(no_eco),', '[CE_SCAN] = CE_FIELD(scan_skip), [CE_ECO] = CE_FIELD(no_eco),\n#if CONFIG_P25_CHANNELS\n\t[CE_P25_TG] = CE_FIELD(p25_tg), [CE_P25_NAC] = CE_FIELD(p25_nac), [CE_P25_ID] = CE_FIELD(p25_id),\n#endif'),
        ('\t\tif ((fm_only && dmr) || (dmr_only && !dmr) || (r == CE_MODE && !CONFIG_DMR))', '#if CONFIG_P25_CHANNELS\n\t\tbool p25 = codeplug_mode_p25(ced_ch.mode);\n\t\tbool p25_only = r >= CE_P25_TG && r <= CE_P25_ID;\n\t\tif ((p25_only && !p25) || (p25 && (fm_only || dmr_only || r == CE_ECO))) continue;\n#endif\n\t\tif ((fm_only && dmr) || (dmr_only && !dmr) || (r == CE_MODE && !CONFIG_DMR))'),
        ('TRN("Battery saver"), TRN("Zones"), TRN("Tune to this ch"),', 'TRN("Battery saver"),\n#if CONFIG_P25_CHANNELS\n\t\tTRN("Talkgroup"), "NAC", TRN("P25 Radio ID"),\n#endif\n\t\tTRN("Zones"), TRN("Tune to this ch"),'),
        ('sb_str(&wb, ch->mode == RADIO_MODE_DMR ? "DMR" : radio_am_channel(ch) ? "AM" : "FM");', 'sb_str(&wb, codeplug_mode_p25(ch->mode) ? "P25" : ch->mode == RADIO_MODE_DMR ? "DMR" : radio_am_channel(ch) ? "AM" : "FM");'),
        ('sb_str(&b, ch->mode == RADIO_MODE_DMR ? "DMR" : "FM");', 'sb_str(&b, codeplug_mode_p25(ch->mode) ? "P25" : ch->mode == RADIO_MODE_DMR ? "DMR" : "FM");'),
        ('if (editing && (r == CE_RX || r == CE_TX || r == CE_ID)) {', 'if (editing && (r == CE_RX || r == CE_TX || r == CE_ID\n#if CONFIG_P25_CHANNELS\n\t\t|| r == CE_P25_TG || r == CE_P25_ID\n#endif\n\t\t)) {'),
        ('if (r != CE_ID && k == UI_ENTRY_MHZ)', 'if ((r == CE_RX || r == CE_TX) && k == UI_ENTRY_MHZ)'),
        ('\tcase CE_NUMBER:\n\t\tsb_u32(&b, ced_num, 1);', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_TG: sb_u32(&b, ch->p25_tg, 1); break;\n\tcase CE_P25_ID: sb_u32(&b, ch->p25_id, 1); break;\n\tcase CE_P25_NAC: {\n\t\tunsigned value = ch->p25_nac, cursor = 3;\n#if CONFIG_CHANNEL_EDIT\n\t\tif (editing) { value = (unsigned)ced_val; cursor = ced_len; }\n#endif\n\t\tsb_chr(&b, \'$\');\n\t\tfor (unsigned digit = 0; digit < 3; digit++) {\n\t\t\tif (digit == cursor) sb_chr(&b, \'[\');\n\t\t\tsb_chr(&b, "0123456789ABCDEF"[(value >> (4 * (2 - digit))) & 15u]);\n\t\t\tif (digit == cursor) sb_chr(&b, \']\');\n\t\t}\n\t\tbreak;\n\t}\n#endif\n\tcase CE_NUMBER:\n\t\tsb_u32(&b, ced_num, 1);'),
        ('\tif (CONFIG_DMR && c->mode == RADIO_MODE_DMR && c->contact == 0)', '#if CONFIG_P25_CHANNELS\n\tif (codeplug_mode_p25(c->mode) && !codeplug_p25_valid(c))\n\t\treturn TR("Check P25 NAC, talkgroup and ID");\n#endif\n\tif (CONFIG_DMR && c->mode == RADIO_MODE_DMR && c->contact == 0)'),
        ('\tcase CE_CC:\n\t\tced_val = (ced_val + dir + 16) % 16;', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_NAC: {\n\t\tunsigned shift = 4u * (2u - ced_len);\n\t\tunsigned value = (((unsigned)ced_val >> shift) + (unsigned)dir) & 15u;\n\t\tced_val = (ced_val & ~(15u << shift)) | (value << shift);\n\t\tbreak;\n\t}\n#endif\n\tcase CE_CC:\n\t\tced_val = (ced_val + dir + 16) % 16;'),
        ('\tcase CE_ID:\n\t\tif (ced_len >= CE_DIGITS)', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_NAC: {\n\t\tunsigned shift = 4u * (2u - ced_len);\n\t\tced_val = (ced_val & ~(15u << shift)) | ((unsigned)(d - \'0\') << shift);\n\t\tced_len = (uint8_t)((ced_len + 1) % 3);\n\t\tbreak;\n\t}\n\tcase CE_P25_TG:\n\tcase CE_P25_ID:\n\t\tif (v > (ced_edit == CE_P25_TG ? 65535 : 9999999)) {\n\t\t\tui_toast(TR("Value too large")); break;\n\t\t}\n        /* fall through */\n        __attribute__((fallthrough));\n#endif\n\tcase CE_ID:\n\t\tif (ced_len >= CE_DIGITS)'),
        ('\tcase CE_ID:\n\t\tif (ced_len > 0)', '#if CONFIG_P25_CHANNELS\n\tcase CE_P25_TG: if (ced_len) c->p25_tg = (uint16_t)ced_val; break;\n\tcase CE_P25_ID: if (ced_len) c->p25_id = (uint32_t)ced_val; break;\n\tcase CE_P25_NAC: c->p25_nac = (uint16_t)ced_val; break;\n#endif\n\tcase CE_ID:\n\t\tif (ced_len > 0)'),
        ('static bool chinfo_key(const input_event_t *ev)\n{\n\tbool press = ev->gesture == GESTURE_PRESS || ev->gesture == GESTURE_LONG;\n\tif (ced_edit == CE_NONE) {\n\t\tif (ev->action == ACT_BACK && ev->gesture == GESTURE_PRESS && ced_num != 0 &&\n\t\t\t\t(ced_new || ced_changed(CE_COUNT))) {\n\t\t\tdiscard_contact = false;\n\t\t\tui_push(SCR_DISCARD, 0);\n\t\t\treturn true;\n\t\t}\n\t\treturn false;\n\t}\n\tint dir = ev->action == ACT_NAV_UP ? 1 : ev->action == ACT_NAV_DOWN ? -1 :\n\t\tev->action == ACT_ENC_STEP ? (ev->value > 0 ? 1 : -1) : 0;\n\tif (dir != 0 && ev->gesture != GESTURE_UP) {', 'static bool chinfo_key(const input_event_t *ev)\n{\n\tbool press = ev->gesture == GESTURE_PRESS || ev->gesture == GESTURE_LONG;\n\tif (ced_edit == CE_NONE) {\n\t\tif (ev->action == ACT_BACK && ev->gesture == GESTURE_PRESS && ced_num != 0 &&\n\t\t\t\t(ced_new || ced_changed(CE_COUNT))) {\n\t\t\tdiscard_contact = false;\n\t\t\tui_push(SCR_DISCARD, 0);\n\t\t\treturn true;\n\t\t}\n\t\treturn false;\n\t}\n\tint dir = ev->action == ACT_NAV_UP ? 1 : ev->action == ACT_NAV_DOWN ? -1 :\n\t\tev->action == ACT_ENC_STEP ? (ev->value > 0 ? 1 : -1) : 0;\n#if CONFIG_P25_CHANNELS\n    if (ced_edit == CE_P25_NAC && dir && ev->gesture != GESTURE_UP && ev->action != ACT_ENC_STEP) {\n        ced_len = (uint8_t)((ced_len + dir + 3) % 3);\n        ui_dirty(); return true;\n    }\n#endif\n\tif (dir != 0 && ev->gesture != GESTURE_UP) {'),
        ('if (ced_edit != CE_RX && ced_edit != CE_TX && ced_edit != CE_ID)', 'if (ced_edit != CE_RX && ced_edit != CE_TX && ced_edit != CE_ID\n#if CONFIG_P25_CHANNELS\n\t\t\t&& ced_edit != CE_P25_TG && ced_edit != CE_P25_ID\n#endif\n\t\t\t)'),
        ('\tcase CE_MODE: c->mode = c->mode == RADIO_MODE_DMR ? RADIO_MODE_FM : RADIO_MODE_DMR; break;', '\tcase CE_MODE:\n#if CONFIG_P25_CHANNELS\n\t\tif (c->mode == RADIO_MODE_DMR) {\n\t\t\tc->mode = RADIO_MODE_P25; c->p25_nac = 0x293;\n\t\t\tc->p25_tg = 1; c->p25_id = 1; c->bw = RADIO_BW_12K5;\n\t\t\tc->dmo = c->roam = c->vox = false;\n\t\t} else if (codeplug_mode_p25(c->mode)) {\n\t\t\tc->mode = RADIO_MODE_FM; c->contact = c->listen_list = c->dmr_id = 0;\n\t\t} else\n#endif\n\t\t\tc->mode = c->mode == RADIO_MODE_DMR ? RADIO_MODE_FM : RADIO_MODE_DMR;\n\t\tbreak;\n#if CONFIG_P25_CHANNELS\n\tcase CE_P25_NAC: ced_edit = r; ced_val = c->p25_nac; ced_len = 0; break;\n\tcase CE_P25_TG:\n\tcase CE_P25_ID:\n#endif'),
        ('ced_edit == CE_TX_TONE) && ced_len > 0)', 'ced_edit == CE_TX_TONE\n#if CONFIG_P25_CHANNELS\n             || ced_edit == CE_P25_TG || ced_edit == CE_P25_ID\n#endif\n             ) && ced_len > 0)'),
    ])
    transform('core/ui/ui_screens.c', [
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
    ])
    transform('core/ui/ui_svc.h', [
        ('\tUI_SET_LANGUAGE,\t// the UI\'s language, i18n.h lang_value: "en" English, 0 never picked (Settings > Display; settings_t.lang)\n\tUI_SET_COUNT',
         '\tUI_SET_LANGUAGE,\t// the UI\'s language, i18n.h lang_value: "en" English, 0 never picked (Settings > Display; settings_t.lang)\n\tUI_SET_STANDBY_LED, UI_SET_STANDBY_GAP_S, UI_SET_STANDBY_ON_MS, UI_SET_LED_STYLE, UI_SET_AUTO_SQL, UI_SET_SPOTLIGHT,\n\tUI_SET_COUNT'),
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
         '\tcase UI_SET_TALK_PERMIT:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.talk_permit;\n\tcase UI_SET_STANDBY_LED:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.standby_led;\n\tcase UI_SET_STANDBY_GAP_S:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.standby_gap_s;\n\tcase UI_SET_STANDBY_ON_MS:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.standby_on_ms;\n\tcase UI_SET_LED_STYLE:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.led_style;\n\tcase UI_SET_AUTO_SQL:\n\t\tradio_get_settings(&rs);\n\t\treturn rs.auto_sql;'),
        ('\tcase UI_SET_UPDOWN_HOME:\n\t\tsettings_read(&st);\n\t\treturn settings_updown_home(&st);',
         '\tcase UI_SET_UPDOWN_HOME:\n\t\tsettings_read(&st);\n\t\treturn settings_updown_home(&st);\n\tcase UI_SET_SPOTLIGHT:\n\t\tsettings_read(&st);\n\t\treturn (st.updown_home >> 2) & 0x03u;'),
        ('\tcase UI_SET_UPDOWN_HOME:\n\tcase UI_SET_LOW_BATTERY:',
         '\tcase UI_SET_UPDOWN_HOME:\n\tcase UI_SET_SPOTLIGHT:\n\tcase UI_SET_LOW_BATTERY:'),
        ('\t\t} else if (id == UI_SET_UPDOWN_HOME) {\n\t\t\tst->updown_home = (uint8_t)((st->updown_home & ~SETTINGS_SMALL_MASK) | clamp16(value, 0, 2));',
         '\t\t} else if (id == UI_SET_UPDOWN_HOME) {\n\t\t\tst->updown_home = (uint8_t)((st->updown_home & ~SETTINGS_SMALL_MASK) | clamp16(value, 0, 2));\n\t\t} else if (id == UI_SET_SPOTLIGHT) {\n\t\t\tst->updown_home = (uint8_t)((st->updown_home & ~0x0Cu) | ((clamp16(value, 0, 2) & 0x03u) << 2));'),
        ('\tcase UI_SET_REPLY_S:\n\tcase UI_SET_TALK_PERMIT:',
         '\tcase UI_SET_REPLY_S:\n\tcase UI_SET_TALK_PERMIT:\n\tcase UI_SET_STANDBY_LED:\n\tcase UI_SET_STANDBY_GAP_S:\n\tcase UI_SET_STANDBY_ON_MS:\n\tcase UI_SET_LED_STYLE:\n\tcase UI_SET_AUTO_SQL:'),
    ])
    transform('core/radio/radio_sm.h', [
        ('\tuint32_t batt_ms;\t\t// last battery reading',
         '\tuint32_t batt_ms;\t\t// last battery reading\n\tuint32_t led_now_ms;\t\t// latest poll timestamp for standby LED blink\n\tuint16_t sql_floor_q8;\t\t// adaptive FM noise floor (Q8, 0 = not seeded)\n\tuint8_t sql_settle;\t\t// polls after tune before sampling noise floor'),
        ('void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms);', '''void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms);
uint8_t radio_sm_tx_check(const radio_sm_t *sm);
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
        ('\tsm->set = *s;',
         '\tif (s->auto_sql != sm->set.auto_sql) { sm->sql_floor_q8 = 0; sm->sql_settle = 0; }\n\tsm->set = *s;\n\tsm->view.auto_sql = sm->set.auto_sql;'),
        ('\tsm->view.tot_s = sm->set.tot_s;',
         '\tsm->view.tot_s = sm->set.tot_s;\n\tsm->view.auto_sql = sm->set.auto_sql;'),
        ('\ts->bcl = st->rs_bcl;\n\ts->roger_beep = (st->rs_flags & SETTINGS_RS_ROGER_BEEP) != 0;\n\ts->tx_inhibit = (st->rs_flags & SETTINGS_RS_TX_INHIBIT) != 0;\n\ts->scan_priority = (st->rs_flags & SETTINGS_RS_SCAN_PRIORITY) != 0;\n\ts->scan_beep = (st->rs_flags & SETTINGS_RS_SCAN_BEEP) != 0;\n\ts->key_beep = (st->rs_flags & SETTINGS_RS_KEY_BEEP) != 0;\n\ts->talk_permit = (st->rs_flags & SETTINGS_RS_NO_TALK_PERMIT) == 0;\n\ts->scan_resume = st->rs_scan_resume;',
         '\ts->bcl = st->rs_bcl & 0x03u;\n\ts->roger_beep = (st->rs_flags & SETTINGS_RS_ROGER_BEEP) != 0;\n\ts->tx_inhibit = (st->rs_flags & SETTINGS_RS_TX_INHIBIT) != 0;\n\ts->scan_priority = (st->rs_flags & SETTINGS_RS_SCAN_PRIORITY) != 0;\n\ts->scan_beep = (st->rs_flags & SETTINGS_RS_SCAN_BEEP) != 0;\n\ts->key_beep = (st->rs_flags & SETTINGS_RS_KEY_BEEP) != 0;\n\ts->talk_permit = (st->rs_flags & SETTINGS_RS_NO_TALK_PERMIT) == 0;\n\ts->standby_led = (st->rs_flags & SETTINGS_RS_STANDBY_LED) != 0;\n\ts->auto_sql = (st->rs_flags & SETTINGS_RS_AUTO_SQL) != 0;\n\ts->standby_gap_s = ((st->rs_bcl >> 2) & 0x1Fu) ? (uint8_t)((st->rs_bcl >> 2) & 0x1Fu) : 5u;\n\ts->scan_resume = st->rs_scan_resume & 0x03u;\n\ts->standby_on_ms = ((st->rs_scan_resume >> 2) & 0x1Fu) ? (uint16_t)(((st->rs_scan_resume >> 2) & 0x1Fu) * 50u) : 100u;'),
        ('\ts->rx_save = st->rs_rx_save;',
         '\ts->rx_save = st->rs_rx_save & 0x03u;\n\ts->led_style = (st->rs_rx_save >> 2) & 0x03u;'),
        ('\tst->rs_bcl = s->bcl;\n\tst->rs_flags = (uint8_t)((s->roger_beep ? SETTINGS_RS_ROGER_BEEP : 0) | (s->tx_inhibit ? SETTINGS_RS_TX_INHIBIT : 0) |\n\t\t\t\t (s->scan_priority ? SETTINGS_RS_SCAN_PRIORITY : 0) |\n\t\t\t\t (s->scan_beep ? SETTINGS_RS_SCAN_BEEP : 0) | (s->key_beep ? SETTINGS_RS_KEY_BEEP : 0) |\n\t\t\t\t (s->talk_permit ? 0 : SETTINGS_RS_NO_TALK_PERMIT));\n\tst->rs_scan_resume = s->scan_resume;',
         '\tuint8_t gap = s->standby_gap_s <= 30u ? s->standby_gap_s : 30u;\n\tuint8_t on50 = (uint8_t)(s->standby_on_ms / 50u);\n\tif (on50 > 20u) on50 = 20u;\n\tst->rs_bcl = (uint8_t)((s->bcl & 0x03u) | ((gap & 0x1Fu) << 2));\n\tst->rs_flags = (uint8_t)((s->roger_beep ? SETTINGS_RS_ROGER_BEEP : 0) | (s->tx_inhibit ? SETTINGS_RS_TX_INHIBIT : 0) |\n\t\t\t\t (s->scan_priority ? SETTINGS_RS_SCAN_PRIORITY : 0) |\n\t\t\t\t (s->scan_beep ? SETTINGS_RS_SCAN_BEEP : 0) | (s->key_beep ? SETTINGS_RS_KEY_BEEP : 0) |\n\t\t\t\t (s->talk_permit ? 0 : SETTINGS_RS_NO_TALK_PERMIT) |\n\t\t\t\t (s->standby_led ? SETTINGS_RS_STANDBY_LED : 0) |\n\t\t\t\t (s->auto_sql ? SETTINGS_RS_AUTO_SQL : 0));\n\tst->rs_scan_resume = (uint8_t)((s->scan_resume & 0x03u) | ((on50 & 0x1Fu) << 2));'),
        ('\tst->rs_rx_save = s->rx_save;',
         '\tst->rs_rx_save = (uint8_t)((s->rx_save & 0x03u) | ((s->led_style & 0x03u) << 2));'),
        ('\tmemset(sm, 0, sizeof(*sm));',
         '\tmemset(sm, 0, sizeof(*sm));\n\tsm->led_now_ms = now_ms;'),
        ('\tsm->retune_pending = sm->retune_due = false;\t// this tune is the one they wait for\n#if CONFIG_SFR_CWID\n\tcw_cancel(sm);\t\t// the repeater\'s ID: the board is set up for the channel below\n#endif\n\tend_reception(sm, now_ms);',
         '\tsm->retune_pending = sm->retune_due = false;\t// this tune is the one they wait for\n#if CONFIG_SFR_CWID\n\tcw_cancel(sm);\t\t// the repeater\'s ID: the board is set up for the channel below\n#endif\n\tend_reception(sm, now_ms);\n\tsm->sql_floor_q8 = 0;\n\tsm->sql_settle = 3;'),
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
         '\tif (!dual_peeking(sm)) auto_sql_update(sm, st.noise, sq);\n\tuint8_t eff_sq = squelch_eff_level(sm, sq);\n\tif (eff_sq != sm->view.sql_eff) { sm->view.sql_eff = eff_sq; changed(sm); }\n\tuint8_t open_below = squelch_open_below(sm, sq);'),
        ('void radio_sm_poll(radio_sm_t *sm, uint32_t now_ms)', '''#if CONFIG_P25_CHANNELS
void radio_sm_native_maintenance(radio_sm_t *sm, uint32_t now_ms)
{
\tsm->led_now_ms = now_ms;
\tfollow_volume(sm);
\tbattery_poll(sm, now_ms);
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
         '#define SETTINGS_RS_NO_TALK_PERMIT\t0x20u\t// the talk permit tone off (0 = on, as before)\n#define SETTINGS_RS_STANDBY_LED\t\t0x40u\t// periodic green LED blink while idle/standby\n#define SETTINGS_RS_AUTO_SQL\t\t0x80u\t// adaptive FM squelch from channel noise floor'),
    ])
    transform('core/ui/ui_svc_radio.c', [
        ('\tcase UI_SET_TALK_PERMIT:\n\t\trs.talk_permit = value != 0;\n\t\tbreak;',
         '\tcase UI_SET_TALK_PERMIT:\n\t\trs.talk_permit = value != 0;\n\t\tbreak;\n\tcase UI_SET_STANDBY_LED:\n\t\trs.standby_led = value != 0;\n\t\tbreak;\n\tcase UI_SET_STANDBY_GAP_S:\n\t\trs.standby_gap_s = (uint8_t)(value < 1 ? 1 : value > 30 ? 30 : value);\n\t\tbreak;\n\tcase UI_SET_STANDBY_ON_MS:\n\t\trs.standby_on_ms = (uint16_t)(value < 50 ? 50 : value > 1000 ? 1000 : value);\n\t\tbreak;\n\tcase UI_SET_LED_STYLE:\n\t\trs.led_style = (uint8_t)(value < 0 || value > 2 ? 0 : value);\n\t\tbreak;\n\tcase UI_SET_AUTO_SQL:\n\t\trs.auto_sql = value != 0;\n\t\tbreak;'),
    ])
    transform('core/debuglink/debuglink_view.c', [
        ('v->ch.mode == RADIO_MODE_DMR ? "dmr" : "fm"', 'codeplug_mode_p25(v->ch.mode) ? "p25" : v->ch.mode == RADIO_MODE_DMR ? "dmr" : "fm"'),
        ('c->mode == RADIO_MODE_DMR ? "dmr" : "fm"', 'codeplug_mode_p25(c->mode) ? "p25" : c->mode == RADIO_MODE_DMR ? "dmr" : "fm"'),
    ])
