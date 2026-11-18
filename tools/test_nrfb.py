#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual generated FD6818 driver/board RF selection, with ASan and UBSan."""
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'build/radiofoundry-p25'
OUT=ROOT/'build/nrfb-host'

def run(args):
    subprocess.run([str(a) for a in args],cwd=ROOT,check=True)

def main():
    run(['python3','tools/build_radiofoundry.py','--prepare-only'])
    OUT.mkdir(parents=True,exist_ok=True)
    # Retain every pinned driver regression, adding a programmable lock-status
    # read to its existing fake three-wire chip and the selected NRFB cases.
    test=(APP/'tests/host/fd6818Test.c').read_text()
    changes=[
        ('uint16_t regs[256];','uint16_t regs[256];\n\tuint16_t (*css)(void);'),
        ('chip.out = chip.regs[chip.shift & 0x7Fu];',
         'chip.out = (chip.shift & 0x7Fu) == 0x0C && chip.css ? chip.css() : chip.regs[chip.shift & 0x7Fu];'),
        ('int main(void)\n{', '#include "test_nrfb.inc"\n\nint main(void)\n{\n\ttest_nrfb();')]
    for old,new in changes:
        if test.count(old)!=1:
            raise SystemExit('Pinned FD6818 test mismatch: '+old)
        test=test.replace(old,new)
    (OUT/'fd6818Test.c').write_text(test)
    board=(APP/'boards/dm32/radio_dm32.c').read_text()
    def function(signature):
        start=board.index(signature);begin=board.index('{',start);depth=1;end=begin+1
        while depth:
            depth+=(board[end]=='{')-(board[end]=='}');end+=1
        return board[start:end]+'\n'
    constants=('RF_PROBE_HZ','RF_LOCK_MS','FD_REG_TX_AUX','FD_TX_AUX_DMR',
               'FD_REG_CSS_A','FD_REG_CSS_B','C7_RF_MODE_RX_INVERT')
    text='#include "hal_radio.h"\n'
    for name in constants:
        text+=re.search(r'^#define '+name+r'\s+.*$',board,re.M)[0]+'\n'
    for signature in ('static uint16_t fm_bw_reg(', 'static void fd_rx_subtone(',
                      'static void dmr_rf_switch(', 'static void dmr_pa_on(', 'static void rf_detect('):
        text+=function(signature)
    (OUT/'board_nrfb.inc').write_text(text)
    sources=[OUT/'fd6818Test.c',APP/'drivers/rf/fd6818/fd6818.c',
             APP/'drivers/rf/fd6818/fd6818_setup_b.c',APP/'drivers/rf/rfbus/rfbus.c',
             APP/'boards/dm1801pro/rf_tables_dm1801pro.c',APP/'drivers/rf/subaudio/subaudio.c']
    run(['clang','-std=gnu99','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
         '-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-fno-omit-frame-pointer',
         '-I'+str(APP/'core'),'-I'+str(APP/'drivers'),'-I'+str(APP/'hal'),
         '-I'+str(ROOT/'tests/native'),'-I'+str(OUT),*sources,'-o',OUT/'test_nrfb'])
    run([OUT/'test_nrfb'])

if __name__=='__main__':main()
