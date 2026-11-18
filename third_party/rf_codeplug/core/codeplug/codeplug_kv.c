// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The codeplug settings area (docs/spec/codeplug-settings.md). Reads walk the flash a few bytes at a time (no sector
// in RAM, and nothing of the area kept between calls); a write applies a batch of sets and removes to the sector in
// the storage's shared buffer and rewrites it through the journal in one step, so a power cut leaves the old area or
// the new one
#include <string.h>
#include "codeplug.h"
#include "codeplug_kv.h"
#include "board.h"
#include "storage/storage.h"

#define ENTRY_HDR	4u	// key, length

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

// None while the open-firmware area holds another firmware's codeplug (codeplug_foreign): that is shown as no
// codeplug, so its bytes at 0xE000 aren't read as our keys nor written over before the user has answered
static bool area_addr(uint32_t *addr)
{
	const hal_flash_map_t *m = storage_map();
	if (m == NULL || codeplug_foreign() || board.flash->size == 0 || m->openfw_eeprom + CODEPLUG_KV_ADDR + STORAGE_SECTOR > board.flash->size) {
		return false;
	}
	*addr = m->openfw_eeprom + CODEPLUG_KV_ADDR;
	return true;
}

// The header's state; on CODEPLUG_KV_VALID the header length, data length and CRC it names (the entries unchecked)
static codeplug_kv_state_t header(const uint8_t h[CODEPLUG_KV_HDR], uint16_t *hdr, uint16_t *len, uint32_t *crc)
{
	if (get32(h) != CODEPLUG_KV_MAGIC) {
		for (unsigned i = 0; i < CODEPLUG_KV_HDR; i++) {
			if (h[i] != 0xFF) {
				return CODEPLUG_KV_BAD;
			}
		}
		return CODEPLUG_KV_EMPTY;
	}
	if (h[4] != CODEPLUG_KV_VERSION) {
		// No version 0 was ever written: a damaged byte, not a later firmware's area that must be left alone
		return h[4] > CODEPLUG_KV_VERSION ? CODEPLUG_KV_NEWER : CODEPLUG_KV_BAD;
	}
	*hdr = h[5];
	*len = get16(&h[6]);
	*crc = get32(&h[8]);
	return *hdr >= CODEPLUG_KV_HDR && (uint32_t)*hdr + *len <= STORAGE_SECTOR ? CODEPLUG_KV_VALID : CODEPLUG_KV_BAD;
}

// Every entry inside the data: true when the walk ends exactly at its end; *count entries
static bool walk_ok(const uint8_t *data, uint16_t len, uint16_t *count)
{
	uint32_t at = 0;
	*count = 0;
	while (at < len) {
		if (len - at < ENTRY_HDR || get16(&data[at + 2]) > len - at - ENTRY_HDR) {
			return false;
		}
		at += ENTRY_HDR + get16(&data[at + 2]);
		(*count)++;
	}
	return true;
}

// The area in flash, checked (magic, version, bounds, CRC, every entry inside the data). On CODEPLUG_KV_VALID: where
// the data starts, its length and its CRC. Under storage_lock
codeplug_kv_state_t codeplug_kv_check(const uint8_t *sector)
{
	uint16_t hdr, len, count;
	uint32_t crc;
	codeplug_kv_state_t s = header(sector, &hdr, &len, &crc);
	if (s != CODEPLUG_KV_VALID) {
		return s;
	}
	return storage_crc32(0, &sector[hdr], len) == crc && walk_ok(&sector[hdr], len, &count) ? CODEPLUG_KV_VALID :
												 CODEPLUG_KV_BAD;
}

/* RFKV v1 continuation, using the header's existing u32 link. Links are combined image offsets. */
#define KV_BANK_A 0xAF000u
#define KV_BANK_B 0xBF000u
#define KV_BANK_SIZE 0x10000u
#define KV_BANK_PAGES 16u
#define KV_CHAIN_PAGES 33u
static bool page_addr(uint32_t off,uint32_t *addr)
{
 const hal_flash_map_t *m=storage_map();
 if(!m||codeplug_foreign()||!board.flash->size)return false;
 if(off==CODEPLUG_KV_ADDR)*addr=m->openfw_eeprom+off;
 else if(off>=KV_BANK_A&&off<KV_BANK_B+KV_BANK_SIZE&&!(off&(STORAGE_SECTOR-1u)))
  *addr=m->openfw_flash+off-0x20000u;
 else return false;
 return *addr<=board.flash->size&&STORAGE_SECTOR<=board.flash->size-*addr&&*addr+STORAGE_SECTOR<=m->openfw_end;
}
static codeplug_kv_state_t page_check(uint32_t off,uint16_t *hdr,uint16_t *len,uint32_t *crc,uint32_t *next)
{
 uint32_t a;uint8_t b[16];
 if(!page_addr(off,&a)||!board.flash->read(a,b,sizeof b))return CODEPLUG_KV_BAD;
 codeplug_kv_state_t state=header(b,hdr,len,crc);if(state!=CODEPLUG_KV_VALID)return state;
 *next=get32(b+12);uint32_t sum=0;
 for(uint32_t at=0;at<*len;){unsigned n=*len-at<sizeof b?*len-at:sizeof b;if(!board.flash->read(a+*hdr+at,b,n))return CODEPLUG_KV_BAD;sum=storage_crc32(sum,b,n);at+=n;}
 if(sum!=*crc)return CODEPLUG_KV_BAD;
 for(uint32_t at=0;at<*len;){if(*len-at<4||!board.flash->read(a+*hdr+at,b,4)||get16(b+2)>*len-at-4)return CODEPLUG_KV_BAD;at+=4+get16(b+2);}
 return CODEPLUG_KV_VALID;
}
static codeplug_kv_state_t chain_check(uint32_t *length,uint16_t *count,uint32_t *banks)
{
 uint32_t off=CODEPLUG_KV_ADDR,seen=0;*length=0;*count=0;*banks=0;
 for(unsigned page=0;page<KV_CHAIN_PAGES;page++){
  uint16_t hdr=0,len=0;uint32_t crc=0,next=0,a;
  if(!page_addr(off,&a))return page?CODEPLUG_KV_NEWER:CODEPLUG_KV_EMPTY;
  codeplug_kv_state_t state=page_check(off,&hdr,&len,&crc,&next);
  if(state!=CODEPLUG_KV_VALID)return page&&state==CODEPLUG_KV_EMPTY?CODEPLUG_KV_BAD:state;
  *length+=len;
  for(uint32_t at=0;at<len;){uint8_t e[4];if(!board.flash->read(a+hdr+at,e,4))return CODEPLUG_KV_BAD;at+=4+get16(e+2);(*count)++;}
  if(!next)return CODEPLUG_KV_VALID;
  if(next&(STORAGE_SECTOR-1u))return CODEPLUG_KV_BAD;
  if(next<KV_BANK_A||next>=KV_BANK_B+KV_BANK_SIZE)return CODEPLUG_KV_NEWER;
  unsigned bit=(next-KV_BANK_A)/STORAGE_SECTOR;
  if(seen&(1u<<bit))return CODEPLUG_KV_BAD;
  seen|=1u<<bit;*banks|=next<KV_BANK_B?1u:2u;off=next;
 }
 return CODEPLUG_KV_BAD;
}
typedef struct{uint32_t page,next,crc;uint16_t hdr,len,at;bool done,started;} kv_walk_t;
typedef struct{uint16_t key,len;uint32_t addr;} kv_entry_t;
static bool raw_next(kv_walk_t *w,kv_entry_t *e)
{
 while(!w->done){
  if(!w->started||w->at==w->len){
   uint32_t off=w->started?w->next:CODEPLUG_KV_ADDR;if(w->started&&!off){w->done=true;break;}
   w->page=off;w->at=0;w->started=true;
   if(page_check(off,&w->hdr,&w->len,&w->crc,&w->next)!=CODEPLUG_KV_VALID){w->done=true;break;}
   if(!w->len)continue;
  }
  uint32_t a;uint8_t b[4];if(!page_addr(w->page,&a)||!board.flash->read(a+w->hdr+w->at,b,4)){w->done=true;break;}
  e->key=get16(b);e->len=get16(b+2);e->addr=a+w->hdr+w->at+4;w->at=(uint16_t)(w->at+4+e->len);return true;
 }
 return false;
}
static bool first_entry(uint16_t key,kv_entry_t *out)
{
 kv_walk_t walk={0};while(raw_next(&walk,out))if(out->key==key)return true;return false;
}

void codeplug_kv_begin(codeplug_kv_cursor_t *c, uint16_t lo, uint16_t hi)
{
	memset(c, 0, sizeof(*c));
	c->lo = lo != 0 ? lo : 1u;		// 0x0000 and 0xFFFF are no keys
	c->hi = hi != 0xFFFFu ? hi : 0xFFFEu;
	c->first = CODEPLUG_KV_NONE;
	c->done = c->lo > c->hi;
}

// Where the walk's data starts: the area checked whole at the first call, its header compared at each later one (a
// rewrite gives it a new CRC or length), so a walk never reads entries at offsets of an area that is gone. Under
// storage_lock
int codeplug_kv_next(codeplug_kv_cursor_t *c, uint16_t *key, void *buf, uint16_t max)
{
 int found=-1;storage_lock();
 if(!c->done){
  uint32_t len,banks;uint16_t entries;
  if(!c->started){c->first=codeplug_kv_serial();if(chain_check(&len,&entries,&banks)!=CODEPLUG_KV_VALID)c->done=true;else{uint8_t h[16];uint32_t a;if(c->first!=codeplug_kv_serial()||!area_addr(&a)||!board.flash->read(a,h,sizeof h))c->done=true;else{c->root_crc=get32(h+8);c->root_next=get32(h+12);}}}
  else{uint8_t h[16];uint32_t a;if(c->first!=codeplug_kv_serial()||!area_addr(&a)||!board.flash->read(a,h,sizeof h)||get32(h+8)!=c->root_crc||get32(h+12)!=c->root_next)c->done=true;}
  kv_walk_t walk={c->page,c->next,c->crc,c->hdr,c->len,c->at,c->done,c->started};kv_entry_t e;
  while(!c->done&&raw_next(&walk,&e)){
   if(e.key<c->lo||e.key>c->hi)continue;
   if(e.key<=c->greatest){kv_entry_t first;if(!first_entry(e.key,&first)||first.addr!=e.addr)continue;}else c->greatest=e.key;
   if(!board.flash->read(e.addr,buf,e.len<max?e.len:max))break;
   *key=e.key;found=e.len;break;
  }
  c->page=walk.page;c->next=walk.next;c->crc=walk.crc;c->hdr=(uint8_t)walk.hdr;c->len=walk.len;c->at=walk.at;c->started=walk.started;c->done=found<0;
 }
 storage_unlock();return found;
}

static uint8_t kv_serial;	// codeplug_kv_serial(): one byte, only compared for a change

uint8_t codeplug_kv_serial(void)
{
	return (uint8_t)(kv_serial + codeplug_serial());	// both only go up: the sum moves when either does
}

int codeplug_kv_get(uint16_t key, void *buf, uint16_t max)
{
	codeplug_kv_cursor_t c;
	uint16_t k;
	codeplug_kv_begin(&c, key, key);
	return codeplug_kv_next(&c, &k, buf, max);
}

bool codeplug_kv_get_id(uint16_t key, uint32_t *id)
{
	uint8_t v[CODEPLUG_KV_SCALAR];
	return codeplug_kv_id(v, codeplug_kv_get(key, v, sizeof(v)), id);
}

codeplug_kv_state_t codeplug_kv_info(uint16_t *length,uint16_t *entries)
{
 uint32_t len,banks;uint16_t count;storage_lock();codeplug_kv_state_t state=chain_check(&len,&count,&banks);storage_unlock();*length=state==CODEPLUG_KV_VALID?(uint16_t)(len>0xFFFFu?0xFFFFu:len):0;*entries=state==CODEPLUG_KV_VALID?count:0;return state;
}
bool codeplug_kv_keep(uint32_t addr,const uint8_t *sector)
{
 uint32_t root;const hal_flash_map_t *m=storage_map();if(!m||!area_addr(&root))return false;
 uint32_t off=addr==root?CODEPLUG_KV_ADDR:addr>=m->openfw_flash?addr-m->openfw_flash+0x20000u:0,a;
 if(!page_addr(off,&a)||a!=addr)return false;
 uint16_t h,l;uint32_t c,n;codeplug_kv_state_t state=page_check(off,&h,&l,&c,&n);
 if(off==CODEPLUG_KV_ADDR){uint32_t length,banks;uint16_t count;state=chain_check(&length,&count,&banks);}
 return state==CODEPLUG_KV_NEWER||(state==CODEPLUG_KV_VALID&&codeplug_kv_check(sector)!=CODEPLUG_KV_VALID);
}

bool codeplug_kv_commit(uint32_t addr, const uint8_t *sector)
{
	return codeplug_kv_keep(addr, sector) || storage_sector_write(addr, sector);
}

// Whether an entry before the one at end has key k
static bool seen(const uint8_t *d, uint32_t end, uint16_t k)
{
	for (uint32_t at = 0; at < end; at += ENTRY_HDR + get16(&d[at + 2])) {
		if (get16(&d[at]) == k) {
			return true;
		}
	}
	return false;
}

// Take out the entry at at, which ends at next: the entries after it close the gap
static uint32_t cut(uint8_t *d, uint32_t len, uint32_t at, uint32_t next)
{
	memmove(&d[at], &d[next], len - next);
	return len - (next - at);
}

// One op on the entries d[0..*len): a set replaces the key's first entry in place or appends one (with
// CODEPLUG_KV_HEAD only its first bytes, the stored value's later ones kept), a remove takes out every entry of the
// key. *changed when a byte changed. False if a set doesn't fit in room
static bool apply(uint8_t *d, uint16_t *len, uint32_t room, const codeplug_kv_op_t *o, bool *changed)
{
	uint32_t l = *len, at = 0;
	codeplug_kv_op_t set = *o;
	const codeplug_kv_op_t *op = &set;
	bool head = set.len != CODEPLUG_KV_REMOVE && (set.len & CODEPLUG_KV_HEAD) != 0;
	if (head) {
		set.len &= (uint16_t)~CODEPLUG_KV_HEAD;
	}
	while (at < l && get16(&d[at]) != op->key) {
		at += ENTRY_HDR + get16(&d[at + 2]);
	}
	if (op->len == CODEPLUG_KV_REMOVE) {
		while (at < l) {
			uint32_t next = at + ENTRY_HDR + get16(&d[at + 2]);
			if (get16(&d[at]) == op->key) {
				l = cut(d, l, at, next);
				*changed = true;
			} else {
				at = next;
			}
		}
	} else if (at < l) {
		uint16_t n = get16(&d[at + 2]);
		uint32_t next = at + ENTRY_HDR + n;
		if (head && n > op->len) {	// the fields this writer knows; the later ones stay
			if (op->len != 0 && memcmp(&d[at + ENTRY_HDR], op->value, op->len) != 0) {
				memcpy(&d[at + ENTRY_HDR], op->value, op->len);
				*changed = true;
			}
		} else if (n != op->len || (n != 0 && memcmp(&d[at + ENTRY_HDR], op->value, n) != 0)) {
			if (l - n + op->len > room) {
				return false;
			}
			memmove(&d[at + ENTRY_HDR + op->len], &d[next], l - next);
			put16(&d[at + 2], op->len);
			if (op->len != 0) {
				memcpy(&d[at + ENTRY_HDR], op->value, op->len);
			}
			l = l - n + op->len;
			*changed = true;
		}
	} else {
		if (l + ENTRY_HDR + op->len > room) {
			return false;
		}
		put16(&d[l], op->key);
		put16(&d[l + 2], op->len);
		if (op->len != 0) {
			memcpy(&d[l + ENTRY_HDR], op->value, op->len);
		}
		l += ENTRY_HDR + op->len;
		*changed = true;
	}
	*len = (uint16_t)l;
	return true;
}

// Apply the ops to the area in b (a whole sector); with *changed, b is the new area: later entries of any key dropped
// (no reader sees them), the rest 0xFF, the header and CRC made. A sector that isn't a valid area starts a new one.
// False if an op doesn't fit or the area is a newer version
static bool edit(uint8_t *b, const codeplug_kv_op_t *ops, uint8_t n, bool *changed)
{
	uint16_t hdr = CODEPLUG_KV_HDR, len = 0;
	uint32_t crc;
	codeplug_kv_state_t s = codeplug_kv_check(b);
	if (s == CODEPLUG_KV_NEWER) {
		return false;	// a later firmware's area: never ours to rewrite
	}
	if (s == CODEPLUG_KV_VALID) {
		header(b, &hdr, &len, &crc);
	} else {
		memset(b, 0, CODEPLUG_KV_HDR);	// nothing in it could be read: start over
	}
	uint8_t *d = &b[hdr];
	uint32_t room = STORAGE_SECTOR - hdr;
	*changed = false;
	for (uint8_t i = 0; i < n; i++) {
		if (!apply(d, &len, room, &ops[i], changed)) {
			return false;
		}
	}
	if (!*changed) {
		return true;
	}
	for (uint32_t at = 0; at < len; ) {
		uint32_t next = at + ENTRY_HDR + get16(&d[at + 2]);
		if (seen(d, at, get16(&d[at]))) {
			len = (uint16_t)cut(d, len, at, next);
		} else {
			at = next;
		}
	}
	memset(&d[len], 0xFF, room - len);	// erased, as the sector after the data
	put32(&b[0], CODEPLUG_KV_MAGIC);
	b[4] = CODEPLUG_KV_VERSION;
	b[5] = (uint8_t)hdr;
	put16(&b[6], len);
	put32(&b[8], storage_crc32(0, d, len));
	// bytes 12 up to hdr stay as found (spec 2.4: the continuation, a later version's header fields); a new area's are 0
	return true;
}

/* A virtual edited stream: keep upstream ordering, prefix updates, and first-key precedence without a second data buffer. */
typedef struct{bool present;uint16_t len;int append;} kv_final_t;
static kv_final_t final_value(uint16_t key,const kv_entry_t *old,bool exists,const codeplug_kv_op_t *ops,uint8_t count)
{
 kv_final_t f={exists,exists?old->len:0u,exists?-1:-2};
 for(unsigned i=0;i<count;i++)if(ops[i].key==key){
  if(ops[i].len==CODEPLUG_KV_REMOVE){f.present=false;f.len=0;f.append=-2;}
  else{uint16_t n=ops[i].len&~CODEPLUG_KV_HEAD;if(!f.present){f.append=(int)i;f.len=n;}else if(!(ops[i].len&CODEPLUG_KV_HEAD)||n>f.len)f.len=n;f.present=true;}
 }
 return f;
}
typedef struct{kv_walk_t walk;unsigned op;bool append;uint8_t seen[32];} kv_output_t;
static bool input_unique(uint8_t *bits){for(unsigned half=0;half<2;half++){memset(bits,0,4096);kv_walk_t w={0};kv_entry_t e;while(raw_next(&w,&e))if((e.key>>15)==half){unsigned k=e.key&0x7FFFu;if(bits[k>>3]&(1u<<(k&7u)))return false;bits[k>>3]|=(uint8_t)(1u<<(k&7u));}}return true;}
static bool output_next(kv_output_t *it,const codeplug_kv_op_t *ops,uint8_t count,kv_entry_t *old,kv_final_t *f,bool unique)
{
 while(!it->append){
  if(!raw_next(&it->walk,old)){it->append=true;break;}
  if(!unique){kv_entry_t first;if(!first_entry(old->key,&first)||first.addr!=old->addr)continue;}
  for(unsigned i=0;i<count;i++)if(ops[i].key==old->key)it->seen[i>>3]|=(uint8_t)(1u<<(i&7u));
  *f=final_value(old->key,old,true,ops,count);if(f->present&&f->append==-1)return true;
 }
 while(it->op<count){unsigned i=it->op++;if(it->seen[i>>3]&(1u<<(i&7u))){bool removed=false;for(unsigned j=0;j<count;j++)if(ops[j].key==ops[i].key&&ops[j].len==CODEPLUG_KV_REMOVE)removed=true;if(!removed)continue;}kv_entry_t first={.key=ops[i].key};*f=final_value(ops[i].key,&first,false,ops,count);if(f->present&&f->append==(int)i){*old=first;return true;}}
 return false;
}
static bool output_bytes(uint8_t *dst,const kv_entry_t *old,uint16_t len,const codeplug_kv_op_t *ops,uint8_t count)
{
 memset(dst,0xFF,len);
 if(old->addr&&old->len&&!board.flash->read(old->addr,dst,old->len<len?old->len:len))return false;
 for(unsigned i=0;i<count;i++)if(ops[i].key==old->key&&ops[i].len!=CODEPLUG_KV_REMOVE){uint16_t n=ops[i].len&~CODEPLUG_KV_HEAD;if(n>len)n=len;if(n)memcpy(dst,ops[i].value,n);}
 return true;
}
static bool unchanged_ops(const codeplug_kv_op_t *ops,uint8_t count,uint8_t *buffer)
{
 for(unsigned i=0;i<count;i++){
  bool repeated=false;for(unsigned j=0;j<i;j++)if(ops[j].key==ops[i].key)repeated=true;if(repeated)continue;
  kv_entry_t old={.key=ops[i].key};bool exists=first_entry(ops[i].key,&old);kv_final_t final=final_value(ops[i].key,&old,exists,ops,count);
  if(final.present!=exists)return false;
  if(!exists)continue;
  if(final.append!=-1||final.len!=old.len||!output_bytes(buffer,&old,final.len,ops,count))return false;
  uint8_t part[32];for(unsigned at=0;at<old.len;at+=sizeof part){unsigned n=old.len-at<sizeof part?old.len-at:sizeof part;if(!board.flash->read(old.addr+at,part,n)||memcmp(part,buffer+at,n))return false;}
 }
 return true;
}
static int single_page_ops(const codeplug_kv_op_t *ops,uint8_t count,uint8_t *buffer,bool *changed)
{
 if(count!=1)return -1;
 kv_walk_t walk={0};kv_entry_t entry,chosen={0};unsigned found=0;
 while(raw_next(&walk,&entry))if(entry.key==ops[0].key){chosen=entry;found++;}
 if(found!=1)return -1;
 uint32_t addr=chosen.addr&~(STORAGE_SECTOR-1u);
 if(!board.flash->read(addr,buffer,STORAGE_SECTOR)||!edit(buffer,ops,count,changed))return -1;
 return !*changed||storage_sector_write(addr,buffer);
}
static void page_pack(uint8_t *b,uint16_t hdr,uint16_t len,uint32_t next)
{
 put32(b,CODEPLUG_KV_MAGIC);b[4]=CODEPLUG_KV_VERSION;b[5]=(uint8_t)hdr;put16(b+6,len);put32(b+8,storage_crc32(0,b+hdr,len));put32(b+12,next);memset(b+hdr+len,0xFF,STORAGE_SECTOR-hdr-len);
}
static bool root_same(const uint8_t *b,uint32_t a)
{
 uint8_t x[32];for(unsigned at=0;at<STORAGE_SECTOR;at+=sizeof x){if(!board.flash->read(a+at,x,sizeof x)||memcmp(b+at,x,sizeof x))return false;}return true;
}
static bool write_chain(const codeplug_kv_op_t *ops,uint8_t count,codeplug_kv_state_t state,uint32_t used_banks,uint8_t *buffer,bool *changed)
{
 if(used_banks==3u)return false;
 uint32_t bank=used_banks==1u?KV_BANK_B:KV_BANK_A;
 bool unique=state!=CODEPLUG_KV_VALID||input_unique(buffer);
 /* Preserve unknown header extensions of each logical page, including when pages move to the other bank. */
 uint32_t old_offsets[KV_BANK_PAGES+1];uint8_t headers[KV_BANK_PAGES+1];unsigned old_pages=0;
 uint32_t off=state==CODEPLUG_KV_VALID?CODEPLUG_KV_ADDR:0;
 while(off){if(old_pages>KV_BANK_PAGES)return false;uint16_t h,l;uint32_t c,next;if(page_check(off,&h,&l,&c,&next)!=CODEPLUG_KV_VALID)return false;old_offsets[old_pages]=off;headers[old_pages++]=(uint8_t)h;off=next;}
 for(unsigned i=old_pages;i<=KV_BANK_PAGES;i++){old_offsets[i]=0;headers[i]=CODEPLUG_KV_HDR;}
 /* First pass: calculate page boundaries and reject capacity errors before touching flash. */
 unsigned pages=1;uint16_t used=0;kv_output_t iter={0};kv_entry_t old;kv_final_t final;
 if(state!=CODEPLUG_KV_VALID)iter.append=true;
 while(output_next(&iter,ops,count,&old,&final,unique)){
  uint32_t n=4u+final.len;if(n>STORAGE_SECTOR-CODEPLUG_KV_HDR)return false;
  if(used+n>STORAGE_SECTOR-headers[pages-1]){if(++pages>KV_BANK_PAGES+1u)return false;used=0;}
  if(n>STORAGE_SECTOR-headers[pages-1])return false;
  used=(uint16_t)(used+n);
 }
 uint32_t root_addr;if(!area_addr(&root_addr))return false;
 /* Generate each page independently with the one shared sector buffer. Root is written last. */
 for(unsigned pass=0;pass<pages;pass++){
  unsigned target=pass+1u<pages?pass+1u:0u;
  memset(buffer,0xFF,STORAGE_SECTOR);uint16_t h=headers[target];
  if(old_offsets[target]){uint32_t a;if(!page_addr(old_offsets[target],&a)||!board.flash->read(a,buffer,h))return false;}else memset(buffer,0,h);
  iter=(kv_output_t){0};if(state!=CODEPLUG_KV_VALID)iter.append=true;
  unsigned page=0;uint16_t at=headers[0];
  while(output_next(&iter,ops,count,&old,&final,unique)){
   uint32_t n=4u+final.len;if(at+n>STORAGE_SECTOR){page++;at=headers[page];}
   if(page==target){put16(buffer+at,old.key);put16(buffer+at+2,final.len);if(!output_bytes(buffer+at+4,&old,final.len,ops,count))return false;}
   at=(uint16_t)(at+n);if(page>target)break;
  }
  /* Recompute this page's actual payload length without relying on the following page's cursor. */
  iter=(kv_output_t){0};if(state!=CODEPLUG_KV_VALID)iter.append=true;page=0;at=headers[0];uint16_t payload=0;
  while(output_next(&iter,ops,count,&old,&final,unique)){uint32_t n=4u+final.len;if(at+n>STORAGE_SECTOR){page++;at=headers[page];}if(page==target)payload=(uint16_t)(payload+n);if(page>target)break;at=(uint16_t)(at+n);}
  uint32_t next=target+1u<pages?bank+target*STORAGE_SECTOR:0;page_pack(buffer,h,payload,next);
  uint32_t dest=target?bank+(target-1u)*STORAGE_SECTOR:CODEPLUG_KV_ADDR,a;if(!page_addr(dest,&a))return false;
  if(target||!root_same(buffer,root_addr)){if(!storage_sector_write(a,buffer))return false;*changed=true;}
  if(target){uint16_t vh,vl;uint32_t vc,vn;if(page_check(dest,&vh,&vl,&vc,&vn)!=CODEPLUG_KV_VALID||vh!=h||vl!=payload||vn!=next||vc!=get32(buffer+8))return false;}
 }
 return true;
}

bool codeplug_kv_write(const codeplug_kv_op_t *ops,uint8_t n)
{
 for(unsigned i=0;i<n;i++){uint16_t len=ops[i].len==CODEPLUG_KV_REMOVE?0:ops[i].len&~CODEPLUG_KV_HEAD;if(!ops[i].key||ops[i].key==0xFFFFu||(len&&!ops[i].value))return false;}
 uint32_t a;if(!area_addr(&a))return false;
 storage_lock();uint8_t *b=storage_buffer_acquire();uint32_t len,banks;uint16_t count;codeplug_kv_state_t state=chain_check(&len,&count,&banks);bool changed=false,ok=false;
 if(b&&state!=CODEPLUG_KV_NEWER){
  /* Keep the original single-sector implementation for a small area. An overflowing batch uses the defined continuation. */
  if(state==CODEPLUG_KV_VALID&&unchanged_ops(ops,n,b))ok=true;
  else if(banks&&state==CODEPLUG_KV_VALID){int single=single_page_ops(ops,n,b,&changed);if(single>=0)ok=single!=0;else{changed=false;ok=write_chain(ops,n,state,banks,b,&changed);}}
  else if(!banks&&board.flash->read(a,b,STORAGE_SECTOR)&&edit(b,ops,n,&changed))ok=!changed||storage_sector_write(a,b);
  else{changed=false;ok=write_chain(ops,n,state,banks,b,&changed);}
 }
 if(b)storage_buffer_release();
 if(ok&&changed)kv_serial++;
 storage_unlock();return ok;
}

bool codeplug_kv_set(uint16_t key, const void *value, uint16_t len)
{
	codeplug_kv_op_t op = { key, len, value };
	return len != CODEPLUG_KV_REMOVE && codeplug_kv_write(&op, 1);
}

bool codeplug_kv_set_u32(uint16_t key, uint32_t value)
{
	uint8_t v[4];
	put32(v, value);
	return codeplug_kv_set(key, v, sizeof(v));
}

bool codeplug_kv_remove(uint16_t key)
{
	codeplug_kv_op_t op = { key, CODEPLUG_KV_REMOVE, NULL };
	return codeplug_kv_write(&op, 1);
}
