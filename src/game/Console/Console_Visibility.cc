#include "Console_Visibility.h"

#include "Game_Clock.h"
#include "Isometric_Utils.h"
#include "Map_Information.h"
#include "OppList.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "StrategicMap.h"
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
	// any teammate's sighting counts. NOT_HEARD_OR_SEEN (=0) is the only
	// "unknown" value; positive values are SEEN_*_TURNS_AGO and negative
	// are HEARD_*_TURNS_AGO. We accept stale-but-not-yet-decayed values
	// (the engine's own AI uses the same `!= NOT_HEARD_OR_SEEN` test for
	// "have we ever encountered this guy" — see AIUtils.cc, Knowledge.cc),
	// and DecayOppListValue (OppList.cc:209) zeroes them out after the
	// OLDEST_*_VALUE thresholds, so the fade-out happens automatically.
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

namespace
{
	struct CivMemory
	{
		bool   seen      = false;
		INT16  gridno    = 0;
		INT8   level     = 0;
		UINT16 animState = 0;
		UINT32 minute    = 0;
	};

	CivMemory g_civ[TOTAL_SOLDIERS];

	// Sentinel that can never equal a real loaded sector: x=0,y=0 is a
	// valid SGPSector at runtime (sector A1), so we use z=-1 to force a
	// mismatch on the very first Tick after process start. StrategicMap
	// initializes gWorldSector to (0,0,-1) too, but on game start
	// gWorldSector is set before any tactical entry, so the first Tick
	// after world load triggers a reset.
	SGPSector g_lastSector{-1, -1, -2};
}

void Tick()
{
	if (gWorldSector != g_lastSector)
	{
		for (auto& m : g_civ) m = CivMemory{};
		g_lastSector = gWorldSector;
	}

	// Nothing to snapshot until a world is loaded — between sectors
	// gWorldSector is briefly valid but bActive flags are unsettled.
	if (!gfWorldLoaded) return;

	const UINT32 now = GetWorldTotalMin();
	FOR_EACH_MERC(it)
	{
		const SOLDIERTYPE& t = **it;
		if (t.bTeam != CIV_TEAM)  continue;
		if (!IsKnownSoldier(t))   continue;
		CivMemory& m = g_civ[t.ubID];
		m.seen      = true;
		m.gridno    = t.sGridNo;
		m.level     = t.bLevel;
		m.animState = t.usAnimState;
		m.minute    = now;
	}
}

bool   CivilianEverSeenHere(UINT8 id) { return g_civ[id].seen;      }
INT16  CivilianLastGridno  (UINT8 id) { return g_civ[id].gridno;    }
INT8   CivilianLastLevel   (UINT8 id) { return g_civ[id].level;     }
UINT16 CivilianLastAnim    (UINT8 id) { return g_civ[id].animState; }
UINT32 CivilianLastSeenMin (UINT8 id) { return g_civ[id].minute;    }

}
