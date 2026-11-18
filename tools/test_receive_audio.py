#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real fixed-point decoder, CAI/replay worker and PCM pacing, host ASan/UBSan."""
from pathlib import Path
import subprocess
import re
import sys
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/audio-host'
APP=ROOT/'build/radiofoundry-p25'
CODEC=ROOT/'build/codec-speed-host'
def run(args,**kw):return subprocess.run([str(a) for a in args],cwd=ROOT,check=True,**kw)
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    run(['python3','tools/sources.py','verify-vendored'])
    run(['python3','tools/build_radiofoundry.py','--prepare-only'])
    run(['python3','experiments/codec/build.py','--speed','--full-ubsan','--out',CODEC])
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
           '-fsanitize=address,undefined','-fno-omit-frame-pointer','-Isrc/p25','-Isrc/codec','-Ithird_party/p25cai/include',
           '-Itests/audio/stubs','-Iintegration/radiofoundry','-I'+str(APP/'core'),
           '-I'+str(APP/'hal'),'-DCONFIG_P25_NATIVE=1']
    objects=[]
    sources=[ROOT/'src/p25'/f'{name}.c' for name in ('p25','voice','phy_stream','selftest','audio_fifo')]
    sources += [ROOT/'integration/radiofoundry/p25_lab.c',ROOT/'integration/radiofoundry/cpu_profile.c',APP/'core/debuglink/dl_fmt.c',APP/'core/radio/beep.c',ROOT/'tests/audio/test_receive_lab.c']
    for source in sources:
        obj=OUT/(source.stem+'.o')
        run(['clang','-std=c11',*flags,'-c',source,'-o',obj]);objects.append(obj)
    for source in sorted((ROOT/'third_party/p25cai/src').glob('*.c')):
        obj=OUT/('cai-'+source.stem+'.o')
        run(['clang','-std=c11',*flags,'-c',source,'-o',obj]);objects.append(obj)
    run(['clang++','-fsanitize=address,undefined',*objects,CODEC/'libdm32_imbe.a','-o',OUT/'test_receive_lab'])
    baseline=run([OUT/'test_receive_lab'],stdout=subprocess.PIPE,text=True).stdout;print(baseline,end='')
    lean=[]
    for source in (ROOT/'integration/radiofoundry/p25_lab.c',ROOT/'tests/audio/test_receive_lab.c'):
        obj=OUT/(source.stem+'-lean.o')
        run(['clang','-std=c11',*flags,'-DCONFIG_P25_BENCH=0','-c',source,'-o',obj]);lean.append(obj)
    common=[o for o in objects if o.stem not in ('p25_lab','test_receive_lab')]
    run(['clang++','-fsanitize=address,undefined',*common,*lean,CODEC/'libdm32_imbe.a','-o',OUT/'test_receive_lean'])
    optimized=run([OUT/'test_receive_lean'],stdout=subprocess.PIPE,text=True).stdout;print(optimized,end='')
    hashes=re.findall(r'PCM_CALLBACK_CRC=([0-9a-f]{8})',baseline)
    if len(hashes)!=1 or hashes!=re.findall(r'PCM_CALLBACK_CRC=([0-9a-f]{8})',optimized):
        raise SystemExit('Lean and bench speaker PCM differ')
    idle=OUT/'test_receive_idle.o'
    run(['clang','-std=c11',*flags,'-DCONFIG_P25_BENCH=0','-DCONFIG_DEV_TX=1',
         '-DCONFIG_P25_CHANNELS=1','-DCONFIG_DMR=1','-ffunction-sections','-fdata-sections',
         '-I'+str(ROOT/'integration/radiofoundry'),'-I'+str(ROOT/'integration/radiofoundry/native'),
         'tests/audio/test_receive_idle.c','-c','-o',idle])
    run(['clang++','-fsanitize=address,undefined',*common,idle,CODEC/'libdm32_imbe.a',
         '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections',
         '-o',OUT/'test_receive_idle'])
    run([OUT/'test_receive_idle'])
    run(['clang','-std=c11',*flags,'tests/test_audio_fifo.c','src/p25/audio_fifo.c','-o',OUT/'test_audio_fifo'])
    run([OUT/'test_audio_fifo'])
    run(['clang','-std=c11',*flags,'tests/test_pcm_clock.c','src/p25/pcm_clock.c','-o',OUT/'test_pcm_clock'])
    run([OUT/'test_pcm_clock'])
    # Read the real assignment so a PCM/native-guard timer collision cannot pass
    # against a test-only constant. IRQ timer numbers equal their timer index.
    platform=(APP/'platform/hrc7000/platform.h').read_text()
    timer=re.search(r'^#define TIMER_PCM[^\n]+',platform,re.M)
    assert timer is not None
    (OUT/'pcm_timer.inc').write_text(timer.group(0)+'\n')
    # Compile the actual generated DM32 route/amp functions into the PCM
    # fixture; board hardware and the source selector are the only doubles.
    board=(APP/'boards/dm32/radio_dm32.c').read_text()
    def function(signature):
        start=board.index(signature);begin=board.index('{',start);depth=1;end=begin+1
        while depth:
            depth+=(board[end]=='{')-(board[end]=='}');end+=1
        return board[start:end]
    (OUT/'board_tx_route.inc').write_text(function('static void speaker_source(void)\n{')+'\n'+
                                          function('static bool tx(uint32_t hz, radio_bw_t bw, uint8_t power, const subtone_t *tone)\n{')+'\n')
    run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-but-set-variable',
         '-fsanitize=address,undefined','-I'+str(APP/'hal'),
         'tests/audio/test_tx_route.c','-o',OUT/'test_tx_route'])
    run([OUT/'test_tx_route'])
    (OUT/'board_speaker.inc').write_text(function('static void amp(uint8_t who, bool on)\n{')+'\n'+
                                         function('void dm32_speaker_pcm(bool on)\n{')+'\n')
    run(['clang','-std=c11',*flags,'-I'+str(APP/'drivers'),'-DCONFIG_DMR=1','-DCONFIG_P25=1','-DCONFIG_VOX=0','-DCONFIG_BEEP=1',
         'tests/audio/test_pcm_driver.c','src/p25/pcm_clock.c','-o',OUT/'test_pcm_driver'])
    run([OUT/'test_pcm_driver'])
    for enabled in (0,1):
        out=OUT/f'test_cpu_profile_{enabled}'
        run(['clang','-std=c11',*flags,f'-DCONFIG_CPU_PROFILE={enabled}',
             'tests/audio/test_cpu_profile.c','integration/radiofoundry/cpu_profile.c',APP/'core/debuglink/dl_fmt.c','-o',out])
        run([out])
    # Compose the actual live caller with voice/CAI/codec, doubling only the
    # microphone/native hardware edges. Codec draining belongs to voice.
    live_tail=OUT/'test_live_tail.o'
    tail_flags=[*flags,'-DCONFIG_P25_BENCH=0','-DCONFIG_DEV_TX=1',
                '-DCONFIG_P25_CHANNELS=1','-DCONFIG_DMR=1',
                '-ffunction-sections','-fdata-sections']
    run(['clang','-std=c11',*tail_flags,'tests/audio/test_live_tail.c','-c','-o',live_tail])
    run(['clang++','-fsanitize=address,undefined',*common,live_tail,CODEC/'libdm32_imbe.a',
         '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections',
         '-o',OUT/'test_live_tail'])
    run([OUT/'test_live_tail'])

if __name__=='__main__':main()
