#include "Console_Query.h"
#include "Console_Address.h"
#include "Console_Visibility.h"

#include "Console.h"

#include "Animation_Control.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "Game_Clock.h"
#include "Interface.h"
#include "Isometric_Utils.h"
#include "ItemModel.h"
#include "Item_Types.h"
#include "LOS.h"
#include "Map_Information.h"
#include "OppList.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "StrategicMap.h"
#include "TileDef.h"
#include "WeaponModels.h"
#include "Weapons.h"
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

	const bool wantEnemies = (filter == "all" || filter == "enemies");
	const bool wantMercs   = (filter == "all" || filter == "mercs");

	if (!wantEnemies && !wantMercs)
	{
		Console_Println(ST::format("unknown filter '{}' (try: enemies, mercs, all)", filter));
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

void Cmd_Inventory(const std::vector<std::string>&)
{
	Console_Println("inventory: not implemented yet.");
}

void Cmd_Path(const std::vector<std::string>&)
{
	Console_Println("path: not implemented yet.");
}
