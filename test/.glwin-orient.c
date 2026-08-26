/* Throwaway: glwin's exact transforms, rows written STRAIGHT (as the present
 * puts them on screen), so the orientation can be judged off-machine before
 * the window is relaunched.  argv[1]=out, argv[2]=modelFlipDeg, argv[3]=tilt */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"
#include "teapot-geometry.h"
#define W 640
#define H 480
static void writeTiffStraight(const char *path, const unsigned long *argb)
{
    FILE *f = fopen(path, "w");
    unsigned char *row = (unsigned char *)malloc(W * 3);
    long pixels = (long)W * H * 3L, bpsOff = 8L + pixels, ifdOff = bpsOff + 6L;
    int x, y, i;
    static const unsigned short tags[10] = {256,257,258,259,262,273,277,278,279,284};
    unsigned long vals[10]; unsigned short types[10]; unsigned long counts[10];
    if (!f || !row) return;
#define P16(v) do{int V=(int)(v);putc(V&0xff,f);putc((V>>8)&0xff,f);}while(0)
#define P32(v) do{unsigned long V=(unsigned long)(v);putc((int)(V&0xff),f);putc((int)((V>>8)&0xff),f);putc((int)((V>>16)&0xff),f);putc((int)((V>>24)&0xff),f);}while(0)
    putc('I',f);putc('I',f);P16(42);P32(ifdOff);
    for (y = 0; y < H; y++) {           /* STRAIGHT: memory row 0 first */
        for (x = 0; x < W; x++) {
            unsigned long p = argb[(long)y*W+x];
            row[x*3]=(p>>16)&0xff; row[x*3+1]=(p>>8)&0xff; row[x*3+2]=p&0xff;
        }
        fwrite(row,1,W*3,f);
    }
    P16(8);P16(8);P16(8);
    vals[0]=W;types[0]=3;counts[0]=1; vals[1]=H;types[1]=3;counts[1]=1;
    vals[2]=bpsOff;types[2]=3;counts[2]=3; vals[3]=1;types[3]=3;counts[3]=1;
    vals[4]=2;types[4]=3;counts[4]=1; vals[5]=8;types[5]=4;counts[5]=1;
    vals[6]=3;types[6]=3;counts[6]=1; vals[7]=H;types[7]=3;counts[7]=1;
    vals[8]=pixels;types[8]=4;counts[8]=1; vals[9]=1;types[9]=3;counts[9]=1;
    P16(10);
    for(i=0;i<10;i++){P16(tags[i]);P16(types[i]);P32(counts[i]);
        if(types[i]==3&&counts[i]==1){P16(vals[i]);P16(0);}else P32(vals[i]);}
    P32(0); free(row); fclose(f);
}
int main(int argc, char **argv)
{
    OSMesaContext ctx; unsigned long *buf;
    float flip = (argc>2)?(float)atof(argv[2]):0.0f;
    float tilt = (argc>3)?(float)atof(argv[3]):-20.0f;
    GLfloat amb[4]={0,0,0,1},dif[4]={1,1,1,1},pos[4]={0,3,3,0},lamb[4]={0.2f,0.2f,0.2f,1};
    GLfloat mdif[4]={0.9f,0.35f,0.15f,1},mspec[4]={0.9f,0.9f,0.9f,1},mamb[4]={0.18f,0.07f,0.03f,1};
    buf=(unsigned long*)malloc((unsigned)(W*H)*4);
    ctx=OSMesaCreateContext(OSMESA_ARGB,NULL);
    if(!ctx||!OSMesaMakeCurrent(ctx,buf,GL_UNSIGNED_BYTE,W,H)){printf("no ctx\n");return 2;}
    glViewport(0,0,W,H);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glFrustum(-1.0,1.0,0.75,-0.75,2.0,20.0);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glTranslatef(0.0f,-0.2f,(argc>6)?(float)atof(argv[6]):-5.0f);
    glRotatef(tilt,1.0f,0.0f,0.0f);
    glDisable(GL_CULL_FACE);glShadeModel(GL_SMOOTH);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glClearDepth(1.0);
    glLightfv(GL_LIGHT0,GL_AMBIENT,amb);glLightfv(GL_LIGHT0,GL_DIFFUSE,dif);
    glLightfv(GL_LIGHT0,GL_POSITION,pos);glLightModelfv(GL_LIGHT_MODEL_AMBIENT,lamb);
    glEnable(GL_LIGHTING);glEnable(GL_LIGHT0);
    glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT,mamb);
    glMaterialfv(GL_FRONT_AND_BACK,GL_DIFFUSE,mdif);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,mspec);
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,50.0f);
    glClearColor(0.06f,0.08f,0.14f,1.0f);
    if (argc>4 && strcmp(argv[4],"soft")==0) OSMGAMesaHookForceSoftware(1);
    else if (argc>4 && atoi(argv[4]) > 0)
        OSMGAMesaHookBatchLimit((unsigned long)atoi(argv[4]));
    /* "hw" parses to nought and means: default batching, touch nothing */
    if (argc>9 && strcmp(argv[9],"time")==0) {
        /* frame breakdown: clear alone, then clear+teapot, 30 reps each */
        int r; double t0,t1,clearMs,frameMs; int reps=(argc>10)?atoi(argv[10]):30;
        struct timeval tv;
        /* glwin's path: present mode stands the whole-surface mirror down,
         * so the timing sees what the window sees */
        OSMGAMesaBufferPresentMode(1);
#define NOW() (gettimeofday(&tv,0), (double)tv.tv_sec*1000.0+(double)tv.tv_usec/1000.0)
        t0=NOW();
        for(r=0;r<reps;r++){glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glFinish();}
        t1=NOW(); clearMs=(t1-t0)/(double)reps;
        t0=NOW();
        for(r=0;r<reps;r++){
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glPushMatrix();
            if (flip != 0.0f) glRotatef(flip,1.0f,0.0f,0.0f);
            glRotatef((float)(r*3)*0.5f,0.0f,1.0f,0.0f);  /* 1.5 deg steps, glwin cadence */
            teapot((argc>5)?atoi(argv[5]):4,(argc>7)?atof(argv[7]):1.3,GL_FILL);
            glPopMatrix();
            glFinish();
        }
        t1=NOW(); frameMs=(t1-t0)/(double)reps;
        printf("clear+finish %.2f ms; clear+teapot+finish %.2f ms; teapot part %.2f ms\n",
               clearMs, frameMs, frameMs-clearMs);
        return 0;
    }
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glPushMatrix();
    if (flip != 0.0f) glRotatef(flip,1.0f,0.0f,0.0f);
    glRotatef((argc>8)?(float)atof(argv[8]):30.0f,0.0f,1.0f,0.0f);
    teapot((argc>5)?atoi(argv[5]):4,(argc>7)?atof(argv[7]):1.3,GL_FILL);
    glPopMatrix();
    glFinish();
    writeTiffStraight((argc>1)?argv[1]:"/tmp/o.tiff",buf);
    printf("wrote; replayed=%lu\n", OSMGAMesaHookReplayed());
    return 0;
}
