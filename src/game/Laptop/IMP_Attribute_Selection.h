#ifndef IMP_ATTRIBUTE_SELECTION_H
#define IMP_ATTRIBUTE_SELECTION_H

#include "Types.h"

void EnterIMPAttributeSelection(void);
void RenderIMPAttributeSelection(void);
void ExitIMPAttributeSelection(void);
void HandleIMPAttributeSelection(void);

void RenderAttributeBoxes(void);

extern BOOLEAN fReviewStats;
extern BOOLEAN fFirstIMPAttribTime;
extern BOOLEAN fReturnStatus;

// Stat indices (HEALTH..MECHANICAL = 0..9). Console_Imp uses these to drive
// IncrementStat / DecrementStat / GetCurrentAttributeValue from outside the
// IMP attribute screen.
enum IMP_AttributeIndex
{
	IMP_ATTR_HEALTH = 0,
	IMP_ATTR_DEXTERITY,
	IMP_ATTR_AGILITY,
	IMP_ATTR_STRENGTH,
	IMP_ATTR_WISDOM,
	IMP_ATTR_LEADERSHIP,
	IMP_ATTR_MARKSMANSHIP,
	IMP_ATTR_EXPLOSIVES,
	IMP_ATTR_MEDICAL,
	IMP_ATTR_MECHANICAL,
	IMP_ATTR_COUNT
};

void IncrementStat(INT32 iStatToIncrement);
void DecrementStat(INT32 iStatToDecrement);
INT32 GetCurrentAttributeValue(INT32 attribute);
INT32 GetIMPCurrentBonusPoints(void);

// starting point of skill boxes on bar
#define SKILL_SLIDE_START_X 186
#define SKILL_SLIDE_START_Y 100
#define SKILL_SLIDE_HEIGHT   20

#endif
