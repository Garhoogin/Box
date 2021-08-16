#include <nds.h>

#include "io.h"
#include "scenes.h"
#include "mi.h"

static int g_setupLastTick = 0;
static int g_setupStart = 0;

#define BLDCNT            (*(u16*)0x04000050)
#define BLDALPHA          (*(u16*)0x04000052)
#define BLDY              (*(u16*)0x04000054)

#define DB_BLDCNT         (*(u16*)0x04001050)
#define DB_BLDALPHA       (*(u16*)0x04001052)
#define DB_BLDY           (*(u16*)0x04001054)

static int g_setupCpuSliderX;
static int g_setupCpuSliderVelocity;

static int g_setupFadingOut = FALSE;
static int g_setupFadeOutStart = 0;
static int g_setupFadeOutToScene = SCENE_TITLE;

void setupSetCpuSlider(int x, int y){
	oamSet(&oamSub, 0, x, y, 0,
		g_cpuPlayers, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 0), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamUpdate(&oamSub);
}

void setupSetPlayersSpinner(int x, int y){
	int pal1 = g_nPlayers == 2 ? 2 : 0;
	int pal2 = g_nPlayers == 8 ? 2 : 1;
	oamSet(&oamSub, 1, x, y, 0,
		pal1, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 1), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 2, x + 64, y, 0,
		pal2, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 2), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamUpdate(&oamSub);
}

void setupSetBackButton(int x, int y, int pal){
	if(y > 192) y = 192;
	oamSet(&oamSub, 3, x, y, 0,
		pal, SpriteSize_32x32, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, (2 << 7)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamUpdate(&oamSub);
}

void setupSetPlayButton(int x, int y, int pal){
	if(y > 192) y = 192;
	oamSet(&oamSub, 5, x, y, 0,
		pal, SpriteSize_32x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 0xD | (1 << 6)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 6, x + 32, y, 0,
		pal, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 0xF | (1 << 6)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamUpdate(&oamSub);
}

void setupSetPlayerCountLabel(int x, int y, int n){
	//annoying swizzling of character index
	int index = 6 + (n << 1);
	index = ((index & 0xF) >> 1) | ((index >> 4) << 6);
	oamSet(&oamSub, 4, x, y, 0,
		0, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, index), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamUpdate(&oamSub);
}

void setupInitialize(){
	videoSetMode(MODE_5_2D);
	videoSetModeSub(MODE_0_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankB(VRAM_B_MAIN_SPRITE);
	vramSetBankC(VRAM_C_SUB_BG);
	vramSetBankD(VRAM_D_SUB_SPRITE);
	oamInit(&oamMain, SpriteMapping_1D_32, FALSE);
	oamInit(&oamSub, SpriteMapping_2D, FALSE);
	lcdMainOnTop();
	
	int bg0 = bgInit(0, BgType_Text8bpp, BgSize_T_256x256, 0, 4);
	int bg0s = bgInitSub(0, BgType_Text4bpp, BgSize_T_256x256, 1, 4);
	int bg1s = bgInitSub(1, BgType_Text4bpp, BgSize_T_256x256, 2, 6);
	bgSetPriority(bg0s, 3);
	bgSetPriority(bg1s, 2);
	
	swiWaitForVBlank();
	
	u32 fileSize;
	void *mainPalette = IO_ReadEntireFile("setup/setup_m_b.ncl.bin", &fileSize);
	dmaCopy(mainPalette, BG_PALETTE, fileSize);
	free(mainPalette);
	
	void *subPalette = IO_ReadEntireFile("setup/setup_s_b.ncl.bin", &fileSize);
	dmaCopy(subPalette, BG_PALETTE_SUB, fileSize);
	free(subPalette);
	
	void *subObjPalette = IO_ReadEntireFile("setup/setup_s_o.ncl.bin", &fileSize);
	dmaCopy(subObjPalette, SPRITE_PALETTE_SUB, fileSize);
	free(subObjPalette);
	
	void *mainGfx = IO_ReadEntireFile("setup/setup_m_b.ncg.bin", &fileSize);
	MI_UncompressLZ16(mainGfx, bgGetGfxPtr(bg0));
	free(mainGfx);
	
	void *mainScreen = IO_ReadEntireFile("setup/setup_m_b.nsc.bin", &fileSize);
	MI_UncompressLZ16(mainScreen, bgGetMapPtr(bg0));
	free(mainGfx);
	
	void *subGfx = IO_ReadEntireFile("setup/setup_s_b.ncg.bin", &fileSize);
	MI_UncompressLZ16(subGfx, bgGetGfxPtr(bg0s));
	free(subGfx);
	
	void *subScreen = IO_ReadEntireFile("setup/setup_s_b.nsc.bin", &fileSize);
	MI_UncompressLZ16(subScreen, bgGetMapPtr(bg0s));
	free(subScreen);
	
	void *subBgTextGfx = IO_ReadEntireFile("setup/setup_text_s_b.ncg.bin", &fileSize);
	MI_UncompressLZ16(subBgTextGfx, bgGetGfxPtr(bg1s));
	free(subBgTextGfx);
	
	void *subBgTextScreen = IO_ReadEntireFile("setup/setup_text_s_b.nsc.bin", &fileSize);
	MI_UncompressLZ16(subBgTextScreen, bgGetMapPtr(bg1s));
	free(subBgTextScreen);
	
	void *subObjGfx = IO_ReadEntireFile("setup/setup_s_o.ncg.bin", &fileSize);
	MI_UncompressLZ16(subObjGfx, oamGetGfxPtr(&oamSub, 0));
	free(subObjGfx);
	
	g_setupCpuSliderX = 56;
	g_cpuPlayers = FALSE;
	g_setupCpuSliderVelocity = 0;
	g_nPlayers = 4;
	g_setupFadingOut = FALSE;
	g_setupFadeOutStart = 0;
	setupSetCpuSlider(g_setupCpuSliderX, 56);
	setupSetPlayersSpinner(40, 104);
	setupSetBackButton(0, 160, 2);
	setupSetPlayerCountLabel(72, 104, g_nPlayers);
	setupSetPlayButton(104, 166, 1);
	
	g_setupStart = g_frameCount;
}

void setupTickProc(){
	if(g_setupLastTick + 1 != g_frameCount){
		setupInitialize();
	}
	
	//first 32 frames, fade in the screen.
	int frame = g_frameCount - g_setupStart;
	if(frame <= 32){
		if(frame == 0){
			BLDY = 16;
			DB_BLDY = 16;
			BLDCNT = 0x3F | (3 << 6);
			DB_BLDCNT = 0x3F | (3 << 6);
		}
		int brightness = 16 - (frame / 2);
		BLDY = brightness;
		DB_BLDY = brightness;
	}
	if(frame == 32){
		BLDCNT = 0;
		DB_BLDCNT = 0;
	}
	
	scanKeys();
	u32 downKeys = keysDown();
	
	touchPosition touchPos;
	touchRead(&touchPos);
	
	//after frame 32, check for input.
	if(frame > 32){
		if(!g_setupFadingOut){
			if(downKeys & KEY_TOUCH){
				if(touchPos.px >= 56 && touchPos.px < 104
					&& touchPos.py >= 56 && touchPos.py < 72){
					g_cpuPlayers ^= 1;
				}
				if(touchPos.px >= 0 && touchPos.px < 32
					&& touchPos.py < 192 && touchPos.py >= 128){
					g_setupFadingOut = TRUE;
					g_setupFadeOutStart = g_frameCount;
					g_setupFadeOutToScene = SCENE_TITLE;
					
					BLDY = 0;
					DB_BLDY = 0;
					BLDCNT = 0x3F | (3 << 6);
					DB_BLDCNT = 0x3F | (3 << 6);
				}
				if(touchPos.px >= 40 && touchPos.px < 56 && touchPos.py >= 104 && touchPos.py < 120){
					g_nPlayers--;
					if(g_nPlayers < 2) g_nPlayers = 2;
					setupSetPlayerCountLabel(72, 104, g_nPlayers);
				}
				if(touchPos.px >= 104 && touchPos.px < 120 && touchPos.py >= 104 && touchPos.py < 120){
					g_nPlayers++;
					if(g_nPlayers > 8) g_nPlayers = 8;
					setupSetPlayerCountLabel(72, 104, g_nPlayers);
				}
				setupSetPlayersSpinner(40, 104);
				if(touchPos.px >= 104 && touchPos.px < 152 && touchPos.py >= 166 && touchPos.py < 182){
					g_setupFadingOut = TRUE;
					g_setupFadeOutStart = g_frameCount;
					g_setupFadeOutToScene = SCENE_GAME;
					setupSetPlayButton(104, 166, 0);
					
					BLDY = 0;
					DB_BLDY = 0;
					BLDCNT = 0x3F | (3 << 6);
					DB_BLDCNT = 0x3F | (3 << 6);
				}
			}
		}
	}
	
	//after frame 32, update graphics.
	if(frame > 32){
		if(g_setupCpuSliderX != 56 && !g_cpuPlayers){
			g_setupCpuSliderVelocity -= 2;
		}
		if(g_setupCpuSliderX != 88 && g_cpuPlayers){
			g_setupCpuSliderVelocity += 2;
		}
		
		g_setupCpuSliderX += g_setupCpuSliderVelocity;
		if(g_setupCpuSliderX > 88){
			g_setupCpuSliderX = 88;
			g_setupCpuSliderVelocity = 0;
		}
		if(g_setupCpuSliderX < 56){
			g_setupCpuSliderX = 56;
			g_setupCpuSliderVelocity = 0;
		}
		
		if(g_setupFadingOut){
			int animFrame = g_frameCount - g_setupFadeOutStart;
			int brightness = animFrame / 2;
			if(brightness > 16) brightness = 16;
			
			BLDY = brightness;
			DB_BLDY = brightness;
			
			if(animFrame == 32) g_scene = g_setupFadeOutToScene;
			
			if(g_setupFadeOutToScene == SCENE_TITLE){
				setupSetBackButton(0, 160 + (animFrame * animFrame) / 4, 0);
			}
		}
		
		setupSetCpuSlider(g_setupCpuSliderX, 56);
	}
	
	g_setupLastTick = g_frameCount;
}