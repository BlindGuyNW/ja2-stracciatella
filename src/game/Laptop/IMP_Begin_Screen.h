#ifndef _IMP_BEGINSCREEN_H
#define _IMP_BEGINSCREEN_H

#include "Types.h"

void EnterIMPBeginScreen( void );
void RenderIMPBeginScreen( void );
void ExitIMPBeginScreen( void );
void HandleIMPBeginScreen( void );

// Gender selection state on the IMP_BEGIN page. -1 = unset, 0 = female,
// 1 = male. ExitIMPBeginScreen copies it into fCharacterIsMale, so the
// console mutates this directly while the begin screen is open.
extern INT8 bGenderFlag;

#endif
