#include <nds.h>

#include "io.h"
#include "mi.h"
#include "scenes.h"

static int g_rulesLastTick = 0;
static int g_rulesStart = 0;
static int g_rulesFadingOut = 0;
static int g_rulesFadeOutStart = 0;
static int g_rulesFadeOutToScene = SCENE_TITLE;

#define VCOUNT            (*(u16*)0x04000006)

#define BLDCNT            (*(u16*)0x04000050)
#define BLDALPHA          (*(u16*)0x04000052)
#define BLDY              (*(u16*)0x04000054)

#define DB_BLDCNT         (*(u16*)0x04001050)
#define DB_BLDALPHA       (*(u16*)0x04001052)
#define DB_BLDY           (*(u16*)0x04001054)

void rulesSetupBackButton(int x, int y, int pal){
	if(y > 192) y = 192;
	oamSet(&oamSub, 0, x, y, 0,
		pal, SpriteSize_32x32, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 0), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamUpdate(&oamSub);
}

void rulesInitialize(){
	videoSetMode(MODE_5_2D);
	videoSetModeSub(MODE_0_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankB(VRAM_B_MAIN_SPRITE);
	vramSetBankC(VRAM_C_SUB_BG);
	vramSetBankD(VRAM_D_SUB_SPRITE);
	oamInit(&oamMain, SpriteMapping_1D_32, FALSE);
	oamInit(&oamSub, SpriteMapping_1D_32, FALSE);
	
	BLDY = 16;
	DB_BLDY = 16;
	BLDCNT = 0x3F | (3 << 6);
	DB_BLDCNT = 0x3F | (3 << 6);
	
	int bg0 = bgInit(0, BgType_Text4bpp, BgSize_T_256x256, 0, 4);
	int bg0s = bgInitSub(0, BgType_Text4bpp, BgSize_T_256x256, 0, 4);
	int bg1s = bgInitSub(1, BgType_Text4bpp, BgSize_T_256x256, 1, 6);
	bgSetPriority(bg0s, 3);
	bgSetPriority(bg1s, 2);
	
	u32 fileSize;
	
	u32 mainBgPaletteSize, subBgPaletteSize, subObjPaletteSize;
	void *mainBgPalette = IO_ReadEntireFile("rules/rules_m_b.ncl.bin", &mainBgPaletteSize);
	void *subBgPalette = IO_ReadEntireFile("rules/rules_s_b.ncl.bin", &subBgPaletteSize);
	void *subObjPalette = IO_ReadEntireFile("rules/rules_s_o.ncl.bin", &subObjPaletteSize);
	
	swiWaitForVBlank();
	dmaCopy(mainBgPalette, BG_PALETTE, mainBgPaletteSize);
	dmaCopy(subBgPalette, BG_PALETTE_SUB, subBgPaletteSize);
	dmaCopy(subObjPalette, SPRITE_PALETTE_SUB, subObjPaletteSize);
	
	free(mainBgPalette);
	free(subBgPalette);
	free(subObjPalette);
	
	void *mainBgGfx = IO_ReadEntireFile("rules/rules_m_b.ncg.bin", &fileSize);
	MI_UncompressLZ16(mainBgGfx, bgGetGfxPtr(bg0));
	free(mainBgGfx);
	
	void *mainBgScreen = IO_ReadEntireFile("rules/rules_m_b.nsc.bin", &fileSize);
	MI_UncompressLZ16(mainBgScreen, bgGetMapPtr(bg0));
	free(mainBgScreen);
	
	void *subBgGfx = IO_ReadEntireFile("rules/rules_s_b.ncg.bin", &fileSize);
	MI_UncompressLZ16(subBgGfx, bgGetGfxPtr(bg0s));
	free(subBgGfx);
	
	void *subBgScreen = IO_ReadEntireFile("rules/rules_s_b.nsc.bin", &fileSize);
	MI_UncompressLZ16(subBgScreen, bgGetMapPtr(bg0s));
	free(subBgScreen);
	
	void *subBgTextGfx = IO_ReadEntireFile("rules/rules_text_s_b.ncg.bin", &fileSize);
	MI_UncompressLZ16(subBgTextGfx, bgGetGfxPtr(bg1s));
	free(subBgTextGfx);
	
	void *subBgTextScreen = IO_ReadEntireFile("rules/rules_text_s_b.nsc.bin", &fileSize);
	MI_UncompressLZ16(subBgTextScreen, bgGetMapPtr(bg1s));
	free(subBgTextScreen);
	
	void *subObjGfx = IO_ReadEntireFile("rules/rules_s_o.ncg.bin", &fileSize);
	MI_UncompressLZ16(subObjGfx, oamGetGfxPtr(&oamSub, 0));
	free(subObjGfx);
	
	rulesSetupBackButton(0, 160, 2);
	
	g_rulesFadingOut = FALSE;
	g_rulesFadeOutStart = 0;
	g_rulesFadeOutToScene = SCENE_TITLE;
	g_rulesStart = g_frameCount;
}

void rulesTickProc(){
	if(g_rulesLastTick + 1 != g_frameCount){
		rulesInitialize();
	}
	
	int frame = g_frameCount - g_rulesStart;
	
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
		if(!g_rulesFadingOut){
			if(downKeys & KEY_TOUCH){
				if(touchPos.px >= 0 && touchPos.px < 32 && touchPos.py < 192 && touchPos.py >= 160){
					g_rulesFadingOut = TRUE;
					g_rulesFadeOutStart = g_frameCount;
					g_rulesFadeOutToScene = SCENE_TITLE;
					
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
		if(g_rulesFadingOut){
			int animFrame = g_frameCount - g_rulesFadeOutStart;
			int brightness = animFrame / 2;
			if(brightness > 16) brightness = 16;
			
			BLDY = brightness;
			DB_BLDY = brightness;
			
			if(animFrame == 32) g_scene = g_rulesFadeOutToScene;
			
			if(g_rulesFadeOutToScene == SCENE_TITLE){
				rulesSetupBackButton(0, 160 + (animFrame * animFrame) / 4, 0);
			}
		}
	}
	
	g_rulesLastTick = g_frameCount;
}