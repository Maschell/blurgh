/*  Copyright 2019 Ash Logan "quarktheawesome" <ash@heyquark.com>
    Copyright 2019 Maschell

    Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "shaders/Texture2DShader.h"
#include <utils/logger.h>

#include <whb/log_udp.h>

#include <wups.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemIntegerRange.h>
#include <wups/config/WUPSConfigItemMultipleValues.h>

#include <memory/mappedmemory.h>

#include <coreinit/cache.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/surface.h>
#include <vpad/input.h>

#include <cstring>

/**
    Mandatory plugin information.
    If not set correctly, the loader will refuse to use the plugin.
**/
WUPS_PLUGIN_NAME("VideoSquoosher");
WUPS_PLUGIN_DESCRIPTION("Squooshes the Gamepad and TV video side-by-side, "
                        "displaying that on your TV");
WUPS_PLUGIN_VERSION("v1.0");
WUPS_PLUGIN_AUTHOR("Maschell & quarktheawesome");
WUPS_PLUGIN_LICENSE("ISC");

WUPS_USE_STORAGE("VideoSquoosher"); // Use the storage API

GX2ColorBuffer *sMainColorBuffer            = nullptr;
GX2Texture *sDRCTex                   = nullptr;
GX2Texture *sTVTex                    = nullptr;
GX2Sampler *sSampler                  = nullptr;
GX2ContextState *sOwnContextState     = nullptr;
GX2ContextState *sOriginalContextState = nullptr;
bool sIsOnForeground                    = true;

#define WUPS_SCREEN_DRC 0
#define WUPS_SCREEN_TV  1

void freeUsedMemory();

typedef struct min_max_pair_ {
    int32_t min;
    int32_t max;
} min_max_pair;

typedef struct screen_settings_min_max_ {
    min_max_pair width;
    min_max_pair height;
    min_max_pair x_offset;
    min_max_pair y_offset;
} screen_settings_min_max;

typedef struct screen_settings_ {
    int32_t width;
    int32_t height;
    int32_t x_offset;
    int32_t y_offset;
} screen_settings;

constexpr screen_settings_min_max sTVMinMax = {
        .width    = {.min = 0, .max = 1280},
        .height   = {.min = 0, .max = 720},
        .x_offset = {.min = -1280, .max = 1280},
        .y_offset = {.min = -720, .max = 720}};

constexpr  screen_settings_min_max sDRCMinMax = {
        .width    = {.min = 0, .max = 1280},
        .height   = {.min = 0, .max = 720},
        .x_offset = {.min = -1280, .max = 1280},
        .y_offset = {.min = -720, .max = 720}};

#define TV_DEFAULT_WIDTH         640
#define TV_DEFAULT_HEIGHT        720
#define TV_DEFAULT_X_OFFSET      640
#define TV_DEFAULT_Y_OFFSET      0

#define DRC_DEFAULT_WIDTH        640
#define DRC_DEFAULT_HEIGHT       720
#define DRC_DEFAULT_X_OFFSET     0
#define DRC_DEFAULT_Y_OFFSET     0

#define TV_CONFIG_WIDTH          "tvwidth"
#define TV_CONFIG_HEIGHT         "tvheight"
#define TV_CONFIG_X_OFFSET       "tvxoffset"
#define TV_CONFIG_Y_OFFSET       "tvyoffset"

#define DRC_CONFIG_WIDTH         "drcwidth"
#define DRC_CONFIG_HEIGHT        "tvheight"
#define DRC_CONFIG_X_OFFSET      "drcxoffset"
#define DRC_CONFIG_Y_OFFSET      "drcyoffset"
#define CONFIG_FOREGROUND_SCREEN "foregroundscreen"

screen_settings sTVScreenSettings  = {.width = TV_DEFAULT_WIDTH, .height = TV_DEFAULT_HEIGHT, .x_offset = TV_DEFAULT_X_OFFSET, .y_offset = TV_DEFAULT_Y_OFFSET};
screen_settings sDRCScreenSettings = {.width = DRC_DEFAULT_WIDTH, .height = DRC_DEFAULT_HEIGHT, .x_offset = DRC_DEFAULT_X_OFFSET, .y_offset = DRC_DEFAULT_Y_OFFSET};

#define DEFAULT_SQOOSHER_ACTIVE   true
#define DEFAULT_FOREGROUND_SCREEN WUPS_SCREEN_DRC
uint32_t sForegroundScreen      = DEFAULT_FOREGROUND_SCREEN;
bool sSqoosherActive             = DEFAULT_SQOOSHER_ACTIVE;
bool sIsInteractiveMode           = false;
int32_t sInteractiveModeScreen = WUPS_SCREEN_DRC;
float sInteractiveModeFGAlpha = 1.0f;
float sInteractiveModeBGAlpha = 0.3f;

float sDRCAlpha = 1.0f;
float sTVAlpha  = 1.0f;


WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle);

void ConfigMenuClosedCallback();

INITIALIZE_PLUGIN() {
    WUPSConfigAPIOptionsV1 configOptions = {.name = "example_plugin_cpp"};
    if (WUPSConfigAPI_Init(configOptions, ConfigMenuOpenedCallback, ConfigMenuClosedCallback) !=
        WUPSCONFIG_API_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE("Failed to init config api");
    }

    WUPSStorageError storageRes;
    if ((storageRes = WUPSStorageAPI::GetOrStoreDefault("sqoosherActive", sSqoosherActive, DEFAULT_SQOOSHER_ACTIVE)) !=
        WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE("GetOrStoreDefault failed: %s (%d)", WUPSStorageAPI_GetStatusStr(storageRes), storageRes);
    }

    WUPSStorageAPI::GetOrStoreDefault(TV_CONFIG_WIDTH, sTVScreenSettings.width, TV_DEFAULT_WIDTH);
    WUPSStorageAPI::GetOrStoreDefault(TV_CONFIG_HEIGHT, sTVScreenSettings.height, TV_DEFAULT_HEIGHT);
    WUPSStorageAPI::GetOrStoreDefault(TV_CONFIG_X_OFFSET, sTVScreenSettings.x_offset, TV_DEFAULT_X_OFFSET);
    WUPSStorageAPI::GetOrStoreDefault(TV_CONFIG_Y_OFFSET, sTVScreenSettings.y_offset, TV_DEFAULT_Y_OFFSET);

    WUPSStorageAPI::GetOrStoreDefault(DRC_CONFIG_WIDTH, sDRCScreenSettings.width, DRC_DEFAULT_WIDTH);
    WUPSStorageAPI::GetOrStoreDefault(DRC_CONFIG_HEIGHT, sDRCScreenSettings.height, DRC_DEFAULT_HEIGHT);
    WUPSStorageAPI::GetOrStoreDefault(DRC_CONFIG_X_OFFSET, sDRCScreenSettings.x_offset, DRC_DEFAULT_X_OFFSET);
    WUPSStorageAPI::GetOrStoreDefault(DRC_CONFIG_Y_OFFSET, sDRCScreenSettings.y_offset, DRC_DEFAULT_Y_OFFSET);
    WUPSStorageAPI::GetOrStoreDefault(CONFIG_FOREGROUND_SCREEN, sForegroundScreen, static_cast<uint32_t>(DEFAULT_SQOOSHER_ACTIVE));
    if ((storageRes = WUPSStorageAPI::SaveStorage()) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE("GetOrStoreDefault failed: %s (%d)", WUPSStorageAPI_GetStatusStr(storageRes), storageRes);
    }
}

ON_APPLICATION_START() {
    WHBLogUdpInit();

    DEBUG_FUNCTION_LINE("VideoSquoosher: Hi!");

    freeUsedMemory();

    sIsOnForeground = true;
    DEBUG_FUNCTION_LINE("onForeground %d", sIsOnForeground);
}

void tvWidthChanged(ConfigItemIntegerRange *item, int newValue) {
    DEBUG_FUNCTION_LINE("TV width %d \n", newValue);
    sTVScreenSettings.width = newValue;
    WUPSStorageAPI::Store(TV_CONFIG_WIDTH, sTVScreenSettings.width);
}

void tvHeightChanged(ConfigItemIntegerRange *item, int newValue) {
    DEBUG_FUNCTION_LINE("TV height %d \n", newValue);
    sTVScreenSettings.height = newValue;
    WUPSStorageAPI::Store(TV_CONFIG_HEIGHT, sTVScreenSettings.height);
}

void tvXOffsetChanged(ConfigItemIntegerRange *item, int newValue) {
    DEBUG_FUNCTION_LINE("TV X offset %d \n", newValue);
    sTVScreenSettings.x_offset = newValue;
    WUPSStorageAPI::Store(TV_CONFIG_X_OFFSET, sTVScreenSettings.x_offset);
}


void tvYOffsetChanged(ConfigItemIntegerRange *item, int newValue) {
    DEBUG_FUNCTION_LINE("TV Y offset %d \n", newValue);
    sTVScreenSettings.y_offset = newValue;
    WUPSStorageAPI::Store(TV_CONFIG_Y_OFFSET, sTVScreenSettings.y_offset);
}

void drcWidthChanged(ConfigItemIntegerRange *item, int newValue) {
    DEBUG_FUNCTION_LINE("DRC width %d \n", newValue);
    sDRCScreenSettings.width = newValue;
    WUPSStorageAPI::Store(DRC_CONFIG_WIDTH, sDRCScreenSettings.width);
}

void drcHeightChanged(ConfigItemIntegerRange *item, int newValue) {
    DEBUG_FUNCTION_LINE("DRC height %d \n", newValue);
    sDRCScreenSettings.height = newValue;
    WUPSStorageAPI::Store(DRC_CONFIG_HEIGHT, sDRCScreenSettings.height);
}

void drcXOffsetChanged(ConfigItemIntegerRange *item, int newValue) {
    DEBUG_FUNCTION_LINE("DRC X offset %d \n", newValue);
    sDRCScreenSettings.x_offset = newValue;
    WUPSStorageAPI::Store(DRC_CONFIG_X_OFFSET, sDRCScreenSettings.x_offset);
}

void drcYOffsetChanged(ConfigItemIntegerRange *item, int newValue) {
    DEBUG_FUNCTION_LINE("DRC Y offset %d \n", newValue);
    sDRCScreenSettings.y_offset = newValue;
    WUPSStorageAPI::Store(DRC_CONFIG_Y_OFFSET, sDRCScreenSettings.y_offset);
}

void foregroundChanged(ConfigItemMultipleValues *item, uint32_t value) {
    DEBUG_FUNCTION_LINE("Foreground value changed to %d \n", value);
    sForegroundScreen = value;
    WUPSStorageAPI::Store(CONFIG_FOREGROUND_SCREEN, sForegroundScreen);
}

void activeCallback(ConfigItemBoolean *item, bool value) {
    DEBUG_FUNCTION_LINE("Foreground value changed to %d \n", value);
    sSqoosherActive = value;
    WUPSStorageAPI::Store("sqoosherActive", sSqoosherActive);
}

WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
    WUPSConfigCategory root = WUPSConfigCategory(rootHandle);
    try {
        // Add a boolean item to this newly created category
        root.add(WUPSConfigItemBoolean::Create("active", "Sqoosher active",
                                               DEFAULT_SQOOSHER_ACTIVE, sSqoosherActive,
                                               activeCallback));

        constexpr WUPSConfigItemMultipleValues::ValuePair possibleValues[] = {
                {WUPS_SCREEN_TV, "TV"},
                {WUPS_SCREEN_DRC, "DRC"},
        };

        root.add(WUPSConfigItemMultipleValues::CreateFromValue("foregroundscreen", "Screen in foreground",
                                                               WUPS_SCREEN_TV, sForegroundScreen,
                                                               possibleValues,
                                                               foregroundChanged));

        auto tvCat  = WUPSConfigCategory::Create("TV");
        auto drcCat = WUPSConfigCategory::Create("DRC");

        tvCat.add(WUPSConfigItemIntegerRange::Create("tvwidth", "width",
                                                     TV_DEFAULT_WIDTH, sTVScreenSettings.width,
                                                     sTVMinMax.width.min, sTVMinMax.width.max,
                                                     tvWidthChanged));
        tvCat.add(WUPSConfigItemIntegerRange::Create("tvheight", "height",
                                                     TV_DEFAULT_HEIGHT, sTVScreenSettings.height,
                                                     sTVMinMax.height.min, sTVMinMax.height.max,
                                                     tvHeightChanged));
        tvCat.add(WUPSConfigItemIntegerRange::Create("tvxoffset", "x offset",
                                                     TV_DEFAULT_HEIGHT, sTVScreenSettings.x_offset,
                                                     sTVMinMax.x_offset.min, sTVMinMax.x_offset.max,
                                                     tvXOffsetChanged));
        tvCat.add(WUPSConfigItemIntegerRange::Create("tvyoffset", "y offset",
                                                     TV_DEFAULT_HEIGHT, sTVScreenSettings.y_offset,
                                                     sTVMinMax.y_offset.min, sTVMinMax.y_offset.max,
                                                     tvYOffsetChanged));

        drcCat.add(WUPSConfigItemIntegerRange::Create("drcwidth", "width",
                                                      TV_DEFAULT_WIDTH, sDRCScreenSettings.width,
                                                      sDRCMinMax.width.min, sDRCMinMax.width.max,
                                                      drcWidthChanged));
        drcCat.add(WUPSConfigItemIntegerRange::Create("drcheight", "height",
                                                      TV_DEFAULT_HEIGHT, sDRCScreenSettings.height,
                                                      sDRCMinMax.height.min, sDRCMinMax.height.max,
                                                      drcHeightChanged));
        drcCat.add(WUPSConfigItemIntegerRange::Create("drcxoffset", "x offset",
                                                      TV_DEFAULT_HEIGHT, sDRCScreenSettings.x_offset,
                                                      sDRCMinMax.x_offset.min, sDRCMinMax.x_offset.max,
                                                      drcXOffsetChanged));
        drcCat.add(WUPSConfigItemIntegerRange::Create("drcyoffset", "y offset",
                                                      TV_DEFAULT_HEIGHT, sDRCScreenSettings.y_offset,
                                                      sDRCMinMax.y_offset.min, sDRCMinMax.y_offset.max,
                                                      drcYOffsetChanged));

        root.add(std::move(tvCat));
        root.add(std::move(drcCat));
    } catch (std::exception &e) {
        DEBUG_FUNCTION_LINE("Creating config menu failed: %s", e.what());
        return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
    }


    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

void ConfigMenuClosedCallback() {
    WUPSStorageAPI::SaveStorage();
}


void freeUsedMemory() {
    if (sMainColorBuffer) {
        if (sMainColorBuffer->surface.image) {
            MEMFreeToMappedMemory(sMainColorBuffer->surface.image);
            sMainColorBuffer->surface.image = nullptr;
        }
        MEMFreeToMappedMemory(sMainColorBuffer);
        sMainColorBuffer = nullptr;
    }
    if (sTVTex) {
        if (sTVTex->surface.image) {
            MEMFreeToMappedMemory(sTVTex->surface.image);
            sTVTex->surface.image = nullptr;
        }
        MEMFreeToMappedMemory(sTVTex);
        sTVTex = nullptr;
    }
    if (sDRCTex) {
        if (sDRCTex->surface.image) {
            MEMFreeToMappedMemory(sDRCTex->surface.image);
            sDRCTex->surface.image = nullptr;
        }
        MEMFreeToMappedMemory(sDRCTex);
        sDRCTex = nullptr;
    }

    if (sSampler) {
        MEMFreeToMappedMemory(sSampler);
        sSampler = nullptr;
    }

    if (sOwnContextState) {
        MEMFreeToMappedMemory(sOwnContextState);
        sOwnContextState = nullptr;
    }
}

ON_APPLICATION_REQUESTS_EXIT() {
    DEBUG_FUNCTION_LINE("VideoSquoosher: shutting down...");

    freeUsedMemory();

    Texture2DShader::destroyInstance();
    sIsOnForeground = false;
}

void copyToTexture(GX2ColorBuffer *sourceBuffer, GX2Texture *target) {
    if (sourceBuffer == nullptr || target == nullptr) {
        return;
    }
    if (sourceBuffer->surface.aa == GX2_AA_MODE1X) {
        // If AA is disabled, we can simply use GX2CopySurface.
        GX2CopySurface(&sourceBuffer->surface,
                       sourceBuffer->viewMip,
                       sourceBuffer->viewFirstSlice,
                       &target->surface, 0, 0);
        GX2DrawDone();
    } else {
        // If AA is enabled, we need to resolve the AA buffer.

        // Allocate surface to resolve buffer onto
        GX2Surface tempSurface;
        tempSurface    = sourceBuffer->surface;
        tempSurface.aa = GX2_AA_MODE1X;
        GX2CalcSurfaceSizeAndAlignment(&tempSurface);


        tempSurface.image = MEMAllocFromMappedMemoryForGX2Ex(
                tempSurface.imageSize,
                tempSurface.alignment);
        if (tempSurface.image == nullptr) {
            DEBUG_FUNCTION_LINE("VideoSquoosher: failed to allocate AA surface");
            if (target->surface.image != nullptr) {
                MEMFreeToMappedMemory(target->surface.image);
                target->surface.image = nullptr;
            }
            return;
        }

        // Resolve, then copy result to target
        GX2ResolveAAColorBuffer(sourceBuffer, &tempSurface, 0, 0);
        GX2CopySurface(&tempSurface, 0, 0, &target->surface, 0, 0);

        if (tempSurface.image != nullptr) {
            MEMFreeToMappedMemory(tempSurface.image);
            tempSurface.image = nullptr;
        }
        GX2DrawDone();
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, target->surface.image, target->surface.imageSize);
    }
}

void drawTexture(GX2Texture *texture, GX2Sampler *_sampler, float x, float y, int32_t width, int32_t height,
                 float alpha = 1.0f) {
    float widthScaleFactor  = 1.0f / (float) 1280;
    float heightScaleFactor = 1.0f / (float) 720;

    auto positionOffsets = glm::vec3(0.0f);

    positionOffsets[0] = (x - ((1280.0f) / 2) + (width / 2.0f)) * widthScaleFactor * 2.0f;
    positionOffsets[1] = -(y - ((720.0f) / 2) + (height / 2.0f)) * heightScaleFactor * 2.0f;

    glm::vec3 scale(width * widthScaleFactor, height * heightScaleFactor, 1.0f);

    Texture2DShader::instance()->setShaders();
    Texture2DShader::instance()->setAttributeBuffer();
    Texture2DShader::instance()->setAngle(0.0f);
    Texture2DShader::instance()->setOffset(positionOffsets);
    Texture2DShader::instance()->setScale(scale);
    Texture2DShader::instance()->setColorIntensity(glm::vec4(alpha));
    Texture2DShader::instance()->setBlurring(glm::vec3(0.0f));
    Texture2DShader::instance()->setTextureAndSampler(texture, _sampler);
    Texture2DShader::draw();
}

DECL_FUNCTION(void, GX2SetContextState, GX2ContextState *curContext) {
    if (sIsOnForeground && sSqoosherActive) {
        sOriginalContextState = curContext;
    }
    real_GX2SetContextState(curContext);
}

ON_ACQUIRED_FOREGROUND() {
    freeUsedMemory();
    sIsOnForeground = true;
    DCFlushRange(&sIsOnForeground, 4);
    DEBUG_FUNCTION_LINE("VideoSquoosher: Move to foreground");
}

ON_RELEASE_FOREGROUND() {
    freeUsedMemory();
    sIsOnForeground = false;
    DCFlushRange(&sIsOnForeground, 4);

    DEBUG_FUNCTION_LINE("VideoSquoosher: Release foreground");
}

DECL_FUNCTION(void, GX2CopyColorBufferToScanBuffer, GX2ColorBuffer *cbuf, GX2ScanTarget target) {
    if (!sSqoosherActive || !sIsOnForeground) {
        real_GX2CopyColorBufferToScanBuffer(cbuf, target);
        return;
    }
    if (!sMainColorBuffer || !sMainColorBuffer->surface.image) {
        freeUsedMemory();
        sMainColorBuffer = (GX2ColorBuffer *) MEMAllocFromMappedMemoryForGX2Ex(sizeof(GX2ColorBuffer), 0x40);

        GX2InitColorBuffer(sMainColorBuffer,
                           GX2_SURFACE_DIM_TEXTURE_2D,
                           1280, 720, 1,
                           GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
                           (GX2AAMode) GX2_AA_MODE1X);

        if (sMainColorBuffer->surface.imageSize) {
            sMainColorBuffer->surface.image = MEMAllocFromMappedMemoryForGX2Ex(
                    sMainColorBuffer->surface.imageSize,
                    sMainColorBuffer->surface.alignment);
            if (sMainColorBuffer->surface.image == nullptr) {
                OSFatal("VideoSquoosher: Failed to alloc main_cbuf");
            }

            DEBUG_FUNCTION_LINE("VideoSquoosher: allocated %dx%d cbuf %08X %08x",
                                sMainColorBuffer->surface.width,
                                sMainColorBuffer->surface.height,
                                sMainColorBuffer->surface.image,
                                sMainColorBuffer->surface.imageSize);
        } else {
            DEBUG_FUNCTION_LINE("VideoSquoosher: GX2InitTexture failed for main_cbuf!");
        }

        sDRCTex = (GX2Texture *) MEMAllocFromMappedMemoryForGX2Ex(sizeof(GX2Texture), 0x40);
        if (sDRCTex == nullptr) {
            OSFatal("alloc drcTex failed");
        }

        GX2InitTexture(sDRCTex,
                       854, 480, 1, 0,
                       GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
                       GX2_SURFACE_DIM_TEXTURE_2D,
                       GX2_TILE_MODE_LINEAR_ALIGNED);
        sDRCTex->surface.use = (GX2SurfaceUse) (GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);

        if (sDRCTex->surface.imageSize) {
            sDRCTex->surface.image = MEMAllocFromMappedMemoryForGX2Ex(
                    sDRCTex->surface.imageSize,
                    sDRCTex->surface.alignment);
            if (sDRCTex->surface.image == nullptr) {
                OSFatal("VideoSquoosher: Failed to alloc drcTex");
            }

            GX2Invalidate(GX2_INVALIDATE_MODE_CPU, sDRCTex->surface.image, sDRCTex->surface.imageSize);
            DEBUG_FUNCTION_LINE("VideoSquoosher: allocated %dx%d drcTex %08X",
                                sDRCTex->surface.width,
                                sDRCTex->surface.height,
                                sDRCTex->surface.image);
        } else {
            DEBUG_FUNCTION_LINE("VideoSquoosher: GX2InitTexture failed for drcTex!");
        }
        DCFlushRange(sDRCTex, sizeof(GX2Texture));

        sTVTex = (GX2Texture *) MEMAllocFromMappedMemoryForGX2Ex(sizeof(GX2Texture), 0x40);
        if (sTVTex == nullptr) {
            OSFatal("alloc tvTex failed");
        }

        GX2InitTexture(sTVTex,
                       1280, 720, 1, 0,
                       GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
                       GX2_SURFACE_DIM_TEXTURE_2D,
                       GX2_TILE_MODE_LINEAR_ALIGNED);
        sTVTex->surface.use = (GX2SurfaceUse) (GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);

        if (sTVTex->surface.imageSize) {
            sTVTex->surface.image = MEMAllocFromMappedMemoryForGX2Ex(
                    sTVTex->surface.imageSize,
                    sTVTex->surface.alignment);
            if (sTVTex->surface.image == nullptr) {
                OSFatal("VideoSquoosher: Failed to alloc tvTex");
            }

            DEBUG_FUNCTION_LINE("VideoSquoosher: allocated %dx%d tvTex %08X",
                                sTVTex->surface.width,
                                sTVTex->surface.height,
                                sTVTex->surface.image);
        } else {
            DEBUG_FUNCTION_LINE("VideoSquoosher: GX2InitTexture failed for tvTex!");
        }

        sSampler = (GX2Sampler *) MEMAllocFromMappedMemoryForGX2Ex(sizeof(GX2Sampler), 0x40);
        if (sSampler == nullptr) {
            OSFatal("alloc sampler failed");
        }

        GX2InitSampler(sSampler,
                       GX2_TEX_CLAMP_MODE_CLAMP,
                       GX2_TEX_XY_FILTER_MODE_LINEAR);

        DCFlushRange(sSampler, sizeof(GX2Sampler));
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, sSampler, sizeof(GX2Sampler));

        sOwnContextState = (GX2ContextState *) MEMAllocFromMappedMemoryForGX2Ex(
                sizeof(GX2ContextState),
                GX2_CONTEXT_STATE_ALIGNMENT);
        if (sOwnContextState == nullptr) {
            OSFatal("VideoSquoosher: Failed to alloc ownContextState");
        }

        GX2SetupContextStateEx(sOwnContextState, GX2_TRUE);
        DCInvalidateRange(sOwnContextState, sizeof(GX2ContextState)); // Important!

        real_GX2SetContextState(sOwnContextState);
        GX2SetColorBuffer(sMainColorBuffer, GX2_RENDER_TARGET_0);
        real_GX2SetContextState(sOriginalContextState);
    }

    if (sMainColorBuffer && sMainColorBuffer->surface.image) {
        if (target == GX2_SCAN_TARGET_DRC) {
            copyToTexture(cbuf, sDRCTex);
            DCFlushRange(&sDRCTex, sizeof(GX2Texture));
        } else if (target == GX2_SCAN_TARGET_TV) {
            copyToTexture(cbuf, sTVTex);
            DCFlushRange(&sTVTex, sizeof(GX2Texture));
        }

        if (target != GX2_SCAN_TARGET_TV) {
            real_GX2CopyColorBufferToScanBuffer(cbuf, target);
            return;
        }

        real_GX2SetContextState(sOwnContextState);
        GX2ClearColor(sMainColorBuffer, 0.0f, 0.0f, 0.0f, 1.0f);
        real_GX2SetContextState(sOwnContextState);

        GX2SetViewport(
                0.0f, 0.0f,
                sMainColorBuffer->surface.width, sMainColorBuffer->surface.height,
                0.0f, 1.0f);
        GX2SetScissor(
                0, 0,
                sMainColorBuffer->surface.width, sMainColorBuffer->surface.height);

        if (sForegroundScreen == WUPS_SCREEN_DRC) {
            // draw TV
            drawTexture(sTVTex, sSampler, sTVScreenSettings.x_offset, sTVScreenSettings.y_offset, sTVScreenSettings.width,
                        sTVScreenSettings.height, sTVAlpha);
            // draw DRC
            drawTexture(sDRCTex, sSampler, sDRCScreenSettings.x_offset, sDRCScreenSettings.y_offset,
                        sDRCScreenSettings.width, sDRCScreenSettings.height, sDRCAlpha);
        } else {
            // draw DRC
            drawTexture(sDRCTex, sSampler, sDRCScreenSettings.x_offset, sDRCScreenSettings.y_offset,
                        sDRCScreenSettings.width, sDRCScreenSettings.height, sDRCAlpha);
            // draw TV
            drawTexture(sTVTex, sSampler, sTVScreenSettings.x_offset, sTVScreenSettings.y_offset, sTVScreenSettings.width,
                        sTVScreenSettings.height, sTVAlpha);
        }

        real_GX2SetContextState(sOriginalContextState);

        real_GX2CopyColorBufferToScanBuffer(sMainColorBuffer, target);
        return;
    } else {
        DEBUG_FUNCTION_LINE("VideoSquoosher: main_cbuf.surface.image is null ");
    }

    real_GX2CopyColorBufferToScanBuffer(cbuf, target);
}

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer, uint32_t buffer_size, VPADReadError *error) {
    VPADReadError real_error;
    int32_t result = real_VPADRead(chan, buffer, buffer_size, &real_error);
    if (sSqoosherActive && result > 0 && real_error == VPAD_READ_SUCCESS) {
        if (sIsInteractiveMode) {
            if ((buffer[0].trigger & VPAD_BUTTON_PLUS)) {
                sIsInteractiveMode = false;
                sTVAlpha          = sInteractiveModeFGAlpha;
                sDRCAlpha         = sInteractiveModeFGAlpha;
                return result;
            }

            screen_settings *settings = &sDRCScreenSettings;
            if (sInteractiveModeScreen == WUPS_SCREEN_TV) {
                settings = &sTVScreenSettings;
            }

            if ((buffer[0].trigger & VPAD_BUTTON_R)) {
                settings->height = settings->width * (9.0f / 16.0f);
            }

            if ((buffer[0].trigger & VPAD_BUTTON_L)) {
                settings->width = settings->height * (16.0f / 9.0f);
            }

            if ((buffer[0].trigger & VPAD_BUTTON_MINUS)) {
                if (sInteractiveModeScreen == WUPS_SCREEN_DRC) {
                    sInteractiveModeScreen = WUPS_SCREEN_TV;
                } else if (sInteractiveModeScreen == WUPS_SCREEN_TV) {
                    sInteractiveModeScreen = WUPS_SCREEN_DRC;
                }
            }

            if (sInteractiveModeScreen == WUPS_SCREEN_DRC) {
                sTVAlpha  = sInteractiveModeBGAlpha;
                sDRCAlpha = sInteractiveModeFGAlpha;
            } else if (sInteractiveModeScreen == WUPS_SCREEN_TV) {
                sTVAlpha  = sInteractiveModeFGAlpha;
                sDRCAlpha = sInteractiveModeBGAlpha;
            }

            int32_t x_offset_delta = 0;
            int32_t y_offset_delta = 0;
            int32_t width_delta    = 0;
            int32_t height_delta   = 0;

            if (buffer[0].hold & VPAD_BUTTON_LEFT) {
                x_offset_delta = -4;
            }
            if (buffer[0].hold & VPAD_BUTTON_RIGHT) {
                x_offset_delta = 4;
            }
            if (buffer[0].hold & VPAD_BUTTON_UP) {
                y_offset_delta = -4;
            }
            if (buffer[0].hold & VPAD_BUTTON_DOWN) {
                y_offset_delta = 4;
            }

            if (buffer[0].hold & VPAD_BUTTON_Y) {
                width_delta = -4;
            }
            if (buffer[0].hold & VPAD_BUTTON_A) {
                width_delta = 4;
            }
            if (buffer[0].hold & VPAD_BUTTON_X) {
                height_delta = -4;
            }
            if (buffer[0].hold & VPAD_BUTTON_B) {
                height_delta = 4;
            }

            settings->x_offset += x_offset_delta;
            settings->y_offset += y_offset_delta;
            settings->width += width_delta;
            settings->height += height_delta;

            // reset stuff, so we don't do shit.
            buffer->hold    = 0;
            buffer->trigger = 0;
            buffer->release = 0;
        } else {
            if (buffer[0].hold == (VPAD_BUTTON_PLUS | VPAD_BUTTON_ZR | VPAD_BUTTON_ZL)) {
                sIsInteractiveMode = true;
            }
        }
    }
    if (error) {
        *error = real_error;
    }

    return result;
}

WUPS_MUST_REPLACE_FOR_PROCESS(GX2CopyColorBufferToScanBuffer, WUPS_LOADER_LIBRARY_GX2, GX2CopyColorBufferToScanBuffer,
                              WUPS_FP_TARGET_PROCESS_ALL);
WUPS_MUST_REPLACE_FOR_PROCESS(GX2SetContextState, WUPS_LOADER_LIBRARY_GX2, GX2SetContextState,
                              WUPS_FP_TARGET_PROCESS_ALL);
WUPS_MUST_REPLACE_FOR_PROCESS(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead, WUPS_FP_TARGET_PROCESS_ALL);
