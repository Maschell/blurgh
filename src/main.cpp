/*  Copyright 2019 Ash Logan "quarktheawesome" <ash@heyquark.com>
    Copyright 2019 Maschell

    Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <wups.h>
#include <malloc.h>
#include <string.h>
#include <nsysnet/socket.h>
#include <utils/logger.h>
#include <coreinit/filesystem.h>
#include <gx2/swap.h>
#include <gx2/enum.h>
#include <gx2/surface.h>
#include <gx2/mem.h>
#include <gx2/context.h>
#include <gx2/state.h>
#include <gx2/clear.h>
#include <gx2/event.h>
#include <coreinit/cache.h>
#include <coreinit/debug.h>

#include "shaders/Texture2DShader.h"

/**
    Mandatory plugin information.
    If not set correctly, the loader will refuse to use the plugin.
**/
WUPS_PLUGIN_NAME("VideoSquoosher");
WUPS_PLUGIN_DESCRIPTION("Squooshes the Gamepad and TV video side-by-side, " \
    "displaying that on your TV");
WUPS_PLUGIN_VERSION("v1.0");
WUPS_PLUGIN_AUTHOR("Maschell & quarktheawesome");
WUPS_PLUGIN_LICENSE("ISC");

GX2ColorBuffer main_cbuf;
GX2Texture drcTex;
GX2Texture tvTex;
GX2Sampler sampler;
GX2ContextState* ownContextState;
GX2ContextState* originalContextSave = NULL;
int32_t curStatus = WUPS_APP_STATUS_BACKGROUND;

/**
    Add this to one of your projects file to have access to SD/USB.
**/
WUPS_FS_ACCESS()


INITIALIZE_PLUGIN(){
    memset(&main_cbuf, 0, sizeof(GX2ColorBuffer));
}

ON_APPLICATION_START(){
    socket_lib_init();
    log_init();

    DEBUG_FUNCTION_LINE("VideoSquoosher: Hi!\n");
}

void freeUsedMemory(){
    if (main_cbuf.surface.image) {
        free(main_cbuf.surface.image);
        main_cbuf.surface.image = NULL;
    }

    if (drcTex.surface.image) {
        free(drcTex.surface.image);
        drcTex.surface.image = NULL;
    }

    if (tvTex.surface.image) {
        free(tvTex.surface.image);
        tvTex.surface.image = NULL;
    }

    if(ownContextState){
        free(ownContextState);
        ownContextState = NULL;
    }
}

ON_APPLICATION_ENDING(){
    DEBUG_FUNCTION_LINE("VideoSquoosher: shutting down...\n");

    freeUsedMemory();

    Texture2DShader::destroyInstance();

}

void copyToTexture(GX2ColorBuffer* sourceBuffer, GX2Texture * target){
    if(sourceBuffer == NULL || target == NULL){
        return;
    }
    if (sourceBuffer->surface.aa == GX2_AA_MODE1X) {
        // If AA is disabled, we can simply use GX2CopySurface.
        GX2CopySurface(&sourceBuffer->surface,
            sourceBuffer->viewMip,
            sourceBuffer->viewFirstSlice,
            &target->surface, 0, 0);
    } else {
        // If AA is enabled, we need to resolve the AA buffer.

        // Allocate surface to resolve buffer onto
        GX2Surface tempSurface;
        tempSurface = sourceBuffer->surface;
        tempSurface.aa = GX2_AA_MODE1X;
        GX2CalcSurfaceSizeAndAlignment(&tempSurface);

        tempSurface.image = memalign(
            tempSurface.alignment,
            tempSurface.imageSize
        );
        if(tempSurface.image == NULL) {
            DEBUG_FUNCTION_LINE("VideoSquoosher: failed to allocate AA surface\n");
            if(target->surface.image != NULL) {
                free(target->surface.image);
                target->surface.image = NULL;
            }
            return;
        }

        // Resolve, then copy result to target
        GX2ResolveAAColorBuffer(sourceBuffer,&tempSurface, 0, 0);
        GX2CopySurface(&tempSurface, 0, 0,&target->surface, 0, 0);

        if(tempSurface.image != NULL) {
            free(tempSurface.image);
            tempSurface.image = NULL;
        }
    }
}

void drawTexture(GX2Texture * texture, GX2Sampler* sampler, float x, float y, int32_t width, int32_t height){
    float widthScaleFactor = 1.0f / (float)1280;
    float heightScaleFactor = 1.0f / (float)720;

    glm::vec3 positionOffsets = glm::vec3(0.0f);

    positionOffsets[0] = (x-((1280)/2)+(width/2)) * widthScaleFactor * 2.0f;
    positionOffsets[1] = -(y-((720)/2)+(height/2)) * heightScaleFactor * 2.0f;

    glm::vec3 scale(width*widthScaleFactor,height*heightScaleFactor,1.0f);

    Texture2DShader::instance()->setShaders();
    Texture2DShader::instance()->setAttributeBuffer();
    Texture2DShader::instance()->setAngle(0.0f);
    Texture2DShader::instance()->setOffset(positionOffsets);
    Texture2DShader::instance()->setScale(scale);
    Texture2DShader::instance()->setColorIntensity(glm::vec4(1.0f));
    Texture2DShader::instance()->setBlurring(glm::vec3(0.0f));
    Texture2DShader::instance()->setTextureAndSampler(texture, sampler);
    Texture2DShader::instance()->draw();
}

DECL_FUNCTION(void, GX2SetContextState, GX2ContextState * curContext) {
    if(curStatus == WUPS_APP_STATUS_FOREGROUND){
        originalContextSave = curContext;
    }
    real_GX2SetContextState(curContext);
}

ON_APP_STATUS_CHANGED(status){
    curStatus = status;

    if(status == WUPS_APP_STATUS_FOREGROUND){
        if (main_cbuf.surface.image) {
            free(main_cbuf.surface.image);
            main_cbuf.surface.image = NULL;
        }
        memset(&main_cbuf, 0, sizeof(GX2ColorBuffer));

        DEBUG_FUNCTION_LINE("VideoSquoosher: Moving to foreground\n");
    }
}

DECL_FUNCTION(void, GX2CopyColorBufferToScanBuffer, GX2ColorBuffer* cbuf, GX2ScanTarget target) {
    if(curStatus != WUPS_APP_STATUS_FOREGROUND){
        real_GX2CopyColorBufferToScanBuffer(cbuf, target);
        return;
    }

    if (!main_cbuf.surface.image) {
        freeUsedMemory();

        GX2InitColorBuffer(&main_cbuf,
            GX2_SURFACE_DIM_TEXTURE_2D,
            1280, 720, 1,
            GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
            (GX2AAMode)GX2_AA_MODE1X
        );

        if (main_cbuf.surface.imageSize) {
            main_cbuf.surface.image = memalign(
                main_cbuf.surface.alignment,
                main_cbuf.surface.imageSize
            );
            if(main_cbuf.surface.image == NULL){
                OSFatal("VideoSquoosher: Failed to alloc main_cbuf\n");
            }

            DEBUG_FUNCTION_LINE("VideoSquoosher: allocated %dx%d cbuf %08X\n",
                main_cbuf.surface.width,
                main_cbuf.surface.height,
                main_cbuf.surface.image);
        } else {
            DEBUG_FUNCTION_LINE("VideoSquoosher: GX2InitTexture failed for main_cbuf!\n");
        }

        GX2InitTexture(&drcTex,
            854, 480, 1, 0,
            GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
            GX2_SURFACE_DIM_TEXTURE_2D,
            GX2_TILE_MODE_LINEAR_ALIGNED
        );
        drcTex.surface.use = (GX2SurfaceUse)
            (GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);

        if (drcTex.surface.imageSize) {
            drcTex.surface.image = memalign(
                drcTex.surface.alignment,
                drcTex.surface.imageSize
            );
            if(drcTex.surface.image == NULL){
                OSFatal("VideoSquoosher: Failed to alloc drcTex\n");
            }

            DEBUG_FUNCTION_LINE("VideoSquoosher: allocated %dx%d drcTex %08X\n",
                drcTex.surface.width,
                drcTex.surface.height,
                drcTex.surface.image);
        } else {
            DEBUG_FUNCTION_LINE("VideoSquoosher: GX2InitTexture failed for drcTex!\n");
        }

        GX2InitTexture(&tvTex,
            1280, 720, 1, 0,
            GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
            GX2_SURFACE_DIM_TEXTURE_2D,
            GX2_TILE_MODE_LINEAR_ALIGNED
        );
        tvTex.surface.use = (GX2SurfaceUse)
            (GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);

        if (tvTex.surface.imageSize) {
            tvTex.surface.image = memalign(
                tvTex.surface.alignment,
                tvTex.surface.imageSize
            );
            if(tvTex.surface.image == NULL){
                OSFatal("VideoSquoosher: Failed to alloc tvTex\n");
            }

            DEBUG_FUNCTION_LINE("VideoSquoosher: allocated %dx%d tvTex %08X\n",
                tvTex.surface.width,
                tvTex.surface.height,
                tvTex.surface.image);
        } else {
            DEBUG_FUNCTION_LINE("VideoSquoosher: GX2InitTexture failed for tvTex!\n");
        }

        GX2InitSampler(&sampler,
            GX2_TEX_CLAMP_MODE_CLAMP,
            GX2_TEX_XY_FILTER_MODE_LINEAR
        );

        ownContextState = (GX2ContextState*)memalign(
            GX2_CONTEXT_STATE_ALIGNMENT,
            sizeof(GX2ContextState)
        );
        if(ownContextState == NULL){
            OSFatal("VideoSquoosher: Failed to alloc ownContextState\n");
        }
        GX2SetupContextStateEx(ownContextState, GX2_TRUE);

        GX2SetContextState(ownContextState);
        GX2SetColorBuffer(&main_cbuf, GX2_RENDER_TARGET_0);
        GX2SetContextState(originalContextSave);
    }

    if(main_cbuf.surface.image){
        if (target == GX2_SCAN_TARGET_DRC) {
            copyToTexture(cbuf,&drcTex);
        } else if (target == GX2_SCAN_TARGET_TV) {
            copyToTexture(cbuf,&tvTex);

            GX2SetContextState(ownContextState);

            GX2ClearColor(&main_cbuf, 1.0f, 0.0f, 0.0f, 1.0f);

            GX2SetContextState(ownContextState);

            GX2SetViewport(
                0.0f, 0.0f,
                main_cbuf.surface.width, main_cbuf.surface.height,
                0.0f, 1.0f
            );
            GX2SetScissor(
                0, 0,
                main_cbuf.surface.width, main_cbuf.surface.height
            );

            // draw DRC
            drawTexture(&drcTex, &sampler, 0, 0, 1280/2, 720);

            // draw TV
            drawTexture(&tvTex, &sampler, 1280/2, 0, 1280/2, 720);

            GX2SetContextState(originalContextSave);

            real_GX2CopyColorBufferToScanBuffer(&main_cbuf, target);
            return;
        }
    }else{
        DEBUG_FUNCTION_LINE("VideoSquoosher: main_cbuf.surface.image is null \n");
    }

    real_GX2CopyColorBufferToScanBuffer(cbuf, target);
}

WUPS_MUST_REPLACE(GX2CopyColorBufferToScanBuffer, WUPS_LOADER_LIBRARY_GX2, GX2CopyColorBufferToScanBuffer);
WUPS_MUST_REPLACE(GX2SetContextState, WUPS_LOADER_LIBRARY_GX2, GX2SetContextState);
