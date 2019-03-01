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
#include <gx2/texture.h>
#include <gx2/sampler.h>
#include <vpad/input.h>
#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <vpad/input.h>
#include <coreinit/memorymap.h>

#include "resources/Resources.h"
#include <coreinit/memexpheap.h>

#include <wups/config.h>
#include <wups/config/WUPSConfigItemIntegerRange.h>
#include <wups/config/WUPSConfigItemMultipleValues.h>
#include <wups/config/WUPSConfigItemBoolean.h>

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

GX2Texture imgTexture __attribute__((section(".data")));

int32_t curStatus = WUPS_APP_STATUS_BACKGROUND;

#define WUPS_SCREEN_DRC     0
#define WUPS_SCREEN_TV      1

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

screen_settings_min_max tv_minmax = {
    .width = { .min = 0, .max = 1280},
    .height = { .min = 0, .max = 720},
    .x_offset = { .min = -1280, .max = 1280},
    .y_offset = { .min = -720, .max = 720}
};

screen_settings_min_max drc_minmax = {
    .width = { .min = 0, .max = 1280},
    .height = { .min = 0, .max = 720},
    .x_offset = { .min = -1280, .max = 1280},
    .y_offset = { .min = -720, .max = 720}
};

screen_settings tv_screen_settings __attribute__((section(".data"))) = {.width = 640, .height = 720, .x_offset = 640, .y_offset = 0};
screen_settings drc_screen_settings __attribute__((section(".data"))) = {.width = 640, .height = 720, .x_offset = 0, .y_offset = 0};
int32_t foreground_screen __attribute__((section(".data"))) = WUPS_SCREEN_DRC;
bool interactive_mode = false;
int32_t interactive_mode_screen = WUPS_SCREEN_DRC;
float interactive_mode_fg_alpha = 1.0f;
float interactive_mode_bg_alpha = 0.3f;

float drcAlpha = 1.0f;
float tvAlpha = 1.0f;

/**
    Add this to one of your projects file to have access to SD/USB.
**/
WUPS_FS_ACCESS()

WUPS_USE_VIDEO_MEMORY()
WUPS_ALLOW_OVERLAY()

INITIALIZE_PLUGIN() {
    memset(&imgTexture, 0, sizeof(GX2Texture));
}


ON_APPLICATION_START() {
    socket_lib_init();
    log_init();

    interactive_mode = false;

    DEBUG_FUNCTION_LINE("VideoSquoosher: Hi!\n");
}

void tvWidthChanged(WUPSConfigItemIntegerRange * item, int newValue) {
    DEBUG_FUNCTION_LINE("TV width %d \n",newValue);
    tv_screen_settings.width = newValue;
    DCFlushRange(&tv_screen_settings.width, 4);
}

void tvHeightChanged(WUPSConfigItemIntegerRange * item, int newValue) {
    DEBUG_FUNCTION_LINE("TV height %d \n",newValue);
    tv_screen_settings.height = newValue;
    DCFlushRange(&tv_screen_settings.height, 4);
}

void tvXOffsetChanged(WUPSConfigItemIntegerRange * item, int newValue) {
    DEBUG_FUNCTION_LINE("TV X offset %d \n",newValue);
    tv_screen_settings.x_offset = newValue;
    DCFlushRange(&tv_screen_settings.x_offset, 4);
}

void tvYOffsetChanged(WUPSConfigItemIntegerRange * item, int newValue) {
    DEBUG_FUNCTION_LINE("TV Y offset %d \n",newValue);
    tv_screen_settings.y_offset = newValue;
    DCFlushRange(&tv_screen_settings.y_offset, 4);
}

void drcWidthChanged(WUPSConfigItemIntegerRange * item, int newValue) {
    DEBUG_FUNCTION_LINE("DRC width %d \n",newValue);
    drc_screen_settings.width = newValue;
    DCFlushRange(&drc_screen_settings.width, 4);
}

void drcHeightChanged(WUPSConfigItemIntegerRange * item, int newValue) {
    DEBUG_FUNCTION_LINE("DRC height %d \n",newValue);
    drc_screen_settings.height = newValue;
    DCFlushRange(&drc_screen_settings.height, 4);
}

void drcXOffsetChanged(WUPSConfigItemIntegerRange * item, int newValue) {
    DEBUG_FUNCTION_LINE("DRC X offset %d \n",newValue);
    drc_screen_settings.x_offset = newValue;
    DCFlushRange(&drc_screen_settings.x_offset, 4);
}

void drcYOffsetChanged(WUPSConfigItemIntegerRange * item, int newValue) {
    DEBUG_FUNCTION_LINE("DRC Y offset %d \n",newValue);
    drc_screen_settings.y_offset = newValue;
    DCFlushRange(&drc_screen_settings.y_offset, 4);
}

void foregroundChanged(WUPSConfigItemMultipleValues* configItem, int32_t newValue) {
    DEBUG_FUNCTION_LINE("Foreground value changed to %d \n",newValue);
    foreground_screen = newValue;
    DCFlushRange(&foreground_screen, 4);
}

WUPS_GET_CONFIG() {
    WUPSConfig* config = new WUPSConfig("VideoSquoosher");
    WUPSConfigCategory* catMain  = config->addCategory("Main");
    WUPSConfigCategory* catTV  = config->addCategory("TV-Screen");
    WUPSConfigCategory* catDRC = config->addCategory("DRC-Screen");

    std::map<int32_t,std::string> screenTypeValues;
    screenTypeValues[WUPS_SCREEN_DRC] = "DRC";
    screenTypeValues[WUPS_SCREEN_TV] = "TV";

    DCFlushRange(&tv_screen_settings, sizeof(tv_screen_settings));
    DCFlushRange(&drc_screen_settings, sizeof(drc_screen_settings));

    catMain->addItem(new WUPSConfigItemMultipleValues("foregrundscreen", "Screen in foreground", foreground_screen, screenTypeValues, foregroundChanged));

    catTV->addItem(new WUPSConfigItemIntegerRange("tvwidth",     "width",       tv_screen_settings.width,     tv_minmax.width.min,     tv_minmax.width.max,     tvWidthChanged));
    catTV->addItem(new WUPSConfigItemIntegerRange("tvheight",    "height",      tv_screen_settings.height,    tv_minmax.height.min,    tv_minmax.height.max,    tvHeightChanged));
    catTV->addItem(new WUPSConfigItemIntegerRange("tvxoffset",   "x offset",    tv_screen_settings.x_offset,  tv_minmax.x_offset.min,  tv_minmax.x_offset.max,  tvXOffsetChanged));
    catTV->addItem(new WUPSConfigItemIntegerRange("tvyoffset",   "y offset",    tv_screen_settings.y_offset,  tv_minmax.y_offset.min,  tv_minmax.y_offset.max,  tvYOffsetChanged));

    catDRC->addItem(new WUPSConfigItemIntegerRange("drcwidth",   "width",       drc_screen_settings.width,    drc_minmax.width.min,    drc_minmax.width.max,    drcWidthChanged));
    catDRC->addItem(new WUPSConfigItemIntegerRange("drcheight",  "height",      drc_screen_settings.height,   drc_minmax.height.min,   drc_minmax.height.max,   drcHeightChanged));
    catDRC->addItem(new WUPSConfigItemIntegerRange("drcxoffset", "x offset",    drc_screen_settings.x_offset, drc_minmax.x_offset.min, drc_minmax.x_offset.max, drcXOffsetChanged));
    catDRC->addItem(new WUPSConfigItemIntegerRange("drcyoffset", "y offset",    drc_screen_settings.y_offset, drc_minmax.y_offset.min, drc_minmax.y_offset.max, drcYOffsetChanged));

    return config;

}

ON_APPLICATION_ENDING() {
    DEBUG_FUNCTION_LINE("VideoSquoosher: shutting down...\n");
}

ON_APP_STATUS_CHANGED(status) {
    curStatus = status;

    if(status == WUPS_APP_STATUS_FOREGROUND) {
        DEBUG_FUNCTION_LINE("VideoSquoosher: Moving to foreground\n");
    }
}

ON_TV_TO_SCAN_BUFFER(args) {
    GX2ColorBuffer* cbuf = (GX2ColorBuffer*) args.color_buffer_ptr;
    GX2Texture*  tvTex = (GX2Texture*) args.tv_texture_ptr;
    GX2Texture*  drcTex = (GX2Texture*) args.drc_texture_ptr;
    GX2Sampler*  sampler = (GX2Sampler*) args.sampler_ptr;

    DCFlushRange(&drc_screen_settings, sizeof(drc_screen_settings));
    DCFlushRange(&tv_screen_settings, sizeof(tv_screen_settings));
    DCFlushRange(&foreground_screen, sizeof(foreground_screen));

    if(foreground_screen == WUPS_SCREEN_DRC) {
        // draw TV
        WUPS_DrawTexture((void*)tvTex, (void*)sampler, tv_screen_settings.x_offset, tv_screen_settings.y_offset, tv_screen_settings.width, tv_screen_settings.height, tvAlpha);
        // draw DRC
        WUPS_DrawTexture((void*)drcTex, (void*)sampler, drc_screen_settings.x_offset, drc_screen_settings.y_offset, drc_screen_settings.width, drc_screen_settings.height, drcAlpha);
    } else {
        // draw DRC
        WUPS_DrawTexture((void*)drcTex, (void*)sampler, drc_screen_settings.x_offset, drc_screen_settings.y_offset, drc_screen_settings.width, drc_screen_settings.height, drcAlpha);
        // draw TV
        WUPS_DrawTexture((void*)tvTex, (void*)sampler, tv_screen_settings.x_offset, tv_screen_settings.y_offset, tv_screen_settings.width, tv_screen_settings.height, tvAlpha);
    }

    // we init this ONCE. then it will be in memory for ever (until we reenter the plugin loader)
    if(!imgTexture.surface.image) {
        const uint8_t * img = Resources::GetFile("cat.png");
        uint32_t imgSize = Resources::GetFileSize("cat.png");
        if(!WUPS_ConvertImageToTexture(img, imgSize, &imgTexture)) {
            DEBUG_FUNCTION_LINE("Failed to convert Texture\n");
        }
        DEBUG_FUNCTION_LINE("Converted Texture\n");
    }

    if(imgTexture.surface.image != NULL) {
        WUPS_DrawTexture((void*)&imgTexture,(void*)sampler, 0,0, imgTexture.surface.width, imgTexture.surface.height, 1.0f);
    }
}

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer, uint32_t buffer_size, VPADReadError *error) {
    int32_t result = real_VPADRead(chan, buffer, buffer_size, error);
    if(result > 0) {
        if(interactive_mode) {
            if((buffer[0].trigger & VPAD_BUTTON_PLUS)) {
                interactive_mode = false;
                tvAlpha = interactive_mode_fg_alpha;
                drcAlpha = interactive_mode_fg_alpha;
                return result;
            }

            screen_settings * settings = &drc_screen_settings;
            if(interactive_mode_screen == WUPS_SCREEN_TV) {
                settings = &tv_screen_settings;
            }

            if((buffer[0].trigger & VPAD_BUTTON_R)) {
                settings->height = settings->width * (9.0f/16.0f);
            }

            if((buffer[0].trigger & VPAD_BUTTON_L)) {
                settings->width = settings->height * (16.0f/9.0f);
            }

            if((buffer[0].trigger & VPAD_BUTTON_MINUS)) {
                if(interactive_mode_screen == WUPS_SCREEN_DRC) {
                    interactive_mode_screen = WUPS_SCREEN_TV;
                } else if(interactive_mode_screen == WUPS_SCREEN_TV) {
                    interactive_mode_screen = WUPS_SCREEN_DRC;
                }
            }

            if(interactive_mode_screen == WUPS_SCREEN_DRC) {
                tvAlpha = interactive_mode_bg_alpha;
                drcAlpha = interactive_mode_fg_alpha;
            } else if(interactive_mode_screen == WUPS_SCREEN_TV) {
                tvAlpha = interactive_mode_fg_alpha;
                drcAlpha = interactive_mode_bg_alpha;
            }

            int32_t x_offset_delta = 0;
            int32_t y_offset_delta = 0;
            int32_t width_delta = 0;
            int32_t height_delta = 0;

            if(buffer[0].hold & VPAD_BUTTON_LEFT) {
                x_offset_delta = -4;
            }
            if(buffer[0].hold & VPAD_BUTTON_RIGHT) {
                x_offset_delta = 4;
            }
            if(buffer[0].hold & VPAD_BUTTON_UP) {
                y_offset_delta = -4;
            }
            if(buffer[0].hold & VPAD_BUTTON_DOWN) {
                y_offset_delta = 4;
            }

            if(buffer[0].hold & VPAD_BUTTON_Y) {
                width_delta = -4;
            }
            if(buffer[0].hold & VPAD_BUTTON_A) {
                width_delta = 4;
            }
            if(buffer[0].hold & VPAD_BUTTON_X) {
                height_delta = -4;
            }
            if(buffer[0].hold & VPAD_BUTTON_B) {
                height_delta = 4;
            }

            settings->x_offset += x_offset_delta;
            settings->y_offset += y_offset_delta;
            settings->width += width_delta;
            settings->height += height_delta;

            // reset stuff, so we don't do shit.
            buffer->hold = 0;
            buffer->trigger = 0;
            buffer->release = 0;
        } else {
            if((buffer[0].hold == (VPAD_BUTTON_PLUS | VPAD_BUTTON_ZR | VPAD_BUTTON_ZL))) {
                interactive_mode = true;
            }
        }
    }

    return result;
}

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);
