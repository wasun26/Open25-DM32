# SPDX-License-Identifier: MIT
"""Use upstream mode records directly; reject retired Classic channel formats."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def prepare(dest,write):
    vendor=ROOT/'third_party/rf_codeplug/core'
    for p in vendor.rglob('*'):
        if p.suffix in ('.c','.h'):write(dest/'core'/p.relative_to(vendor),p.read_bytes())
    p=dest/'core/codeplug/codeplug_modes.h';s=p.read_text().replace('X(2, P25, \"P25\", 0)', 'X(2, P25, \"P25\", CONFIG_P25_CHANNELS)');write(p,s.encode())
    p=dest/'core/codeplug/codeplug.h';s=p.read_text();s=s.replace('#include \"config.h\"','#include \"config.h\"\n#include \"codeplug_modes.h\"');s=s.replace('} channel_t;','\tuint8_t kind,system; /* upstream channel mode tag */\n} channel_t;')
    s=s.replace('ch->p25_id <= 9999999u','ch->p25_id <= 0xFFFFFEu')
    s=s.replace('return ch->p25_nac;', 'return ch->p25_nac==0xF7Eu?0x293u:ch->p25_nac;')
    at='#if CONFIG_P25_CHANNELS\nstatic inline uint16_t codeplug_p25_tx_nac'
    s=s.replace(at,'#if CONFIG_P25_CHANNELS\nuint32_t codeplug_p25_unit_id(uint32_t);\nvoid codeplug_p25_params_encode(const channel_t*,uint8_t[9]);\nbool codeplug_p25_params_decode(const uint8_t*,uint16_t,channel_t*);\nstatic inline uint16_t codeplug_p25_tx_nac')
    write(p,s.encode())
    p=dest/'core/codeplug/codeplug.c';s=p.read_text();s=s.replace('#include "codeplug.h"','#include "codeplug.h"\n#include "codeplug_tag.h"\n#if CONFIG_P25_CHANNELS\nstatic bool incompatible_mode_records;\nstatic bool classic_standard_load(uint16_t,channel_t*);\nstatic bool classic_standard_store(uint16_t,const channel_t*,bool);\n#endif')
    # Unsupported mode bytes never become FM/DMR and require explicit Start new.
    s=s.replace('\tchannel_max = 0;\n\tzone_count = 0;', '\tchannel_max = 0;\n\tzone_count = 0;\n#if CONFIG_P25_CHANNELS\n    incompatible_mode_records=false;\n#endif')
    s=s.replace('\t\t\t\tnamed(&r[CH_NAME]);', '#if CONFIG_P25_CHANNELS\n                if(r[CH_MODE]!=0u && r[CH_MODE]!=1u) incompatible_mode_records=true;\n#endif\n\t\t\t\tnamed(&r[CH_NAME]);', 1)
    s=s.replace('return codeplug_acceptance() != CODEPLUG_ACCEPT_YES && ev_bad != 0 && (uint32_t)ev_bad * 4u > ev_good;', '#if CONFIG_P25_CHANNELS\n    if(incompatible_mode_records) return true;\n#endif\n\treturn codeplug_acceptance() != CODEPLUG_ACCEPT_YES && ev_bad != 0 && (uint32_t)ev_bad * 4u > ev_good;')
    s=s.replace('\t\tev_bad = 0;\t// cleared:', '#if CONFIG_P25_CHANNELS\n        if(ok) incompatible_mode_records=false;\n#endif\n\t\tev_bad = 0;\t// cleared:')
    needle='void codeplug_openfw_encode(const channel_t *ch, uint8_t *r)\n{'
    assert needle in s
    s=s.replace(needle,needle+'''\n#if CONFIG_P25_CHANNELS
    channel_t base=*ch;
    if(codeplug_mode_p25(ch->mode)) {
        base.mode=RADIO_MODE_DMR;base.rx_only=true;
        base.dmr_id=ch->kind==CODEPLUG_KIND_P25?ch->dmr_id:ch->p25_id;base.color_code=1;base.rx_tone=base.tx_tone=(subtone_t){0};
        ch=&base;
    }
#endif''')
    needle='ok = openfw_channel(number, out);'
    s=s.replace(needle,needle+'\n#if CONFIG_P25_CHANNELS\n\t\tif(ok) ok=classic_standard_load(number,out);\n#endif')
    needle='\t\tcodeplug_openfw_encode(ch, r);'
    s=s.replace(needle,"""#if CONFIG_P25_CHANNELS
        char old_name[CODEPLUG_NAME_LEN+1];copy_name(old_name,r,CODEPLUG_NAME_LEN);
        channel_t tagged=*ch;
        if(codeplug_mode_p25(ch->mode)) tagged.kind=CODEPLUG_KIND_P25;
        else if(tagged.kind==CODEPLUG_KIND_P25) tagged.kind=CODEPLUG_KIND_NONE;
        codeplug_tag_t previous_tag;bool safe_shape;
        uint8_t tag_action=codeplug_tag_before_write(number,&tagged,is_used(number),old_name,&previous_tag,&safe_shape);
#endif
"""+needle+"\n#if CONFIG_P25_CHANNELS\n        if(safe_shape){r[CH_MODE]=1u;r[CH_FLAG4]|=4u;}\n#endif")
    needle='ok = flash_write(channel_addr(number), r, sizeof(r));'
    assert s.count(needle)==1
    s=s.replace(needle,needle+'\n#if CONFIG_P25_CHANNELS\n\t\tif(ok) ok=codeplug_mode_p25(ch->mode)?classic_standard_store(number,ch,tag_action==CODEPLUG_TAG_W_FOLLOW):codeplug_tag_after_write(number,&tagged,tag_action,&previous_tag);\n#endif')
    needle='bool ok = materialize();\n\tif (ok && is_used(number))'
    assert needle in s
    delete_start=s.index('bool codeplug_delete_channel(');delete_end=s.index('\nstatic bool write_zone_bitmap',delete_start)
    deletion=s[delete_start:delete_end].replace('\tif (ok) {\n\t\tfind_max();','#if CONFIG_P25_CHANNELS\n\tif(ok) ok=codeplug_tag_remove(number);\n#endif\n\tif (ok) {\n\t\tfind_max();')
    s=s[:delete_start]+deletion+s[delete_end:]
    s+='\n#include "classic_standard.inc"\n';write(p,s.encode())
    write(dest/'core/codeplug/classic_standard.inc',(ROOT/'integration/radiofoundry/codeplug/classic_standard.inc').read_bytes())
    # Reuse upstream's protection for keyed settings when a raw file lacks them.
    for name in ['cps_openfw.c','cps_fast.c']:
        p=dest/'core/cps'/name;s=p.read_text();s=s.replace('#include "config.h"','#include "config.h"\n#include "codeplug/codeplug_kv.h"')
        if 'codeplug/codeplug_kv.h' not in s:s='#include "codeplug/codeplug_kv.h"\n'+s
        s=s.replace('storage_sector_write(', 'codeplug_kv_commit(');write(p,s.encode(),(ROOT/'research/upstream/radiofoundry/core/cps'/name).read_bytes())
    p=dest/'core/debuglink/debuglink.c';s=p.read_text();s=s.replace('dl_text(&t, "features=%s\\n", build_features);','''#if CONFIG_P25_CHANNELS
    dl_text(&t,"codeplug=rfkv1\\ncodeplug_chain=1\\n");
#endif
    dl_text(&t, "features=%s\\n", build_features);''');write(p,s.encode())

    p=dest/'tests/host/Makefile';s=p.read_text()
    if 'codeplug/codeplug_kv.c' not in s:
        s=s.replace('$(CORE)/cps/cps_openfw.c','$(CORE)/cps/cps_openfw.c $(CORE)/codeplug/codeplug_kv.c $(CORE)/codeplug/codeplug_tag.c')
    write(p,s.encode(),(ROOT/'research/upstream/radiofoundry/tests/host/Makefile').read_bytes())

    p=dest/'core/ui/ui_menus.c';s=p.read_text();s=s.replace('case CE_P25_ID: if (ced_len) c->p25_id = (uint32_t)ced_val;', 'case CE_P25_ID: if (ced_len) { c->dmr_id = (uint32_t)ced_val; c->p25_id=codeplug_p25_unit_id(c->dmr_id); }');write(p,s.encode())

    p=dest/'core/ui/ui_menus.c';s=p.read_text();s=s.replace('ced_edit == CE_P25_TG ? 65535 : 9999999','ced_edit == CE_P25_TG ? 65535 : 0xFFFFFE');s=s.replace('[CE_P25_ID] = CE_FIELD(p25_id)', '[CE_P25_ID] = CE_FIELD(dmr_id)');s=s.replace('case CE_P25_ID: sb_u32(&b, ch->p25_id, 1); break;', 'case CE_P25_ID: if(ch->dmr_id) sb_u32(&b,ch->dmr_id,1); else sb_str(&b,TR("Default")); break;');write(p,s.encode())

    p=dest/'core/ui/ui_menus.c';s=p.read_text()
    s=s.replace('case CE_P25_TG: if (ced_len) c->p25_tg = (uint16_t)ced_val;',
                'case CE_P25_TG: if (ced_len) { c->p25_tg = (uint16_t)ced_val; c->contact=c->listen_list=0; }')
    s=s.replace('c->p25_tg = 1; c->p25_squelch=1;', 'c->p25_tg = 1; c->contact=c->listen_list=0; c->p25_squelch=1;')
    write(p,s.encode())

    p=dest/'core/ui/ui_menus.c';s=p.read_text();s=s.replace('c->p25_tg = 1; c->p25_id = 1; c->bw', 'c->p25_tg = 1; c->p25_id = 1; c->dmr_id=1; c->kind=CODEPLUG_KIND_P25; c->system=0xFF; c->bw');s=s.replace('c->mode = RADIO_MODE_FM; c->contact = c->listen_list = c->dmr_id = 0;', 'c->mode = RADIO_MODE_FM; c->kind=CODEPLUG_KIND_NONE; c->system=0xFF; c->contact = c->listen_list = c->dmr_id = 0;');write(p,s.encode())
