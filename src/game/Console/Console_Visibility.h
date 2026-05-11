#ifndef GAME_CONSOLE_VISIBILITY_H_
#define GAME_CONSOLE_VISIBILITY_H_

#include "JA2Types.h"
#include "Types.h"

/* Single point of fog-of-war reasoning for the console surface.
 *
 * Every player-facing query routes its visibility check through one of
 * these predicates so the rule lives in one place, gets reviewed in one
 * place, and is consistent between commands. Bypassing them is a bug.
 *
 * The team-known channel (gbPublicOpplist[OUR_TEAM]) is the right unit
 * of "the player knows this": if any teammate has a sighting on Ivan,
 * the player sees Ivan, even if the selected merc cannot. */

namespace ConsoleVis
{
	/** True if the player is allowed to see this soldier exists.
	 *  Always false for inactive, dead, or off-sector soldiers. */
	bool IsKnownSoldier(const SOLDIERTYPE& s);

	/** True if the world tile at gridno is on a revealed mapelement
	 *  for the given level (0 = ground, 1 = roof). */
	bool IsKnownTile(INT16 gridno, INT8 level);

	/* Civilian sticky-memory.
	 *
	 * gbPublicOpplist decays civilian sightings on the same timer it
	 * decays enemies — but for the SR user this loses an asymmetry a
	 * sighted player keeps for free: "I remember the bartender was at
	 * the bar." Tick() runs once per frame, snapshots every
	 * IsKnownSoldier civilian's position/stance/time, and remembers
	 * them across opplist decay until the sector changes. Cleared on
	 * sector load (detected by comparing gWorldSector). */
	void Tick();

	bool   CivilianEverSeenHere(UINT8 ubID);
	INT16  CivilianLastGridno  (UINT8 ubID);
	INT8   CivilianLastLevel   (UINT8 ubID);
	UINT16 CivilianLastAnim    (UINT8 ubID);
	UINT32 CivilianLastSeenMin (UINT8 ubID);
}

#endif // GAME_CONSOLE_VISIBILITY_H_
