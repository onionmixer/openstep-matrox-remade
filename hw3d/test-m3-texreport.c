/*
 * test-m3-texreport.c -- does M3's T3 verifier measure what it claims?
 *
 * This session has twice written a test that could not have detected the
 * thing it existed for: a completion predicate that was unreachable, and a
 * texture mapping whose every sample sat on a texel boundary so that the
 * anchor error it was meant to find could never show.  Both cost a reboot
 * to discover.
 *
 * So before spending another one, the verifier is fed images whose answer
 * is known and asked whether it says the right thing:
 *
 *   perfect          -> zero wrong
 *   uniformly shifted -> that exact shift, as a TIGHT du/dv range
 *   scrambled        -> a WIDE range, unmistakably not a convention
 *   malformed codes  -> counted apart from wrong addresses
 *
 * The middle two are the point.  The driver reports a range rather than a
 * count precisely so that "one anchor convention to compensate" and "the
 * gradient is wrong" are different readings, and a verifier that blurred
 * them would be worse than none.
 *
 * The arithmetic here is a copy of the driver's, deliberately: it is the
 * REPORTING that is under test, and a copy that drifts will disagree with
 * the driver on the shifted cases.
 */
#include <stdio.h>
#define BLK 64
#define TRI_LO 8
#define TRI_HI 56
#define TEXBASE 8
#define RGB 0x00FFFFFFUL
#define SENT (0x5A5A5A5AUL & RGB)

static int inside(unsigned long r, unsigned long c){
    unsigned long leg = TRI_HI - TRI_LO;
    if (r < TRI_LO || r >= TRI_LO+leg) return 0;
    if (c < TRI_LO || c >= TRI_LO+leg) return 0;
    return ((r-TRI_LO)+(c-TRI_LO) <= leg-1) ? 1 : 0;
}
/* the driver's report, reduced to its arithmetic */
/* `shape` says what the residual should LOOK like, which is the property
 * under test: 0 = no wrong pixels, 1 = a tight range (one convention),
 * 2 = a wide range (a gradient).  Overloading a count for this is what
 * made the first version of this file report a false failure. */
#define SHAPE_NONE   0
#define SHAPE_TIGHT  1
#define SHAPE_WIDE   2
static int report(unsigned long *blk, const char *what,
                  long wantExact, long wantWrong, long wantMal, int shape){
    unsigned long r,c,exact=0,wrong=0,mal=0,seen=0;
    long duMin=127,duMax=-127,dvMin=127,dvMax=-127;
    for(r=0;r<BLK;r++)for(c=0;c<BLK;c++){
        unsigned long v,tx,ty; long du,dv;
        if(!inside(r,c)) continue;
        v = blk[r*BLK+c] & RGB; seen++;
        if((v & 0xFF) != 0x40){ mal++; continue; }
        tx=(v>>16)&0xFF; ty=(v>>8)&0xFF;
        du=(long)tx-(long)(TEXBASE+(c-TRI_LO));
        dv=(long)ty-(long)(TEXBASE+(r-TRI_LO));
        if(du==0&&dv==0) exact++;
        else { wrong++;
            if(du<duMin) duMin=du;
            if(du>duMax) duMax=du;
            if(dv<dvMin) dvMin=dv;
            if(dv>dvMax) dvMax=dv;
        }
    }
    printf("%-22s seen %4lu exact %4lu wrong %4lu malformed %4lu",
           what, seen, exact, wrong, mal);
    if(wrong) printf("  du %ld..%ld dv %ld..%ld", duMin,duMax,dvMin,dvMax);
    printf("\n");
    /* -1 means "do not check this one"; the scrambled case is asserted by
     * its RANGE below rather than by a count. */
    if(wantExact>=0 && (long)exact!=wantExact) return 1;
    if(wantWrong>=0 && (long)wrong!=wantWrong) return 1;
    if(wantMal  >=0 && (long)mal  !=wantMal  ) return 1;
    if(shape==SHAPE_TIGHT && (duMax-duMin > 0 || dvMax-dvMin > 0)){
        printf("   FAIL: a uniform shift reported a range -- it would not "
               "read as one convention\n");
        return 1;
    }
    if(shape==SHAPE_WIDE && (duMax-duMin < 8 && dvMax-dvMin < 8)){
        printf("   FAIL: a scrambled image reported a narrow range -- it "
               "would read as a uniform convention\n");
        return 1;
    }
    if(shape==SHAPE_NONE && wrong != 0){
        printf("   FAIL: wrong pixels where none were expected\n");
        return 1;
    }
    return 0;
}
static unsigned long fb[BLK*BLK];
static void paint(long du,long dv,int scramble){
    unsigned long r,c;
    for(r=0;r<BLK*BLK;r++) fb[r]=SENT;
    for(r=0;r<BLK;r++)for(c=0;c<BLK;c++){
        long tx,ty;
        if(!inside(r,c)) continue;
        tx=(long)(TEXBASE+(c-TRI_LO))+du;
        ty=(long)(TEXBASE+(r-TRI_LO))+dv;
        if(scramble){ tx=(tx*7)%64; ty=(ty*5)%64; }
        fb[r*BLK+c]=((unsigned long)tx<<16)|((unsigned long)ty<<8)|0x40;
    }
}
int main(void){
    int bad = 0;
    paint(0,0,0);  bad += report(fb,"perfect", 1176,0,0, SHAPE_NONE);
    paint(1,0,0);  bad += report(fb,"shifted u by +1", 0,1176,0, SHAPE_TIGHT);
    paint(0,-1,0); bad += report(fb,"shifted v by -1", 0,1176,0, SHAPE_TIGHT);
    paint(0,0,1);  bad += report(fb,"scrambled", -1,-1,0, SHAPE_WIDE);
    { unsigned long i; paint(0,0,0);
      for(i=0;i<BLK*BLK;i++) if(fb[i]!=SENT && (i%7)==0) fb[i]=0x123456;
      bad += report(fb,"some malformed", -1,0,189, SHAPE_NONE); }
    if (bad == 0)
        printf("m3-texreport: the verifier separates exact, uniform, "
               "varying and malformed\n");
    else
        printf("m3-texreport: %d cases misreported\n", bad);
    return bad ? 1 : 0;
}
