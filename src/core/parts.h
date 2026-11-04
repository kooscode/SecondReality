// Entry points of the ported demo parts (one per original EXE).
#pragma once

void part_start();      // START.EXE    setup menu
void part_alku();       // ALKU.EXE     opening credits
void part_u2a();        // U2A.EXE      vector part 0 (ship over the horizon)
void part_pam();        // PAM.EXE      explosion
void part_beglogo();    // BEGLOGO.EXE  title picture
void part_glenz();      // GLENZ.EXE
void part_tunneli();    // TUNNELI.EXE  dot tunnel
void part_techno();     // TECHNO.EXE
void part_panicend();   // PANICEND.EXE
void part_mntscrl();    // MNTSCRL.EXE  mountain scroller (FOREST)
void part_lnszoom();    // LNS&ZOOM.EXE lens + rotozoomer
void part_plzpart();    // PLZPART.EXE  plasma + plasma cube
void part_minvball();   // MINVBALL.EXE mini vector balls (DOTS)
void part_rayscrl();    // RAYSCRL.EXE  raytraced mirror ball scroller (WATER)
void part_3dsinfld();   // 3DSINFLD.EXE 3D sinus field (COMAN)
void part_jplogo();     // JPLOGO.EXE   jelly picture
void part_u2e();        // U2E.EXE      vector part II (city)
void part_ddstars();    // DDSTARS.EXE  hidden part
void part_endlogo();    // ENDLOGO.EXE  end picture
void part_cred();       // CRED.EXE     credits
void part_endscrl();    // ENDSCRL.EXE  end scroller

// Loader helper also used by parts (U2A.ASM inittwk)
void inittwk();
