#ifndef __IMP_PORTRAIT_H
#define __IMP_PORTRAIT_H

#include "Types.h"

void EnterIMPPortraits( void );
void RenderIMPPortraits( void );
void ExitIMPPortraits( void );
void HandleIMPPortraits( void );

// Console_Imp drives the portrait carousel directly. Mirrors what the
// on-screen next/previous buttons do internally; caller is responsible for
// requesting a redraw via fReDrawPortraitScreenFlag.
void IncrementPictureIndex(void);
void DecrementPicture(void);

extern INT32 iPortraitNumber;
extern INT32 iCurrentPortrait;
extern INT32 iLastPicture;
extern BOOLEAN fReDrawPortraitScreenFlag;


#endif
