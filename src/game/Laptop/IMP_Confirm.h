#ifndef __IMP_CONFIRM_H
#define __IMP_CONFIRM_H

#include "Types.h"

#include "Button_System.h"

void EnterIMPConfirm( void );
void RenderIMPConfirm( void );
void ExitIMPConfirm( void );
void HandleIMPConfirm( void );

void ResetIMPCharactersEyesAndMouthOffsets( UINT8 ubMercProfileID );

// 0 = Yes (hire), 1 = No (back). The console's `imp hire` synthesises a
// click on giIMPConfirmButton[0] so it picks up all the side effects of
// BtnIMPConfirmYes (HireMerc, profile copy, charging, history entry).
extern GUIButtonRef giIMPConfirmButton[2];

#endif
