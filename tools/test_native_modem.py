#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Native modem control/register/protocol host tests. No radio or serial access."""
from pathlib import Path
import subprocess
import re
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"build/native-host"
UP=ROOT/"research/upstream/radiofoundry"
def run(args): subprocess.run([str(x) for x in args],cwd=ROOT,check=True)
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    run(["python3","tools/sources.py","verify-vendored"])
    run(["python3","tools/build_radiofoundry.py","--prepare-only"])
    app=ROOT/"build/radiofoundry-p25"
    platform=(app/"platform/hrc7000/platform.h").read_text()
    timer=re.search(r"^#define TIMER_PCM[^\n]+",platform,re.M)
    assert timer is not None
    (OUT/"pcm_timer.inc").write_text(timer.group(0)+"\n")
    flags=["-std=c11","-O1","-g","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",
           "-Isrc/modem","-Iintegration/radiofoundry/native","-Ithird_party/p25cai/include","-I"+str(UP/"drivers")]
    run(["clang",*flags,"src/modem/native_test.c","integration/radiofoundry/native/hrc_native_chip.c",
         "tests/native/test_native.c","-o",OUT/"test_native"])
    run([OUT/"test_native"])
    run(["clang",*flags,"-DCONFIG_DEV_TX=1","src/modem/native_test.c",
         "tests/native/test_live_start.c","-o",OUT/"test_live_start"])
    run([OUT/"test_live_start"])
    run(["clang",*flags,"-DCONFIG_DEV_TX=1","src/modem/native_test.c",
         "tests/native/test_chip_start.c","-o",OUT/"test_chip_start"])
    run([OUT/"test_chip_start"])
    board=(app/"boards/dm32/radio_dm32.c").read_text()
    start=board.index("static void set_mode(radio_mode_t m)\n{")
    end=board.index("\n}\n",start)+3
    (OUT/"board_set_mode.inc").write_text(board[start:end]+"\n")
    run(["clang",*flags,"-DCONFIG_DEV_TX=1","-DCONFIG_P25_NATIVE=1","-DCONFIG_DMR=1",
         "-I"+str(app/"core"),"-I"+str(app/"hal"),"-I"+str(OUT),
         "tests/native/test_board_prepare.c","-o",OUT/"test_board_prepare"])
    run([OUT/"test_board_prepare"])
    run(["clang",*flags,"-Itests/native/stubs","-Iintegration/radiofoundry","-I"+str(app/"core"),
         "-I"+str(app/"hal"),"-DCONFIG_P25_NATIVE=1","-Wno-unused-parameter",
         "src/modem/native_test.c","integration/radiofoundry/native/native_lab.c","tests/native/test_lab.c",
         "-o",OUT/"test_lab"])
    run([OUT/"test_lab"])
    owner_flags=[*flags,'-Itests/native/stubs','-Iintegration/radiofoundry','-I'+str(app/'core'),'-I'+str(app/'hal'),'-Wno-unused-parameter']
    run(['clang',*owner_flags,'-c','src/modem/native_test.c','-o',OUT/'owner_controller.o'])
    run(['clang',*owner_flags,'-DCONFIG_FIRSTBOOT=1','-DCONFIG_DEV_UPDATE=1','-DCONFIG_P25_NATIVE=1',
         'integration/radiofoundry/native/native_lab.c','tests/native/test_update_owner.c',OUT/'owner_controller.o','-o',OUT/'test_update_owner'])
    run([OUT/'test_update_owner'])
    run(['clang',*owner_flags,'-DCONFIG_FIRSTBOOT=1','-DCONFIG_DEV_UPDATE=1','-DCONFIG_DEV_TX=1','-DCONFIG_P25_NATIVE=1',
         'src/modem/native_test.c','integration/radiofoundry/native/native_lab.c',
         'integration/radiofoundry/firstboot/native_tx_gate.c','integration/radiofoundry/firstboot/firstboot.c',
         'tests/native/test_tx_owner.c','-o',OUT/'test_tx_owner'])
    run([OUT/'test_tx_owner'])
    port_flags=[*flags,"-Itests/native/stubs","-I"+str(app/"core"),"-DCONFIG_P25_NATIVE=1","-DCONFIG_DEV_TX=1"]
    run(["clang",*port_flags,"-DCONFIG_APRS=0","src/modem/native_test.c",
         "integration/radiofoundry/native/hrc_native_port.c",
         "tests/native/test_port.c","-o",OUT/"test_port"])
    run([OUT/"test_port"])
    # Run the generated upstream PCM driver and the native port/controller
    # together. Host doubles cover only RTOS, MMIO and modem IRQ events.
    run(["clang",*flags,"-Itests/audio/stubs","-Itests/native/stubs","-I"+str(app/"core"),
         "-I"+str(app/"hal"),"-Isrc/p25","-DCONFIG_P25_NATIVE=1","-DCONFIG_DEV_TX=1",
         "-DCONFIG_P25=1","-DCONFIG_DMR=1","-DCONFIG_VOX=0","-DCONFIG_APRS=0","-DCONFIG_BEEP=0",
         "src/modem/native_test.c","src/p25/pcm_clock.c","integration/radiofoundry/native/hrc_native_port.c",
         "tests/native/test_pcm_coexist.c","-o",OUT/"test_pcm_coexist"])
    run([OUT/"test_pcm_coexist"])
    # Invalid timer ownership must fail during compilation rather than silently
    # making P25 unavailable at runtime. Test the real compile guards.
    def refused(args,diagnostic):
        result=subprocess.run([str(x) for x in args],cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        assert result.returncode and diagnostic in result.stderr,result.stderr
    port="integration/radiofoundry/native/hrc_native_port.c"
    refused(["clang",*port_flags,"-DCONFIG_APRS=1","-fsyntax-only",port],
            "Native P25 requires APRS disabled")
    collision=OUT/"timer-collision"
    collision.mkdir(exist_ok=True)
    header=(ROOT/"tests/native/stubs/platform.h").read_text()
    header=header.replace('#include "../../../build/native-host/pcm_timer.inc"',
                          '#define TIMER_PCM 4')
    (collision/"platform.h").write_text(header)
    refused(["clang","-I"+str(collision),*port_flags,"-DCONFIG_APRS=0","-fsyntax-only",port],
            "Native P25 guard must not share the PCM or PA timer")
    print("PASS native timer ownership guards: APRS and PCM collisions rejected at compile time")
if __name__=="__main__": main()
