#include <nds.h>
#include <filesystem.h>

#include "scenes.h"

int g_frameCount = 0;
int g_scene = SCENE_TITLE;
int g_nPlayers = 4;
int g_shift = 0x8988;
int g_cpuPlayers = FALSE;
void (*sceneFuncs[SCENE_MAX + 1])();

int getRandom(){
	int b1 = (g_shift >> 1) & 1;
	int b9 = (g_shift >> 9) & 1;
	int x = b1 ^ b9;
	return (g_shift = ((g_shift >> 1) | (x << 15))) >> 1;
}

int main(void){
	
	//Box game. 
	//Rules:
	//	Go clockwise through each player.
	//	Player rolls 2 dice. Must pick unflipped numbers that add up to the
	//	rolled number. If this cannot be done, the player is done, and then
	//	adds up the numbers left unflipped. 
	//	The gameplay continues clockwise until someone flips up all their
	//	numbers, or all the players have been reached. The player with the
	//	lowest total wins. If someone manages to flip all their numbers, they
	//	win if the next player cannot. If the next player also flips all their
	//	numbers, then nobody wins, and continues in a circle until it reaches
	//	the player that first flipped all their numbers up. When continuing a
	//	game, the person who won the last round goes first, then it continues
	//	clockwise.
	
	nitroFSInit(NULL);
	sceneFuncs[SCENE_TITLE] = titleTickProc;
	sceneFuncs[SCENE_GAMESETUP] = setupTickProc;
	sceneFuncs[SCENE_RULES] = rulesTickProc;
	sceneFuncs[SCENE_GAME] = gameTickProc;
	
	swiWaitForVBlank();
	while(1){
		sceneFuncs[g_scene]();
		
		swiWaitForVBlank();
		g_frameCount++;
		getRandom();
	}
	
	return 0;
}