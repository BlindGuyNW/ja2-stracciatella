#include "Console_Query.h"
#include "Console_Address.h"
#include "Console_Visibility.h"

#include "Console.h"

#include "Animation_Control.h"
#include "CalibreModel.h"
#include "ContentManager.h"
#include "Exit_Grids.h"
#include "GameInstance.h"
#include "Game_Clock.h"
#include "Handle_Items.h"
#include "Interface.h"
#include "Isometric_Utils.h"
#include "ItemModel.h"
#include "Item_Types.h"
#include "Keys.h"
#include "LOS.h"
#include "MagazineModel.h"
#include "Map_Edgepoints.h"
#include "Map_Information.h"
#include "OppList.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "StrategicMap.h"
#include "Structure.h"
#include "Structure_Internals.h"
#include "TileDef.h"
#include "WeaponModels.h"
#include "Weapons.h"
#include "World_Items.h"
#include "WorldDef.h"
#include "WorldMan.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	const char* directionWord(UINT8 dir)
	{
		switch (dir)
		{
			case NORTH:     return "N";
			case NORTHEAST: return "NE";
			case EAST:      return "E";
			case SOUTHEAST: return "SE";
			case SOUTH:     return "S";
			case SOUTHWEST: return "SW";
			case WEST:      return "W";
			case NORTHWEST: return "NW";
			default:        return "?";
		}
	}

	const char* stanceWord(const SOLDIERTYPE& s)
	{
		switch (GetStance(s))
		{
			case ANIM_STAND:  return "standing";
			case ANIM_CROUCH: return "crouched";
			case ANIM_PRONE:  return "prone";
		}
		return "?";
	}

	const char* terrainWord(TerrainTypeDefines t)
	{
		switch (t)
		{
			case NO_TERRAIN:   return "void";
			case FLAT_GROUND:  return "ground";
			case FLAT_FLOOR:   return "floor";
			case PAVED_ROAD:   return "paved road";
			case DIRT_ROAD:    return "dirt road";
			case LOW_GRASS:    return "low grass";
			case HIGH_GRASS:   return "tall grass";
			case TRAIN_TRACKS: return "tracks";
			case LOW_WATER:    return "shallow water";
			case MED_WATER:    return "water";
			case DEEP_WATER:   return "deep water";
			default:           return "unknown terrain";
		}
	}

	bool startsWithCI(const ST::string& haystack, const std::string& needle)
	{
		if (needle.size() > haystack.size()) return false;
		for (std::size_t i = 0; i < needle.size(); ++i)
		{
			const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(haystack.c_str()[i])));
			const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(needle[i])));
			if (a != b) return false;
		}
		return true;
	}

	// merc <name> matches teammates only — non-teammate names should go
	// through the more general address parser when they make sense.
	SOLDIERTYPE* findTeammateByName(const std::string& needle, ST::string& errorOut)
	{
		SOLDIERTYPE* match = nullptr;
		int matchCount = 0;
		FOR_EACH_MERC(it)
		{
			SOLDIERTYPE* const s = *it;
			if (s->bTeam != OUR_TEAM) continue;
			if (!s->bActive)          continue;
			if (s->bLife <= 0)        continue;
			if (!s->bInSector)        continue;
			if (!startsWithCI(s->name, needle)) continue;
			match = s;
			++matchCount;
		}
		if (matchCount == 0) { errorOut = ST::format("no teammate matches '{}'", needle); return nullptr; }
		if (matchCount > 1)  { errorOut = ST::format("'{}' is ambiguous; use a longer prefix", needle); return nullptr; }
		return match;
	}

	ST::string itemName(UINT16 usItem)
	{
		if (usItem == 0) return ST::string("empty");
		return GCM->getItem(usItem)->getName();
	}

	// Tiles of effective range for the held item, or 0 if not a weapon.
	UINT16 weaponRangeTiles(UINT16 usItem)
	{
		if (usItem == 0) return 0;
		const ItemModel* item = GCM->getItem(usItem);
		if (item->getItemClass() != IC_GUN) return 0;
		return GCM->getWeapon(usItem)->usRange / 10;
	}
}

void Cmd_Sector(const std::vector<std::string>&)
{
	if (!gfWorldLoaded)
	{
		Console_Println("Not in a sector (no world loaded).");
		return;
	}

	ST::string name = GetSectorIDString(gWorldSector, FALSE);
	const char* phase = (gTacticalStatus.uiFlags & INCOMBAT) ? "in combat" : "real time";

	Console_Println(ST::format(
		"Sector: {}.  Time: {02d}:{02d}.  {}.",
		name, guiHour, guiMin, phase));
}

void Cmd_Merc(const std::vector<std::string>& args)
{
	SOLDIERTYPE* s = nullptr;
	if (args.size() < 2)
	{
		s = GetSelectedMan();
		if (!s) { Console_Println("No merc selected."); return; }
	}
	else
	{
		ST::string err;
		s = findTeammateByName(args[1], err);
		if (!s) { Console_Println(err); return; }
	}

	const UINT16 inHand    = s->inv[HANDPOS].usItem;
	const UINT8  shotsLeft = s->inv[HANDPOS].ubGunShotsLeft;

	Console_Println(ST::format(
		"{}: life {}/{}, breath {}/{}, AP {}, {} facing {}.",
		s->name, s->bLife, s->bLifeMax, s->bBreath, s->bBreathMax,
		s->bActionPoints, stanceWord(*s), directionWord(s->bDirection)));

	if (inHand == 0)
	{
		Console_Println("  In hand: empty.");
		return;
	}

	const UINT16 rangeTiles = weaponRangeTiles(inHand);
	if (rangeTiles > 0)
	{
		Console_Println(ST::format("  In hand: {}, range {} tiles, {} shots loaded.",
		                           itemName(inHand), rangeTiles, shotsLeft));
	}
	else
	{
		Console_Println(ST::format("  In hand: {}.", itemName(inHand)));
	}
}

namespace
{
	bool parseInt(const std::string& s, int& out)
	{
		if (s.empty()) return false;
		char* end = nullptr;
		long v = std::strtol(s.c_str(), &end, 10);
		if (end == s.c_str() || *end != '\0') return false;
		out = static_cast<int>(v);
		return true;
	}

	ST::string formatHostileLine(const ListedSoldier& ls, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const SOLDIERTYPE& t = *ls.soldier;
		const UINT8  dir    = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer.sGridNo, t.sGridNo));
		const UINT16 weapon = t.inv[HANDPOS].usItem;
		return weapon != 0
			? ST::format("  e{} {}, {} tiles {}, {}, life {}, {}",
			             tagN, t.name, ls.distance, directionWord(dir),
			             stanceWord(t), t.bLife, itemName(weapon))
			: ST::format("  e{} {}, {} tiles {}, {}, life {}",
			             tagN, t.name, ls.distance, directionWord(dir),
			             stanceWord(t), t.bLife);
	}

	ST::string formatFriendlyLine(const ListedSoldier& ls, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const SOLDIERTYPE& t = *ls.soldier;
		const UINT8 dir = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer.sGridNo, t.sGridNo));
		return ST::format("  m{} {}, {} tiles {}, life {}, AP {}",
		                  tagN, t.name, ls.distance, directionWord(dir),
		                  t.bLife, t.bActionPoints);
	}

	struct ItemPile
	{
		INT16                distance;
		INT16                gridno;
		INT8                 level;
		std::vector<UINT16>  items; // usItem of each visible WORLDITEM at this gridno
	};

	// One ItemPile per gridno+level: corpses commonly drop several items at
	// the same tile, and the player wants them collapsed. Visibility is
	// gated solely by the engine's WORLDITEM.bVisible flag — that's the
	// authoritative "player has spotted this" bit (set when an item enters
	// a merc's FOV). Layering IsKnownTile on top would drop items on
	// already-spotted tiles whose MAPELEMENT_REVEALED happens to be unset.
	void enumerateVisibleItems(const SOLDIERTYPE& observer, std::vector<ItemPile>& out)
	{
		CFOR_EACH_WORLD_ITEM(wi)
		{
			if (wi.bVisible < VISIBLE) continue;
			if (wi.o.usItem == 0)      continue;

			// Find or create the pile for this (gridno, level).
			std::size_t idx = out.size();
			for (std::size_t i = 0; i < out.size(); ++i)
			{
				if (out[i].gridno == wi.sGridNo &&
				    out[i].level  == static_cast<INT8>(wi.ubLevel))
				{
					idx = i;
					break;
				}
			}
			if (idx == out.size())
			{
				ItemPile p{};
				p.gridno   = wi.sGridNo;
				p.level    = static_cast<INT8>(wi.ubLevel);
				p.distance = PythSpacesAway(observer.sGridNo, wi.sGridNo);
				out.push_back(std::move(p));
			}
			out[idx].items.push_back(wi.o.usItem);
		}

		std::sort(out.begin(), out.end(),
			[](const ItemPile& a, const ItemPile& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.gridno < b.gridno;
			});
	}

	ST::string formatItemPileLine(const ItemPile& p, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const UINT8 dir = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer.sGridNo, p.gridno));

		// Group identical item types into "name xN" so a corpse with one
		// AK and four mags reads as "AK-47, 4x 7.62mm mag" not five lines.
		// Order is first-seen so the most "interesting" item (usually the
		// weapon, dropped first) leads.
		std::vector<UINT16> kinds;
		std::vector<int>    counts;
		for (UINT16 it : p.items)
		{
			std::size_t i = 0;
			for (; i < kinds.size(); ++i) if (kinds[i] == it) break;
			if (i == kinds.size()) { kinds.push_back(it); counts.push_back(1); }
			else                   { ++counts[i]; }
		}

		ST::string list;
		for (std::size_t i = 0; i < kinds.size(); ++i)
		{
			if (!list.empty()) list += ", ";
			list += counts[i] > 1
				? ST::format("{} x{}", itemName(kinds[i]), counts[i])
				: itemName(kinds[i]);
		}

		return ST::format("  i{} {} tiles {}: {}",
		                  tagN, p.distance, directionWord(dir), list);
	}

	struct ListedDoor
	{
		INT16 distance;
		INT16 gridno;
		bool  perceivedOpen;
		bool  perceivedKnown; // false = player has never seen this door's state
		INT8  perceivedLock;  // DOOR_PERCEIVED_* (UNKNOWN if no entry / unlocked)
	};

	// Doors are map infrastructure, not gameplay secrets — sighted players
	// see them at a glance from the rendered map graphics regardless of
	// MAPELEMENT_REVEALED state. We list all doors in the sector so SR
	// users get the same building-layout awareness. Lock and open/closed
	// state still respect player perception (DOOR_STATUS / DOOR.bPerceived*).
	void enumerateDoors(const SOLDIERTYPE& observer, std::vector<ListedDoor>& out)
	{
		for (INT16 g = 0; g < WORLD_MAX; ++g)
		{
			STRUCTURE* const s = FindStructure(g, STRUCTURE_ANYDOOR);
			if (!s) continue;
			// Dedupe multi-tile doors: only count the base tile.
			if (s->sBaseGridNo != g) continue;

			ListedDoor d{};
			d.gridno   = g;
			d.distance = PythSpacesAway(observer.sGridNo, g);

			// Perceived open/closed lives in DOOR_STATUS; absence means the
			// player hasn't observed this door yet.
			DOOR_STATUS const* const ds = GetDoorStatus(g);
			if (ds && !(ds->ubFlags & DOOR_PERCEIVED_NOTSET))
			{
				d.perceivedKnown = true;
				d.perceivedOpen  = (ds->ubFlags & DOOR_PERCEIVED_OPEN) != 0;
			}
			else
			{
				// Fall back to actual structure state. Reasonable: if the
				// tile is revealed enough to enumerate, the player has at
				// least eyes on it; status records lag a frame after sector
				// load anyway.
				d.perceivedKnown = true;
				d.perceivedOpen  = (s->fFlags & STRUCTURE_OPEN) != 0;
			}

			DOOR const* const door = FindDoorInfoAtGridNo(g);
			d.perceivedLock = door ? door->bPerceivedLocked : DOOR_PERCEIVED_UNKNOWN;

			out.push_back(d);
		}

		std::sort(out.begin(), out.end(),
			[](const ListedDoor& a, const ListedDoor& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.gridno < b.gridno;
			});
	}

	// First base-tile structure at gridno matching `wanted` and not
	// matching `excluded`. FindStructure's flag mask is OR-only, so we
	// iterate manually for "openable but not a door" etc.
	STRUCTURE* findStructureExcluding(INT16 g, UINT32 wanted, UINT32 excluded)
	{
		for (STRUCTURE* s = gpWorldLevelData[g].pStructureHead; s; s = s->pNext)
		{
			if (s->sBaseGridNo != g)   continue;
			if (!(s->fFlags & wanted)) continue;
			if (s->fFlags & excluded)  continue;
			return s;
		}
		return nullptr;
	}

	struct ListedContainer
	{
		INT16 distance;
		INT16 gridno;
	};

	void enumerateContainers(const SOLDIERTYPE& observer, std::vector<ListedContainer>& out)
	{
		for (INT16 g = 0; g < WORLD_MAX; ++g)
		{
			STRUCTURE* const s = findStructureExcluding(
				g, STRUCTURE_OPENABLE, STRUCTURE_ANYDOOR | STRUCTURE_SWITCH);
			if (!s) continue;
			out.push_back({ PythSpacesAway(observer.sGridNo, g), g });
		}
		std::sort(out.begin(), out.end(),
			[](const ListedContainer& a, const ListedContainer& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.gridno < b.gridno;
			});
	}

	ST::string formatContainerLine(const ListedContainer& c, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const UINT8 dir = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer.sGridNo, c.gridno));
		return ST::format("  k{} {} tiles {}: container",
		                  tagN, c.distance, directionWord(dir));
	}

	ST::string formatDoorLine(const ListedDoor& d, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const UINT8 dir = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer.sGridNo, d.gridno));

		const char* state = d.perceivedKnown
			? (d.perceivedOpen ? "open" : "closed")
			: "state unknown";

		const char* lock;
		switch (d.perceivedLock)
		{
			case DOOR_PERCEIVED_LOCKED:   lock = ", locked";        break;
			case DOOR_PERCEIVED_UNLOCKED: lock = ", unlocked";      break;
			case DOOR_PERCEIVED_BROKEN:   lock = ", lock broken";   break;
			default:                      lock = "";                break; // UNKNOWN: don't volunteer
		}

		return ST::format("  d{} {} tiles {}: {}{}",
		                  tagN, d.distance, directionWord(dir), state, lock);
	}

	// Civilians use the same fog rule as enemies — the engine's
	// gbPublicOpplist tracks who's been spotted by the team. Civs can
	// hide (quest NPCs, child cowering, etc.); listing only known ones
	// keeps that spoiler-safe.
	void enumerateCivilians(const SOLDIERTYPE& observer, std::vector<ListedSoldier>& out)
	{
		FOR_EACH_MERC(it)
		{
			SOLDIERTYPE* const t = *it;
			if (t->ubID == observer.ubID)        continue;
			if (t->bTeam != CIV_TEAM)            continue;
			if (!ConsoleVis::IsKnownSoldier(*t)) continue;
			out.push_back({ t, PythSpacesAway(observer.sGridNo, t->sGridNo) });
		}
		std::sort(out.begin(), out.end(),
			[](const ListedSoldier& a, const ListedSoldier& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.soldier->ubID < b.soldier->ubID;
			});
	}

	ST::string formatCivilianLine(const ListedSoldier& ls, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const SOLDIERTYPE& t = *ls.soldier;
		const UINT8 dir = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer.sGridNo, t.sGridNo));
		// Civilians' names are often generic ("Civilian") for crowd extras;
		// quest NPCs have proper names. Either way, lead with the name and
		// leave gameplay vitals (life/AP) off — civs aren't typically a
		// resource the player manages.
		return ST::format("  c{} {}, {} tiles {}, {}",
		                  tagN, t.name, ls.distance, directionWord(dir),
		                  stanceWord(t));
	}

	struct ListedExit
	{
		INT16       distance;
		INT16       gridno;
		bool        hasGridDest;  // true = EXITGRID with explicit dest sector
		SGPSector   dest;         // for grid exits: dest from EXITGRID;
		                          // for edges: neighbor sector (always valid here)
		UINT8       side;         // for edges only: NORTH/SOUTH/EAST/WEST
	};

	// Two kinds of exit:
	//   1. EXITGRID — explicit transition to a specific destination sector
	//      (basements, building entrances, town markers). Iterate WORLD_MAX
	//      and probe via GetExitGrid (same pattern the engine uses,
	//      Exit_Grids.cc:139).
	//   2. Map edgepoints — the engine pre-computes a list of *legitimate*
	//      exit tiles per cardinal side during world load (WorldDef.cc:1674,
	//      `GenerateMapEdgepoints`). Stored in `gps1st<Side>EdgepointArray`.
	//      These exclude geometrically-edge tiles that aren't actually
	//      reachable from the playable interior (walls, water blocks,
	//      isolated areas). The manual's tutorial points at *these* tiles,
	//      not "anywhere on the literal edge", which is why my earlier
	//      neighbor-based detection was firing inside buildings — interior
	//      walls produce gridnos whose neighbors are off-map but those
	//      tiles aren't real exits.
	//
	// Secondary edgepoint arrays (`gps2nd...`) cover isolated tactical
	// areas (Grumm, Alma) that aren't accessible at strategic level.
	// Skipping for v1 — primary is what the player almost always wants.
	void enumerateExits(const SOLDIERTYPE& observer, std::vector<ListedExit>& out)
	{
		const INT16 origin = observer.sGridNo;

		for (INT16 g = 0; g < WORLD_MAX; ++g)
		{
			EXITGRID xg;
			if (!GetExitGrid(g, &xg)) continue;
			ListedExit e{};
			e.gridno      = g;
			e.distance    = PythSpacesAway(origin, g);
			e.hasGridDest = true;
			e.dest        = xg.ubGotoSector;
			out.push_back(e);
		}

		auto considerEdge = [&](UINT8 side, const std::vector<INT16>& tiles,
		                        INT16 dx, INT16 dy)
		{
			if (tiles.empty()) return;
			SGPSector dest = gWorldSector;
			dest.x = static_cast<INT16>(dest.x + dx);
			dest.y = static_cast<INT16>(dest.y + dy);
			if (!dest.IsValid()) return; // off the strategic map

			INT16 bestGridNo = NOWHERE;
			INT16 bestDist   = INT16_MAX;
			for (INT16 g : tiles)
			{
				const INT16 d = PythSpacesAway(origin, g);
				if (d < bestDist) { bestDist = d; bestGridNo = g; }
			}
			if (bestGridNo == NOWHERE) return;

			ListedExit e{};
			e.gridno      = bestGridNo;
			e.distance    = bestDist;
			e.hasGridDest = false;
			e.side        = side;
			e.dest        = dest;
			out.push_back(e);
		};

		considerEdge(NORTH, gps1stNorthEdgepointArray,  0, -1);
		considerEdge(SOUTH, gps1stSouthEdgepointArray,  0, +1);
		considerEdge(EAST,  gps1stEastEdgepointArray,  +1,  0);
		considerEdge(WEST,  gps1stWestEdgepointArray,  -1,  0);

		std::sort(out.begin(), out.end(),
			[](const ListedExit& a, const ListedExit& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.gridno < b.gridno;
			});
	}

	ST::string formatExitLine(const ListedExit& e, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const UINT8 dir = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer.sGridNo, e.gridno));
		const ST::string dest = GetSectorIDString(e.dest, FALSE);
		if (e.hasGridDest)
		{
			return ST::format("  x{} {} tiles {}: to {}",
			                  tagN, e.distance, directionWord(dir), dest);
		}
		// Map edge: walking off this tile transitions to the neighbor
		// sector on that side; engine still pops the sector-exit dialog.
		return ST::format("  x{} {} tiles {}: map edge {} -> {}",
		                  tagN, e.distance, directionWord(dir),
		                  directionWord(e.side), dest);
	}

	// One entry per compass direction the observer can look. count is the
	// number of unrevealed playable tiles in that direction; closestGridno
	// is the nearest of them, addressable directly via `move <col,row>`.
	struct Frontier
	{
		bool  present;
		INT16 closestGridno;
		INT16 closestDist;
		INT32 count;
	};

	struct UnexploredSummary
	{
		Frontier byDir[NUM_WORLD_DIRECTIONS];
		INT32    playableTotal;
		INT32    playableRevealed;
	};

	// `MAPELEMENT_REVEALED` is set persistently the first time any merc's
	// FOV reaches a tile (Render_Fun.cc:96, FOV.cc:630/639), survives
	// save/load, and isn't cleared on combat end. Tiles without it that
	// also have actual terrain (i.e. inside the playable map area, not the
	// black off-map border) are unexplored. We bucket every unexplored
	// tile by its compass direction from the observer so the player can
	// see at a glance "where the unknown is" — and pick up a (col,row)
	// for the closest one to walk straight at.
	void summarizeUnexplored(const SOLDIERTYPE& observer, UnexploredSummary& out)
	{
		out = {};
		const INT16 origin = observer.sGridNo;
		for (INT16 g = 0; g < WORLD_MAX; ++g)
		{
			// Use the engine's authoritative playable-tile check, not
			// GetTerrainType — NO_TERRAIN occurs in plenty of in-bounds
			// tiles (walls, interior voids), so it'd both undercount the
			// playable total and miss real unexplored tiles inside the
			// world.
			if (!GridNoOnVisibleWorldTile(g)) continue;
			++out.playableTotal;

			const bool revealed = (gpWorldLevelData[g].uiFlags & MAPELEMENT_REVEALED) != 0;
			if (revealed)
			{
				++out.playableRevealed;
				continue;
			}
			if (g == origin) continue;

			const UINT8 dir = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(origin, g));
			if (dir >= NUM_WORLD_DIRECTIONS) continue;

			const INT16 dist = PythSpacesAway(origin, g);
			Frontier& f = out.byDir[dir];
			if (!f.present || dist < f.closestDist)
			{
				f.present       = true;
				f.closestGridno = g;
				f.closestDist   = dist;
			}
			++f.count;
		}
	}

	ST::string formatFrontierLine(std::size_t tagN, UINT8 dir, const Frontier& f)
	{
		const INT16 col = f.closestGridno % WORLD_COLS;
		const INT16 row = f.closestGridno / WORLD_COLS;
		return ST::format("  u{} {} tiles {}: nearest at ({},{}), {} tiles unseen this way",
		                  tagN, f.closestDist, directionWord(dir), col, row, f.count);
	}
}

void Cmd_Nearby(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const observer = GetSelectedMan();
	if (!observer)
	{
		Console_Println("No merc selected to anchor 'nearby' on.");
		return;
	}

	std::string filter = "all";
	INT16 maxDist = 0;
	for (std::size_t i = 1; i < args.size(); ++i)
	{
		int n;
		if (parseInt(args[i], n)) maxDist = static_cast<INT16>(n);
		else                      filter  = args[i];
	}

	const bool wantEnemies    = (filter == "all" || filter == "enemies");
	const bool wantMercs      = (filter == "all" || filter == "mercs");
	const bool wantItems      = (filter == "all" || filter == "items");
	const bool wantDoors      = (filter == "all" || filter == "doors");
	const bool wantContainers = (filter == "all" || filter == "containers");
	const bool wantCivilians  = (filter == "all" || filter == "civilians" || filter == "civs");
	const bool wantExits      = (filter == "all" || filter == "exits");
	// Unexplored is opt-in only — it's a different question ("where to
	// go?") from the rest of `nearby` ("what's around me?") and pulling
	// it into `all` would bury those answers in compass-rose chatter.
	const bool wantUnexplored = (filter == "unexplored" || filter == "unknown");

	if (!wantEnemies && !wantMercs && !wantItems && !wantDoors && !wantContainers &&
	    !wantCivilians && !wantExits && !wantUnexplored)
	{
		Console_Println(ST::format(
			"unknown filter '{}' (try: enemies, mercs, civs, items, doors, containers, exits, unexplored, all)",
			filter));
		return;
	}

	// Indices stay aligned with the address parser even when a distance
	// cap is in effect: enumerate the full distance-sorted list, then
	// only print entries within the cap. So 'fire e3' resolves to the
	// third hostile overall, regardless of what the user filtered out.
	if (wantEnemies)
	{
		std::vector<ListedSoldier> hostiles;
		enumerateHostiles(*observer, hostiles);
		Console_Println(ST::format("Visible hostiles ({}):", hostiles.size()));
		std::size_t shown = 0;
		for (std::size_t i = 0; i < hostiles.size(); ++i)
		{
			if (maxDist > 0 && hostiles[i].distance > maxDist) continue;
			Console_Println(formatHostileLine(hostiles[i], i + 1, *observer));
			++shown;
		}
		if (shown == 0) Console_Println("  (none)");
	}
	if (wantMercs)
	{
		std::vector<ListedSoldier> friends;
		enumerateTeammates(*observer, friends);
		Console_Println(ST::format("Teammates ({}):", friends.size()));
		std::size_t shown = 0;
		for (std::size_t i = 0; i < friends.size(); ++i)
		{
			if (maxDist > 0 && friends[i].distance > maxDist) continue;
			Console_Println(formatFriendlyLine(friends[i], i + 1, *observer));
			++shown;
		}
		if (shown == 0) Console_Println("  (none)");
	}
	if (wantItems)
	{
		std::vector<ItemPile> piles;
		enumerateVisibleItems(*observer, piles);
		Console_Println(ST::format("Item piles ({}):", piles.size()));
		std::size_t shown = 0;
		for (std::size_t i = 0; i < piles.size(); ++i)
		{
			if (maxDist > 0 && piles[i].distance > maxDist) continue;
			Console_Println(formatItemPileLine(piles[i], i + 1, *observer));
			++shown;
		}
		if (shown == 0) Console_Println("  (none)");
	}
	if (wantDoors)
	{
		std::vector<ListedDoor> doors;
		enumerateDoors(*observer, doors);
		Console_Println(ST::format("Doors ({}):", doors.size()));
		std::size_t shown = 0;
		for (std::size_t i = 0; i < doors.size(); ++i)
		{
			if (maxDist > 0 && doors[i].distance > maxDist) continue;
			Console_Println(formatDoorLine(doors[i], i + 1, *observer));
			++shown;
		}
		if (shown == 0) Console_Println("  (none)");
	}
	if (wantContainers)
	{
		std::vector<ListedContainer> containers;
		enumerateContainers(*observer, containers);
		Console_Println(ST::format("Containers ({}):", containers.size()));
		std::size_t shown = 0;
		for (std::size_t i = 0; i < containers.size(); ++i)
		{
			if (maxDist > 0 && containers[i].distance > maxDist) continue;
			Console_Println(formatContainerLine(containers[i], i + 1, *observer));
			++shown;
		}
		if (shown == 0) Console_Println("  (none)");
	}
	if (wantCivilians)
	{
		std::vector<ListedSoldier> civs;
		enumerateCivilians(*observer, civs);
		Console_Println(ST::format("Civilians ({}):", civs.size()));
		std::size_t shown = 0;
		for (std::size_t i = 0; i < civs.size(); ++i)
		{
			if (maxDist > 0 && civs[i].distance > maxDist) continue;
			Console_Println(formatCivilianLine(civs[i], i + 1, *observer));
			++shown;
		}
		if (shown == 0) Console_Println("  (none)");
	}
	if (wantExits)
	{
		std::vector<ListedExit> exits;
		enumerateExits(*observer, exits);
		Console_Println(ST::format("Exits ({}):", exits.size()));
		std::size_t shown = 0;
		for (std::size_t i = 0; i < exits.size(); ++i)
		{
			if (maxDist > 0 && exits[i].distance > maxDist) continue;
			Console_Println(formatExitLine(exits[i], i + 1, *observer));
			++shown;
		}
		if (shown == 0) Console_Println("  (none)");
	}
	if (wantUnexplored)
	{
		UnexploredSummary u;
		summarizeUnexplored(*observer, u);

		const INT32 unseen = u.playableTotal - u.playableRevealed;
		const INT32 pct = u.playableTotal > 0
			? (100 * u.playableRevealed / u.playableTotal)
			: 100;
		Console_Println(ST::format("Sector: {}% explored ({} of {} playable tiles seen).",
		                           pct, u.playableRevealed, u.playableTotal));

		if (unseen == 0)
		{
			Console_Println("Sector is fully explored.");
		}
		else
		{
			// Order frontiers by ascending distance so the closest unknown
			// reads first. Walking N is more accessible than walking SW;
			// putting closest-first matches that affordance.
			std::vector<UINT8> dirs;
			for (UINT8 d = 0; d < NUM_WORLD_DIRECTIONS; ++d)
			{
				if (u.byDir[d].present) dirs.push_back(d);
			}
			std::sort(dirs.begin(), dirs.end(),
				[&](UINT8 a, UINT8 b)
				{
					return u.byDir[a].closestDist < u.byDir[b].closestDist;
				});

			Console_Println(ST::format("Unexplored frontiers ({}):", dirs.size()));
			std::size_t shown = 0;
			for (UINT8 d : dirs)
			{
				const Frontier& f = u.byDir[d];
				if (maxDist > 0 && f.closestDist > maxDist) continue;
				Console_Println(formatFrontierLine(shown + 1, d, f));
				++shown;
			}
			if (shown == 0) Console_Println("  (none within range)");
		}
	}
}

namespace
{
	// What we found on a tile while scanning. Each non-noise discovery
	// becomes a transition emitted to the user. We deliberately don't
	// report walls — they're everywhere indoors and would drown the
	// signal — but terrain changes, doors, items, known soldiers, the
	// revealed/unrevealed boundary, and the map edge all count.
	struct Transition
	{
		INT16       steps;
		ST::string  desc;
	};

	INT8 parseCompassLocal(const std::string& tok)
	{
		std::string s;
		s.reserve(tok.size());
		for (char c : tok) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		if (s == "n"  || s == "north")     return NORTH;
		if (s == "ne" || s == "northeast") return NORTHEAST;
		if (s == "e"  || s == "east")      return EAST;
		if (s == "se" || s == "southeast") return SOUTHEAST;
		if (s == "s"  || s == "south")     return SOUTH;
		if (s == "sw" || s == "southwest") return SOUTHWEST;
		if (s == "w"  || s == "west")      return WEST;
		if (s == "nw" || s == "northwest") return NORTHWEST;
		return -1;
	}

	ST::string describeDoor(STRUCTURE& s, INT16 g)
	{
		const DOOR_STATUS* const ds = GetDoorStatus(g);
		const bool perceivedKnown = ds && !(ds->ubFlags & DOOR_PERCEIVED_NOTSET);
		const bool isOpen = perceivedKnown
			? (ds->ubFlags & DOOR_PERCEIVED_OPEN) != 0
			: (s.fFlags & STRUCTURE_OPEN) != 0;

		const DOOR* const door = FindDoorInfoAtGridNo(g);
		const INT8 lock = door ? door->bPerceivedLocked : DOOR_PERCEIVED_UNKNOWN;
		const char* lockSuffix = "";
		switch (lock)
		{
			case DOOR_PERCEIVED_LOCKED: lockSuffix = ", locked";      break;
			case DOOR_PERCEIVED_BROKEN: lockSuffix = ", lock broken"; break;
			default: break;
		}

		return ST::format("door ({}{})", isOpen ? "open" : "closed", lockSuffix);
	}

	ST::string describeItemPool(INT16 g, INT8 level)
	{
		ITEM_POOL* const ip = GetItemPool(g, level);
		if (!ip || !IsItemPoolVisible(ip)) return ST::string();

		// Lead with the first visible item's name; if more, append a count.
		std::vector<UINT16> seen;
		for (ITEM_POOL* i = ip; i; i = i->pNext)
		{
			const WORLDITEM& wi = GetWorldItem(i->iItemIndex);
			if (wi.bVisible < VISIBLE) continue;
			if (wi.o.usItem == 0)      continue;
			seen.push_back(wi.o.usItem);
		}
		if (seen.empty()) return ST::string();
		if (seen.size() == 1) return ST::format("item ({})", itemName(seen[0]));
		return ST::format("items ({} types incl. {})", seen.size(), itemName(seen[0]));
	}

	void scanInDirection(const SOLDIERTYPE& observer, UINT8 dir,
	                     INT16 maxSteps, std::vector<Transition>& out)
	{
		const INT16 origin = observer.sGridNo;
		const INT16 step   = DirIncrementer[dir];
		const INT8  level  = static_cast<INT8>(gsInterfaceLevel);

		TerrainTypeDefines prevTerrain = GetTerrainType(origin);
		bool prevExplored = (gpWorldLevelData[origin].uiFlags & MAPELEMENT_REVEALED) != 0;
		bool prevInTrees  = FindStructure(origin, STRUCTURE_TREE) != nullptr;

		INT16 g = origin;
		for (INT16 s = 1; s <= maxSteps; ++s)
		{
			const INT16 next = g + step;
			if (next < 0 || next >= WORLD_MAX || !GridNoOnVisibleWorldTile(next))
			{
				out.push_back({s, ST::string("map edge")});
				return;
			}
			g = next;

			// Terrain change — gates noise from generic-floor sequences. We
			// suppress NO_TERRAIN as the new state because it commonly
			// shows up under walls/voids and isn't a useful transition.
			const TerrainTypeDefines terrain = GetTerrainType(g);
			if (terrain != prevTerrain && terrain != NO_TERRAIN)
			{
				out.push_back({s, ST::string(terrainWord(terrain))});
				prevTerrain = terrain;
			}

			// Door — only at the base tile so multi-tile doors don't
			// double-report. Excluded from the openable/window/fence
			// checks below.
			STRUCTURE* const door = FindStructure(g, STRUCTURE_ANYDOOR);
			if (door && door->sBaseGridNo == g)
			{
				out.push_back({s, describeDoor(*door, g)});
			}

			// Container: openable but not a door and not a switch. Crates,
			// lockers, footlockers — common loot sources sighted players
			// spot from across the room.
			else if (findStructureExcluding(g, STRUCTURE_OPENABLE,
			                                STRUCTURE_ANYDOOR | STRUCTURE_SWITCH))
			{
				out.push_back({s, ST::string("container")});
			}
			// Window in a wall — tactically critical: shootable through,
			// climbable, line-of-sight. Reported separately from plain
			// walls (which we still suppress entirely as too noisy).
			else if (findStructureExcluding(g, STRUCTURE_WALLNWINDOW, 0))
			{
				out.push_back({s, ST::string("window")});
			}
			// Fence — movement barrier (some climbable). STRUCTURE_ANYFENCE
			// covers both wood and wire variants.
			else if (findStructureExcluding(g, STRUCTURE_ANYFENCE, 0))
			{
				out.push_back({s, ST::string("fence")});
			}

			// Visible items.
			if (gpWorldLevelData[g].uiFlags & MAPELEMENT_ITEMPOOL_PRESENT)
			{
				ST::string desc = describeItemPool(g, level);
				if (!desc.empty()) out.push_back({s, std::move(desc)});
			}

			// Known soldier on this tile.
			if (SOLDIERTYPE* const occ = WhoIsThere2(g, level))
			{
				if (occ->ubID != observer.ubID && ConsoleVis::IsKnownSoldier(*occ))
				{
					out.push_back({s, ST::string(occ->name.c_str())});
				}
			}

			// Tree-cluster transitions only — emitting a line per tree
			// would drown everything else in a wooded sector. The player
			// learns where dense vegetation begins/ends; per-tree detail
			// is available via `tile <col,row>`.
			const bool inTrees = FindStructure(g, STRUCTURE_TREE) != nullptr;
			if (inTrees != prevInTrees)
			{
				out.push_back({s, ST::string(inTrees ? "trees begin" : "trees end")});
				prevInTrees = inTrees;
			}

			// Explored/unexplored transition. The first crossing into fog
			// is genuinely interesting ("here's where you've never been");
			// the reverse crossing matters less but we report it for
			// symmetry so the user can re-orient.
			const bool explored = (gpWorldLevelData[g].uiFlags & MAPELEMENT_REVEALED) != 0;
			if (explored != prevExplored)
			{
				out.push_back({s, ST::string(explored ? "back into explored area" : "edge of explored")});
				prevExplored = explored;
			}
		}
	}

	void printSingleDirection(const SOLDIERTYPE& observer, UINT8 dir,
	                          const std::vector<Transition>& trs)
	{
		const INT16 col = observer.sGridNo % WORLD_COLS;
		const INT16 row = observer.sGridNo / WORLD_COLS;
		Console_Println(ST::format("{} from ({},{}):", directionWord(dir), col, row));
		if (trs.empty())
		{
			Console_Println("  (nothing of note within range)");
			return;
		}
		for (const Transition& t : trs)
		{
			Console_Println(ST::format("  {} tiles: {}", t.steps, t.desc));
		}
	}

	void printCompactDirection(UINT8 dir, const std::vector<Transition>& trs)
	{
		// One line per direction: "N: 4 paved road -> 8 door (closed) -> ..."
		// `->` keeps temporal order readable on a single line and screen
		// readers handle it as a brief pause.
		ST::string line = ST::format("{}:", directionWord(dir));
		if (trs.empty())
		{
			line += " (nothing)";
			Console_Println(line);
			return;
		}
		for (std::size_t i = 0; i < trs.size(); ++i)
		{
			line += i == 0 ? ST::string(" ") : ST::string(" -> ");
			line += ST::format("{} {}", trs[i].steps, trs[i].desc);
		}
		Console_Println(line);
	}
}

void Cmd_Look(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const observer = GetSelectedMan();
	if (!observer)
	{
		Console_Println("No merc selected to anchor 'look' on.");
		return;
	}

	// Default scan range. Most sectors are 80 tiles wide playable, so 60
	// is "more than half a sector" — far enough to be useful, short
	// enough to keep output bounded.
	INT16 maxSteps = 60;

	if (args.size() == 1)
	{
		// `look` — scan all four cardinals, compact one-line-per-direction.
		const UINT8 dirs[] = { NORTH, EAST, SOUTH, WEST };
		const INT16 col = observer->sGridNo % WORLD_COLS;
		const INT16 row = observer->sGridNo / WORLD_COLS;
		Console_Println(ST::format("Around ({},{}):", col, row));
		for (UINT8 d : dirs)
		{
			std::vector<Transition> trs;
			scanInDirection(*observer, d, maxSteps, trs);
			printCompactDirection(d, trs);
		}
		return;
	}

	const INT8 dir = parseCompassLocal(args[1]);
	if (dir < 0)
	{
		Console_Println(ST::format(
			"unknown direction '{}' (use n/ne/e/se/s/sw/w/nw, or no arg for all 4 cardinals)",
			args[1]));
		return;
	}

	if (args.size() >= 3)
	{
		char* end = nullptr;
		long v = std::strtol(args[2].c_str(), &end, 10);
		if (end == args[2].c_str() || *end != '\0' || v <= 0 || v > 200)
		{
			Console_Println("max steps must be a positive integer up to 200.");
			return;
		}
		maxSteps = static_cast<INT16>(v);
	}

	std::vector<Transition> trs;
	scanInDirection(*observer, static_cast<UINT8>(dir), maxSteps, trs);
	printSingleDirection(*observer, static_cast<UINT8>(dir), trs);
}

void Cmd_Tile(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		Console_Println("usage: tile <name> | tile <dir> <steps> | tile <col,row>");
		return;
	}

	SOLDIERTYPE* const observer = GetSelectedMan();
	Target tgt;
	ST::string err;
	if (parseTarget(args, 1, observer, tgt, err) == 0)
	{
		Console_Println(err);
		return;
	}

	const INT8 level = static_cast<INT8>(gsInterfaceLevel);
	const TerrainTypeDefines terrain = GetTerrainType(tgt.gridno);

	// Lead with the human-readable address the user typed, not a raw gridno.
	ST::string label;
	if (tgt.soldier)
	{
		label = ST::format("Tile of {}", tgt.soldier->name);
	}
	else if (observer)
	{
		const INT16 dist = PythSpacesAway(observer->sGridNo, tgt.gridno);
		const UINT8 dir  = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer->sGridNo, tgt.gridno));
		label = dist == 0 ? ST::string("Tile here") : ST::format("Tile {} {}", dist, directionWord(dir));
	}
	else
	{
		label = ST::string("Tile");
	}

	Console_Println(ST::format("{}: {}.", label, terrainWord(terrain)));

	if (!ConsoleVis::IsKnownTile(tgt.gridno, level))
	{
		Console_Println("  Unrevealed — no further detail.");
		return;
	}

	if (SOLDIERTYPE* occupant = WhoIsThere2(tgt.gridno, level))
	{
		if (ConsoleVis::IsKnownSoldier(*occupant))
		{
			Console_Println(ST::format("  Occupant: {} ({}).",
			                           occupant->name, stanceWord(*occupant)));
		}
	}
}

void Cmd_Cth(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel) { Console_Println("No merc selected."); return; }
	if (args.size() < 2)
	{
		Console_Println("usage: cth <target>");
		return;
	}

	Target tgt;
	ST::string err;
	if (parseTarget(args, 1, sel, tgt, err) == 0) { Console_Println(err); return; }

	const UINT16 weapon = sel->inv[HANDPOS].usItem;
	if (weapon == 0)
	{
		Console_Println("No weapon in main hand.");
		return;
	}
	if (GCM->getItem(weapon)->getItemClass() != IC_GUN)
	{
		Console_Println(ST::format("Held item ({}) is not a gun.", GCM->getItem(weapon)->getName()));
		return;
	}

	// Mirror what UI_Cursors.cc:244-269 computes for the cursor's CTH text:
	// base gun CTH × chance-to-get-through (cover/LOS), as a percent.
	// Body part defaults to torso; tile shots use cube level 2 (the value
	// Handle_UI.cc:2192 sets for shoot-at-interactive-tile, the closest
	// thing the engine has to "centre of tile").
	const UINT8 part = AIM_SHOT_TORSO;

	ST::string cells;
	for (int aim = 0; aim <= 4; ++aim)
	{
		UINT32 base = CalcChanceToHitGun(sel, static_cast<UINT16>(tgt.gridno),
		                                 static_cast<UINT8>(aim), part, FALSE);
		UINT32 through = tgt.soldier
			? SoldierToSoldierBodyPartChanceToGetThrough(sel, tgt.soldier, part)
			: SoldierToLocationChanceToGetThrough(sel, tgt.gridno, gsInterfaceLevel, 2, nullptr);
		const int pct = static_cast<int>(base * through / 100);

		if (!cells.empty()) cells += "  ";
		cells += ST::format("aim {}: {}%", aim, pct);
	}

	const ST::string label = tgt.soldier ? tgt.soldier->name : ST::string("target tile");
	Console_Println(ST::format("CTH on {}:  {}", label, cells));
}

namespace
{
	const char* slotLabel(int slot)
	{
		switch (slot)
		{
			case HELMETPOS:     return "Helmet";
			case VESTPOS:       return "Vest";
			case LEGPOS:        return "Legs";
			case HEAD1POS:      return "Head 1";
			case HEAD2POS:      return "Head 2";
			case HANDPOS:       return "In hand";
			case SECONDHANDPOS: return "Off hand";
			case BIGPOCK1POS:   return "Big pocket 1";
			case BIGPOCK2POS:   return "Big pocket 2";
			case BIGPOCK3POS:   return "Big pocket 3";
			case BIGPOCK4POS:   return "Big pocket 4";
			case SMALLPOCK1POS: return "Small pocket 1";
			case SMALLPOCK2POS: return "Small pocket 2";
			case SMALLPOCK3POS: return "Small pocket 3";
			case SMALLPOCK4POS: return "Small pocket 4";
			case SMALLPOCK5POS: return "Small pocket 5";
			case SMALLPOCK6POS: return "Small pocket 6";
			case SMALLPOCK7POS: return "Small pocket 7";
			case SMALLPOCK8POS: return "Small pocket 8";
		}
		return "?";
	}

	int sumMagShots(const OBJECTTYPE& o)
	{
		const int n = std::max<int>(1, o.ubNumberOfObjects);
		int total = 0;
		for (int i = 0; i < n && i < MAX_OBJECTS_PER_SLOT; ++i) total += o.ubShotsLeft[i];
		return total;
	}

	ST::string formatGun(const OBJECTTYPE& o)
	{
		const WeaponModel* const w = GCM->getWeapon(o.usItem);
		const ST::string cal = w->calibre ? w->calibre->getName() : ST::string();
		const int pct = o.bGunStatus;
		if (!cal.empty())
		{
			return ST::format("{}, {}, {}/{} shots, {}%",
			                  itemName(o.usItem), cal,
			                  o.ubGunShotsLeft, w->ubMagSize, pct);
		}
		return ST::format("{}, {}/{} shots, {}%",
		                  itemName(o.usItem),
		                  o.ubGunShotsLeft, w->ubMagSize, pct);
	}

	ST::string formatMagazine(const OBJECTTYPE& o)
	{
		const MagazineModel* const m = GCM->getItem(o.usItem)->asAmmo();
		const ST::string cal = (m && m->calibre) ? m->calibre->getName() : ST::string();
		const UINT16 cap = m ? m->capacity : 0;
		const int n = std::max<int>(1, o.ubNumberOfObjects);
		if (n == 1)
		{
			return cal.empty()
				? ST::format("{}, {}/{} rounds", itemName(o.usItem), o.ubShotsLeft[0], cap)
				: ST::format("{}, {}/{} ({})", itemName(o.usItem), o.ubShotsLeft[0], cap, cal);
		}
		const int total = sumMagShots(o);
		return cal.empty()
			? ST::format("{} x{}, {} rounds total", itemName(o.usItem), n, total)
			: ST::format("{} x{}, {} rounds total ({})", itemName(o.usItem), n, total, cal);
	}

	ST::string formatGenericObject(const OBJECTTYPE& o)
	{
		const ItemModel* const it = GCM->getItem(o.usItem);
		const int n = std::max<int>(1, o.ubNumberOfObjects);

		if (it->isMoney())
		{
			return ST::format("${}", o.uiMoneyAmount);
		}
		if (n > 1)
		{
			return ST::format("{} x{}", itemName(o.usItem), n);
		}
		// Single non-stacked item: report condition for things that wear out.
		if (it->isArmour() || it->isMedkit() || it->isKit() || it->isBlade() ||
		    it->isPunch()  || it->isFace())
		{
			return ST::format("{}, {}%", itemName(o.usItem), o.bStatus[0]);
		}
		return itemName(o.usItem);
	}

	ST::string formatSlot(const OBJECTTYPE& o)
	{
		const ItemModel* const it = GCM->getItem(o.usItem);
		if (it->isGun())  return formatGun(o);
		if (it->isAmmo()) return formatMagazine(o);
		return formatGenericObject(o);
	}
}

void Cmd_Inventory(const std::vector<std::string>& args)
{
	SOLDIERTYPE* s = nullptr;
	if (args.size() < 2)
	{
		s = GetSelectedMan();
		if (!s) { Console_Println("No merc selected."); return; }
	}
	else
	{
		ST::string err;
		s = findTeammateByName(args[1], err);
		if (!s) { Console_Println(err); return; }
	}

	// Order: hands first (most actionable), then worn slots, then pockets.
	static const int kSlotOrder[] =
	{
		HANDPOS, SECONDHANDPOS,
		HELMETPOS, VESTPOS, LEGPOS, HEAD1POS, HEAD2POS,
		BIGPOCK1POS, BIGPOCK2POS, BIGPOCK3POS, BIGPOCK4POS,
		SMALLPOCK1POS, SMALLPOCK2POS, SMALLPOCK3POS, SMALLPOCK4POS,
		SMALLPOCK5POS, SMALLPOCK6POS, SMALLPOCK7POS, SMALLPOCK8POS,
	};

	Console_Println(ST::format("{} inventory:", s->name));

	int emptyCount = 0;
	for (int slot : kSlotOrder)
	{
		const OBJECTTYPE& o = s->inv[slot];
		if (o.usItem == 0) { ++emptyCount; continue; }
		Console_Println(ST::format("  {}: {}", slotLabel(slot), formatSlot(o)));
	}
	if (emptyCount > 0)
	{
		Console_Println(ST::format("  ({} empty slot{}.)",
		                           emptyCount, emptyCount == 1 ? "" : "s"));
	}

	// Reload-sources rollup: if the held weapon is a gun, scan every other
	// slot for matching-calibre ammo and report total rounds available. The
	// answer to "what can I reload with?" without the player having to
	// cross-reference calibres slot by slot.
	const OBJECTTYPE& held = s->inv[HANDPOS];
	if (held.usItem == 0 || !GCM->getItem(held.usItem)->isGun()) return;

	const WeaponModel* const w = GCM->getWeapon(held.usItem);
	const CalibreModel* const wantCal = w->calibre;
	if (!wantCal) return;

	int        magCount   = 0;
	int        totalShots = 0;
	ST::string sources;
	for (int slot : kSlotOrder)
	{
		if (slot == HANDPOS) continue;
		const OBJECTTYPE& o = s->inv[slot];
		if (o.usItem == 0) continue;
		const ItemModel* const it = GCM->getItem(o.usItem);
		if (!it->isAmmo()) continue;
		const MagazineModel* const m = it->asAmmo();
		if (!m || !m->calibre || m->calibre->index != wantCal->index) continue;

		const int n         = std::max<int>(1, o.ubNumberOfObjects);
		const int slotShots = sumMagShots(o);
		magCount   += n;
		totalShots += slotShots;
		if (!sources.empty()) sources += ", ";
		sources += ST::format("{} ({})", slotLabel(slot), slotShots);
	}

	if (magCount == 0)
	{
		Console_Println(ST::format("  No {} ammo on this merc.", wantCal->getName()));
	}
	else
	{
		Console_Println(ST::format("  Reload sources ({}): {} mag{}, {} rounds — {}.",
		                           wantCal->getName(),
		                           magCount, magCount == 1 ? "" : "s",
		                           totalShots, sources));
	}
}

void Cmd_Path(const std::vector<std::string>&)
{
	Console_Println("path: not implemented yet.");
}
