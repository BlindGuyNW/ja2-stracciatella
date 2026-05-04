#include "Console_Visibility.h"

#include "Isometric_Utils.h"
#include "OppList.h"
#include "Overhead.h"
#include "Soldier_Control.h"
#include "WorldDef.h"

namespace ConsoleVis
{

bool IsKnownSoldier(const SOLDIERTYPE& s)
{
	if (!s.bActive)         return false;
	if (s.bLife <= 0)       return false;
	if (!s.bInSector)       return false;

	// The player's own mercs are always "known" to the player.
	if (s.bTeam == OUR_TEAM) return true;

	// For non-friendly soldiers, the team-known opplist drives visibility:
	// any teammate's sighting counts. Values > NOT_HEARD_OR_SEEN mean we
	// have at least heard or seen the target.
	const INT8 known = gbPublicOpplist[OUR_TEAM][s.ubID];
	return known != NOT_HEARD_OR_SEEN;
}

bool IsKnownTile(INT16 gridno, INT8 level)
{
	if (gridno < 0 || gridno >= WORLD_MAX) return false;
	const UINT32 flags = gpWorldLevelData[gridno].uiFlags;
	return level >= 1
		? (flags & MAPELEMENT_REVEALED_ROOF) != 0
		: (flags & MAPELEMENT_REVEALED)      != 0;
}

}
