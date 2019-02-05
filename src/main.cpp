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

#include "shaders/Texture2DShader.h"
#include "shaders/ColorShader.h"
#include "SimpleColorDrawer.h"

/**
    Mandatory plugin information.
    If not set correctly, the loader will refuse to use the plugin.
**/
WUPS_PLUGIN_NAME("Example plugin");
WUPS_PLUGIN_DESCRIPTION("This is just an example plugin and will log the FSOpenFile function.");
WUPS_PLUGIN_VERSION("v1.0");
WUPS_PLUGIN_AUTHOR("Maschell");
WUPS_PLUGIN_LICENSE("BSD");

GX2ColorBuffer main_cbuf;
GX2Texture drcTex;
GX2Texture tvTex;
GX2Sampler sampler;
GX2ContextState* tvContextState;
bool copy = true;

/**
    Add this to one of your projects file to have access to SD/USB.
**/
WUPS_FS_ACCESS()

/**
    All of this defines can be used in ANY file.
    It's possible to split it up into multiple files.

**/

/**
    Get's called ONCE when the loader exits, but BEFORE the ON_APPLICATION_START gets called or functions are overridden.
**/
INITIALIZE_PLUGIN(){
    socket_lib_init();
    log_init();
	DEBUG_FUNCTION_LINE("INITIALIZE_PLUGIN of example_plugin!\n");
}


DEINITIALIZE_PLUGIN(){
    socket_lib_init();
    log_init();
    DEBUG_FUNCTION_LINE("DEINITIALIZE_PLUGIN of example_plugin!\n");
}

ON_APPLICATION_START(){
    socket_lib_init();
    log_init();

    DEBUG_FUNCTION_LINE("ON_APPLICATION_START of example_plugin!\n");
	
	//tvContextState = (GX2ContextState*)memalign(GX2_CONTEXT_STATE_ALIGNMENT,sizeof(GX2ContextState));
    //GX2SetupContextStateEx(tvContextState, GX2_TRUE);	
	
	//GX2SetContextState(tvContextState);
	//GX2SetColorBuffer(&main_cbuf, GX2_RENDER_TARGET_0);
}
ON_APPLICATION_ENDING(){
    DEBUG_FUNCTION_LINE("ON_APPLICATION_ENDING of example_plugin!\n");

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
	SimpleColorDrawer::destroyInstance();
    Texture2DShader::destroyInstance();
	ColorShader::destroyInstance();
	//if(tvContextState){
	//	free(tvContextState);
	//	tvContextState = NULL;
	//}
	
}

void copyToTexture(GX2ColorBuffer* sourceBuffer, GX2Texture * target){
	if (sourceBuffer->surface.aa == GX2_AA_MODE1X) {
		//DEBUG_FUNCTION_LINE("copy \n");
		// If AA is disabled, we can simply use GX2CopySurface.
		GX2CopySurface(&sourceBuffer->surface,
		sourceBuffer->viewMip,
		sourceBuffer->viewFirstSlice,
		&target->surface, 0, 0);
		
		GX2DrawDone();
	} else {
		DEBUG_FUNCTION_LINE("Resolve AA \n");
		// If AA is enabled, we need to resolve the AA buffer.
		GX2Surface tempSurface;
		tempSurface = sourceBuffer->surface;
		tempSurface.aa = GX2_AA_MODE1X;
		GX2CalcSurfaceSizeAndAlignment(&tempSurface);

		tempSurface.image = memalign(tempSurface.alignment,tempSurface.imageSize);
		if(tempSurface.image == NULL) {
			DEBUG_FUNCTION_LINE("failed to allocate data AA.\n");
			if(target->surface.image != NULL) {
				free(target->surface.image);
				target->surface.image = NULL;
			}
			DEBUG_FUNCTION_LINE("KILL ME \n");
			
		}
		GX2ResolveAAColorBuffer(sourceBuffer,&tempSurface, 0, 0);
		GX2CopySurface(&tempSurface, 0, 0,&target->surface, 0, 0);

		// Sync CPU and GPU
		GX2DrawDone();

		if(tempSurface.image != NULL) {
			free(tempSurface.image);
			tempSurface.image = NULL;
		}
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU, target->surface.image, target->surface.imageSize);
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

DECL_FUNCTION(void, GX2CopyColorBufferToScanBuffer, GX2ColorBuffer* cbuf, GX2ScanTarget target) {
    //DEBUG_FUNCTION_LINE("cbuf: %08X; target: %d\n", cbuf, target);

    if (!main_cbuf.surface.imageSize) {
		GX2InitColorBuffer(&main_cbuf, GX2_SURFACE_DIM_TEXTURE_2D, 854, 480, 1, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, (GX2AAMode)GX2_AA_MODE1X);
		GX2InitTexture(&drcTex,854,480,1,0,GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,GX2_SURFACE_DIM_TEXTURE_2D, GX2_TILE_MODE_LINEAR_ALIGNED);
		drcTex.surface.use =  (GX2SurfaceUse) (GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);
		
		GX2InitTexture(&tvTex,1280,720,1,0,GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,GX2_SURFACE_DIM_TEXTURE_2D, GX2_TILE_MODE_LINEAR_ALIGNED);
		tvTex.surface.use =  (GX2SurfaceUse) (GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);

        GX2InitSampler(&sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);

        if (main_cbuf.surface.imageSize) {
            main_cbuf.surface.image = memalign(main_cbuf.surface.alignment, main_cbuf.surface.imageSize);
			GX2Invalidate(GX2_INVALIDATE_MODE_CPU, main_cbuf.surface.image, main_cbuf.surface.imageSize);
            DEBUG_FUNCTION_LINE("allocated cbuf %08X\n", main_cbuf.surface.image);
        } else {
            DEBUG_FUNCTION_LINE("didn't allocate cbuf?\n");
        }
        if (drcTex.surface.imageSize) {
            drcTex.surface.image = memalign(drcTex.surface.alignment, drcTex.surface.imageSize);
			GX2Invalidate(GX2_INVALIDATE_MODE_CPU, drcTex.surface.image, drcTex.surface.imageSize);
            DEBUG_FUNCTION_LINE("allocated drcTex %08X\n", drcTex.surface.image);
        } else {
            DEBUG_FUNCTION_LINE("didn't allocate drcTex?\n");
        }
		if (tvTex.surface.imageSize) {
            tvTex.surface.image = memalign(tvTex.surface.alignment, tvTex.surface.imageSize);
			GX2Invalidate(GX2_INVALIDATE_MODE_CPU, tvTex.surface.image, tvTex.surface.imageSize);
            DEBUG_FUNCTION_LINE("allocated tvTex %08X\n", tvTex.surface.image);
        } else {
            DEBUG_FUNCTION_LINE("didn't allocate tvTex?\n");
        }
    }

    if (target == GX2_SCAN_TARGET_DRC) {
        if (main_cbuf.surface.image) {
			copyToTexture(cbuf,&drcTex);
        } else {
            //DEBUG_FUNCTION_LINE("oop\n");
        }
        real_GX2CopyColorBufferToScanBuffer(cbuf, target);
    } else if (target == GX2_SCAN_TARGET_TV) {
		if(copy){
			// When we copy the current frame, everything stop working.
			copyToTexture(cbuf,&tvTex);			
		}else{
			//GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_CPU_TEXTURE), cbuf->surface.image, cbuf->surface.imageSize);
				
			//GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
			//GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_NEVER);
			//GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);
			//GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
			
			//
			//copyToTexture(cbuf,&tvTex);
			//GX2DrawDone();
			
			//GX2ClearColor(cbuf, 0.0f, 0.0f, 0.0f, 1.0f);
			//GX2SetViewport(0.0f, 0.0f, cbuf->surface.width, cbuf->surface.height, 0.0f, 1.0f);
			//GX2SetScissor(0, 0, cbuf->surface.width, cbuf->surface.height);
			
			//GX2SetDepthOnlyControl(GX2_ENABLE, GX2_ENABLE, GX2_COMPARE_FUNC_LEQUAL);
			//GX2SetColorControl(GX2_LOGIC_OP_COPY, 1, GX2_DISABLE, GX2_ENABLE);
			//GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD, GX2_ENABLE, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD);
			//GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, GX2_DISABLE, GX2_ENABLE);
			
			//GX2DrawDone();
			
			// When we use GX2ClearColor everything stops working. This fills the whole screen white.
			SimpleColorDrawer::draw(0,0,1280,720);					
			
			// draw DRC
			drawTexture(&drcTex,&sampler, 0,0,1280/2,720);
			
			// draw TV
			drawTexture(&tvTex,&sampler, 1280/2,0,1280/2,720);
			
			GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_CPU_TEXTURE), cbuf->surface.image, cbuf->surface.imageSize);
			GX2Flush();	
			
			
			real_GX2CopyColorBufferToScanBuffer(cbuf, target);
		}
		copy = !copy;
		
		
    }
}

WUPS_MUST_REPLACE(GX2CopyColorBufferToScanBuffer, WUPS_LOADER_LIBRARY_GX2, GX2CopyColorBufferToScanBuffer);
