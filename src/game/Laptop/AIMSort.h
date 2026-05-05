#ifndef __AIMSORT_H_
#define __AIMSORT_H_

#include "Types.h"

extern UINT8 gubCurrentSortMode;
extern UINT8 gubCurrentListMode;


#define AIM_ASCEND	6
#define AIM_DESCEND	7


void GameInitAimSort(void);
void EnterAimSort(void);
void ExitAimSort(void);
void RenderAimSort(void);

// Sort AimMercArray in place per the current gubCurrentSortMode /
// gubCurrentListMode. Idempotent. Exposed so console verbs that drive
// the Members page (AIMMembers_ShowProfile) can pre-sort before
// computing an index — the engine's own sort runs only on
// ExitAimSort, so without this the index can be stale when leaving
// the Sort page.
void SortAimMercArray(void);

#endif
