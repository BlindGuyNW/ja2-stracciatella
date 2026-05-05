#ifndef __IMP_VOICES_H
#define __IMP_VOICES_H

#include "Types.h"

void EnterIMPVoices( void );
void RenderIMPVoices( void );
void ExitIMPVoices( void );
void HandleIMPVoices( void );

// Console_Imp drives voice cycling without going through the on-screen
// next/previous buttons. The play-on-change behaviour lives in the button
// callback, so the console plays the sample explicitly after either call.
void IncrementVoice(void);
void DecrementVoice(void);

// Stops the prior sample (if any) and plays the one for the current voice
// index. Pulls the static PlayVoice helper from inside this module.
void IMP_Voices_PlayCurrentSample(void);

extern INT32 iCurrentVoices;
extern INT32 iVoiceId;
#endif
