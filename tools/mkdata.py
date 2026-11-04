#!/usr/bin/env python3
"""Collect the runtime data files for the port into build/data/ from the original
Second Reality source tree (third_party/SecondReality/). Embedded data is pulled out of
the OMF object files the original EXEs were linked from."""
import os, shutil, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from omf import OMF

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'third_party', 'SecondReality')
OUT = os.path.join(ROOT, 'build', 'data')

def copy(rel, name=None):
    dst = os.path.join(OUT, (name or os.path.basename(rel)).upper())
    shutil.copyfile(os.path.join(SRC, rel), dst)

def obj(rel, name, seg=1):
    m = OMF(os.path.join(SRC, rel))
    s = m.segs[seg - 1]
    open(os.path.join(OUT, name.upper()), 'wb').write(bytes(s['data'][:s['length']]))

os.makedirs(OUT, exist_ok=True)
# loader
copy('MAIN/DATA/REALITY.FC')
copy('MAIN/DATA/U2END.BIN')
shutil.copyfile(os.path.join(ROOT,'tools','font8x16.bin'), os.path.join(OUT,'FONT8X16.BIN'))

def objcat(rel, name, segs, pad_to=None):
    m = OMF(os.path.join(SRC, rel))
    out = bytearray()
    for i in segs:
        s = m.segs[i - 1]
        out += s['data'][:s['length']]
    if pad_to and len(out) < pad_to:
        out += bytes(pad_to - len(out))
    open(os.path.join(OUT, name.upper()), 'wb').write(bytes(out))

# ALKU: horizon picture (two consecutive segments) and the font
objcat('ALKU/INCLUDE.OBJ', 'ALKU_HZPIC.BIN', [3, 4])
objcat('ALKU/INCLUDE.OBJ', 'ALKU_FONT.BIN', [5], pad_to=32 * 1500)

# U2A / U2E: scene files are read at run time from MAIN/DATA
import glob
for f in glob.glob(os.path.join(SRC, 'MAIN/DATA/U2[AE].*')):
    if not f.upper().endswith('.EXE'):
        shutil.copyfile(f, os.path.join(OUT, os.path.basename(f).upper()))
obj('VISU/C/_BG.OBK', 'U2A_BG.BIN')

# PAM: explosion animation (OUT.ANI == OUT.IN0..4) and palette
shutil.copyfile(os.path.join(SRC, 'PAM/OUT.ANI'), os.path.join(OUT, 'PAM_ANI.BIN'))
def incbytes(rel):
    import re
    vals = []
    for line in open(os.path.join(SRC, rel), encoding='latin1'):
        line = line.split(';')[0]
        m = re.match(r'^\s*db\s+(.*)$', line, re.I)
        if m:
            vals += [int(v) & 255 for v in m.group(1).split(',') if v.strip()]
    return bytes(vals)
open(os.path.join(OUT, 'PAM_PAL.BIN'), 'wb').write(incbytes('PAM/PAL.INC')[:768])
obj('BEG/_PIC.OBK', 'BEG_PIC.BIN')
obj('GLENZ/_FC.OBK', 'GLENZ_FC.BIN')
shutil.copyfile(os.path.join(SRC, 'TUNNELI/TUNNEL.DAT'), os.path.join(OUT, 'TUN_PCALC.BIN'))
shutil.copyfile(os.path.join(SRC, 'TUNNELI/SINIT.DAT'), os.path.join(OUT, 'TUN_SINIT.BIN'))
obj('TECHNO/_CIRCLE.OBK', 'TECH_CIRCLE.BIN')
obj('TECHNO/_CIRCLE2.OBK', 'TECH_CIRCLE2.BIN')
copy('MAIN/DATA/TROLL.UP')
obj('FOREST/HILLBACK.OBJ', 'MNT_HBACK.BIN')
obj('FOREST/POS1.OBJ', 'MNT_POS1.BIN')
obj('FOREST/POS2.OBJ', 'MNT_POS2.BIN')
obj('FOREST/POS3.OBJ', 'MNT_POS3.BIN')
copy('MAIN/DATA/O2.SCI')
obj('LENS/_LENSEX0.OBK', 'LENS_EX0.BIN')
obj('LENS/_LENSEX1.OBK', 'LENS_EX1.BIN')
obj('LENS/_LENSEX2.OBK', 'LENS_EX2.BIN')
obj('LENS/_LENSEX3.OBK', 'LENS_EX3.BIN')
obj('LENS/_LENSEX4.OBK', 'LENS_EX4.BIN')
obj('LENS/_LENSEXB.OBK', 'LENS_EXB.BIN')
obj('LENS/_LENSEXP.OBK', 'LENS_EXP.BIN')
obj('LENS/_FILLER.OBK', 'LENS_FILLER.BIN')
obj('WATER/BKG.OBJ', 'RAY_TAUSTA.BIN')
obj('WATER/MIEKKA.OBJ', 'RAY_MIEKKA.BIN')
obj('WATER/WAT1.OBJ', 'RAY_WAT1.BIN')
obj('WATER/WAT2.OBJ', 'RAY_WAT2.BIN')
obj('WATER/WAT3.OBJ', 'RAY_WAT3.BIN')
shutil.copyfile(os.path.join(SRC, 'COMAN/W1DTA.BIN'), os.path.join(OUT, 'COMAN_W1.BIN'))
shutil.copyfile(os.path.join(SRC, 'COMAN/W2DTA.BIN'), os.path.join(OUT, 'COMAN_W2.BIN'))
obj('JPLOGO/_PIC.OBK', 'JP_PIC.BIN')
obj('END/_PIC.OBK', 'END_PIC.BIN')
# CRED: 21 pictures (16 byte header, palette, 160x100) and the font
objcat('CREDITS/INCLUDE.OBJ', 'CRED_PICS1.BIN', range(1, 13))
objcat('CREDITS/INCLUD2.OBJ', 'CRED_PICS2.BIN', range(1, 10))
objcat('CREDITS/INCLUDE.OBJ', 'CRED_FONT.BIN', [13])

# ENDSCRL: the shipped ENDSCRL.EXE (PKLITE packed) is newer than ENDSCRL/MAIN.C;
# its 25x1550 font was taken from the unpacked executable
shutil.copyfile(os.path.join(ROOT, 'tools', 'endscrl_font.bin'), os.path.join(OUT, 'ENDSCRL_FONT.BIN'))
copy('MAIN/DATA/ENDSCROL.TXT')

# DDSTARS: the two text pictures (2 bit planes per row)
obj('DDSTARS/_TEXTPIC.OBK', 'DD_TEXTPIC.BIN')
