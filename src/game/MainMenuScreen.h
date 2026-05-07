#ifndef MAINMENUSCREEN_H
#define MAINMENUSCREEN_H

#include "ScreenIDs.h"


void InitMainMenu(void);
void ClearMainMenu(void);

ScreenID MainMenuScreenHandle(void);

// Drive the main menu out to `uiNewScreen` on the next frame. Sets
// `gfMainMenuScreenExit` so `MainMenuScreenHandle()` runs `ExitMainMenu()`
// (button + background-image teardown) before returning the new screen.
// Exposed so the console-driven load path can transition cleanly out of
// MAINMENU_SCREEN — `SetPendingNewScreen()` jumps `guiCurrentScreen` directly
// and would otherwise leave the menu's GUI elements alive in tactical.
void SetMainMenuExitScreen(ScreenID uiNewScreen);

#endif
