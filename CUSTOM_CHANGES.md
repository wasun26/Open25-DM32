# Open25-DM32 Custom Changes (Beyond Fork)

เอกสารนี้สรุปฟีเจอร์และแพตช์ทั้งหมดที่เพิ่มเติมนอกเหนือจากต้นฉบับที่ fork มา (ฐาน `1942068 Release Classic r7` / RadioFoundry upstream `dfbaf20c400c73e2ccf6edb80af7d1d255bee418`) เพื่อใช้อ้างอิงเวลาอัปเดตหรือ merge กับ upstream ในอนาคต

---

## 1. ไฟล์หลักที่แก้ไขในโปรเจกต์
- `tools/build_radiofoundry.py` — เพิ่มฟิลด์ใน `radio_settings_t` / `radio_view_t` (`core/radio/radio.h`) และปรับรายการฟีเจอร์ที่เปิดในพรีเซ็ต `--classic`
- `tools/classic_channels.py` — แพตช์การทำงานของฟีเจอร์ทั้งหมดเข้าสู่ซอร์ส RadioFoundry (`core/settings/settings.h`, `core/radio/radio_sm.h`, `core/radio/radio_sm.c`, `core/ui/ui_svc.h`, `core/ui/ui_svc_radio.c`, `core/ui/ui_svc_storage.c`, `core/ui/ui_menus.c`, `core/ui/ui_screens.c`, `core/ui/ui_app.c`)
- `tests/classic/test_radio.inc` — เพิ่ม host unit tests สำหรับ Standby LED, Auto SQL และ Auto Power Save (`test_classic_standby_led`, `test_classic_auto_sql`, `test_classic_auto_power_save`)

---

## 2. รายการฟีเจอร์ที่เพิ่มเข้ามา

### 2.1 Standby LED (ไฟกะพริบสถานะสแตนด์บาย)
- **เมนู**: `Settings → Display`
  - `Standby LED`: `On / Off`
  - `Standby interval`: `1..30 s` (ค่าเริ่มต้น `5 s`)
  - `Standby blink`: `50..1000 ms` ทีละ `50 ms` (ค่าเริ่มต้น `100 ms`)
- **การทำงาน**: กะพริบไฟเขียวเป็นจังหวะเฉพาะตอนเครื่องว่าง (`RS_IDLE`, ไม่ TX, ไม่ RX, ไม่เปิด squelch)

### 2.2 LED Style (รูปแบบไฟ TX/RX)
- **เมนู**: `Settings → Display → LED style`
  - `Default` (`0`): พฤติกรรมเดิมของ RadioFoundry (RX เขียวค้าง, TX แดงค้าง)
  - `Hytera` (`1`): `MONI` หรือ DMR `RX_OTHER` ติดสีส้ม (`LED_GREEN + LED_RED`)
  - `Motorola` (`2`): รับสัญญาณเข้ากะพริบเขียว (`led_now_ms % 400 < 200`), `MONI` หรือ DMR `RX_OTHER` ติดสีส้ม (`LED_GREEN + LED_RED`)

### 2.3 Auto SQL / ASQ (Adaptive Noise-Floor Squelch)
- **เมนู & ทางลัด**:
  - `Settings → Talk & listen → Auto squelch`: `On / Off`
  - หน้าจอ **Quick VFO (`*`) → Squelch**: กด `#` (`ACT_CONTEXT`) เพื่อสลับ `Auto SQL on / off` ได้ทันที
- **การแสดงผลบนหน้าจอ**:
  - หน้าจอหลัก (Home) แสดง `ASQ<n>` (เช่น `ASQ2`, `ASQ5`) เมื่อเปิด Auto SQL และ `squelch > 0`
  - หน้า Quick VFO tile `Squelch` แสดง `A<n>` (เช่น `A2`)
- **การทำงาน**:
  - เก็บค่าเฉลี่ยพื้นเสียงรบกวน (`sql_floor_q8`, Q8 IIR filter) เฉพาะตอน `RS_IDLE` (หลังจูนนิ่ง `sql_settle`, ไม่ TX/RX/MONI/Scan/Broadcast FM)
  - รองรับทั้ง **FM** และ **Air Band (AM)** (ในโหมด AM ใช้ `radio_am_noise(rssi_dbm)`)
  - คำนวณจุดเปิด squelch แบบปรับตามพื้น noise: `thresh = clamp(floor - (8 + sq), 16, 120)`

### 2.4 แก้บั๊ก VFO Hold OK ไม่บันทึก Squelch
- **ปัญหาเดิมของ upstream**: ในโหมด VFO เมื่อปรับ Squelch แล้วกด Hold OK ฟังก์ชัน `ui_try_save_channel()` แจ้ง `"VFO settings save automatically"` โดยไม่ได้บันทึก `st->squelch` ทำให้กด Back แล้ว undo กลับค่าเดิม
- **ที่แก้ไขใน `core/ui/ui_app.c`**: เมื่ออยู่ใน VFO และมี `view->temp & RADIO_TEMP_SQUELCH` การกด Hold OK จะบันทึก `UI_SET_SQUELCH`, เรียก `radio_req_clear_temporary()`, และแจ้ง `"Squelch saved"`

### 2.5 Spotlight Mode (เลือกเปิดหน้า Find เมื่อกดตัวเลขหน้า Home)
- **เมนู**: `Settings → Keys → Spotlight`
  - `Channel` (`0`, ค่าเริ่มต้น): กดเลขในโหมดช่องเปิด `SCR_FIND`, โหมด VFO พิมพ์ความถี่ตรง (`entry_digit`)
  - `VFO` (`1`): กดเลขในโหมด VFO เปิด `SCR_FIND`
  - `Both` (`2`): กดเลขเปิด `SCR_FIND` ทั้งในโหมดช่องและ VFO

### 2.6 Auto Power Save (โหมดประหยัดพลังงานอัตโนมัติเมื่อแบตต่ำ)
- **เมนู**: `Settings → Power → Auto power save`: `On / Off`
- **การทำงานเมื่อ `battery >= RADIO_BATT_LOW`**:
  - **RX Battery Saver**: เปิด `rx_save` อัตโนมัติ (อย่างน้อยระดับ 1) โดยไม่ต้องตั้งเอง และหน้าเมนู `Battery saver` แสดง `Auto: On / Auto: Off`
  - **Low-Battery TX Power Step-Down**: ลดกำลังส่ง TX อัตโนมัติลงระดับ Low (`L` บน DM-32UV / ระดับ `0` บนบอร์ดที่มี ≤3 ระดับ) ครอบคลุมทั้ง **FM, DMR (Voice/Data/Roam) และ P25 Native TX** โดยไม่ทับค่ากำลังส่งที่ตั้งไว้ในช่อง/VFO และคืนค่าเดิมทันทีเมื่อแบตกลับมาปกติหรือปิดฟีเจอร์นี้
  - **Standby LED Throttling**: ยืดรอบไฟกะพริบ Standby LED เป็น `2x` และจำกัดความกว้างพัลส์ไม่เกิน `50 ms`
  - **การแสดงผล**: หน้าจอ Home (FM/DMR/P25) และ Quick tile `Power` แสดง `<power>><eff>` สีเตือน (เช่น `H>L`) ขณะกำลังส่งถูกลดลง

### 2.7 FM Auto Clear (ลดแบนด์วิดท์ FM อัตโนมัติเมื่อสัญญาณรบกวนสูง)
- **เมนู**: `Settings → Talk & listen → FM Auto Clear`: `On / Off`
- **การทำงาน**: ในโหมด FM 25 kHz ขณะรับสัญญาณ (`RS_RX`) หากค่า noise ≥ 46 จะลดแบนด์วิดท์เป็น 12.5 kHz ชั่วคราว (ฮิสเทอรีซิสคืนกลับ 25 kHz เมื่อ noise < 38)

### 2.8 SQL Fading Hold (กันเสียงขาดเป็นท่อนเมื่อสัญญาณ Analog แกว่ง)
- **เมนู**: `Settings → Talk & listen → SQL fading hold`: `On / Off` (ค่าเริ่มต้น `Off`)
- **การทำงาน** (รองรับทั้ง Auto SQL และ Manual SQL, ปิดอัตโนมัติขณะ Scan):
  - ขยาย hysteresis ช่วงรับเป็น `+6` (เดิม `+3`) เพื่ออุ้มสัญญาณที่แกว่งเล็กน้อย
  - ช่วงสัญญาณวูบแต่ยังไม่หลุดถึงพื้น static (`noise < floor - 3` หรือ `< open_below + 16`) จะยืดเวลา hang เป็น `10` polls (~200–250 ms) เพื่อข้ามจังหวะ fading ไม่ให้เสียงขาดๆ หายๆ
  - เมื่ออีกฝั่งปล่อยคีย์จริง (`noise` พุ่งกลับถึงพื้น static หรือโทน CTCSS/DCS หลุด) จะปิดลำโพงเร็วเท่าเดิม (`2` polls ≈ 40 ms) ไม่มีเสียงซ่าค้างท้ายคีย์

### 2.9 Analog (FM / AM) Activity Log ใน `Recent` และหน้า Home
- **การทำงาน**: บันทึกกิจกรรมรับ-ส่งของ Analog FM และ Air Band AM (`RS_RX` ยาว ≥ 300 ms และ `RS_TX`) ลงในหน้า **`Recent`** (`SCR_RECENT`) ร่วมกับ DMR พร้อมเก็บชื่อช่อง/ความถี่ VFO, Peak RSSI (`dBm`), ระยะเวลา (`s`), และเวลาที่ผ่านมา
- **การแสดงผลหน้า Home**: แสดงกิจกรรม Analog ล่าสุดในตำแหน่งและรูปแบบ 2 บรรทัดเดียวกับ DMR (`Last heard`)
- **ทางลัดจูนกลับ**: กด **OK** หรือ **`#`** บนรายการ Analog ในหน้า `Recent` จะจูนกลับไปยังช่องหรือความถี่ VFO นั้นทันที (และไม่ไปชนกับ Call-Back / Actions ของ DMR)

### 2.9 พรีเซ็ตฟีเจอร์ใน `--classic` (`tools/build_radiofoundry.py`)
- **เปิดใช้งานเพิ่ม**:
  - `AM_RX` (Air Band `108–136 MHz`, AM รับอย่างเดียว)
  - `GPS`, `APRS` (`CONFIG_APRS=y` เฉพาะ `--classic`)
  - DMR ขั้นสูง: `DMR_GPS`, `DMR_SMS`, `DMR_TMS`, `DMR_ARS`, `DMR_LRRP`, `DMR_SIGNALING`, `DMR_EMERGENCY`, `DMR_ROAMING`, `DMR_UDP_COMPRESS`, `DMR_CONFIRMED`
- **ยังคงปิดไว้ (`=n`) เพื่อประหยัด RAM (ข้อจำกัด 211,456 B ก่อนหน้าต่าง codec 049)**:
  - `DMR_IP_GATEWAY`, `NET_PPP`, `SFR`, `HOTSPOT`, `ZONE_SHARE`, `BCAST_FM`, `DUAL_WATCH`, `VOX`, `CRASH_REPORT`, `LANG_ES`, `LANG_IT`

---

## 3. การจัดวางบิตใน `settings_t` (สำคัญมากเวลา Merge Upstream)
เพื่อให้ `sizeof(settings_t) == 392` ไม่เปลี่ยนและไม่พัง settings store เดิม ค่าทั้งหมดถูกแพ็กไว้ในบิตว่างของฟิลด์เดิม:

| ฟีเจอร์ | ฟิลด์ใน `settings_t` | บิตที่ใช้ | ค่าเริ่มต้น |
|---|---|---|---|
| `standby_led` | `rs_flags` | bit 6 (`0x40`, `SETTINGS_RS_STANDBY_LED`) | `0` (Off) |
| `auto_sql` | `rs_flags` | bit 7 (`0x80`, `SETTINGS_RS_AUTO_SQL`) | `0` (Off) |
| `standby_gap_s` | `rs_bcl` | bits 2..6 (`(rs_bcl >> 2) & 0x1F`) | `0` → `5 s` |
| `standby_on_ms` | `rs_scan_resume` | bits 2..6 (`((rs_scan_resume >> 2) & 0x1F) * 50`) | `0` → `100 ms` |
| `led_style` | `rs_rx_save` | bits 2..3 (`(rs_rx_save >> 2) & 0x03`) | `0` (Default) |
| `spotlight_mode` | `updown_home` | bits 2..3 (`(updown_home >> 2) & 0x03`) | `0` (Channel) |
| `auto_power_save` | `unused_dmr_id` | bit 0 (`0x01`, `SETTINGS_CUSTOM_AUTO_SAVE`) | `0` (Off) |
| `fm_auto_clear` | `unused_dmr_id` | bit 1 (`0x02`, `SETTINGS_CUSTOM_FM_CLEAR`) | `0` (Off) |
| `sql_hold` | `unused_dmr_id` | bit 2 (`0x04`, `SETTINGS_CUSTOM_SQL_HOLD`) | `0` (Off) |

> **ข้อควรระวังเมื่ออัปเดต upstream**: ตรวจสอบ `core/settings/settings.h` ของ upstream เสมอว่าไม่มีการนำบิตเหล่านี้ใน `rs_flags`, `rs_bcl`, `rs_scan_resume`, `rs_rx_save` หรือ `updown_home` ไปใช้งานซ้ำ และตรวจ `_Static_assert(sizeof(settings_t) == 392, ...)`

---

## 4. คำสั่งทดสอบ คอมไพล์ และแพ็กไฟล์ (WSL)
```bash
# 1. รัน host tests ทั้งหมดของ Classic
make classic-test

# 2. คอมไพล์เฟิร์มแวร์ DM-32 Classic
make radiofoundry

# 3. แพ็กเป็นไฟล์ .zip สำหรับแฟลชผ่าน Firmware Utility
python3 tools/prepare_web_firmware.py \
  --image build/radiofoundry-p25/build/dm32-p25-classic/rfoundry_dm32.bin \
  --out artifacts/firmware/Open25-DM32-Classic-custom.zip
```
