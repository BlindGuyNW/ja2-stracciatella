#ifndef _IMP_SKILL_TRAIT_H
#define _IMP_SKILL_TRAIT_H

#include "Types.h"

void EnterIMPSkillTrait(void);
void RenderIMPSkillTrait(void);
void ExitIMPSkillTrait(void);
void HandleIMPSkillTrait(void);


INT8 DoesPlayerHaveExtraAttibutePointsToDistributeBasedOnSkillSelection();
void AddSelectedSkillsToSkillsList();

// Toggles a single trait on/off, enforcing the "max 2" / "NONE clears the
// rest" rules. Mirrors what the on-screen trait button callback does. fResetAllButtons
// is the engine's reset-everything escape hatch — pass FALSE for normal toggling.
void HandleIMPSkillTraitAnswers(UINT32 uiSkillPressed, BOOLEAN fResetAllButtons);

// Per-trait selection state. Index 0..14 maps to gzIMPSkillTraitsText[]
// (LOCKPICK..MARTIAL_ARTS, then NONE at 14). Read by Console_Imp's
// `imp traits` for status display.
constexpr UINT32 IMP_SKILL_TRAITS_COUNT = 15;
extern BOOLEAN gfSkillTraitQuestions[IMP_SKILL_TRAITS_COUNT];

#endif