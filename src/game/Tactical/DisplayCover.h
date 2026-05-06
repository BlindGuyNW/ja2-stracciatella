#ifndef _DISPLAY_COVER__H_
#define _DISPLAY_COVER__H_

#include "JA2Types.h"


void DisplayCoverOfSelectedGridNo(void);
void RemoveCoverOfSelectedGridNo(void);

void DisplayRangeToTarget(SOLDIERTYPE* pSoldier, INT16 sTargetGridNo);


void RemoveVisibleGridNoAtSelectedGridNo(void);
void DisplayGridNoVisibleToSoldierGrid(void);

void ChangeSizeOfDisplayCover( INT32 iNewSize );

void ChangeSizeOfLOS( INT32 iNewSize );

/* 0..100 cover for `pSoldier` standing at `sTargetGridNo` in `bStance`,
 * computed against every team-known living opponent who can see and shoot
 * the target tile. Same value the hold-DELETE overlay buckets into colours.
 * 100 = no known enemy can see/shoot the tile; 0 = fully exposed. */
INT8 CalcCoverForGridNoBasedOnTeamKnownEnemies(const SOLDIERTYPE* pSoldier,
                                               INT16              sTargetGridNo,
                                               INT8               bStance);

#endif
