#ifndef _DISPLAY_COVER__H_
#define _DISPLAY_COVER__H_

#include "JA2Types.h"

#include <vector>


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

/* Per-enemy decomposition of the same cover computation. One entry per
 * team-known living opponent (same "known" sense the overlay uses:
 * SEEN_CURRENTLY or SEEN_THIS_TURN, personal or public). `threat` is the
 * per-enemy contribution that CalcCoverForGridNoBasedOnTeamKnownEnemies
 * sums internally; `threat == 0` means the opponent is known but their
 * LOS, range, or weapon reach to (sTargetGridNo, bStance) fails — they
 * don't contribute to cover loss, but the SR-console cover verb surfaces
 * them as "covered from <them>" so the user can tell "no enemies" from
 * "enemies blocked." */
struct CoverOpponent
{
	SOLDIERTYPE* opponent;
	INT8         threat;
};

void EvaluateCoverOpponentsAtGridNo(const SOLDIERTYPE*          pSoldier,
                                    INT16                       sTargetGridNo,
                                    INT8                        bStance,
                                    std::vector<CoverOpponent>& out);

#endif
