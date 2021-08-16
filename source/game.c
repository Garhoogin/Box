#include <nds.h>

#include "scenes.h"
#include "io.h"
#include "mi.h"

static int g_gameLastTick = 0;
static int g_gameStart = 0;
static int g_gameFadingOut = 0;
static int g_gameFadeOutStart = 0;
static int g_gameFadeOutToScene = SCENE_TITLE;

#define BLDCNT            (*(u16*)0x04000050)
#define BLDALPHA          (*(u16*)0x04000052)
#define BLDY              (*(u16*)0x04000054)

#define DB_BLDCNT         (*(u16*)0x04001050)
#define DB_BLDALPHA       (*(u16*)0x04001052)
#define DB_BLDY           (*(u16*)0x04001054)

#define DB_BG2PA          (*(s16*)0x04001020)
#define DB_BG2PB          (*(s16*)0x04001022)
#define DB_BG2PC          (*(s16*)0x04001024)
#define DB_BG2PD          (*(s16*)0x04001026)
#define DB_BG2X           (*(s32*)0x04001028)
#define DB_BG2Y           (*(s32*)0x0400102C)

#define CYCLE_ANTE    0
#define CYCLE_ROLL    1
#define CYCLE_FLIP    2
#define CYCLE_WIN     3
#define CYCLE_LOSE    4
#define CYCLE_PAUSE   5
#define CYCLE_PREROLL 6
#define CYCLE_WINNER  7

static int g_numberStates[9];	//whether each tile is currently flipped
static int g_numberLocks[9];	//whether each tile cannot be unflipped
static int g_numberY[9];		//Y position of each tile for animation
static int g_currentPlayer;		//current player, 0-indexed
static int g_matchPoint;		//does the current player need to open the box to prevent the last player from winning?
static int g_antes[8];			//ante from each player
static int g_balances[8];		//balance of each player
static int g_playerScores[8];   //current score of each player. Lowest score wins.
static int g_lastWon;			//who last won? 0-indexed
static int g_firstPlayer;		//who plays first? 0-indexed
static int g_currentNumber;		//who is playing? 0-indexed
static int g_currentCycle;		//current game cycle
static int g_cycleReturnTo;		//
static int g_currentCycleTime;	//how long has the current cycle lasted so far?
static int g_diceValues[2];		//what numbers are on the dice?
static int g_nDice;				//how many dice are out, 1 or 2?
static int g_nTurns = 0;		//number of players played this round

#define SWIZZLE(ind) ((((ind)&0xF)>>1)|(((ind)>>4)<<6))

int getDiceTotal(){
	int total = g_diceValues[0];
	if(g_nDice == 2) total += g_diceValues[1];
	return total;
}

int getFlipTotal(){
	int total = 0, i = 0;
	for(; i < 9; i++){
		if(g_numberStates[i]) total += i + 1;
	}
	return total;
}

int getAnteTotal(){
	int total = 0, i = 0;
	for(; i < g_nPlayers; i++){
		total += g_antes[i];
	}
	return total;
}

int isInArray(int *arr, int nElems, int n){
	int i = 0;
	for(; i < nElems; i++){
		if(arr[i] == n) return 1;
	}
	return 0;
}

int canGetTotal(int total, int *used, int nUsed){
	//can the total be achieved using the leftover tiles?
	int localUsed[9];
	int i;
	for(i = 0; i < nUsed; i++){
		localUsed[i] = used[i];
	}
	
	//check all the tiles
	for(i = 0; i < 9; i++){
		if(g_numberLocks[i]) continue;
		if(isInArray(localUsed, nUsed, i)) continue;
		int tile = i + 1;
		if(tile > total) break;
		if(tile == total) return 1;
		
		//tile is less than the total. Can we add up to that?
		localUsed[nUsed] = i;
		if(canGetTotal(total - tile, localUsed, nUsed + 1)) return 1;
	}
	return 0;
}

//is there any possible way the player can win?
int canPlay(){
	//try to find any possible combination of tiles that can add to the total.
	int i;
	int found = 0;
	int diceTotal = getDiceTotal();
	for(i = 0; i < 9; i++){
		if(g_numberLocks[i]) continue;
		int tile = i + 1;
		if(tile > diceTotal) break;
		if(tile == diceTotal) return 1;
		
		//an unlocked tile that is less than the dice total. Can a win be made with this tile?
		if(canGetTotal(diceTotal - tile, &i, 1)) return 1;
	}
	return found;
}

int countBits(u32 x){
	int i = 0, n = 0;
	for(; i < 32; i++){
		if(x & (1 << i)) n++;
	}
	return n;
}

int scoreMove(int mask){
	int nTiles = countBits(mask);
	int score = 1, i = 0;
	for(i = 0; i < 9; i++){
		if(mask & (1 << i)){
			score *= i + 1;
		}
	}
	score *= 256; //reduce rounding errors
	score /= nTiles * nTiles;
	return score;
}

int recursiveConstructCpuMove(int total, int masked){
	int move = 0;
	//maximize product of tiles times number of tiles
	
	int i;
	for(i = 0; i < 9; i++){
		if(g_numberLocks[i]) continue;
		if(masked & (1 << i)) continue;
		
		int tile = i + 1;
		if(tile > total) break;
		if(tile == total) return (1 << i);
	}
	
	//now, check if we can add to the total.
	int bestScore = 0;
	for(i = 0; i < 9; i++){
		if(g_numberLocks[i]) continue;
		if(masked & (1 << i)) continue;
		
		int tile = i + 1;
		if(tile > total) break;
		
		int addMove = recursiveConstructCpuMove(total - tile, masked | (1 << i)) | (1 << i);
		if(addMove == 0) continue;
		
		int score = scoreMove(addMove | masked);
		if(score > bestScore) {
			bestScore = score;
			move = addMove;
		}
		
	}
	
	return move;
}

//return bit field of tiles CPU player will flip
int makeCpuMove(){
	int total = getDiceTotal();
	if(!canPlay()) return 0;
	
	//first check: Can the total be represented as one tile?
	if(total <= 9 && !g_numberLocks[total - 1]){
		return 1 << (total - 1);
	}
	
	//next: Find an alternative move.
	int used[9];
	int i = 0, bestMove = 0, bestMoveScore = 0;
	for(; i < 9; i++){
		if(g_numberLocks[i]) continue;
		if(i + 1 > total) break;
		
		int tile = i + 1;
		used[0] = i;
		if(!canGetTotal(total - tile, used, 1)) continue;
		
		//this is a potential avenue of adding to the dice total. Start constructing a move plan.
		int move = 1 << i;
		move |= recursiveConstructCpuMove(total - tile, move);
		int score = scoreMove(move);
		if(score > bestMoveScore){
			bestMoveScore = score;
			bestMove = move;
		}
	}
	
	return bestMove;
}

int hasPlayerControl(){
	if(g_currentPlayer == 0) return 1;
	return !g_cpuPlayers;
}

void gameDisplayScoreList(){
	//for each player that has played, show their scores.
	int i;
	for(i = 0; i < g_nPlayers; i++){
		oamSet(&oamSub, i * 4, 10, 31 + 16 * i, 0,
			0, SpriteSize_32x16, SpriteColorFormat_16Color,
			oamGetGfxPtr(&oamSub, 0), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		oamSet(&oamSub, i * 4 + 1, 10 + 32 + 2, 31 + 16 * i, 0,
			0, SpriteSize_16x16, SpriteColorFormat_16Color,
			oamGetGfxPtr(&oamSub, SWIZZLE(6 + 2 * i)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		
		int score = g_playerScores[i];
		int scoreHigh = score / 10;
		int scoreLow = score % 10;
		
		if(i < g_currentPlayer || g_nTurns == g_nPlayers){
			oamSet(&oamSub, i * 4 + 2, 128, 31 + 16 * i, 0,
				0, SpriteSize_16x16, SpriteColorFormat_16Color,
				oamGetGfxPtr(&oamSub, SWIZZLE(4 + 2 * scoreHigh)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
			oamSet(&oamSub, i * 4 + 3, 128 + 6, 31 + 16 * i, 0,
				0, SpriteSize_16x16, SpriteColorFormat_16Color,
				oamGetGfxPtr(&oamSub, SWIZZLE(4 + 2 * scoreLow)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		}
	}
	oamUpdate(&oamSub);
}

void gameSetupNumber(int x, int y, int n){
	int index = n - 1;
	
	u32 gfxIndex = ((index & 7) << 2) | ((index >> 3) << 7);
	gfxIndex = ((gfxIndex & 0xF) >> 1) | ((gfxIndex >> 4) << 6);
	
	oamSet(&oamMain, index, x, y, 0,
		0, SpriteSize_16x32, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, gfxIndex), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamUpdate(&oamMain);
}

void newRound(){
	g_firstPlayer = 0;//g_lastWon;  //The other way makes the game a little more confusing
	g_currentPlayer = g_firstPlayer;
	g_currentCycle = CYCLE_ANTE;
	g_currentNumber = 0;
	
	int i;
	for(i = 0; i < 9; i++){
		g_numberStates[i] = 0;
		g_numberLocks[i] = 0;
		g_numberY[i] = 0;
	}
	for(i = 0; i < 8; i++){
		g_antes[i] = 0;
		g_playerScores[i] = 0;
	}
	g_nDice = 2;
	g_nTurns = 0;
	
	oamClear(&oamMain, 12, 116);
	oamClear(&oamSub, 0, 0);
}

void gameInitialize(){
	videoSetMode(MODE_5_2D);
	videoSetModeSub(MODE_0_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankB(VRAM_B_MAIN_SPRITE);
	vramSetBankC(VRAM_C_SUB_BG);
	vramSetBankD(VRAM_D_SUB_SPRITE);
	oamInit(&oamMain, SpriteMapping_2D, FALSE);
	oamInit(&oamSub, SpriteMapping_2D, FALSE);
	
	int bg0 = bgInit(0, BgType_Text4bpp, BgSize_T_256x256, 0, 4);
	int bg1 = bgInit(1, BgType_Text4bpp, BgSize_T_256x256, 1, 6);
	int bg0s = bgInitSub(0, BgType_Text4bpp, BgSize_T_256x256, 0, 2);
	int bg2s = bgInitSub(2, BgType_Text4bpp, BgSize_T_256x256, 1, 4);
	bgSetPriority(bg0, 3);
	bgSetPriority(bg1, 2);
	bgSetPriority(bg0s, 3);
	bgSetPriority(bg2s, 2);
	bgHide(bg2s);
	
	lcdMainOnBottom();
	swiWaitForVBlank();
	
	u32 fileSize;
	
	u32 mainObjPaletteSize, mainBgPaletteSize, subObjPaletteSize;
	void *mainObjPalette = IO_ReadEntireFile("game/game_m_o.ncl.bin", &mainObjPaletteSize);
	void *mainBgPalette = IO_ReadEntireFile("title/title_s_b.ncl.bin", &mainBgPaletteSize);
	void *subObjPalette = IO_ReadEntireFile("game/game_s_o.ncl.bin", &subObjPaletteSize);
	
	dmaCopy(mainObjPalette, SPRITE_PALETTE, mainObjPaletteSize);
	dmaCopy(mainBgPalette, BG_PALETTE, mainBgPaletteSize);
	dmaCopy(mainBgPalette, BG_PALETTE_SUB, mainBgPaletteSize);
	dmaCopy(subObjPalette, SPRITE_PALETTE_SUB, subObjPaletteSize);
	
	free(mainObjPalette);
	free(mainBgPalette);
	free(subObjPalette);
	
	void *mainObjGfx = IO_ReadEntireFile("game/game_m_o.ncg.bin", &fileSize);
	MI_UncompressLZ16(mainObjGfx, oamGetGfxPtr(&oamMain, 0));
	free(mainObjGfx);
	
	void *mainBgGfx = IO_ReadEntireFile("title/title_s_b.ncg.bin", &fileSize);
	MI_UncompressLZ16(mainBgGfx, bgGetGfxPtr(bg0));
	MI_UncompressLZ16(mainBgGfx, bgGetGfxPtr(bg0s));
	free(mainBgGfx);
	
	void *mainBgScreen = IO_ReadEntireFile("title/title_s_b.nsc.bin", &fileSize);
	MI_UncompressLZ16(mainBgScreen, bgGetMapPtr(bg0));
	MI_UncompressLZ16(mainBgScreen, bgGetMapPtr(bg0s));
	free(mainBgScreen);
	
	void *mainFrameGfx = IO_ReadEntireFile("game/frame_m_b.ncg.bin", &fileSize);
	MI_UncompressLZ16(mainFrameGfx, bgGetGfxPtr(bg1));
	free(mainFrameGfx);
	
	void *mainFrameScreen = IO_ReadEntireFile("game/frame_m_b.nsc.bin", &fileSize);
	MI_UncompressLZ16(mainFrameScreen, bgGetMapPtr(bg1));
	free(mainFrameScreen);
	
	void *subObjGfx = IO_ReadEntireFile("game/game_s_o.ncg.bin", &fileSize);
	MI_UncompressLZ16(subObjGfx, oamGetGfxPtr(&oamSub, 0));
	free(subObjGfx);
	
	void *subCloseGfx = IO_ReadEntireFile("game/close_s_b.ncg.bin", &fileSize);
	MI_UncompressLZ16(subCloseGfx, bgGetGfxPtr(bg2s));
	free(subCloseGfx);
	
	void *subCloseScreen = IO_ReadEntireFile("game/close_s_b.nsc.bin", &fileSize);
	MI_UncompressLZ16(subCloseScreen, bgGetMapPtr(bg2s));
	free(subCloseScreen);
	
	//setup main OBJ
	int i;
	for(i = 0; i < 9; i++){
		g_numberStates[i] = 0;
		g_numberY[i] = 0;
		g_numberLocks[i] = 0;
		gameSetupNumber(24 + i * 24, 0, i + 1);
	}
	
	g_gameStart = g_frameCount;
	g_gameFadingOut = FALSE;
	g_gameFadeOutStart = 0;
	g_gameFadeOutToScene = SCENE_TITLE;
	
	memset(g_numberStates, 0, sizeof(g_numberStates));
	g_currentPlayer = 0;
	g_matchPoint = FALSE;
	g_lastWon = 0;
	g_currentNumber = 0;
	g_currentCycle = CYCLE_ANTE;
	g_cycleReturnTo = CYCLE_FLIP;
	g_firstPlayer = 0;
	g_currentCycleTime = 0;
	for(i = 0; i < 8; i++){
		g_antes[i] = 0;
		g_balances[i] = 10;
	}
	newRound();
}

void gameSetupPlayerText(){
	u32 playerNumberGfxOffset = 148 + 2 * g_currentPlayer;
	if(g_currentPlayer >= 6) playerNumberGfxOffset = 214 + (g_currentPlayer - 6) * 2;

	oamSet(&oamMain, 12, 109, 64, 0,
		0, SpriteSize_32x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(144)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamMain, 13, 109 + 34, 64, 0,
		0, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(playerNumberGfxOffset)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamUpdate(&oamMain);
}

void gameSetupAnteText(){
	gameSetupPlayerText();
	oamSet(&oamMain, 14, 108, 64 + 16, 0,
		0, SpriteSize_32x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(208)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamMain, 15, 108 + 32, 64 + 16, 0,
		0, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(212)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	
	oamUpdate(&oamMain);
	
	oamSet(&oamSub, 0, 96, 10, 0,
			0, SpriteSize_32x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(24)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 1, 192, 10, 0,
			0, SpriteSize_32x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(64)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 2, 192 + 32, 10, 0,
			0, SpriteSize_16x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(68)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	
	int i;
	for(i = 0; i < g_nPlayers; i++){
		oamSet(&oamSub, i * 5 + 3, 10, 10 + 5 + 16 + i * 16, 0,
			0, SpriteSize_32x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(0)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		oamSet(&oamSub, i * 5 + 4, 10 + 34, 10 + 5 + 16 + i * 16, 0,
			0, SpriteSize_16x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(6 + 2 * i)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		oamSet(&oamSub, i * 5 + 5, 96, 10 + 5 + 16 + i * 16, 0,
			0, SpriteSize_16x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(4 + 2 * g_antes[i])), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		oamSet(&oamSub, i * 5 + 6, 192, 10 + 5 + 16 + i * 16, 0,
			0, SpriteSize_16x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(4 + 2 * (g_balances[i] / 10))), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		oamSet(&oamSub, i * 5 + 7, 199, 10 + 5 + 16 + i * 16, 0,
			0, SpriteSize_16x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(4 + 2 * (g_balances[i] % 10))), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	}
	
	oamUpdate(&oamSub);
}

void gameDisplayBalances(){
	oamClear(&oamSub, 0, 0);
	oamSet(&oamSub, 1, 192, 10, 0,
			0, SpriteSize_32x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(64)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, 2, 192 + 32, 10, 0,
			0, SpriteSize_16x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(68)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	
	int i;
	for(i = 0; i < g_nPlayers; i++){
		oamSet(&oamSub, i * 5 + 3, 10, 10 + 5 + 16 + i * 16, 0,
			0, SpriteSize_32x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(0)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		oamSet(&oamSub, i * 5 + 4, 10 + 34, 10 + 5 + 16 + i * 16, 0,
			0, SpriteSize_16x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(6 + 2 * i)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		oamSet(&oamSub, i * 5 + 6, 192, 10 + 5 + 16 + i * 16, 0,
			0, SpriteSize_16x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(4 + 2 * (g_balances[i] / 10))), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		oamSet(&oamSub, i * 5 + 7, 199, 10 + 5 + 16 + i * 16, 0,
			0, SpriteSize_16x16, SpriteColorFormat_16Color, 
			oamGetGfxPtr(&oamSub, SWIZZLE(4 + 2 * (g_balances[i] % 10))), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	}
	
	oamUpdate(&oamSub);
}

void anteLoop(int frame){
	//players ante up. Current player placing bets is in g_currentPlayer.
	static int framesThisAnte = 0;
	if(g_currentCycleTime == 0){
		//setup ante
		gameSetupAnteText();
		g_currentPlayer = 0;
		framesThisAnte = 0;
	}
	
	//get input
	scanKeys();
	u32 keys = keysDown();
	
	touchPosition touchPos;
	touchRead(&touchPos);
	
	if(!hasPlayerControl()){
		keys &= ~KEY_TOUCH;
		if(framesThisAnte == 20) {
			int ante = 1 + (getRandom() % 3);
			keys |= KEY_TOUCH;
			touchPos.px = 24 + 24 * (ante - 1);
			touchPos.py = 0;
		}
	}
	
	//read inputs. Start with touch input.
	if(keys & KEY_TOUCH){
		//is touching one of the numbers?
		if(touchPos.py < 24 && touchPos.px >= 10 && touchPos.px < 246 && touchPos.px >= 24 && touchPos.px < 232){
			int pressed = (touchPos.px - 20) / 24 + 1;
			
			g_antes[g_currentPlayer] = pressed;
			g_balances[g_currentPlayer] -= pressed;
			g_currentPlayer++;
			framesThisAnte = 0;
			
			if(g_currentPlayer == g_nPlayers){
				g_currentPlayer = 0; //g_lastWon; //see above
				g_currentCycle = CYCLE_PREROLL;
				g_currentCycleTime = -1; //later gets incremented
			}
			gameSetupAnteText();
		}
	}
	framesThisAnte++;
}

void showDie(int x, int y, int value, int index){
	u32 gfxOffset = 384 + 4 * value;
	oamSet(&oamSub, index + 1, x, y, 0,
		0, SpriteSize_32x32, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, SWIZZLE(384)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamSub, index, x, y, 0,
		0, SpriteSize_32x32, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamSub, SWIZZLE(gfxOffset)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamUpdate(&oamSub);
}

void gameSetupRollGraphics(){
	u32 playerNumberGfxOffset = 148 + 2 * g_currentPlayer;
	if(g_currentPlayer >= 6) playerNumberGfxOffset = 214 + (g_currentPlayer - 6) * 2;
	
	oamClear(&oamMain, 12, 116);
	oamClear(&oamSub, 0, 0);
	
	oamSet(&oamMain, 12, 109, 64, 0,
		0, SpriteSize_32x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(144)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamMain, 13, 109 + 34, 64, 0,
		0, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(playerNumberGfxOffset)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		
	oamUpdate(&oamMain);
	oamUpdate(&oamSub);
	
}

void showStopButton(int x, int y, int pal){
	oamSet(&oamMain, 14, x, y, 0,
		pal, SpriteSize_64x32, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(256)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamMain, 15, x + 64, y, 0,
		pal, SpriteSize_32x32, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(264)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamUpdate(&oamMain);
}

void showRollButton(int x, int y, int pal){
	oamSet(&oamMain, 14, x, y, 0,
		pal, SpriteSize_64x32, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(268)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamMain, 15, x + 64, y, 0,
		pal, SpriteSize_32x32, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(276)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamUpdate(&oamMain);
}

void gameSetupNumberOfDiceGraphics(int x, int y, int oam){
	oamSet(&oamMain, oam, x, y + 4, 0,
		2, SpriteSize_32x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(344)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamMain, oam + 1, x + 32, y + 4, 0,
		2, SpriteSize_32x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(348)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamMain, oam + 2, x + 64, y + 4, 0,
		2, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(384)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		
	oamSet(&oamMain, oam + 3, x + 64 + 17, y, 0,
		g_nDice == 1 ? 1 : 2, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(386)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamSet(&oamMain, oam + 4, x + 64 + 17 + 16, y, 0,
		g_nDice == 1 ? 2 : 1, SpriteSize_16x16, SpriteColorFormat_16Color,
		oamGetGfxPtr(&oamMain, SWIZZLE(388)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
	oamUpdate(&oamMain);
}

void prerollLoop(int frame){
	if(g_currentCycleTime == 0){
		showRollButton(80, 80, 2);
		gameSetupPlayerText();
		
		//also show on OAM 17, if applicable, the option to use 1 or 2 dice
		if(g_numberLocks[7 - 1] && g_numberLocks[8 - 1] && g_numberLocks[9 - 1]){
			//Number of dice + 1/2 graphic size: 76 + 5 + 32
			gameSetupNumberOfDiceGraphics(128 - 38 - 16 - 2, 128, 17);
		}
	}
	
	//get input
	scanKeys();
	u32 keys = keysDown();
	
	touchPosition touchPos;
	touchRead(&touchPos);
	
	if(!hasPlayerControl()){
		keys &= ~KEY_TOUCH;
		if(g_currentCycleTime == 30){
			keys |= KEY_TOUCH;
			touchPos.px = 153;
			touchPos.py = 128;
		}
		if(g_currentCycleTime == 60){
			keys |= KEY_TOUCH;
			touchPos.px = 80;
			touchPos.py = 80;
		}
	}
	
	if(keys & KEY_TOUCH){
		if(touchPos.px >= 80 && touchPos.py >= 80 && touchPos.px < 80 + 96 && touchPos.py < 80 + 32){
			g_currentCycleTime = -1;
			g_currentCycle = CYCLE_ROLL;
		}
		
		//changing the number of dice?
		if(g_numberLocks[7 - 1] && g_numberLocks[8 - 1] && g_numberLocks[9 - 1]){
			if(touchPos.px >= 153 && touchPos.py >= 128 && touchPos.px < 169 && touchPos.py < 144){
				g_nDice = 1;
				gameSetupNumberOfDiceGraphics(128 - 38 - 16 - 2, 128, 17);
			}
			if(touchPos.px >= 169 && touchPos.py >= 128 && touchPos.px < 185 && touchPos.py < 144){
				g_nDice = 2;
				gameSetupNumberOfDiceGraphics(128 - 38 - 16 - 2, 128, 17);
			}
		}
	}
}

void rollLoop(int frame){
	static int rolling = 0;
	
	if(g_currentCycleTime == 0){
		//setup roll
		gameSetupRollGraphics();
		rolling = 1;
	}
	
	//get input
	scanKeys();
	u32 keys = keysDown();
	
	touchPosition touchPos;
	touchRead(&touchPos);
	
	if(rolling){
		if((g_currentCycleTime & 0x3) == 0){
			g_diceValues[0] = (getRandom() % 6) + 1;
			g_diceValues[1] = (getRandom() % 6) + 1;
		}
		
		//if not in player control, stop dice roll after 1 second.
		if(!hasPlayerControl()){
			keys &= ~KEY_TOUCH;
			if(g_currentCycleTime == 60){
				keys |= KEY_TOUCH;
				touchPos.px = 80;
				touchPos.py = 80;
			}
		}
		
		//is pressing stop?
		if(keys & KEY_TOUCH){
			if(touchPos.px >= 80 && touchPos.py >= 80 && touchPos.px < 80 + 96 && touchPos.py < 80 + 32){
				g_diceValues[0] = (getRandom() % 6) + 1;
				g_diceValues[1] = (getRandom() % 6) + 1;
				rolling = 0;
			}
		}
		
		if(g_nDice == 2){
			//2 dice, 16 px in between. 32+32+16=64+16=80px width
			showDie(88, 80, g_diceValues[0], 0);
			showDie(136, 80, g_diceValues[1], 2);
		} else {
			showDie(112, 80, g_diceValues[0], 0);
		}
		showStopButton(80, 80, 2);
		
	} else {
		oamClear(&oamMain, 14, 2);
		oamUpdate(&oamMain);
		
		g_currentCycleTime = -1;
		g_currentCycle = CYCLE_FLIP;
	}
}

void flipLoop(int frame){
	int i;
	static int cpuMove;
	if(g_currentCycleTime == 0){		
		oamSet(&oamMain, 14, 128 - 24, 80, 0,
			3, SpriteSize_32x16, SpriteColorFormat_16Color,
			oamGetGfxPtr(&oamMain, SWIZZLE(280)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
		oamSet(&oamMain, 15, 128 - 24 + 32, 80, 0,
			3, SpriteSize_16x16, SpriteColorFormat_16Color,
			oamGetGfxPtr(&oamMain, SWIZZLE(284)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
			
		//if locked, just hide the locked tiles.
		for(i = 0; i < 9; i++){
			if(g_numberLocks[i]){
				gameSetupNumber(24 + i * 24, 192, i + 1);
				g_numberStates[i] = 0;
			}
		}
		
		//also check if the player can even win here
		int canPlayerWin = canPlay();
		if(!canPlayerWin){
			int totalScore = 0;
			for(i = 0; i < 9; i++){
				if(!g_numberLocks[i]) totalScore += i + 1;
			}
			g_playerScores[g_currentPlayer] = totalScore;
			
			g_currentPlayer++;
			g_currentCycleTime = -1;
			g_currentCycle = totalScore ? CYCLE_LOSE : CYCLE_WIN;
			g_nTurns++;
			
			return;
		}
		
		//is this a player or CPU?
		if(!hasPlayerControl()){
			cpuMove = makeCpuMove();
		}
	}
	
	//get input
	scanKeys();
	u32 keys = keysDown();
	
	touchPosition touchPos;
	touchRead(&touchPos);
	
	//make CPU moves if applicable.
	if(!hasPlayerControl()){
		keys &= ~KEY_TOUCH;
		if(g_currentCycleTime >= 20 && g_currentCycleTime <= 110){
			if((g_currentCycleTime - 20) % 10 == 0){
				if(cpuMove == 0){
					keys |= KEY_TOUCH;
					touchPos.px = 104;
					touchPos.py = 80;
				} else {
					int bit = (g_currentCycleTime - 20) / 10;
					if(cpuMove & (1 << bit)){
						keys |= KEY_TOUCH;
						touchPos.py = 0;
						touchPos.px = 24 + bit * 24;
						cpuMove &= ~(1 << bit);
					}
				}
			}
		}
	}
	
	if(keys & KEY_TOUCH){
		
		//is touching a tile?
		if(touchPos.py < 24 && touchPos.px >= 10 && touchPos.px < 246 && touchPos.px >= 24 && touchPos.px < 232){
			int pressed = (touchPos.px - 20) / 24 + 1;
			
			if(!g_numberLocks[pressed - 1]){
				
				g_numberStates[pressed - 1] ^= 1;
				
				//count total, is it valid?
				int total = getFlipTotal();
				int diceTotal = getDiceTotal();
				
				if(total == diceTotal){
					//show continue play button
					oamSet(&oamMain, 14, 128 - 24, 80, 0,
						2, SpriteSize_32x16, SpriteColorFormat_16Color,
						oamGetGfxPtr(&oamMain, SWIZZLE(280)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
					oamSet(&oamMain, 15, 128 - 24 + 32, 80, 0,
						2, SpriteSize_16x16, SpriteColorFormat_16Color,
						oamGetGfxPtr(&oamMain, SWIZZLE(284)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
				} else {
					//hide play button
					oamSet(&oamMain, 14, 128 - 24, 80, 0,
						3, SpriteSize_32x16, SpriteColorFormat_16Color,
						oamGetGfxPtr(&oamMain, SWIZZLE(280)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
					oamSet(&oamMain, 15, 128 - 24 + 32, 80, 0,
						3, SpriteSize_16x16, SpriteColorFormat_16Color,
						oamGetGfxPtr(&oamMain, SWIZZLE(284)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
				}
				
				//TODO: show total on top screen
			}
			
		}
		
		//is touching the play button?
		if(touchPos.px >= 128 - 24 && touchPos.py >= 80 && touchPos.px <= 128 + 24 && touchPos.py < 96 && getFlipTotal() == getDiceTotal()){
			//clicked play. Lock the current flipped tiles, go back to preroll cycle.
			for(i = 0; i < 9; i++){
				if(g_numberStates[i]){
					g_numberLocks[i] = 1;
					gameSetupNumber(24 + i * 24, 192, i + 1);
					g_numberStates[i] = 0;
				}
			}
			
			//check: Did the player just flip all the tiles? If so, don't bother with a preroll, jump to the box opening.
			int hasUnflipped = 0;
			for(i = 0; i < 9; i++){
				if(!g_numberLocks[i]){
					hasUnflipped = 1;
					break;
				}
			}
			
			if(!hasUnflipped){
				g_playerScores[g_currentPlayer] = 0;
			
				g_currentPlayer++;
				g_nTurns++;
				g_currentCycle = CYCLE_WIN;
			} else {
				g_currentCycle = CYCLE_PREROLL;
			}
			g_currentCycleTime = -1;
		}
		
	}
	
	for(i = 0; i < 9; i++){
		if(g_numberStates[i] && g_numberY[i] != -4){
			g_numberY[i]--;
		}
		if(!g_numberStates[i] && g_numberY[i] != 0){
			g_numberY[i]++;
		}
		
		if(!g_numberLocks[i]) gameSetupNumber(24 + i * 24, g_numberY[i], i + 1);
	}
	
}

void loseLoop(int frame){
	int i;
	//wait for 2 seconds, or until g_currentCycleTime >= 120.
	if(g_currentCycleTime >= 120){
		if(g_nTurns == g_nPlayers) {
			oamClear(&oamMain, 0, 0);
			oamUpdate(&oamMain);
		}
		//reset locks and flips
		for(i = 0; i < 9; i++){
			g_numberLocks[i] = 0;
			g_numberStates[i] = 0;
			g_numberY[i] = 0;
			g_nDice = 2;
			gameSetupNumber(24 + i * 24, 0, i + 1);
		}
		
		//display scores list.
		gameDisplayScoreList();
		
		g_currentCycleTime = -1;
		g_currentCycle = (g_nTurns == g_nPlayers || g_matchPoint) ? CYCLE_WINNER : CYCLE_PREROLL;
	}
}

void winnerLoop(int frame){
	int i;
	if(g_currentCycleTime == 0){
		gameDisplayScoreList();
		oamClear(&oamMain, 12, 116);
		
		//divy up the score. If there's a tie, divide the antes among all winning players, giving
		//priority to the first players.
		
		int bestScore = 45;
		for(i = 0; i < g_nPlayers; i++){
			if(g_playerScores[i] < bestScore) bestScore = g_playerScores[i];
		}
		int nWinners = 0;
		int firstWinner = -1;
		for(i = 0; i < g_nPlayers; i++){
			if(g_playerScores[i] == bestScore) {
				nWinners++;
				if(firstWinner == -1) firstWinner = i;
			}
		}
		g_lastWon = firstWinner;
		
		//divide the scores
		int anteTotal = getAnteTotal();
		for(i = 0; i < g_nPlayers; i++){
			int playerIndex = (g_firstPlayer + i) % g_nPlayers;
			if(g_playerScores[playerIndex] == bestScore){
				int add = (anteTotal + nWinners - 1) / nWinners;
				g_balances[playerIndex] += add;
				anteTotal -= add;
				nWinners--;
				if(nWinners == 0) break;
			}
		}
	}
	
	//get input
	scanKeys();
	u32 keys = keysDown();
	
	touchPosition touchPos;
	touchRead(&touchPos);
	
	//after g_nPlayers seconds, advance.
	if(g_currentCycleTime == g_nPlayers * 60){
		gameDisplayBalances();
	}
	
	//after a bit, display option to keep playing or exit.
	if(g_currentCycleTime - g_nPlayers * 60 >= 60){
		if(g_currentCycleTime - g_nPlayers * 60 == 60){
			oamClear(&oamMain, 12, 116);
			
			oamSet(&oamMain, 12, 101, 92, 0,
				2, SpriteSize_32x16, SpriteColorFormat_16Color,
				oamGetGfxPtr(&oamMain, SWIZZLE(390)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
			oamSet(&oamMain, 13, 101 + 32, 92, 0,
				2, SpriteSize_32x16, SpriteColorFormat_16Color,
				oamGetGfxPtr(&oamMain, SWIZZLE(394)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
				
			//69px width
			oamSet(&oamMain, 14, 128 - 34, 108, 0,
				1, SpriteSize_32x16, SpriteColorFormat_16Color,
				oamGetGfxPtr(&oamMain, SWIZZLE(398)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
			oamSet(&oamMain, 15, 131, 108, 0,
				2, SpriteSize_32x16, SpriteColorFormat_16Color,
				oamGetGfxPtr(&oamMain, SWIZZLE(402)), -1, FALSE, FALSE, FALSE, FALSE, FALSE);
				
			oamUpdate(&oamMain);
		}
		
		if(keys & KEY_TOUCH){
			if(touchPos.py >= 108 && touchPos.py < 124){
				if(touchPos.px >= 94 && touchPos.px < 126){
					//return to menu
					g_gameFadingOut = 1;
					g_gameFadeOutStart = g_frameCount;
					g_gameFadeOutToScene = SCENE_TITLE;
					
					BLDY = 0;
					DB_BLDY = 0;
					BLDCNT = 0x3F | (3 << 6);
					DB_BLDCNT = 0x3F | (3 << 6);
				}
				
				if(touchPos.px >= 131 && touchPos.px < 163){
					newRound();
					g_currentCycle = CYCLE_ANTE;
					g_currentCycleTime = -1;
				}
			}
		}
	}
}

void winLoop(int frame){
	int i;
	if(g_currentCycleTime == 0){
		oamClear(&oamMain, 14, 2);
		
		//show "Closed the box!" graphic
		bgShow(6);
		
		oamClear(&oamSub, 0, 0);
		oamUpdate(&oamSub);
		
		DB_BLDCNT = (1 << 2) | (1 << 6) | ((0x3F & ~(1 << 2)) << 8);
		DB_BLDALPHA = (8 << 8) | (8);
		
		if(g_matchPoint) g_matchPoint = 0;
		else g_matchPoint = 1;
	}
	
	if(g_currentCycleTime < 60){
		int animFrame = g_currentCycleTime >> 1;
		if(animFrame > 16)  animFrame = 16;
		DB_BLDALPHA = ((16 - animFrame) << 8) | (animFrame);
	}
	
	if(g_currentCycleTime >= 60){
		int animFrame = (g_currentCycleTime - 60) >> 1;
		if(animFrame > 16)  animFrame = 16;
		animFrame = 16 - animFrame;
		DB_BLDALPHA = ((16 - animFrame) << 8) | (animFrame);
	}
	
	if(g_currentCycleTime == 92){
		DB_BLDCNT = 0;
		DB_BLDALPHA = 0;
		bgHide(6);
		
		//reset locks and flips
		for(i = 0; i < 9; i++){
			g_numberLocks[i] = 0;
			g_numberStates[i] = 0;
			g_numberY[i] = 0;
			g_nDice = 2;
			gameSetupNumber(24 + i * 24, 0, i + 1);
		}
			
		//display scores list.
		gameDisplayScoreList();
		
		g_currentCycleTime = -1;
		g_currentCycle = g_nTurns == g_nPlayers ? CYCLE_WINNER : CYCLE_PREROLL;
	}
}

void gameLoop(int frame){
	switch(g_currentCycle){
		case CYCLE_ANTE:
			anteLoop(frame);
			break;
		case CYCLE_ROLL:
			rollLoop(frame);
			break;
		case CYCLE_PREROLL:
			prerollLoop(frame);
			break;
		case CYCLE_FLIP:
			flipLoop(frame);
			break;
		case CYCLE_WIN:
			winLoop(frame);
			break;
		case CYCLE_LOSE:
			loseLoop(frame);
			break;
		case CYCLE_PAUSE:
			break;
		case CYCLE_WINNER:
			winnerLoop(frame);
			break;
	}
	
	g_currentCycleTime++;
}

void gameTickProc(){
	if(g_gameLastTick + 1 != g_frameCount){
		gameInitialize();
	}
	
	//first 32 frames, fade in the screen.
	int frame = g_frameCount - g_gameStart;
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
	
	//if after frame 32, run game code.
	if(frame > 32 && !g_gameFadingOut){
		gameLoop(frame - 33);
	}
	
	if(g_gameFadingOut){
		int animFrame = g_frameCount - g_gameFadeOutStart;
		int brightness = animFrame / 2;
		if(brightness > 16) brightness = 16;
		
		BLDY = brightness;
		DB_BLDY = brightness;
		
		if(animFrame == 32) g_scene = g_gameFadeOutToScene;
	}
	
	g_gameLastTick = g_frameCount;
}