/*
 * Prove the packaged software Mesa fallback at the fixed first render size.
 *
 * This is an off-screen OSMesa client with no machine-interface dependency.
 * The caller owns the ordinary process-memory buffer and destroys the context
 * before exit.
 */

#include <stdio.h>
#include <stdlib.h>

#include <GL/gl.h>
#include <GL/osmesa.h>

#define OSMGA_OSMESA_WIDTH 1024
#define OSMGA_OSMESA_HEIGHT 768
#define OSMGA_OSMESA_COMPONENTS 4

int
main(void)
{
    OSMesaContext context;
    GLubyte *pixels;
    size_t byte_count;
    int result;

    byte_count = (size_t)OSMGA_OSMESA_WIDTH *
                 (size_t)OSMGA_OSMESA_HEIGHT *
                 (size_t)OSMGA_OSMESA_COMPONENTS;
    pixels = (GLubyte *)malloc(byte_count);
    if (pixels == NULL) {
        fprintf(stderr, "OPENSTEP_MGA_OSMESA_FALLBACK_ALLOCATION=fail\n");
        return 1;
    }

    result = 1;
    context = OSMesaCreateContext(GL_RGBA, NULL);
    if (context == NULL) {
        fprintf(stderr, "OPENSTEP_MGA_OSMESA_FALLBACK_CONTEXT=fail\n");
        free(pixels);
        return result;
    }
    if (!OSMesaMakeCurrent(context, pixels, GL_UNSIGNED_BYTE,
                            OSMGA_OSMESA_WIDTH, OSMGA_OSMESA_HEIGHT)) {
        fprintf(stderr, "OPENSTEP_MGA_OSMESA_FALLBACK_BIND=fail\n");
        OSMesaDestroyContext(context);
        free(pixels);
        return result;
    }

    /* Match the top-left convention of the CPU presentation reference. */
    OSMesaPixelStore(OSMESA_Y_UP, 0);
    glViewport(0, 0, OSMGA_OSMESA_WIDTH, OSMGA_OSMESA_HEIGHT);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (glGetError() != GL_NO_ERROR ||
        pixels[0] != 255 || pixels[1] != 0 ||
        pixels[2] != 0 || pixels[3] != 255) {
        fprintf(stderr, "OPENSTEP_MGA_OSMESA_FALLBACK_RENDER=fail\n");
        OSMesaDestroyContext(context);
        free(pixels);
        return result;
    }

    OSMesaDestroyContext(context);
    free(pixels);
    printf("OPENSTEP_MGA_OSMESA_FALLBACK_STATUS=pass SIZE=%dx%d\n",
           OSMGA_OSMESA_WIDTH, OSMGA_OSMESA_HEIGHT);
    return 0;
}
