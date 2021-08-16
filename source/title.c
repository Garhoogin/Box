#include <nds.h>
#include <filesystem.h>
#include <nds/arm9/input.h>

#include "scenes.h"
#include "io.h"
#include "mi.h"

static int g_titleLastTick = 0;
static int g_titleStart = 0;
static int g_titleFadingOut = 0;
static int g_titleFadeOutStart = 0;
static int g_titleFadeOutToScene = SCENE_GAMESETUP;

#define VCOUNT            (*(u16*)0x04000006)

#define BLDCNT            (*(u16*)0x04000050)
#define BLDALPHA          (*(u16*)0x04000052)
#define BLDY              (*(u16*)0x04000054)

#define DB_BLDCNT         (*(u16*)0x04001050)
#define DB_BLDALPHA       (*(u16*)0x04001052)
#define DB_BLDY           (*(u16*)0x04001054)

void titleSetupButton(int x, int y, int slot){
	oamSet(&oamSub, slot, x, y, 0,
		0, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 0), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 1 + slot, x + 16, y, 0,
		0, SpriteSize_32x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 1), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 2 + slot, x + 32 + 16, y, 0,
		0, SpriteSize_32x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 1), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 3 + slot, x + 32 * 2 + 16, y, 0,
		0, SpriteSize_32x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 1), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 4 + slot, x, y + 16, 0,
		0, SpriteSize_16x8, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, (1 << 7) | (4)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 5 + slot, x + 16, y + 16, 0,
		0, SpriteSize_32x8, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 4), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 6 + slot, x + 16 + 32, y + 16, 0,
		0, SpriteSize_32x8, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 4), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 7 + slot, x + 16 + 32 * 2, y + 16, 0,
		0, SpriteSize_32x8, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 4), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 8 + slot, x + 16 + 32 * 3, y, 0,
		0, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 3), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 9 + slot, x + 16 + 32 * 3, y + 16, 0,
		0, SpriteSize_16x8, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, (1 << 7) | 5), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
}

void titleSetupPlayButton(int x, int y){
	if(y > 192) y = 192;
	titleSetupButton(x, y, 1);
	oamSet(&oamSub, 0, x + 48, y + 4, 0,
		0, SpriteSize_32x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, 2 << 7), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	
	oamUpdate(&oamSub);
}

void titleSetupRulesButton(int x, int y){
	if(y > 192) y = 192;
	titleSetupButton(x, y, 13);
	oamSet(&oamSub, 11, x + 45, y + 4, 0,\
		0, SpriteSize_32x16, SpriteColorFormat_16Color,\
		oamGetGfxPtr(&oamSub, (2 << 7) | (8 >> 2)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 12, x + 45 + 32, y + 4, 0,\
		0, SpriteSize_8x16, SpriteColorFormat_16Color,\
		oamGetGfxPtr(&oamSub, (2 << 7) | (16 >> 2)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);	
	
	oamUpdate(&oamSub);
}

void titleInitialize(){
	videoSetMode(MODE_5_2D);
	videoSetModeSub(MODE_0_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankB(VRAM_B_MAIN_SPRITE);
	vramSetBankC(VRAM_C_SUB_BG);
	vramSetBankD(VRAM_D_SUB_SPRITE);
	oamInit(&oamMain, SpriteMapping_1D_32, FALSE);
	oamInit(&oamSub, SpriteMapping_2D, FALSE);
	lcdMainOnTop();
	
	BLDY = 16;
	DB_BLDY = 16;
	BLDCNT = 0x3F | (3 << 6);
	DB_BLDCNT = 0x3F | (3 << 6);
	
	int bg0 = bgInit(0, BgType_Text8bpp, BgSize_T_256x256, 0, 4);
	int bg1 = bgInit(1, BgType_Text8bpp, BgSize_T_256x256, 1, 4);
	int bg0s = bgInitSub(0, BgType_Text4bpp, BgSize_T_256x256, 0, 2);
	bgSetPriority(bg0, 3);
	bgSetPriority(bg1, 2);
	bgSetPriority(bg0s, 3);
	bgHide(bg1);
	
	u32 fileSize;
	
	//load palette
	u32 mainPaletteSize, subPaletteSize, subObjPaletteSize;
	u16 *mainPalette = (u16 *) IO_ReadEntireFile("title/title_m_b.ncl.bin", &mainPaletteSize);
	u16 *subPalette = (u16 *) IO_ReadEntireFile("title/title_s_b.ncl.bin", &subPaletteSize);
	u16 *subObjPalette = (u16 *) IO_ReadEntireFile("title/title_s_o.ncl.bin", &subObjPaletteSize);
	
	swiWaitForVBlank();
	dmaCopy(mainPalette, BG_PALETTE, mainPaletteSize);
	dmaCopy(subPalette, BG_PALETTE_SUB, subPaletteSize);
	dmaCopy(subObjPalette, SPRITE_PALETTE_SUB, subObjPaletteSize);
	
	free(mainPalette);
	free(subPalette);
	free(subObjPalette);
	
	//load chars
	u8 *mainGfx = (u8 *) IO_ReadEntireFile("title/title_m_b.ncg.bin", &fileSize);
	MI_UncompressLZ16(mainGfx, bgGetGfxPtr(bg0));
	free(mainGfx);
	
	//load BG under map
	u16 *mainScreen = (u16 *) IO_ReadEntireFile("title/title1_m_b.nsc.bin", &fileSize);
	MI_UncompressLZ16(mainScreen, bgGetMapPtr(bg0));
	free(mainScreen);
	
	//load BG over map
	u16 *mainScreen2 = (u16 *) IO_ReadEntireFile("title/title2_m_b.nsc.bin", &fileSize);
	MI_UncompressLZ16(mainScreen2, bgGetMapPtr(bg1));
	free(mainScreen2);
	
	//load chars
	u8 *subGfx = (u8 *) IO_ReadEntireFile("title/title_s_b.ncg.bin", &fileSize);
	MI_UncompressLZ16(subGfx, bgGetGfxPtr(bg0s));
	free(mainGfx);
	
	//load BG map
	u16 *subScreen = (u16 *) IO_ReadEntireFile("title/title_s_b.nsc.bin", &fileSize);
	//dmaCopy(subScreen, bgGetMapPtr(bg0s), fileSize);
	MI_UncompressLZ16(subScreen, bgGetMapPtr(bg0s));
	free(subScreen);
	
	u8 *subObjGfx = (u8 *) IO_ReadEntireFile("title/title_s_o.ncg.bin", &fileSize);
	MI_UncompressLZ16(subObjGfx, oamGetGfxPtr(&oamSub, 0));
	free(subObjGfx);
	
	//set up button sprite
	titleSetupPlayButton(256, 0);
	
	//set start frame
	g_titleStart = g_frameCount;
	g_titleFadingOut = FALSE;
	g_titleFadeOutStart = 0;
}

static int previousButtonX[24] = {0};
static short dummy[2];

void animateButton(int frame){
	static int velocity = 0; //8-bit FX
	static int x = 256 << 8;
	static int nBounces = 0;
	if(frame == 0){
		int i;
		x = 256 << 8;
		velocity = (-4) << 8;
		nBounces = 0;
		for(i = 0; i < 24; i++) previousButtonX[i] = 256;
	}
	
	x += velocity;
	velocity -= (8 << 4);
	
	if(x < (64 << 8)) {
		//bounce. Lose 50% velocity.
		velocity = (-velocity) / 3;
		x = (64 << 8);
		nBounces++;
	}
	
	if(nBounces > 4){
		velocity = 0;
		x = 64 << 8;
	}
	
	//set up previous button X
	int i;
	for(i = 0; i < 23; i++){
		int _i = 22 - i;
		previousButtonX[_i + 1] = previousButtonX[_i];
	}
	previousButtonX[0] = x >> 8;
	
	titleSetupPlayButton(x >> 8, 5);
}

int animateButtonOutDisplacement(int frame){
	static int velocity = 0;
	static int y = 5 << 8;
	if(frame == 0){
		velocity = -(4 << 8);
		y = 5 << 8;
	}
	
	y += velocity;
	velocity += 1 << 8;
	if(y > (192 << 8)) y = 192 << 8;
	
	return y >> 8;
}

void animateButtonOut(int frame){
	static int velocity = 0;
	static int y = 5 << 8;
	if(frame == 0){
		velocity = -(4 << 8);
		y = 5 << 8;
	}
	
	y += velocity;
	velocity += 1 << 8;
	if(y > (192 << 8)) y = 192 << 8;
	
	titleSetupPlayButton(previousButtonX[0], y >> 8);
}

void titleTickProc(){
	if(g_titleLastTick + 1 != g_frameCount){
		titleInitialize();
	}
	int frame = g_frameCount - g_titleStart;
	
	//first 32 frames, fade in the screen.
	if(frame <= 32){
		int brightness = 16 - (frame / 2);
		BLDY = brightness;
		DB_BLDY = brightness;
	}
	
	//wait before blending BGs
	if(frame >= 62 && frame <= 78){
		if(frame == 30 + 32){
			//set up blending
			BLDY = 0;
			DB_BLDCNT = 0;
			DB_BLDY = 0;
			BLDALPHA = 0 | (16 << 8);
			BLDCNT = (1 << 6) | (1 << 1) | (1 << 8);
			DB_BLDCNT = 0;
		}
		int bgAnimFrame = frame - 32 - 30;
		bgShow(1); //main BG 1
		BLDALPHA = bgAnimFrame | ((16 - bgAnimFrame) << 8);
	}
	
	//wait before sliding in buttons
	if(!g_titleFadingOut && frame >= 78 && frame <= 232){
		BLDALPHA = 0;
		DB_BLDALPHA = 0;
		BLDY = 0;
		DB_BLDY = 0;
		BLDCNT = 0;
		DB_BLDCNT = 0;
		
		int i;
		//destination x: 64
		//come in from right: 256
		animateButton(frame - 78);
		titleSetupRulesButton(previousButtonX[0], 5 + 24 + 10);
		
		//fancy HDMA magic heheh
		while(DMA_CR(0) & DMA_BUSY);
		for(i = 0; i < 192; i++){
			DMA_DEST(0) = (u32) dummy;
			DMA_SRC(0) = (u32) (dummy + 1);
			DMA_CR(0) = DMA_COPY_HALFWORDS | DMA_START_HBL | 1;
			while(DMA_CR(0) & DMA_BUSY);
			
			//seems these take a bit longer than a line to run, so
			//I will use VCOUNT.
			int line = VCOUNT & 0xFF;
			if(i >= 5 && (i - 5) < 24){
				titleSetupPlayButton(previousButtonX[i - 5], 5);
			}
			if(line >= 39 && line < 63){
				titleSetupRulesButton(previousButtonX[(line - (5 + 24 + 10)) / 2], 5 + 24 + 10);
			}
			if((i - 5) >= 24 + 10 + 24) break;
		}
	}
	
	touchPosition touch;
	
	scanKeys();
	touchRead(&touch);
	u32 keys = keysHeld();
	
	if(!g_titleFadingOut){
		if(keys & KEY_TOUCH){
			//is pressing play?
			if(touch.py >= 5 && touch.py < 29 && touch.px >= previousButtonX[0] \
				&& touch.px <= previousButtonX[0] + 128){
				
				g_titleFadingOut = 1;
				g_titleFadeOutStart = g_frameCount;
				g_titleFadeOutToScene = SCENE_GAMESETUP;
				
				BLDY = 0;
				DB_BLDY = 0;
				BLDCNT = 0x3F | (3 << 6);
				DB_BLDCNT = 0x3F | (3 << 6);
			}
			//is pressing rules?
			if(touch.py >= 39 && touch.py < 63 && touch.px >= previousButtonX[0] \
				&& touch.px <= previousButtonX[0] + 128){
					
				g_titleFadingOut = 1;
				g_titleFadeOutStart = g_frameCount;
				g_titleFadeOutToScene = SCENE_RULES;
				
				BLDY = 0;
				DB_BLDY = 0;
				BLDCNT = 0x3F | (3 << 6);
				DB_BLDCNT = 0x3F | (3 << 6);
			}
		}
	}
	
	if(g_titleFadingOut){
		//advance the fadeout animation.
		int animFrame = g_frameCount - g_titleFadeOutStart;
		int brightness = animFrame / 2;
		if(brightness > 16) brightness = 16;
		
		BLDY = brightness;
		DB_BLDY = brightness;
		//animateButtonOut(animFrame);
		int displacement = animateButtonOutDisplacement(animFrame);
		
		switch(g_titleFadeOutToScene){
			case SCENE_GAMESETUP:
				titleSetupPlayButton(previousButtonX[0], 5 + displacement);
				titleSetupRulesButton(previousButtonX[0], 39);
				break;
			case SCENE_RULES:
				titleSetupRulesButton(previousButtonX[0], 39 + displacement);
				titleSetupPlayButton(previousButtonX[0], 5);
				break;
		}
		
		//once we reach frame 32, change scene.
		if(animFrame >= 32){
			g_scene = g_titleFadeOutToScene;
		}
	}
	
	g_titleLastTick = g_frameCount;
}