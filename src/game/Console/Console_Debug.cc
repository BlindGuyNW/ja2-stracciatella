#include "Console_Debug.h"

#include "Console.h"
#include "Console_Visibility.h"

#include "Handle_Items.h"
#include "Item_Types.h"
#include "OppList.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Render_Fun.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "Structure.h"
#include "Structure_Internals.h"
#include "WorldDef.h"
#include "World_Items.h"

#include <string_theory/format>
#include <string_theory/string>

#include <array>

namespace
{
	// One row of the structure-flag census.
	struct FlagRow
	{
		const char*    name;
		StructureFlags mask;
		int            total;       // structures whose fFlags overlap mask
		int            baseTiles;   // of those, ones with sBaseGridNo == g
		INT16          sample[5];   // first up-to-five base-tile gridnos
		int            sampleCount;
	};

	void cmdDebugStructures()
	{
		// Track every flag a player-built console verb might want to ask
		// about, plus the umbrella ANYDOOR mask the `nearby doors` enumerator
		// uses. If ANYDOOR shows zero but DOOR/DDOOR_*/SLIDINGDOOR/GARAGEDOOR
		// individually show non-zero, the umbrella mask itself is broken;
		// if everything is zero, structures aren't being loaded at all.
		std::array<FlagRow, 11> rows = {{
			{ "ANYDOOR",       STRUCTURE_ANYDOOR,      0, 0, {}, 0 },
			{ "DOOR",          STRUCTURE_DOOR,         0, 0, {}, 0 },
			{ "DDOOR_LEFT",    STRUCTURE_DDOOR_LEFT,   0, 0, {}, 0 },
			{ "DDOOR_RIGHT",   STRUCTURE_DDOOR_RIGHT,  0, 0, {}, 0 },
			{ "SLIDINGDOOR",   STRUCTURE_SLIDINGDOOR,  0, 0, {}, 0 },
			{ "GARAGEDOOR",    STRUCTURE_GARAGEDOOR,   0, 0, {}, 0 },
			{ "OPENABLE",      STRUCTURE_OPENABLE,     0, 0, {}, 0 },
			{ "WALL",          STRUCTURE_WALL,         0, 0, {}, 0 },
			{ "WALLNWINDOW",   STRUCTURE_WALLNWINDOW,  0, 0, {}, 0 },
			{ "TREE",          STRUCTURE_TREE,         0, 0, {}, 0 },
			{ "ROOF",          STRUCTURE_ROOF,         0, 0, {}, 0 },
		}};

		int totalStructs    = 0;
		int gridnosWithAny  = 0;

		for (INT16 g = 0; g < WORLD_MAX; ++g)
		{
			bool anyHere = false;
			for (STRUCTURE const* i = gpWorldLevelData[g].pStructureHead; i; i = i->pNext)
			{
				++totalStructs;
				anyHere = true;
				for (FlagRow& r : rows)
				{
					if (i->fFlags & r.mask)
					{
						++r.total;
						// STRUCTURE_BASE_TILE is the canonical base-tile flag
						// (Structure.cc:401). sBaseGridNo is only assigned on
						// non-base satellite records.
						if (i->fFlags & STRUCTURE_BASE_TILE)
						{
							++r.baseTiles;
							if (r.sampleCount < 5)
							{
								r.sample[r.sampleCount++] = g;
							}
						}
					}
				}
			}
			if (anyHere) ++gridnosWithAny;
		}

		Console_Println(ST::format(
			"Structure census: {} structures across {} gridnos (of {} total).",
			totalStructs, gridnosWithAny, WORLD_MAX));

		for (const FlagRow& r : rows)
		{
			ST::string samples;
			for (int s = 0; s < r.sampleCount; ++s)
			{
				if (s > 0) samples += ", ";
				const INT16 col = r.sample[s] % WORLD_COLS;
				const INT16 row = r.sample[s] / WORLD_COLS;
				samples += ST::format("({},{})", col, row);
			}
			if (r.total == 0)
			{
				Console_Println(ST::format("  {} 0", r.name));
			}
			else
			{
				Console_Println(ST::format("  {} {} matches, {} base tiles, samples: {}",
				                           r.name, r.total, r.baseTiles, samples));
			}
		}
	}

	const char* visibilityName(INT8 b)
	{
		switch (b)
		{
			case HIDDEN_ITEM:      return "HIDDEN_ITEM";
			case BURIED:           return "BURIED";
			case HIDDEN_IN_OBJECT: return "HIDDEN_IN_OBJECT";
			case INVISIBLE:        return "INVISIBLE";
			case VISIBLE:          return "VISIBLE";
			default:               return "?";
		}
	}

	void cmdDebugItems()
	{
		int total   = 0;
		int byVis[6] = {0,0,0,0,0,0}; // matches enum -4..1, indexed via b+4
		int sampleVisible = 0;
		ST::string visSamples;

		CFOR_EACH_WORLD_ITEM(wi)
		{
			++total;
			const int idx = wi.bVisible + 4;
			if (idx >= 0 && idx < 6) ++byVis[idx];
			if (wi.bVisible >= VISIBLE && sampleVisible < 5)
			{
				if (sampleVisible > 0) visSamples += ", ";
				const INT16 col = wi.sGridNo % WORLD_COLS;
				const INT16 row = wi.sGridNo / WORLD_COLS;
				visSamples += ST::format("({},{}):item#{}", col, row, wi.o.usItem);
				++sampleVisible;
			}
		}

		Console_Println(ST::format("WORLDITEM census: {} entries.", total));
		for (int b = HIDDEN_ITEM; b <= VISIBLE; ++b)
		{
			const int idx = b + 4;
			if (idx < 0 || idx >= 6) continue;
			if (byVis[idx] == 0) continue;
			Console_Println(ST::format("  {} (b={}): {}",
			                           visibilityName(static_cast<INT8>(b)), b, byVis[idx]));
		}
		if (sampleVisible > 0)
		{
			Console_Println(ST::format("  visible samples: {}", visSamples));
		}
	}

	// Symbolic name for the opplist sighting value, with the numeric
	// value appended so the user can see the raw byte.
	const char* opplistName(INT8 v)
	{
		switch (v)
		{
			case NOT_HEARD_OR_SEEN:   return "NOT_HEARD_OR_SEEN";
			case SEEN_CURRENTLY:      return "SEEN_CURRENTLY";
			case SEEN_THIS_TURN:      return "SEEN_THIS_TURN";
			case HEARD_THIS_TURN:     return "HEARD_THIS_TURN";
		}
		// SEEN_*_TURNS_AGO and HEARD_*_TURNS_AGO fall through; the
		// numeric value (printed alongside) tells the story.
		if (v > 0) return "SEEN_N_TURNS_AGO";
		if (v < 0) return "HEARD_N_TURNS_AGO";
		return "?";
	}

	// Lists every CIV_TEAM soldier in the slot table, regardless of any
	// fog or sticky-memory filter. Use this to compare against `nearby c`
	// and `room` — if a civ is in the census but not in those listings,
	// look at the active/life/inSector/opp/sticky columns to see why.
	void cmdDebugCivilians()
	{
		int total = 0;
		FOR_EACH_MERC(it)
		{
			const SOLDIERTYPE& t = **it;
			if (t.bTeam != CIV_TEAM) continue;
			++total;
		}
		Console_Println(ST::format("Civilian census: {} CIV_TEAM soldiers.", total));
		if (total == 0) return;

		FOR_EACH_MERC(it)
		{
			const SOLDIERTYPE& t = **it;
			if (t.bTeam != CIV_TEAM) continue;
			const INT8 opp = gbPublicOpplist[OUR_TEAM][t.ubID];
			const bool sticky = ConsoleVis::CivilianEverSeenHere(t.ubID);
			ST::string stickyPart;
			if (sticky)
			{
				const INT16 g = ConsoleVis::CivilianLastGridno(t.ubID);
				stickyPart = ST::format(" sticky=yes last=({},{}) lvl{} t={}m",
				                        g % WORLD_COLS, g / WORLD_COLS,
				                        ConsoleVis::CivilianLastLevel(t.ubID),
				                        ConsoleVis::CivilianLastSeenMin(t.ubID));
			}
			else
			{
				stickyPart = " sticky=no";
			}
			Console_Println(ST::format(
				"  id={} {} at ({},{}) lvl{} active={} life={} inSector={} opp={}({}){}",
				t.ubID, t.name,
				t.sGridNo % WORLD_COLS, t.sGridNo / WORLD_COLS, t.bLevel,
				t.bActive ? 1 : 0, t.bLife, t.bInSector ? 1 : 0,
				opplistName(opp), static_cast<int>(opp),
				stickyPart));
		}
	}

	void cmdDebugWorld()
	{
		// Rough sanity check: did the world data load at all?
		int withTerrain  = 0;
		int withRevealed = 0;
		int withRoom     = 0;
		for (INT16 g = 0; g < WORLD_MAX; ++g)
		{
			if (gpWorldLevelData[g].ubTerrainID != NO_TERRAIN) ++withTerrain;
			if (gpWorldLevelData[g].uiFlags & MAPELEMENT_REVEALED) ++withRevealed;
		}
		// Room IDs are stored in gubWorldRoomInfo (Render_Fun.h), not in
		// gpWorldLevelData. NO_ROOM (=0) means "outdoors / no room here."
		for (INT16 g = 0; g < WORLD_MAX; ++g)
		{
			if (gubWorldRoomInfo[g] != NO_ROOM) ++withRoom;
		}

		Console_Println(ST::format(
			"World load: {} tiles with terrain, {} revealed, {} in a room.",
			withTerrain, withRevealed, withRoom));

		const SOLDIERTYPE* sel = GetSelectedMan();
		if (sel)
		{
			Console_Println(ST::format(
				"  selected: {} at gridno {} ({},{}), level {}.",
				sel->name, sel->sGridNo,
				sel->sGridNo % WORLD_COLS, sel->sGridNo / WORLD_COLS,
				sel->bLevel));
		}
	}
}

void Cmd_Debug(const std::vector<std::string>& args)
{
	const std::string sub = args.size() >= 2 ? args[1] : std::string("summary");

	if (sub == "summary" || sub == "all")
	{
		cmdDebugWorld();
		cmdDebugStructures();
		cmdDebugItems();
		// civs deliberately omitted from "all" — it can be long (50+
		// lines in a town) and is targeted at a specific question.
		return;
	}
	if (sub == "structures" || sub == "structs") { cmdDebugStructures(); return; }
	if (sub == "items")                          { cmdDebugItems();      return; }
	if (sub == "world")                          { cmdDebugWorld();      return; }
	if (sub == "civs" || sub == "civilians")     { cmdDebugCivilians();  return; }

	Console_Println(ST::format(
		"unknown debug topic '{}' (try: summary, structures, items, world, civs)", sub));
}
