#pragma once

#define SCENE_TITLE        0
#define SCENE_GAMESETUP    1
#define SCENE_GAME         2
#define SCENE_RULES        3
#define SCENE_MAX          3

extern void (*sceneFuncs[SCENE_MAX + 1])();

extern int g_scene;
extern int g_frameCount;
extern int g_nPlayers;
extern int g_cpuPlayers;

int getRandom();

void titleTickProc();

void setupTickProc();

void rulesTickProc();

void gameTickProc();