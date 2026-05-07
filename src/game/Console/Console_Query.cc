#include "Console_Query.h"
#include "Console_Address.h"
#include "Console_Visibility.h"

#include "Console.h"

#include "Animation_Control.h"
#include "ArmourModel.h"
#include "CalibreModel.h"
#include "ContentManager.h"
#include "DisplayCover.h"
#include "Exit_Grids.h"
#include "ExplosiveModel.h"
#include "GameInstance.h"
#include "Game_Clock.h"
#include "Handle_Items.h"
#include "Interface.h"
#include "Isometric_Utils.h"
#include "ItemModel.h"
#include "Item_Types.h"
#include "Items.h"
#include "Keys.h"
#include "LOS.h"
#include "MagazineModel.h"
#include "SmokeEffectModel.h"
#include "Map_Edgepoints.h"
#include "Map_Information.h"
#include "GameSettings.h"
#include "OppList.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "PathAI.h"
#include "Render_Fun.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "Soldier_Profile_Type.h"
#include "StrategicMap.h"
#include "Structure.h"
#include "Structure_Internals.h"
#include "Text.h"
#include "TileDef.h"
#include "Timer_Control.h"
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

void PrintMercSummary(const SOLDIERTYPE& s)
{
	const UINT16 inHand    = s.inv[HANDPOS].usItem;
	const UINT8  shotsLeft = s.inv[HANDPOS].ubGunShotsLeft;

	Console_Println(ST::format(
		"{}: life {}/{}, breath {}/{}, AP {}, {} facing {}.",
		s.name, s.bLife, s.bLifeMax, s.bBreath, s.bBreathMax,
		s.bActionPoints, stanceWord(s), directionWord(s.bDirection)));

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

	PrintMercSummary(*s);
}

namespace
{
	const char* skillTraitName(UINT8 trait)
	{
		if (trait == NO_SKILLTRAIT) return nullptr;
		if (trait >= NUM_SKILLTRAITS) return nullptr;
		return gzMercSkillText[trait].c_str();
	}

	// "" if the stat hasn't moved within the recently-changed window;
	// " (up)" or " (down)" if it has. usValueGoneUp's bit is set on increase
	// and cleared on decrease (the timer is updated either way — see
	// Drugs_And_Alcohol.cc:167 for the heart-attack down-path), so the bit's
	// state at lookup time is the up/down direction. Same 60s window the
	// mapscreen panel uses for green/red coloring (MapScreen.cc:639 PrintStat).
	const char* statTrend(UINT16 usValueGoneUp, UINT16 increaseBit, UINT32 changeTime)
	{
		if (changeTime == 0) return "";
		if (GetJA2Clock() >= CHANGE_STAT_RECENTLY_DURATION + changeTime) return "";
		return (usValueGoneUp & increaseBit) ? " (up)" : " (down)";
	}
}

void Cmd_Stats(const std::vector<std::string>& args)
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

	const UINT16 up = s->usValueGoneUp;

	Console_Println(ST::format("{}, level {}{}.",
		s->name, s->bExpLevel,
		statTrend(up, LVL_INCREASE, s->uiChangeLevelTime)));

	Console_Println(ST::format(
		"  Strength {}{}, Agility {}{}, Dexterity {}{}, Wisdom {}{}.",
		s->bStrength,    statTrend(up, STRENGTH_INCREASE, s->uiChangeStrengthTime),
		s->bAgility,     statTrend(up, AGIL_INCREASE,     s->uiChangeAgilityTime),
		s->bDexterity,   statTrend(up, DEX_INCREASE,      s->uiChangeDexterityTime),
		s->bWisdom,      statTrend(up, WIS_INCREASE,      s->uiChangeWisdomTime)));

	Console_Println(ST::format(
		"  Marksmanship {}{}, Mechanical {}{}, Explosives {}{}, Medical {}{}, Leadership {}{}.",
		s->bMarksmanship, statTrend(up, MRK_INCREASE,  s->uiChangeMarksmanshipTime),
		s->bMechanical,   statTrend(up, MECH_INCREASE, s->uiChangeMechanicalTime),
		s->bExplosive,    statTrend(up, EXP_INCREASE,  s->uiChangeExplosivesTime),
		s->bMedical,      statTrend(up, MED_INCREASE,  s->uiChangeMedicalTime),
		s->bLeadership,   statTrend(up, LDR_INCREASE,  s->uiChangeLeadershipTime)));

	if (AM_A_ROBOT(s)) return;

	const char* t1 = skillTraitName(s->ubSkillTrait1);
	const char* t2 = skillTraitName(s->ubSkillTrait2);
	if (!t1 && !t2) return;

	ST::string traitLine;
	if (t1 && t2 && s->ubSkillTrait1 == s->ubSkillTrait2)
	{
		traitLine = ST::format("{} {}", t1, gzMercSkillText[NUM_SKILLTRAITS]);
	}
	else
	{
		if (t1) traitLine = t1;
		if (t2)
		{
			if (!traitLine.empty()) traitLine += ", ";
			traitLine += t2;
		}
	}
	Console_Println(ST::format("  Skill traits: {}.", traitLine));
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

	// Absolute (col,row) for a soldier, in the same format `move <col,row>`
	// accepts. The relative `<dist> <dir>` after it is still useful for
	// quick spatial framing, but the coord lets the user address the tile
	// directly without first having to walk-to-name.
	ST::string coordLabel(INT16 gridno)
	{
		return ST::format("({},{})", gridno % WORLD_COLS, gridno / WORLD_COLS);
	}

	// "[same room]" or "[room N]" suffix for nearby entries whose tile is
	// in an editor-marked enclosed area. Outdoor tiles (NO_ROOM) get no
	// annotation — sighted players see continuous outdoor space and tagging
	// it would overclaim. Same-room indoor tiles read "[same room]" so the
	// user knows no walls separate them from the entry. Different rooms
	// get the numeric ID; rooms have no human names in the data, but the
	// IDs are stable across saves so "room 3" is a usable landmark within
	// a sector.
	ST::string roomSuffix(const SOLDIERTYPE& observer, INT16 entryGridno)
	{
		const UINT8 entryRoom = GetRoom(entryGridno);
		if (entryRoom == NO_ROOM) return ST::string();
		const UINT8 obsRoom = GetRoom(observer.sGridNo);
		if (entryRoom == obsRoom) return ST::string(" [same room]");
		return ST::format(" [room {}]", entryRoom);
	}

	ST::string formatHostileLine(const ListedSoldier& ls, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const SOLDIERTYPE& t = *ls.soldier;
		const UINT8  dir    = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer.sGridNo, t.sGridNo));
		const UINT16 weapon = t.inv[HANDPOS].usItem;
		const ST::string room = roomSuffix(observer, t.sGridNo);
		return weapon != 0
			? ST::format("  e{} {} {}, {} tiles {}, {}, life {}, {}{}",
			             tagN, t.name, coordLabel(t.sGridNo),
			             ls.distance, directionWord(dir),
			             stanceWord(t), t.bLife, itemName(weapon), room)
			: ST::format("  e{} {} {}, {} tiles {}, {}, life {}{}",
			             tagN, t.name, coordLabel(t.sGridNo),
			             ls.distance, directionWord(dir),
			             stanceWord(t), t.bLife, room);
	}

	ST::string formatFriendlyLine(const ListedSoldier& ls, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const SOLDIERTYPE& t = *ls.soldier;
		const UINT8 dir = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer.sGridNo, t.sGridNo));
		return ST::format("  m{} {} {}, {} tiles {}, life {}, AP {}{}",
		                  tagN, t.name, coordLabel(t.sGridNo),
		                  ls.distance, directionWord(dir),
		                  t.bLife, t.bActionPoints,
		                  roomSuffix(observer, t.sGridNo));
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
				p.distance = SpacesAway(observer.sGridNo, wi.sGridNo);
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

		return ST::format("  i{} {} tiles {}: {}{}",
		                  tagN, p.distance, directionWord(dir), list,
		                  roomSuffix(observer, p.gridno));
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
			// Dedupe multi-tile doors: only count the base tile. The engine
			// flags it explicitly via STRUCTURE_BASE_TILE; sBaseGridNo is
			// left at default-init (0) on bases and only set on the
			// non-base satellite records (Structure.cc:720), so testing
			// `sBaseGridNo == g` would only match at gridno 0.
			if (!(s->fFlags & STRUCTURE_BASE_TILE)) continue;

			ListedDoor d{};
			d.gridno   = g;
			d.distance = SpacesAway(observer.sGridNo, g);

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
			// Skip non-base records of multi-tile structures so we don't
			// count one container three times. STRUCTURE_BASE_TILE is the
			// engine's canonical base-tile flag (Structure.cc:401).
			if (!(s->fFlags & STRUCTURE_BASE_TILE)) continue;
			if (!(s->fFlags & wanted))              continue;
			if (s->fFlags & excluded)               continue;
			return s;
		}
		return nullptr;
	}

	struct ListedContainer
	{
		INT16 distance;
		INT16 gridno;
	};

	// Unlike doors (which are ungated because they're the building's outer
	// silhouette and visible to a sighted player at a glance), containers
	// are interior loot markers — sighted players don't see footlockers
	// inside an unentered building because the roof hides them. Gate on
	// MAPELEMENT_REVEALED so we don't pre-spoil the contents of every
	// crate in the sector. Level 0 is the right channel: openable
	// structures sit on the ground, and the engine sets REVEALED on the
	// ground tile when a merc's FOV reaches it.
	void enumerateContainers(const SOLDIERTYPE& observer, std::vector<ListedContainer>& out)
	{
		for (INT16 g = 0; g < WORLD_MAX; ++g)
		{
			if (!ConsoleVis::IsKnownTile(g, 0)) continue;
			STRUCTURE* const s = findStructureExcluding(
				g, STRUCTURE_OPENABLE, STRUCTURE_ANYDOOR | STRUCTURE_SWITCH);
			if (!s) continue;
			out.push_back({ SpacesAway(observer.sGridNo, g), g });
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
		return ST::format("  k{} {} tiles {}: container{}",
		                  tagN, c.distance, directionWord(dir),
		                  roomSuffix(observer, c.gridno));
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

		return ST::format("  d{} {} tiles {}: {}{}{}",
		                  tagN, d.distance, directionWord(dir), state, lock,
		                  roomSuffix(observer, d.gridno));
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
			out.push_back({ t, SpacesAway(observer.sGridNo, t->sGridNo) });
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
		return ST::format("  c{} {} {}, {} tiles {}, {}{}",
		                  tagN, t.name, coordLabel(t.sGridNo),
		                  ls.distance, directionWord(dir),
		                  stanceWord(t),
		                  roomSuffix(observer, t.sGridNo));
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
			e.distance    = SpacesAway(origin, g);
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
				const INT16 d = SpacesAway(origin, g);
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

			const INT16 dist = SpacesAway(origin, g);
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

	// Hazard label priority: most-dangerous wins when a tile carries
	// multiple flags (rare but possible — overlapping clouds). Mustard
	// gas degrades over multiple turns and resists masks; tear gas is
	// less lethal but still incapacitating. Smoke is just LOS.
	struct HazardType { UINT8 mask; const char* name; };
	const HazardType kHazardTypes[] = {
		{ MAPELEMENT_EXT_MUSTARDGAS,  "mustard gas"  },
		{ MAPELEMENT_EXT_TEARGAS,     "tear gas"     },
		{ MAPELEMENT_EXT_CREATUREGAS, "creature gas" },
		{ MAPELEMENT_EXT_SMOKE,       "smoke"        },
	};

	const char* hazardLabel(UINT8 ext)
	{
		for (const auto& h : kHazardTypes)
		{
			if (ext & h.mask) return h.name;
		}
		return nullptr;
	}

	struct ListedHazard
	{
		INT16       distance;       // to closest tile in cluster
		INT16       closestGridno;
		INT16       tileCount;
		const char* label;
	};

	// Group adjacent hazard tiles into one cluster per cloud — sighted
	// players see one drifting blob per smoke pop, not N separate sprites,
	// so reporting per-tile would inflate noise badly. Hazards aren't
	// gated on MAPELEMENT_REVEALED: smoke and gas are visually obvious
	// rendered effects regardless of whether the team has explored the
	// underlying ground tile (a cloud rising over fog still tells you
	// "something's burning over there").
	void enumerateHazards(const SOLDIERTYPE& observer, std::vector<ListedHazard>& out)
	{
		std::vector<bool> seen(WORLD_MAX, false);
		for (INT16 g = 0; g < WORLD_MAX; ++g)
		{
			if (seen[g]) continue;
			if (!(gpWorldLevelData[g].ubExtFlags[0] & ANY_SMOKE_EFFECT)) continue;

			UINT8 clusterTypes = 0;
			INT16 closestG = g;
			INT16 closestDist = SpacesAway(observer.sGridNo, g);
			int   count = 0;

			std::vector<INT16> stack;
			stack.push_back(g);
			while (!stack.empty())
			{
				const INT16 cur = stack.back();
				stack.pop_back();
				if (cur < 0 || cur >= WORLD_MAX) continue;
				if (seen[cur]) continue;
				const UINT8 e = gpWorldLevelData[cur].ubExtFlags[0] & ANY_SMOKE_EFFECT;
				if (!e) continue;

				seen[cur] = true;
				++count;
				clusterTypes |= e;
				const INT16 d = SpacesAway(observer.sGridNo, cur);
				if (d < closestDist) { closestDist = d; closestG = cur; }

				// 8-connected neighbours, with a column-delta guard so we
				// don't wrap from col 0 on row R to col 159 on row R-1.
				const INT16 cx = cur % WORLD_COLS;
				const INT16 cy = cur / WORLD_COLS;
				for (UINT8 dir = 0; dir < NUM_WORLD_DIRECTIONS; ++dir)
				{
					const INT16 nbr = cur + DirIncrementer[dir];
					if (nbr < 0 || nbr >= WORLD_MAX) continue;
					const INT16 nx = nbr % WORLD_COLS;
					const INT16 ny = nbr / WORLD_COLS;
					if (std::abs(nx - cx) > 1 || std::abs(ny - cy) > 1) continue;
					stack.push_back(nbr);
				}
			}

			ListedHazard h{};
			h.distance      = closestDist;
			h.closestGridno = closestG;
			h.tileCount     = static_cast<INT16>(count);
			h.label         = hazardLabel(clusterTypes);
			if (h.label) out.push_back(h);
		}

		std::sort(out.begin(), out.end(),
			[](const ListedHazard& a, const ListedHazard& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.closestGridno < b.closestGridno;
			});
	}

	ST::string formatHazardLine(const ListedHazard& h, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const UINT8 dir = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer.sGridNo, h.closestGridno));
		if (h.tileCount <= 1)
		{
			return ST::format("  z{} {} tiles {}: {}",
			                  tagN, h.distance, directionWord(dir), h.label);
		}
		return ST::format("  z{} {} tiles {}: {} ({} tiles)",
		                  tagN, h.distance, directionWord(dir),
		                  h.label, h.tileCount);
	}

	struct ListedMine
	{
		INT16 distance;
		INT16 gridno;
		bool  player;  // player-laid; false = enemy mine spotted by team
	};

	// MAPELEMENT_PLAYER_MINE_PRESENT is set on lay (Handle_Items.cc:957);
	// MAPELEMENT_ENEMY_MINE_PRESENT is set when a team merc detects or
	// triggers one (Overhead.cc:1237) — it IS the "player knows about it"
	// bit, so no further visibility gate is needed. Both are persistent
	// in save data, matching the sighted-player experience: once you've
	// seen a mine you never forget where it is.
	void enumerateMines(const SOLDIERTYPE& observer, std::vector<ListedMine>& out)
	{
		for (INT16 g = 0; g < WORLD_MAX; ++g)
		{
			const UINT16 flags = gpWorldLevelData[g].uiFlags;
			const bool player = (flags & MAPELEMENT_PLAYER_MINE_PRESENT) != 0;
			const bool enemy  = (flags & MAPELEMENT_ENEMY_MINE_PRESENT)  != 0;
			if (!player && !enemy) continue;
			ListedMine m{};
			m.gridno   = g;
			m.distance = SpacesAway(observer.sGridNo, g);
			// Player flag wins on the rare both-set case: a friendly mine
			// you laid on a tile that already had a known enemy mine is
			// still safe to walk through if you know the trigger pattern.
			m.player   = player;
			out.push_back(m);
		}
		std::sort(out.begin(), out.end(),
			[](const ListedMine& a, const ListedMine& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.gridno < b.gridno;
			});
	}

	ST::string formatMineLine(const ListedMine& m, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const UINT8 dir = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer.sGridNo, m.gridno));
		return ST::format("  b{} {} tiles {}: {} mine{}",
		                  tagN, m.distance, directionWord(dir),
		                  m.player ? "friendly" : "enemy",
		                  roomSuffix(observer, m.gridno));
	}

	struct ListedBomb
	{
		INT16  distance;
		INT16  gridno;
		UINT16 itemId;       // usBombItem if armed, else usItem
		INT8   detonator;
		INT8   delay;        // valid when detonator == BOMB_TIMED
		INT8   frequency;    // valid when detonator == BOMB_REMOTE / BOMB_SWITCH
	};

	// "Our team" predicate copied from Handle_Items.cc:2848-2850 — the
	// owner byte stores soldier ID + 2, with 0 reserved for "no owner"
	// and 1 for editor-placed (Editor/EditorItems.cc:746). Subtract 2
	// and check against the OUR_TEAM ID range.
	bool bombOwnedByOurTeam(const OBJECTTYPE& o)
	{
		if (o.ubBombOwner <= 1) return false;
		const INT32 sid = static_cast<INT32>(o.ubBombOwner) - 2;
		return sid >= gTacticalStatus.Team[OUR_TEAM].bFirstID &&
		       sid <= gTacticalStatus.Team[OUR_TEAM].bLastID;
	}

	// Walk gWorldBombs (the per-sector planted-bomb registry) and pick
	// out our team's placed-and-armed bombs. We deliberately skip
	// non-armed bomb items in the world (they'd be a dropped-but-not-
	// planted bomb) and skip enemy bombs — those, when known, surface
	// through `nearby mines` via MAPELEMENT_ENEMY_MINE_PRESENT instead.
	void enumerateBombs(const SOLDIERTYPE& observer, std::vector<ListedBomb>& out)
	{
		CFOR_EACH_WORLD_BOMB(wb)
		{
			const WORLDITEM& wi = GetWorldItem(wb.iItemIndex);
			if (!wi.fExists) continue;
			if (!(wi.o.fFlags & OBJECT_ARMED_BOMB)) continue;
			if (!bombOwnedByOurTeam(wi.o)) continue;

			ListedBomb b{};
			b.distance  = SpacesAway(observer.sGridNo, wi.sGridNo);
			b.gridno    = wi.sGridNo;
			b.itemId    = (wi.o.usBombItem != NOTHING) ? wi.o.usBombItem : wi.o.usItem;
			b.detonator = wi.o.bDetonatorType;
			b.delay     = wi.o.bDelay;
			b.frequency = wi.o.bFrequency;
			out.push_back(b);
		}
		std::sort(out.begin(), out.end(),
			[](const ListedBomb& a, const ListedBomb& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.gridno < b.gridno;
			});
	}

	ST::string formatBombLine(const ListedBomb& b, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const UINT8 dir = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer.sGridNo, b.gridno));
		ST::string mode;
		switch (b.detonator)
		{
			case BOMB_TIMED:    mode = ST::format("timed {}t",  b.delay);     break;
			case BOMB_REMOTE:   mode = ST::format("remote f{}", b.frequency); break;
			case BOMB_PRESSURE: mode = ST::string("pressure");                break;
			case BOMB_SWITCH:   mode = ST::format("switch f{}", b.frequency); break;
			default:            mode = ST::string("armed");                   break;
		}
		// Tag prefix `p` for "placed" — distinct from `b` (mines) so the
		// two filters can coexist when a player runs `nearby all`.
		return ST::format("  p{} {} tiles {}: {} ({}){}",
		                  tagN, b.distance, directionWord(dir),
		                  itemName(b.itemId), mode,
		                  roomSuffix(observer, b.gridno));
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

	std::string filter;
	INT16 maxDist = 0;
	for (std::size_t i = 1; i < args.size(); ++i)
	{
		int n;
		if (parseInt(args[i], n)) maxDist = static_cast<INT16>(n);
		else                      filter  = args[i];
	}

	// Two tiers. Bare `nearby` (no filter token) shows only what a sighted
	// player would clock as immediately actionable: people on the map plus
	// hazards and mines. Spatial inventory — doors, containers, exits, item
	// piles — is one keystroke away via an explicit category, but pulling
	// every door on a town map into the default buries the combat picture.
	// `nearby all` keeps the full kitchen-sink behavior for muscle memory.
	// Unexplored stays opt-in for the same reason it always was: it answers
	// a different question ("where to go?") and would dominate the output.
	const bool isDefault = filter.empty();
	const bool isAll     = (filter == "all");

	const bool wantEnemies    = isDefault || isAll || filter == "enemies";
	const bool wantMercs      = isDefault || isAll || filter == "mercs";
	const bool wantCivilians  = isDefault || isAll || filter == "civilians" || filter == "civs";
	const bool wantHazards    = isDefault || isAll || filter == "hazards";
	const bool wantMines      = isDefault || isAll || filter == "mines";
	const bool wantBombs      = isDefault || isAll || filter == "bombs";
	const bool wantItems      = isAll || filter == "items";
	const bool wantDoors      = isAll || filter == "doors";
	const bool wantContainers = isAll || filter == "containers";
	const bool wantExits      = isAll || filter == "exits";
	const bool wantUnexplored = (filter == "unexplored" || filter == "unknown");

	if (!wantEnemies && !wantMercs && !wantItems && !wantDoors && !wantContainers &&
	    !wantCivilians && !wantExits && !wantHazards && !wantMines && !wantBombs &&
	    !wantUnexplored)
	{
		Console_Println(ST::format(
			"unknown filter '{}' (try: enemies, mercs, civs, items, doors, containers, exits, hazards, mines, bombs, unexplored, all)",
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
	if (wantHazards)
	{
		std::vector<ListedHazard> hazards;
		enumerateHazards(*observer, hazards);
		Console_Println(ST::format("Hazards ({}):", hazards.size()));
		std::size_t shown = 0;
		for (std::size_t i = 0; i < hazards.size(); ++i)
		{
			if (maxDist > 0 && hazards[i].distance > maxDist) continue;
			Console_Println(formatHazardLine(hazards[i], i + 1, *observer));
			++shown;
		}
		if (shown == 0) Console_Println("  (none)");
	}
	if (wantMines)
	{
		std::vector<ListedMine> mines;
		enumerateMines(*observer, mines);
		Console_Println(ST::format("Mines ({}):", mines.size()));
		std::size_t shown = 0;
		for (std::size_t i = 0; i < mines.size(); ++i)
		{
			if (maxDist > 0 && mines[i].distance > maxDist) continue;
			Console_Println(formatMineLine(mines[i], i + 1, *observer));
			++shown;
		}
		if (shown == 0) Console_Println("  (none)");
	}
	if (wantBombs)
	{
		std::vector<ListedBomb> bombs;
		enumerateBombs(*observer, bombs);
		Console_Println(ST::format("Placed bombs ({}):", bombs.size()));
		std::size_t shown = 0;
		for (std::size_t i = 0; i < bombs.size(); ++i)
		{
			if (maxDist > 0 && bombs[i].distance > maxDist) continue;
			Console_Println(formatBombLine(bombs[i], i + 1, *observer));
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
		UINT8 prevHazard  = gpWorldLevelData[origin].ubExtFlags[0] & ANY_SMOKE_EFFECT;

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
			// checks below. STRUCTURE_BASE_TILE is the canonical base-tile
			// flag; sBaseGridNo is unset on bases (see enumerateDoors).
			STRUCTURE* const door = FindStructure(g, STRUCTURE_ANYDOOR);
			if (door && (door->fFlags & STRUCTURE_BASE_TILE))
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

			// Hazard cloud transitions: announce entering a smoke/gas
			// cloud and exiting one. We pick one label per transition
			// even if multiple hazard flags are present (dominant by the
			// same priority `nearby hazards` uses).
			const UINT8 hazard = gpWorldLevelData[g].ubExtFlags[0] & ANY_SMOKE_EFFECT;
			if (hazard != prevHazard)
			{
				if (hazard)
				{
					out.push_back({s, ST::format("{} begins", hazardLabel(hazard))});
				}
				else
				{
					out.push_back({s, ST::format("{} ends", hazardLabel(prevHazard))});
				}
				prevHazard = hazard;
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
		const INT16 dist = SpacesAway(observer->sGridNo, tgt.gridno);
		const UINT8 dir  = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(observer->sGridNo, tgt.gridno));
		label = dist == 0 ? ST::string("Tile here") : ST::format("Tile {} {}", dist, directionWord(dir));
	}
	else
	{
		label = ST::string("Tile");
	}

	Console_Println(ST::format("{}: {}.", label, terrainWord(terrain)));

	// Room ID: anonymous numeric, but a stable per-sector "are we in the
	// same enclosed space?" anchor. NO_ROOM is the engine's outdoor /
	// unbounded sentinel — report it explicitly so the user can tell
	// "outside" from "this tile happens to have no room data."
	const UINT8 room = GetRoom(tgt.gridno);
	if (room == NO_ROOM)
	{
		Console_Println("  Outdoors.");
	}
	else
	{
		Console_Println(ST::format("  In room {}.", room));
	}

	// Hazards: surfaced even on unrevealed tiles (clouds rise visibly
	// over fog) so they're announced before the visibility short-circuit.
	const UINT8 hazard = gpWorldLevelData[tgt.gridno].ubExtFlags[0] & ANY_SMOKE_EFFECT;
	if (hazard)
	{
		Console_Println(ST::format("  Hazard: {}.", hazardLabel(hazard)));
	}

	// Mines tracked by MAPELEMENT_*_MINE_PRESENT are already gated to
	// "player knows about it" (see enumerateMines). Surface them on `tile`
	// even before the revealed check for the same reason.
	const UINT16 flags = gpWorldLevelData[tgt.gridno].uiFlags;
	if (flags & MAPELEMENT_PLAYER_MINE_PRESENT) Console_Println("  Friendly mine here.");
	if (flags & MAPELEMENT_ENEMY_MINE_PRESENT)  Console_Println("  Enemy mine here.");

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
	// Bucket boundaries match the engine's hold-DELETE overlay
	// (DisplayCover.cc:142-147): five colour bands at 20/40/60/80. We
	// emit the same bands as English labels so the SR user gets the
	// same granularity a sighted player perceives — no precise percent,
	// because the cursor overlay never shows one either.
	const char* coverBucket(INT8 cover)
	{
		if (cover <= 20) return "exposed";
		if (cover <= 40) return "weak cover";
		if (cover <= 60) return "partial cover";
		if (cover <= 80) return "good cover";
		return "safe";
	}
}

namespace
{
	struct ScanCandidate
	{
		INT16 gridno;
		INT16 dist;
		UINT8 dir;
		INT8  cover;
		INT16 apCost;
	};
}

static void RunCoverScan(SOLDIERTYPE& sel, int radius);

void Cmd_Cover(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel) { Console_Println("No merc selected."); return; }

	// `cover scan [N]` runs the radius scan; everything else is a single-tile
	// query (defaulting to "here" if no target).
	if (args.size() >= 2 && args[1] == "scan")
	{
		int radius = gGameSettings.ubSizeOfDisplayCover;
		if (args.size() >= 3 && !parseInt(args[2], radius))
		{
			Console_Println(ST::format("cover scan: bad radius '{}'", args[2]));
			return;
		}
		radius = std::clamp(radius, 4, 11);
		RunCoverScan(*sel, radius);
		return;
	}

	INT16      gridno = sel->sGridNo;
	ST::string label  = ST::string("here");
	if (args.size() >= 2)
	{
		Target tgt;
		ST::string err;
		if (parseTarget(args, 1, sel, tgt, err) == 0) { Console_Println(err); return; }
		gridno = tgt.gridno;
		const INT16 dist = SpacesAway(sel->sGridNo, gridno);
		const UINT8 dir  = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(sel->sGridNo, gridno));
		label = dist == 0
			? ST::string("here")
			: ST::format("{} tiles {}", dist, directionWord(dir));
	}

	// Report all three stances at once. The overlay only shows the merc's
	// current stance; toggling stance to compare requires repainting. One
	// line lets the SR user compare without re-querying.
	const INT8 stand  = CalcCoverForGridNoBasedOnTeamKnownEnemies(sel, gridno, ANIM_STAND);
	const INT8 crouch = CalcCoverForGridNoBasedOnTeamKnownEnemies(sel, gridno, ANIM_CROUCH);
	const INT8 prone  = CalcCoverForGridNoBasedOnTeamKnownEnemies(sel, gridno, ANIM_PRONE);

	Console_Println(ST::format("Cover at {}: standing {}, crouched {}, prone {}.",
	                           label,
	                           coverBucket(stand),
	                           coverBucket(crouch),
	                           coverBucket(prone)));
}

static void RunCoverScan(SOLDIERTYPE& sel, int radius)
{
	// Mirror DisplayCover.cc's overlay: paint cover only on tiles you can
	// actually walk to. LocalReachableTest sets MAPELEMENT_REACHABLE on
	// every tile within `radius` that has a foot path from the merc.
	LocalReachableTest(sel.sGridNo, static_cast<INT8>(radius));

	const INT16 maxLeft  = std::min<INT16>(radius,                  sel.sGridNo % MAXCOL);
	const INT16 maxRight = std::min<INT16>(radius, MAXCOL - 1     - sel.sGridNo % MAXCOL);
	const INT16 maxUp    = std::min<INT16>(radius,                  sel.sGridNo / MAXROW);
	const INT16 maxDown  = std::min<INT16>(radius, MAXROW - 1     - sel.sGridNo / MAXROW);

	const INT8 stance = GetStance(sel);

	std::vector<ScanCandidate> cands;
	cands.reserve(static_cast<size_t>((maxLeft + maxRight + 1) * (maxUp + maxDown + 1)));

	int bucketCounts[5] = { 0, 0, 0, 0, 0 }; // exposed, weak, partial, good, safe
	int exposedByDir[NUM_WORLD_DIRECTIONS] = { 0 };

	for (INT16 dy = -maxUp; dy <= maxDown; ++dy)
	{
		for (INT16 dx = -maxLeft; dx <= maxRight; ++dx)
		{
			const INT16 gridno = sel.sGridNo + dx + (MAXCOL * dy);
			if (gridno == sel.sGridNo) continue;
			if (!(gpWorldLevelData[gridno].uiFlags & MAPELEMENT_REACHABLE)) continue;

			// Skip tiles a visible enemy is standing on — you can't move
			// onto them. Mirrors DisplayCover.cc's WhoIsThere2 filter.
			SOLDIERTYPE const* occupant = WhoIsThere2(gridno, sel.bLevel);
			if (occupant && occupant->bVisible == TRUE && occupant->bTeam != sel.bTeam) continue;

			const INT8 cover = CalcCoverForGridNoBasedOnTeamKnownEnemies(&sel, gridno, stance);

			int bucket;
			if      (cover <= 20) bucket = 0;
			else if (cover <= 40) bucket = 1;
			else if (cover <= 60) bucket = 2;
			else if (cover <= 80) bucket = 3;
			else                  bucket = 4;
			++bucketCounts[bucket];

			ScanCandidate c;
			c.gridno = gridno;
			c.dist   = SpacesAway(sel.sGridNo, gridno);
			c.dir    = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(sel.sGridNo, gridno));
			c.cover  = cover;
			c.apCost = PlotPath(&sel, gridno, NO_COPYROUTE, FALSE, WALKING, 0);

			if (bucket == 0 && c.dir < NUM_WORLD_DIRECTIONS) ++exposedByDir[c.dir];

			cands.push_back(c);
		}
	}

	const int total = bucketCounts[0] + bucketCounts[1] + bucketCounts[2] + bucketCounts[3] + bucketCounts[4];
	if (total == 0)
	{
		Console_Println(ST::format("Cover scan within {}: no reachable tiles.", radius));
		return;
	}

	Console_Println(ST::format(
		"Cover scan within {} (stance {}): {} reachable — {} safe, {} good, {} partial, {} weak, {} exposed.",
		radius, stanceWord(sel), total,
		bucketCounts[4], bucketCounts[3], bucketCounts[2], bucketCounts[1], bucketCounts[0]));

	const INT8 hereCover = CalcCoverForGridNoBasedOnTeamKnownEnemies(&sel, sel.sGridNo, stance);
	int hereBucket;
	if      (hereCover <= 20) hereBucket = 0;
	else if (hereCover <= 40) hereBucket = 1;
	else if (hereCover <= 60) hereBucket = 2;
	else if (hereCover <= 80) hereBucket = 3;
	else                      hereBucket = 4;

	Console_Println(ST::format("Here: {}.", coverBucket(hereCover)));

	// "Better moves" = strictly higher bucket than where we stand. Sort by
	// (cover desc, AP cost asc) so the cheapest top-cover options surface.
	std::vector<ScanCandidate> better;
	better.reserve(cands.size());
	for (auto const& c : cands)
	{
		int b;
		if      (c.cover <= 20) b = 0;
		else if (c.cover <= 40) b = 1;
		else if (c.cover <= 60) b = 2;
		else if (c.cover <= 80) b = 3;
		else                    b = 4;
		if (b > hereBucket) better.push_back(c);
	}
	std::sort(better.begin(), better.end(),
		[](ScanCandidate const& a, ScanCandidate const& b) {
			if (a.cover != b.cover) return a.cover > b.cover;
			return a.apCost < b.apCost;
		});

	if (better.empty())
	{
		Console_Println("No reachable tile improves it.");
	}
	else
	{
		Console_Println("Better moves:");
		const std::size_t cap = std::min<std::size_t>(better.size(), 3);
		for (std::size_t i = 0; i < cap; ++i)
		{
			ScanCandidate const& c = better[i];
			const ST::string overBudget = (c.apCost > sel.bActionPoints)
				? ST::string(", over budget") : ST::string();
			Console_Println(ST::format("  {} {}: {}, {} AP{}",
				c.dist, directionWord(c.dir), coverBucket(c.cover), c.apCost, overBudget));
		}
		if (better.size() > cap)
		{
			Console_Println(ST::format("  (+{} more)", better.size() - cap));
		}
	}

	// Inverse view: only useful when you're not already exposed (in which
	// case "exposed within N" is most of the field anyway).
	if (hereBucket > 0 && bucketCounts[0] > 0)
	{
		ST::string parts;
		const UINT8 order[8] = { NORTH, NORTHEAST, EAST, SOUTHEAST,
		                        SOUTH, SOUTHWEST, WEST, NORTHWEST };
		for (UINT8 d : order)
		{
			if (exposedByDir[d] == 0) continue;
			if (!parts.empty()) parts += ", ";
			parts += ST::format("{} {}", exposedByDir[d], directionWord(d));
		}
		Console_Println(ST::format("Exposed within {}: {}.", radius, parts));
	}
}

namespace
{
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

	// Inventory line for an explosive: the at-a-glance fields a player
	// uses to pick one off the belt — damage and radius for the threat
	// envelope, plus the arming state for IC_BOMB items so the user
	// knows whether `plant` is ready or `arm` is needed first. We don't
	// repeat everything `examine` shows; just the minimum to choose.
	ST::string formatExplosive(const OBJECTTYPE& o)
	{
		const ItemModel*      const it = GCM->getItem(o.usItem);
		const ExplosiveModel* const e  = it->asExplosive();
		if (!e) return formatGenericObject(o);

		const int n = std::max<int>(1, o.ubNumberOfObjects);
		const ST::string head = (n > 1)
			? ST::format("{} x{}", itemName(o.usItem), n)
			: itemName(o.usItem);

		ST::string stats;
		if (const ExplosiveBlastEffect* b = e->getBlastEffect();
		    b && (b->damage || b->radius))
		{
			stats = ST::format("{} dmg, {} tiles", b->damage, b->radius);
		}
		else if (const ExplosiveSmokeEffect* sm = e->getSmokeEffect();
		         sm && sm->smokeEffect)
		{
			stats = ST::format("{}, {} tiles", sm->smokeEffect->getName(), sm->maxRadius);
		}

		ST::string state;
		if (o.fFlags & OBJECT_ARMED_BOMB)
		{
			switch (o.bDetonatorType)
			{
				case BOMB_TIMED:    state = ST::format("armed, timed {}t",  o.bDelay);     break;
				case BOMB_REMOTE:   state = ST::format("armed, remote f{}", o.bFrequency); break;
				case BOMB_PRESSURE: state = ST::string("armed, pressure");                 break;
				case BOMB_SWITCH:   state = ST::format("armed, switch f{}", o.bFrequency); break;
				default:            state = ST::string("armed");                           break;
			}
		}
		else if (it->getItemClass() == IC_BOMB && !e->isPressureTriggered())
		{
			const bool hasDet = FindAttachment(&o, DETONATOR)    != ITEM_NOT_FOUND ||
			                    FindAttachment(&o, REMDETONATOR) != ITEM_NOT_FOUND;
			if (!hasDet) state = ST::string("no detonator");
		}

		if (!stats.empty() && !state.empty())
			return ST::format("{} ({}; {})", head, stats, state);
		if (!stats.empty()) return ST::format("{} ({})", head, stats);
		if (!state.empty()) return ST::format("{} ({})", head, state);
		return head;
	}

	ST::string formatSlot(const OBJECTTYPE& o)
	{
		const ItemModel* const it = GCM->getItem(o.usItem);
		if (it->isGun())       return formatGun(o);
		if (it->isAmmo())      return formatMagazine(o);
		if (it->asExplosive()) return formatExplosive(o);
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

	Console_Println(ST::format("{} inventory:", s->name));

	int emptyCount = 0;
	for (int slot : kSlotOrder)
	{
		const OBJECTTYPE& o = s->inv[slot];
		if (o.usItem == 0) { ++emptyCount; continue; }
		// Slot-tag prefix lets the user read "s8 Big pocket 1: AK-47..."
		// and immediately address it as `examine s8` or `swap s8 s1`.
		Console_Println(ST::format("  {} {}: {}",
		                           slotTag(static_cast<INT8>(slot)),
		                           slotLabel(static_cast<INT8>(slot)),
		                           formatSlot(o)));
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
		sources += ST::format("{} ({})", slotTag(static_cast<INT8>(slot)), slotShots);
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

namespace
{
	// Verb hint line: tell the player what they can actually do with
	// this slot from the console. Each verb listed here is one we've
	// already wired to an engine entry point, so the hint stays honest.
	ST::string examineVerbs(const SOLDIERTYPE& s, INT8 slot, const OBJECTTYPE& o)
	{
		const ItemModel* const it = GCM->getItem(o.usItem);
		std::vector<const char*> verbs;
		verbs.push_back("examine");

		if (slot == HANDPOS || slot == SECONDHANDPOS)
		{
			verbs.push_back("swap-hands");
		}
		if (it->isGun() && slot == HANDPOS)
		{
			verbs.push_back("fire <target>");
			if (o.usItem != ROCKET_LAUNCHER) verbs.push_back("reload");
		}
		// Equipping into the main hand uses the engine's hand-swap path —
		// only worth advertising if the slot isn't already a hand.
		if (slot != HANDPOS && slot != SECONDHANDPOS &&
		    (it->isGun() || it->isBlade() || it->isThrowingKnife() ||
		     it->isLauncher() || it->isPunch()))
		{
			verbs.push_back("swap <slot> s1");
		}
		if (it->isMedkit())
		{
			verbs.push_back("bandage [<target>]");
		}
		// Explosive verb hints — only what the slot can actually do given
		// its current state. A grenade in s1 is throwable; a plastic
		// charge with no detonator wants `attach` first; a fully-armed
		// bomb wants `plant`. Don't list `detonate` here: it's a player-
		// global trigger keyed on frequency, not on this slot.
		const ExplosiveModel* const expl = it->asExplosive();
		if (expl)
		{
			if (it->getItemClass() == IC_GRENADE && slot == HANDPOS)
			{
				verbs.push_back("throw <target>");
			}
			if (it->getItemClass() == IC_BOMB)
			{
				const bool armed     = (o.fFlags & OBJECT_ARMED_BOMB) != 0;
				const bool hasTimed  = FindAttachment(&o, DETONATOR)    != ITEM_NOT_FOUND;
				const bool hasRemote = FindAttachment(&o, REMDETONATOR) != ITEM_NOT_FOUND;
				const bool pressure  = expl->isPressureTriggered();
				if (!armed && !hasTimed && !hasRemote && !pressure)
				{
					verbs.push_back("attach <slot> detonator|remotedet");
				}
				if (!armed)
				{
					if      (hasTimed)  verbs.push_back("arm <slot> turns <N>");
					else if (hasRemote) verbs.push_back("arm <slot> freq <F>");
					else if (pressure)  verbs.push_back("arm <slot>");
					else                verbs.push_back("arm <slot> turns|freq|pressure");
				}
				if (armed) verbs.push_back("plant <slot>");
			}
		}
		verbs.push_back("swap <slot> <slot>");
		verbs.push_back("give <name>");
		verbs.push_back("drop");

		ST::string line;
		for (std::size_t i = 0; i < verbs.size(); ++i)
		{
			if (i > 0) line += ", ";
			line += verbs[i];
		}
		(void)s; // unused for now — kept for future per-merc gating
		return line;
	}

	void describeWeapon(const OBJECTTYPE& o)
	{
		const WeaponModel* const w = GCM->getWeapon(o.usItem);
		if (!w) return;

		const ST::string cal = w->calibre ? w->calibre->getName() : ST::string("n/a");
		const UINT16 rangeTiles = w->usRange / 10;

		Console_Println(ST::format(
			"  Gun, calibre {}, range {} tiles, mag {}, {} loaded.",
			cal, rangeTiles, w->ubMagSize, o.ubGunShotsLeft));
		Console_Println(ST::format(
			"  Damage: impact {}, deadliness {}.{}",
			w->ubImpact, w->ubDeadliness,
			w->ubShotsPerBurst > 0
				? ST::format(" Burst {} rounds.", w->ubShotsPerBurst)
				: ST::string()));
	}

	void describeAmmo(const OBJECTTYPE& o)
	{
		const MagazineModel* const m = GCM->getItem(o.usItem)->asAmmo();
		if (!m) return;
		const ST::string cal = m->calibre ? m->calibre->getName() : ST::string("n/a");
		const int n = std::max<int>(1, o.ubNumberOfObjects);
		const int total = sumMagShots(o);
		Console_Println(ST::format(
			"  Ammo, calibre {}, capacity {} per mag.", cal, m->capacity));
		if (n == 1)
		{
			Console_Println(ST::format("  {} of {} rounds remaining.",
			                           o.ubShotsLeft[0], m->capacity));
		}
		else
		{
			Console_Println(ST::format("  {} mags, {} rounds total.", n, total));
		}
	}

	void describeArmour(const OBJECTTYPE& o)
	{
		const ArmourModel* const a = GCM->getItem(o.usItem)->asArmour();
		if (!a) return;
		const char* cls = "armour";
		switch (a->getArmourClass())
		{
			case ARMOURCLASS_VEST:     cls = "vest";     break;
			case ARMOURCLASS_HELMET:   cls = "helmet";   break;
			case ARMOURCLASS_LEGGINGS: cls = "leggings"; break;
			case ARMOURCLASS_PLATE:    cls = "plates";   break;
		}
		Console_Println(ST::format(
			"  Armour ({}): protection {}, explosive protection {}.",
			cls, a->getProtection(), a->getExplosivesProtection()));
	}

	void describeMedkit(const OBJECTTYPE& o)
	{
		// TotalPoints sums bStatus across the stack — for medkits each
		// "status" point is one HP-worth of healing remaining, so this
		// is the practical "how much healing is left" number.
		Console_Println(ST::format(
			"  Medkit, {} healing points remaining.", TotalPoints(&o)));
	}

	void describeExplosive(const OBJECTTYPE& o)
	{
		const ItemModel*      const it = GCM->getItem(o.usItem);
		const ExplosiveModel* const e  = it->asExplosive();
		if (!e) return;

		// Class line. IC_GRENADE covers both hand grenades and launched
		// (GL/mortar) grenades — distinguish by whether the explosive
		// carries a launchable calibre. IC_BOMB covers everything you
		// place: pressure mines, plastics, trip-wires, shaped charges.
		const char* cls;
		if      (e->isLaunchable())                    cls = "Launched grenade";
		else if (it->getItemClass() == IC_GRENADE)     cls = "Hand grenade";
		else if (e->isPressureTriggered())             cls = "Pressure-triggered explosive";
		else                                           cls = "Plantable explosive";
		Console_Println(ST::format("  {}, volatility {}.", cls, e->getVolatility()));

		if (const ExplosiveBlastEffect* b = e->getBlastEffect();
		    b && (b->damage || b->radius))
		{
			Console_Println(ST::format(
				"  Blast: {} damage, radius {} tiles.", b->damage, b->radius));
		}
		if (const ExplosiveStunEffect* st = e->getStunEffect();
		    st && (st->breathDamage || st->radius))
		{
			Console_Println(ST::format(
				"  Stun: {} breath damage, radius {} tiles.",
				st->breathDamage, st->radius));
		}
		if (const ExplosiveSmokeEffect* sm = e->getSmokeEffect();
		    sm && sm->smokeEffect)
		{
			Console_Println(ST::format(
				"  Cloud: {}, radius {}-{} tiles, lasts {} turn{}.",
				sm->smokeEffect->getName(),
				sm->initialRadius, sm->maxRadius,
				sm->duration, sm->duration == 1 ? "" : "s"));
		}
		if (const ExplosiveLightEffect* l = e->getLightEffect();
		    l && (l->radius || l->duration))
		{
			Console_Println(ST::format(
				"  Light: radius {} tiles, lasts {} turn{}.",
				l->radius, l->duration, l->duration == 1 ? "" : "s"));
		}

		// Per-instance arming state. Only IC_BOMB items hold this between
		// console interactions — grenades arm-on-throw via the throw
		// animation, so seeing OBJECT_ARMED_BOMB on a slotted grenade
		// would be a bug worth surfacing rather than hiding.
		if (o.fFlags & OBJECT_ARMED_BOMB)
		{
			switch (o.bDetonatorType)
			{
				case BOMB_TIMED:
					Console_Println(ST::format(
						"  Armed: timed fuse, {} turn{} until detonation.",
						o.bDelay, o.bDelay == 1 ? "" : "s"));
					break;
				case BOMB_REMOTE:
					Console_Println(ST::format(
						"  Armed: remote detonator, frequency {}.", o.bFrequency));
					break;
				case BOMB_PRESSURE:
					Console_Println("  Armed: pressure trigger.");
					break;
				case BOMB_SWITCH:
					Console_Println(ST::format(
						"  Armed: panic-trigger switch, frequency {}.", o.bFrequency));
					break;
				default:
					Console_Println("  Armed.");
					break;
			}
		}
		else if (it->getItemClass() == IC_BOMB)
		{
			// Wiring hint for unarmed bombs — tells the player whether
			// they need to `attach` a detonator before they can `arm`.
			const bool hasTimed  = FindAttachment(&o, DETONATOR)    != ITEM_NOT_FOUND;
			const bool hasRemote = FindAttachment(&o, REMDETONATOR) != ITEM_NOT_FOUND;
			if      (hasTimed && hasRemote) Console_Println("  Wiring: timed and remote detonators attached.");
			else if (hasTimed)              Console_Println("  Wiring: timed detonator attached.");
			else if (hasRemote)             Console_Println("  Wiring: remote detonator attached.");
			else if (e->isPressureTriggered()) Console_Println("  Wiring: pressure trigger (no detonator needed).");
			else                            Console_Println("  Wiring: no detonator attached — needs DETONATOR or REMDETONATOR.");
		}
	}

	void describeAttachments(const OBJECTTYPE& o)
	{
		if (!ItemHasAttachments(o)) return;
		ST::string list;
		for (UINT16 i : o.usAttachItem)
		{
			if (i == NOTHING) continue;
			if (!list.empty()) list += ", ";
			list += GCM->getItem(i)->getName();
		}
		if (!list.empty()) Console_Println(ST::format("  Attachments: {}.", list));
	}
}

void Cmd_Examine(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const s = GetSelectedMan();
	if (!s) { Console_Println("No merc selected."); return; }
	if (args.size() < 2)
	{
		Console_Println("usage: examine <slot>  (slot is s1..s19; see 'inventory')");
		return;
	}

	const INT8 slot = parseSlotTag(args[1]);
	if (slot < 0)
	{
		Console_Println(ST::format(
			"unknown slot '{}' (use s1..s19; see 'inventory')", args[1]));
		return;
	}

	const OBJECTTYPE& o = s->inv[slot];
	if (o.usItem == NOTHING)
	{
		Console_Println(ST::format("{} {}: empty.",
		                           slotTag(slot), slotLabel(slot)));
		return;
	}

	const ItemModel* const it = GCM->getItem(o.usItem);

	Console_Println(ST::format("Examining {} ({}): {}.",
	                           slotTag(slot), slotLabel(slot), it->getName()));

	// Description is the engine's localized flavor text — same string the
	// inventory description box shows on the SDL side. Empty for some
	// stub items; only print when there's actually content.
	const ST::string& desc = it->getDescription();
	if (!desc.empty()) Console_Println(ST::format("  {}", desc));

	// Status line: condition (bStatus[0]) for non-stacked single items,
	// or "Nx, total" for stacks. Money is its own thing.
	if (it->isMoney())
	{
		Console_Println(ST::format("  ${}.", o.uiMoneyAmount));
	}
	else
	{
		const int n = std::max<int>(1, o.ubNumberOfObjects);
		// Weight in grams; 1 ubWeight unit = 100 g (hectogram).
		const int gramsTotal = it->getWeight() * 100 * n;
		ST::string weight;
		if (gramsTotal >= 1000)
		{
			weight = ST::format("{}.{} kg",
			                    gramsTotal / 1000, (gramsTotal % 1000) / 100);
		}
		else
		{
			weight = ST::format("{} g", gramsTotal);
		}
		if (n > 1)
		{
			Console_Println(ST::format("  Stack of {}, weight {}.", n, weight));
		}
		else if (it->isGun() || it->isArmour() || it->isMedkit() || it->isKit() ||
		         it->isBlade() || it->isPunch() || it->isFace())
		{
			Console_Println(ST::format("  Condition {}%, weight {}.",
			                           o.bStatus[0], weight));
		}
		else
		{
			Console_Println(ST::format("  Weight {}.", weight));
		}
	}

	if      (it->isGun())       describeWeapon(o);
	else if (it->isAmmo())      describeAmmo(o);
	else if (it->isArmour())    describeArmour(o);
	else if (it->isMedkit())    describeMedkit(o);
	else if (it->asExplosive()) describeExplosive(o);

	describeAttachments(o);

	if (o.fFlags & OBJECT_KNOWN_TO_BE_TRAPPED)
	{
		Console_Println("  *** This item is known to be trapped. ***");
	}

	Console_Println(ST::format("  Verbs: {}.", examineVerbs(*s, slot, o)));
}

void Cmd_Path(const std::vector<std::string>&)
{
	Console_Println("path: not implemented yet.");
}
