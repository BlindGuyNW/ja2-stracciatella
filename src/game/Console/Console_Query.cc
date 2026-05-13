#include "Console_Query.h"
#include "Console_Address.h"
#include "Console_Tags.h"
#include "Console_Visibility.h"

#include "Console.h"

#include "Animation_Control.h"
#include "ArmourModel.h"
#include "CalibreModel.h"
#include "ContentManager.h"
#include "DisplayCover.h"
#include "Environment.h"
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
#include "Lighting.h"
#include "MagazineModel.h"
#include "SmokeEffectModel.h"
#include "Map_Edgepoints.h"
#include "Map_Information.h"
#include "GameSettings.h"
#include "OppList.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "PathAI.h"
#include "Points.h"
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

	const char* stanceWord(AnimationHeight h)
	{
		switch (h)
		{
			case ANIM_STAND:  return "standing";
			case ANIM_CROUCH: return "crouched";
			case ANIM_PRONE:  return "prone";
		}
		return "?";
	}

	const char* stanceWord(const SOLDIERTYPE& s)
	{
		return stanceWord(GetStance(s));
	}

	// Stance for an animation state read out of memory (e.g. a sticky
	// civilian snapshot) rather than off a live SOLDIERTYPE. Same lookup
	// the engine's GetStance helper uses. Guarded because the input
	// comes from cached state — a malformed save or a snapshot pulled
	// during a transitional anim should degrade to "?" rather than read
	// out of bounds.
	const char* stanceWordForAnim(UINT16 animState)
	{
		if (animState >= NUMANIMATIONSTATES) return "?";
		return stanceWord(
			static_cast<AnimationHeight>(gAnimControl[animState].ubEndHeight));
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

	// bLevel > 0 means standing on a rooftop (the engine has only two
	// levels — I_GROUND_LEVEL / I_ROOF_LEVEL). After 'climb' the only
	// quick confirmation the SR user has is this annotation.
	const char* const roofTag = (s.bLevel > 0) ? ", on rooftop" : "";
	Console_Println(ST::format(
		"{}: life {}/{}, breath {}/{}, AP {}, {} facing {}{}.",
		s.name, s.bLife, s.bLifeMax, s.bBreath, s.bBreathMax,
		s.bActionPoints, stanceWord(s), directionWord(s.bDirection), roofTag));

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
		s = requireSelectedMerc();
		if (!s) return;
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
		s = requireSelectedMerc();
		if (!s) return;
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
		const ST::string offset = formatOffset(observer.sGridNo, t.sGridNo);
		const UINT16 weapon = t.inv[HANDPOS].usItem;
		const ST::string room = roomSuffix(observer, t.sGridNo);
		return weapon != 0
			? ST::format("  e{} {} {}, {}, {}, life {}, {}{}",
			             tagN, t.name, coordLabel(t.sGridNo),
			             offset,
			             stanceWord(t), t.bLife, itemName(weapon), room)
			: ST::format("  e{} {} {}, {}, {}, life {}{}",
			             tagN, t.name, coordLabel(t.sGridNo),
			             offset,
			             stanceWord(t), t.bLife, room);
	}

	ST::string formatFriendlyLine(const ListedSoldier& ls, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const SOLDIERTYPE& t = *ls.soldier;
		return ST::format("  m{} {} {}, {}, life {}, AP {}{}",
		                  tagN, t.name, coordLabel(t.sGridNo),
		                  formatOffset(observer.sGridNo, t.sGridNo),
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

		return ST::format("  i{} {}: {}{}",
		                  tagN, formatOffset(observer.sGridNo, p.gridno), list,
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
		bool  isOpen;
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
			ListedContainer c{};
			c.distance = SpacesAway(observer.sGridNo, g);
			c.gridno   = g;
			c.isOpen   = (s->fFlags & STRUCTURE_OPEN) != 0;
			out.push_back(c);
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
		return ST::format("  k{} {}: container ({}){}",
		                  tagN, formatOffset(observer.sGridNo, c.gridno),
		                  c.isOpen ? "open" : "closed",
		                  roomSuffix(observer, c.gridno));
	}

	// Containers carry persistent open/closed state (STRUCTURE_OPEN,
	// saved in the per-sector .tmp map files via SaveLoadMap), so we can
	// use it as a "have I checked this?" filter. Bare `nearby containers`
	// is action-oriented — "what's left to investigate?" — and hides
	// already-opened containers. `nearby all` includes everything,
	// annotated, for callers who want the kitchen sink. Tag indices stay
	// stable: k<N> always points to the Nth container in the canonical
	// enumeration (matching ConsoleTags::resolve), so a `lookat k4` still
	// resolves correctly even if k4 wasn't printed in the current view.
	// The heading reports both counts when hiding is in effect, so the
	// gap in tag numbering isn't surprising.
	bool printNearbyContainers(const SOLDIERTYPE& observer, INT16 maxDist, bool hideOpen, bool multi)
	{
		std::vector<ListedContainer> list;
		enumerateContainers(observer, list);

		if (list.empty() && multi) return false;

		std::size_t openedCount = 0;
		for (const auto& c : list) if (c.isOpen) ++openedCount;
		const std::size_t closedCount = list.size() - openedCount;

		if (hideOpen && openedCount > 0)
		{
			Console_Println(ST::format("Containers ({} closed, {} already opened hidden):",
			                           closedCount, openedCount));
		}
		else
		{
			Console_Println(ST::format("Containers ({}):", list.size()));
		}

		std::size_t printed = 0;
		for (std::size_t i = 0; i < list.size(); ++i)
		{
			if (maxDist > 0 && list[i].distance > maxDist) continue;
			if (hideOpen && list[i].isOpen)                continue;
			Console_Println(formatContainerLine(list[i], i + 1, observer));
			++printed;
		}
		if (printed == 0) Console_Println("  (none)");
		return true;
	}

	// Canonical structure taxonomy shared by every verb that needs to
	// say "what is this thing in the world?". Ten kinds covering the
	// engine's full STRUCTURE_* surface; callers pick the subset they
	// care about and format their own labels (capitalization, state,
	// material adjective, edge orientation — those are verb-specific).
	//
	// Priority order matters and mirrors DamageLog_Hooks.cc:ClassifyNoun:
	// EXPLOSIVE outranks the wall/openable bits gas tanks also carry,
	// because the explosive-headline property is what callers want to
	// surface ("gas tank" beats "wall" or "container" for the same
	// world object).
	enum class StructureKind : UINT8
	{
		Door,       // STRUCTURE_ANYDOOR — closed-or-open carried separately
		Explosive,  // STRUCTURE_EXPLOSIVE — gas tanks, red barrels
		Vehicle,    // STRUCTURE_VEHICLE
		Tree,       // STRUCTURE_TREE
		Fence,      // STRUCTURE_ANYFENCE = FENCE | WIREFENCE
		Window,     // STRUCTURE_WALLNWINDOW — wall with embedded window
		Wall,       // STRUCTURE_WALL — plain wall, after WALLNWINDOW filtered
		Switch,     // STRUCTURE_SWITCH
		Container,  // STRUCTURE_OPENABLE without ANYDOOR — lockers, cabinets
		Obstacle,   // STRUCTURE_GENERIC / anything else still classifiable
	};

	bool classifyStructure(STRUCTURE const* s, StructureKind& out)
	{
		if (s == nullptr) return false;
		const UINT32 f = s->fFlags;
		if (!(f & STRUCTURE_BASE_TILE)) return false;
		if (f & (STRUCTURE_PERSON | STRUCTURE_CORPSE)) return false;
		if (f & STRUCTURE_ANYDOOR)     { out = StructureKind::Door;      return true; }
		if (f & STRUCTURE_EXPLOSIVE)   { out = StructureKind::Explosive; return true; }
		if (f & STRUCTURE_VEHICLE)     { out = StructureKind::Vehicle;   return true; }
		if (f & STRUCTURE_TREE)        { out = StructureKind::Tree;      return true; }
		if (f & STRUCTURE_ANYFENCE)    { out = StructureKind::Fence;     return true; }
		if (f & STRUCTURE_WALLNWINDOW) { out = StructureKind::Window;    return true; }
		if (f & STRUCTURE_WALL)        { out = StructureKind::Wall;      return true; }
		if (f & STRUCTURE_SWITCH)      { out = StructureKind::Switch;    return true; }
		if (f & STRUCTURE_OPENABLE)    { out = StructureKind::Container; return true; }
		out = StructureKind::Obstacle;
		return true;
	}

	// Destructibles: HP-bearing kinds the damage system can break.
	// Subset of StructureKind; the destructibles list also wants doors
	// / containers / switches / obstacles filtered out (each has its
	// own surface elsewhere).
	enum class DestNoun : UINT8
	{
		Wall, Window, Fence, Tree, Vehicle, Explosive
	};

	struct ListedDestructible
	{
		INT16    distance;
		INT16    gridno;
		DestNoun noun;
		UINT8    material; // gubMaterialArmour index, 0 if N/A
	};

	const char* destNounWord(DestNoun n)
	{
		switch (n)
		{
			case DestNoun::Wall:      return "wall";
			case DestNoun::Window:    return "window";
			case DestNoun::Fence:     return "fence";
			case DestNoun::Tree:      return "tree";
			case DestNoun::Vehicle:   return "vehicle";
			case DestNoun::Explosive: return "explosive prop";
		}
		return "structure";
	}

	const char* destMaterialAdjective(UINT8 material)
	{
		switch (material)
		{
			case MATERIAL_WOOD_WALL:    return "wooden";
			case MATERIAL_PLYWOOD_WALL: return "plywood";
			case MATERIAL_STONE:        return "stone";
			case MATERIAL_CONCRETE1:    return "concrete";
			case MATERIAL_CONCRETE2:    return "concrete";
			case MATERIAL_ROCK:         return "rock";
			case MATERIAL_LIGHT_METAL:  return "metal";
			case MATERIAL_THICKER_METAL: return "metal";
			case MATERIAL_HEAVY_METAL:  return "heavy metal";
			default:                    return "";
		}
	}

	// Destructibles adapter: keep only the HP-bearing kinds the damage
	// system actually breaks. Doors, switches, containers, and plain
	// obstacles each have their own surface; the destructibles list
	// (`nearby destructibles`, the `v<N>` tag namespace) stays focused
	// on what the player can target with grenades / shaped charges.
	bool classifyDestructible(STRUCTURE const* s, DestNoun& out)
	{
		StructureKind kind;
		if (!classifyStructure(s, kind)) return false;
		switch (kind)
		{
			case StructureKind::Explosive: out = DestNoun::Explosive; return true;
			case StructureKind::Vehicle:   out = DestNoun::Vehicle;   return true;
			case StructureKind::Tree:      out = DestNoun::Tree;      return true;
			case StructureKind::Fence:     out = DestNoun::Fence;     return true;
			case StructureKind::Window:    out = DestNoun::Window;    return true;
			case StructureKind::Wall:      out = DestNoun::Wall;      return true;
			case StructureKind::Door:
			case StructureKind::Switch:
			case StructureKind::Container:
			case StructureKind::Obstacle:
				return false;
		}
		return false;
	}

	// Same visibility gate as containers (interior loot is roof-hidden
	// for sighted players). Walls, fences, trees, and gas tanks are
	// usually outdoors so this rarely over-filters; for indoor walls
	// the user can move closer and re-scan, which matches sighted play.
	void enumerateDestructibles(const SOLDIERTYPE& observer, std::vector<ListedDestructible>& out)
	{
		for (INT16 g = 0; g < WORLD_MAX; ++g)
		{
			if (!ConsoleVis::IsKnownTile(g, 0)) continue;
			for (STRUCTURE* s = gpWorldLevelData[g].pStructureHead; s; s = s->pNext)
			{
				if (!(s->fFlags & STRUCTURE_BASE_TILE)) continue;
				DestNoun n;
				if (!classifyDestructible(s, n)) continue;
				ListedDestructible d{};
				d.distance = SpacesAway(observer.sGridNo, g);
				d.gridno   = g;
				d.noun     = n;
				d.material = (s->pDBStructureRef && s->pDBStructureRef->pDBStructure)
				             ? s->pDBStructureRef->pDBStructure->ubArmour
				             : MATERIAL_NOTHING;
				out.push_back(d);
				break; // one entry per tile; multiple structures per tile collapse to the headline
			}
		}
		std::sort(out.begin(), out.end(),
			[](const ListedDestructible& a, const ListedDestructible& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.gridno < b.gridno;
			});
	}

	ST::string formatDestructibleLine(const ListedDestructible& d, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const char* const adj  = destMaterialAdjective(d.material);
		const char* const noun = destNounWord(d.noun);
		ST::string label = adj[0] != '\0'
			? ST::format("{} {}", adj, noun)
			: ST::string(noun);
		return ST::format("  v{} {}: {}{}",
		                  tagN, formatOffset(observer.sGridNo, d.gridno),
		                  label, roomSuffix(observer, d.gridno));
	}

	ST::string formatDoorLine(const ListedDoor& d, std::size_t tagN, const SOLDIERTYPE& observer)
	{
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

		return ST::format("  d{} {}: {}{}{}",
		                  tagN, formatOffset(observer.sGridNo, d.gridno), state, lock,
		                  roomSuffix(observer, d.gridno));
	}

	// Civilians use the engine's public opplist as their live-visibility
	// signal, the same as enemies. But civs decay out of the opplist on
	// the same timer enemies do, which loses an asymmetry a sighted
	// player keeps for free: visual memory of where someone was. We
	// extend the listing with ConsoleVis's sticky-memory channel —
	// civilians the player has seen *at any point this sector* show up
	// even after they've decayed, with their last-known tile/stance and
	// a "(last seen Nh ago)" suffix on the printed line. Hidden quest
	// NPCs the player has never encountered stay unlisted, preserving
	// the spoiler property of the original fog rule.
	void enumerateCivilians(const SOLDIERTYPE& observer, std::vector<ListedSoldier>& out)
	{
		FOR_EACH_MERC(it)
		{
			SOLDIERTYPE* const t = *it;
			if (t->ubID == observer.ubID)                continue;
			if (t->bTeam != CIV_TEAM)                    continue;
			// Drop dead / off-sector / inactive civs from the listing
			// regardless of whether we have a sticky record of them —
			// once they're gone we have no useful position to surface.
			if (!t->bActive || t->bLife <= 0 || !t->bInSector) continue;

			const bool live = ConsoleVis::IsKnownSoldier(*t);
			ListedSoldier ls{};
			ls.soldier = t;
			if (live)
			{
				ls.gridno    = t->sGridNo;
				ls.distance  = SpacesAway(observer.sGridNo, t->sGridNo);
				ls.stale     = false;
			}
			else if (ConsoleVis::CivilianEverSeenHere(t->ubID))
			{
				ls.gridno      = ConsoleVis::CivilianLastGridno(t->ubID);
				ls.distance    = SpacesAway(observer.sGridNo, ls.gridno);
				ls.stale       = true;
				ls.lastSeenMin = ConsoleVis::CivilianLastSeenMin(t->ubID);
				ls.animState   = ConsoleVis::CivilianLastAnim(t->ubID);
			}
			else
			{
				continue;
			}
			out.push_back(ls);
		}
		std::sort(out.begin(), out.end(),
			[](const ListedSoldier& a, const ListedSoldier& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.soldier->ubID < b.soldier->ubID;
			});
	}

	ST::string lastSeenSuffix(UINT32 sinceMin)
	{
		const UINT32 ago = GetWorldTotalMin() - sinceMin;
		if (ago < 60)        return ST::format(" (last seen {}m ago)", ago);
		if (ago < 60 * 24)   return ST::format(" (last seen {}h ago)", ago / 60);
		return ST::format(" (last seen {}d ago)", ago / (60 * 24));
	}

	ST::string formatCivilianLine(const ListedSoldier& ls, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const SOLDIERTYPE& t = *ls.soldier;
		// Civilians' names are often generic ("Civilian") for crowd extras;
		// quest NPCs have proper names. Either way, lead with the name and
		// leave gameplay vitals (life/AP) off — civs aren't typically a
		// resource the player manages. For stale entries everything
		// position/stance-related comes from the snapshot, not the live
		// SOLDIERTYPE, so we don't leak the engine's current truth.
		const char* stance = ls.stale ? stanceWordForAnim(ls.animState)
		                              : stanceWord(t);
		return ST::format("  c{} {} {}, {}, {}{}{}",
		                  tagN, t.name, coordLabel(ls.gridno),
		                  formatOffset(observer.sGridNo, ls.gridno),
		                  stance,
		                  roomSuffix(observer, ls.gridno),
		                  ls.stale ? lastSeenSuffix(ls.lastSeenMin) : ST::string());
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
		const ST::string offset = formatOffset(observer.sGridNo, e.gridno);
		const ST::string dest   = GetSectorIDString(e.dest, FALSE);
		if (e.hasGridDest)
		{
			return ST::format("  x{} {}: to {}",
			                  tagN, offset, dest);
		}
		// Map edge: walking off this tile transitions to the neighbor
		// sector on that side; engine still pops the sector-exit dialog.
		// e.side stays as a directionWord because it labels which *wall*
		// the edge is on, not a bearing from the merc.
		return ST::format("  x{} {}: map edge {} -> {}",
		                  tagN, offset,
		                  directionWord(e.side), dest);
	}

	// Per-direction accumulator while scanning. count is the number of
	// unrevealed playable tiles in that direction; gridno is the nearest
	// of them, addressable directly via `move <col,row>`.
	struct Frontier
	{
		bool  present;
		INT16 gridno;
		INT16 distance;
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
			if (!f.present || dist < f.distance)
			{
				f.present  = true;
				f.gridno   = g;
				f.distance = dist;
			}
			++f.count;
		}
	}

	// One row per compass direction with at least one unexplored tile,
	// flattened from UnexploredSummary so it has the same shape as every
	// other Listed* category (distance, gridno, plus per-category extras).
	// Sorted by ascending distance, ties broken by direction ordinal so
	// the result is deterministic between calls — the `u<N>` tag relies on
	// this matching the order Cmd_Nearby printed.
	struct ListedFrontier
	{
		INT16 distance;
		INT16 gridno;
		INT32 count;
		UINT8 dir;
	};

	void enumerateFrontiers(const UnexploredSummary& u, std::vector<ListedFrontier>& out)
	{
		for (UINT8 d = 0; d < NUM_WORLD_DIRECTIONS; ++d)
		{
			const Frontier& f = u.byDir[d];
			if (!f.present) continue;
			out.push_back({ f.distance, f.gridno, f.count, d });
		}
		std::sort(out.begin(), out.end(),
			[](const ListedFrontier& a, const ListedFrontier& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.dir < b.dir;
			});
	}

	ST::string formatFrontierLine(const ListedFrontier& f, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		// f.dir is the *scan axis* the frontier was found along, not a
		// derived bearing — keep it cardinal. The nearest-tile gridno is a
		// concrete point, so it gets the precise cartesian offset.
		const INT16 col = f.gridno % WORLD_COLS;
		const INT16 row = f.gridno / WORLD_COLS;
		return ST::format("  u{} {} axis: nearest at {} ({},{}), {} tiles unseen this way",
		                  tagN, directionWord(f.dir),
		                  formatOffset(observer.sGridNo, f.gridno),
		                  col, row, f.count);
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
		INT16       gridno;         // closest cluster tile (the tag's target)
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
			h.gridno = closestG;
			h.tileCount     = static_cast<INT16>(count);
			h.label         = hazardLabel(clusterTypes);
			if (h.label) out.push_back(h);
		}

		std::sort(out.begin(), out.end(),
			[](const ListedHazard& a, const ListedHazard& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.gridno < b.gridno;
			});
	}

	ST::string formatHazardLine(const ListedHazard& h, std::size_t tagN, const SOLDIERTYPE& observer)
	{
		const ST::string offset = formatOffset(observer.sGridNo, h.gridno);
		if (h.tileCount <= 1)
		{
			return ST::format("  z{} {}: {}", tagN, offset, h.label);
		}
		return ST::format("  z{} {}: {} ({} tiles)",
		                  tagN, offset, h.label, h.tileCount);
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
		return ST::format("  b{} {}: {} mine{}",
		                  tagN, formatOffset(observer.sGridNo, m.gridno),
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
		return ST::format("  p{} {}: {} ({}){}",
		                  tagN, formatOffset(observer.sGridNo, b.gridno),
		                  itemName(b.itemId), mode,
		                  roomSuffix(observer, b.gridno));
	}

	// Generic "Heading (N):" / distance-filtered list / "(none)" block
	// shared by every category whose enumerator yields a vector<T> sorted
	// by ascending T::distance and a per-line formatter with the standard
	// (entry, 1-based-index, observer) signature. The `nearby` print order
	// must match the order ConsoleTags::resolve indexes by — both call
	// sites route through this helper (or its frontiers analogue) so the
	// ordering invariant lives in one place.
	//
	// `suppressIfEmpty` collapses genuinely-empty categories to nothing.
	// Used by the multi-category modes (bare `nearby` / `nearby all`) so
	// a dozen "(0): (none)" stanzas don't bury the categories with content.
	// Distance-filtered categories still print: a "(3)" heading with
	// "(none)" body tells the user there are entries but the cap hid them,
	// which is different from "no civs anywhere." Returns true if any
	// output was emitted.
	template <typename T>
	bool printNearbyCategory(
		const SOLDIERTYPE& observer,
		INT16              maxDist,
		const char*        heading,
		void               (*enumerate)(const SOLDIERTYPE&, std::vector<T>&),
		ST::string         (*formatLine)(const T&, std::size_t, const SOLDIERTYPE&),
		bool               suppressIfEmpty = false)
	{
		std::vector<T> list;
		enumerate(observer, list);
		if (list.empty() && suppressIfEmpty) return false;

		Console_Println(ST::format("{} ({}):", heading, list.size()));
		// Indices stay aligned with the address parser even when a
		// distance cap is in effect: enumerate the full distance-sorted
		// list, then only print entries within the cap. So 'fire e3'
		// resolves to the third hostile overall, regardless of what the
		// user filtered out.
		std::size_t shown = 0;
		for (std::size_t i = 0; i < list.size(); ++i)
		{
			if (maxDist > 0 && list[i].distance > maxDist) continue;
			Console_Println(formatLine(list[i], i + 1, observer));
			++shown;
		}
		if (shown == 0) Console_Println("  (none)");
		return true;
	}
}

void Cmd_Nearby(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const observer = requireSelectedMerc("No merc selected to anchor 'nearby' on.");
	if (!observer) return;

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

	const bool wantEnemies       = isDefault || isAll || filter == "enemies";
	const bool wantMercs         = isDefault || isAll || filter == "mercs";
	const bool wantCivilians     = isDefault || isAll || filter == "civilians" || filter == "civs";
	const bool wantHazards       = isDefault || isAll || filter == "hazards";
	const bool wantMines         = isDefault || isAll || filter == "mines";
	const bool wantBombs         = isDefault || isAll || filter == "bombs";
	const bool wantItems         = isAll || filter == "items";
	const bool wantDoors         = isAll || filter == "doors";
	const bool wantContainers    = isAll || filter == "containers";
	const bool wantExits         = isAll || filter == "exits";
	// Destructibles (walls/windows/fences/trees/vehicles/explosive props)
	// stay opt-in like doors/containers — on a town map they balloon and
	// drown the combat picture. `nearby all` includes them. `dest` and
	// `destructables` are accepted as friendlier aliases.
	const bool wantDestructibles = isAll || filter == "destructibles" ||
	                               filter == "destructables" || filter == "dest";
	const bool wantUnexplored    = (filter == "unexplored" || filter == "unknown");

	if (!wantEnemies && !wantMercs && !wantItems && !wantDoors && !wantContainers &&
	    !wantCivilians && !wantExits && !wantHazards && !wantMines && !wantBombs &&
	    !wantDestructibles && !wantUnexplored)
	{
		Console_Println(ST::format(
			"unknown filter '{}' (try: enemies, mercs, civs, items, doors, containers, exits, hazards, mines, bombs, destructibles, unexplored, all)",
			filter));
		return;
	}

	// Multi-category modes hide categories that come back genuinely
	// empty (so "Civilians (0): (none)" etc. stop crowding the
	// tactically-important rows). Single-filter mode (e.g. `nearby civs`)
	// keeps the "(0)" affirmation -- if you asked specifically, you want
	// the explicit "checked, none" answer rather than silence.
	const bool multi      = isDefault || isAll;
	bool       anyPrinted = false;

	auto run = [&](auto fn) { anyPrinted = fn() || anyPrinted; };
	if (wantEnemies)    run([&]{ return printNearbyCategory<ListedSoldier>  (*observer, maxDist, "Visible hostiles", enumerateHostiles,     formatHostileLine,    multi); });
	if (wantMercs)      run([&]{ return printNearbyCategory<ListedSoldier>  (*observer, maxDist, "Teammates",        enumerateTeammates,    formatFriendlyLine,   multi); });
	if (wantCivilians)  run([&]{ return printNearbyCategory<ListedSoldier>  (*observer, maxDist, "Civilians",        enumerateCivilians,    formatCivilianLine,   multi); });
	if (wantItems)      run([&]{ return printNearbyCategory<ItemPile>       (*observer, maxDist, "Item piles",       enumerateVisibleItems, formatItemPileLine,   multi); });
	if (wantDoors)      run([&]{ return printNearbyCategory<ListedDoor>     (*observer, maxDist, "Doors",            enumerateDoors,        formatDoorLine,       multi); });
	if (wantContainers)
	{
		// Hide already-opened containers when the user asked specifically
		// for "containers" (the "what's left to investigate" intent).
		// `nearby all` includes them annotated, on the principle that the
		// kitchen-sink dump should hide nothing.
		const bool hideOpen = (filter == "containers");
		run([&]{ return printNearbyContainers(*observer, maxDist, hideOpen, multi); });
	}
	if (wantExits)      run([&]{ return printNearbyCategory<ListedExit>     (*observer, maxDist, "Exits",            enumerateExits,        formatExitLine,       multi); });
	if (wantHazards)    run([&]{ return printNearbyCategory<ListedHazard>   (*observer, maxDist, "Hazards",          enumerateHazards,      formatHazardLine,     multi); });
	if (wantMines)      run([&]{ return printNearbyCategory<ListedMine>     (*observer, maxDist, "Mines",            enumerateMines,        formatMineLine,       multi); });
	if (wantBombs)      run([&]{ return printNearbyCategory<ListedBomb>     (*observer, maxDist, "Placed bombs",     enumerateBombs,        formatBombLine,       multi); });
	if (wantDestructibles)
		run([&]{ return printNearbyCategory<ListedDestructible>(*observer, maxDist, "Destructibles", enumerateDestructibles, formatDestructibleLine, multi); });
	if (wantUnexplored)
	{
		// Frontiers carry a preamble (% explored) and a fully-explored
		// short-circuit that the generic helper doesn't model, so this
		// block stays bespoke. The list itself goes through the same
		// distance-sorted, tag-ordered shape via enumerateFrontiers, so
		// the `u<N>` tag still round-trips with what's printed here.
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
			std::vector<ListedFrontier> frontiers;
			enumerateFrontiers(u, frontiers);
			Console_Println(ST::format("Unexplored frontiers ({}):", frontiers.size()));
			std::size_t shown = 0;
			for (std::size_t i = 0; i < frontiers.size(); ++i)
			{
				if (maxDist > 0 && frontiers[i].distance > maxDist) continue;
				Console_Println(formatFrontierLine(frontiers[i], i + 1, *observer));
				++shown;
			}
			if (shown == 0) Console_Println("  (none within range)");
		}
	}

	// Multi-category mode + every category empty -> the console would
	// otherwise sit silent. Emit one terse line so the user knows the
	// scan ran and produced nothing, rather than wondering if the verb
	// hung. The unexplored branch always prints (sector-progress line),
	// so it never contributes to this case.
	if (multi && !anyPrinted) Console_Println("Nothing nearby.");
}

namespace ConsoleTags
{
	// Soldier-typed categories (e/m/c) all return ListedSoldier from
	// their enumerator and resolve to the soldier's current tile.
	// Factored out so the dispatch case bodies can be one-liners.
	static Result resolveSoldierCategory(
		const SOLDIERTYPE& observer,
		int                idx,
		Target&            out,
		int&               listSize,
		void               (*enumerate)(const SOLDIERTYPE&, std::vector<ListedSoldier>&))
	{
		std::vector<ListedSoldier> list;
		enumerate(observer, list);
		listSize = static_cast<int>(list.size());
		if (idx < 1 || idx > listSize) return kIndexOutOfRange;
		// Use the listed gridno rather than soldier->sGridNo: for stale
		// civilians these differ, and we want `cN` to address the place
		// the player remembers seeing them, not the engine's current
		// position (which the player has no in-game way to know).
		out.soldier = list[idx - 1].soldier;
		out.gridno  = list[idx - 1].gridno;
		return kResolved;
	}

	// Tile-typed categories: enumerator yields T with a `gridno` member;
	// resolution writes that gridno and a null soldier.
	template <typename T>
	static Result resolveTileCategory(
		const SOLDIERTYPE& observer,
		int                idx,
		Target&            out,
		int&               listSize,
		void               (*enumerate)(const SOLDIERTYPE&, std::vector<T>&))
	{
		std::vector<T> list;
		enumerate(observer, list);
		listSize = static_cast<int>(list.size());
		if (idx < 1 || idx > listSize) return kIndexOutOfRange;
		out.gridno  = list[idx - 1].gridno;
		out.soldier = nullptr;
		return kResolved;
	}

	Result resolve(char prefix, int idx, const SOLDIERTYPE& observer,
	               Target& out, int& listSize)
	{
		out      = {};
		listSize = 0;

		switch (prefix)
		{
			case 'e': return resolveSoldierCategory(observer, idx, out, listSize, enumerateHostiles);
			case 'm': return resolveSoldierCategory(observer, idx, out, listSize, enumerateTeammates);
			case 'c': return resolveSoldierCategory(observer, idx, out, listSize, enumerateCivilians);
			case 'i': return resolveTileCategory<ItemPile>       (observer, idx, out, listSize, enumerateVisibleItems);
			case 'd': return resolveTileCategory<ListedDoor>     (observer, idx, out, listSize, enumerateDoors);
			case 'k': return resolveTileCategory<ListedContainer>(observer, idx, out, listSize, enumerateContainers);
			case 'x': return resolveTileCategory<ListedExit>     (observer, idx, out, listSize, enumerateExits);
			case 'z': return resolveTileCategory<ListedHazard>   (observer, idx, out, listSize, enumerateHazards);
			case 'b': return resolveTileCategory<ListedMine>     (observer, idx, out, listSize, enumerateMines);
			case 'p': return resolveTileCategory<ListedBomb>     (observer, idx, out, listSize, enumerateBombs);
			case 'v': return resolveTileCategory<ListedDestructible>(observer, idx, out, listSize, enumerateDestructibles);
			case 'u':
			{
				// Frontiers need a two-step build: scan, then flatten +
				// sort. enumerateFrontiers does the second step the same
				// way Cmd_Nearby's unexplored block does, so the indices
				// align with what was printed.
				UnexploredSummary u;
				summarizeUnexplored(observer, u);
				std::vector<ListedFrontier> list;
				enumerateFrontiers(u, list);
				listSize = static_cast<int>(list.size());
				if (idx < 1 || idx > listSize) return kIndexOutOfRange;
				out.gridno  = list[idx - 1].gridno;
				out.soldier = nullptr;
				return kResolved;
			}
		}
		return kNotATag;
	}

	const char* categoryName(char prefix)
	{
		switch (prefix)
		{
			case 'e': return "hostiles";
			case 'm': return "teammates";
			case 'c': return "civilians";
			case 'i': return "item piles";
			case 'd': return "doors";
			case 'k': return "containers";
			case 'x': return "exits";
			case 'z': return "hazards";
			case 'b': return "mines";
			case 'p': return "placed bombs";
			case 'u': return "unexplored frontiers";
			case 'v': return "destructibles";
		}
		return nullptr;
	}

	bool isTagPrefix(char c)
	{
		return categoryName(c) != nullptr;
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
	SOLDIERTYPE* const observer = requireSelectedMerc("No merc selected to anchor 'look' on.");
	if (!observer) return;

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

	const INT8 dir = parseCompass(args[1]);
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

namespace
{
	// Wall orientation → tile edge. JA2's INSIDE/OUTSIDE prefix
	// describes which face of the wall is drawn (interior vs
	// exterior surface); both block bullets the same. The TOP_LEFT /
	// TOP_RIGHT half describes which axis the wall runs along, and
	// walls are always stored on the south/east side of their seam:
	//   - TOP_LEFT  wall runs E-W; stored on the tile's SOUTH edge
	//                (Explosion_Control.cc:512-519 — damage propagates
	//                W/E along the wall and SOUTH for the attached
	//                tile; the editor's EXTERIOR_TOP path also stores
	//                north-of-building walls on the outdoor tile to
	//                the north, on that tile's S edge).
	//   - TOP_RIGHT wall runs N-S; stored on the tile's EAST edge
	//                (same source, lines 522-531: damage propagates
	//                N/S along the wall, EAST for the attached tile).
	const char* tileEdge(UINT8 orient)
	{
		switch (orient)
		{
			case INSIDE_TOP_LEFT:
			case OUTSIDE_TOP_LEFT:  return "S";
			case INSIDE_TOP_RIGHT:
			case OUTSIDE_TOP_RIGHT: return "E";
		}
		return nullptr;
	}

	void printTileStructures(GridNo gridno)
	{
		// Walk the per-tile structure list directly. FindStructure is
		// flag-filtered and doesn't let us classify each entry as we
		// go; raw traversal is simpler here. Skip subtiles of
		// multi-tile structures (they'd duplicate the base entry's
		// info on every covered tile).
		STRUCTURE* head = gpWorldLevelData[gridno].pStructureHead;
		for (STRUCTURE* s = head; s; s = s->pNext)
		{
			const StructureFlags f = s->fFlags;
			if (!(f & STRUCTURE_BASE_TILE))    continue;
			if (f & STRUCTURE_PERSON)          continue; // shown as Occupant
			if (f & STRUCTURE_CORPSE)          continue;
			if (f & STRUCTURE_ROOF)            continue; // not ground-level geometry
			if (f & STRUCTURE_NORMAL_ROOF)     continue;

			const bool isOpen     = (f & STRUCTURE_OPEN)     != 0;
			const bool isPassable = (f & STRUCTURE_PASSABLE) != 0;

			StructureKind sk;
			if (!classifyStructure(s, sk)) continue;

			ST::string kind;
			switch (sk)
			{
				case StructureKind::Door:
					kind = isOpen ? "Open door" : "Closed door";
					if (s->ubLockStrength > 0) kind += " (locked)";
					break;
				case StructureKind::Window:
					kind = isOpen ? "Wall with open window" : "Wall with closed window";
					break;
				case StructureKind::Wall:      kind = "Wall";      break;
				case StructureKind::Fence:     kind = "Fence";     break;
				case StructureKind::Tree:      kind = "Tree";      break;
				case StructureKind::Vehicle:   kind = "Vehicle";   break;
				case StructureKind::Explosive: kind = "Gas tank";  break;
				case StructureKind::Switch:
					kind = isOpen ? "Switch (on)" : "Switch (off)";
					break;
				case StructureKind::Container:
					kind = isOpen ? "Container (open)" : "Container (closed)";
					if (s->ubLockStrength > 0) kind += " (locked)";
					break;
				case StructureKind::Obstacle:  kind = "Obstacle";  break;
			}

			// Cube extent: bottom = sCubeOffset; height = StructureHeight (1..4).
			// Cubes 0..4 are roughly foot, knee, torso, head, above-head
			// — same scale the LOS routines use.
			const INT8  height = StructureHeight(s);
			const INT16 bottom = s->sCubeOffset;
			const INT16 top    = bottom + std::max<INT8>(height, 1) - 1;

			ST::string line;
			const char* edge = tileEdge(s->ubWallOrientation);
			const bool isEdgeFeature =
				(f & (STRUCTURE_WALL | STRUCTURE_WALLNWINDOW | STRUCTURE_ANYDOOR)) != 0;

			if (edge && isEdgeFeature)
				line = ST::format("  {} on {} edge (cubes {}-{}).", kind, edge, bottom, top);
			else
				line = ST::format("  {} (cubes {}-{}).", kind, bottom, top);

			if (isPassable) line += " Passable.";

			Console_Println(line);
		}
	}
}

void Cmd_Tile(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		Console_Println("usage: tile <name> | tile <tag> (e.g. d2) | tile <dir> <steps> [<dir> <steps>] (either order: '2 e' or 'e 2') | tile <col,row>");
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
		label = ST::format("Tile {}", formatOffset(observer->sGridNo, tgt.gridno));
	}
	else
	{
		label = ST::string("Tile");
	}

	Console_Println(ST::format("{}: {}.", label, terrainWord(terrain)));

	// Room ID: anonymous numeric, but a stable per-sector "are we in the
	// same enclosed space?" anchor. NO_ROOM is the engine's outdoor /
	// unbounded sentinel — report it explicitly so the user can tell
	// "outside" from "this tile happens to have no room data." When the
	// view is at roof level (the user climbed up), reword "in room N" as
	// "on roof of room N" — the engine extends room IDs through the roof,
	// so the same number means a different physical position depending on
	// level.
	const UINT8 room = GetRoom(tgt.gridno);
	const bool onRoof = (level > 0);
	if (room == NO_ROOM)
	{
		Console_Println(onRoof ? "  On rooftop (no room below)." : "  Outdoors.");
	}
	else
	{
		Console_Println(ST::format(onRoof ? "  On roof of room {}." : "  In room {}.", room));
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

	printTileStructures(tgt.gridno);
}

void Cmd_Room(const std::vector<std::string>& args)
{
	if (!gfWorldLoaded)
	{
		Console_Println("No world loaded.");
		return;
	}
	SOLDIERTYPE* const observer = requireSelectedMerc("No merc selected to anchor 'room' on.");
	if (!observer) return;

	// Pick the target room: explicit arg or merc's current.
	UINT8 roomId;
	if (args.size() < 2)
	{
		roomId = GetRoom(observer->sGridNo);
		if (roomId == NO_ROOM)
		{
			Console_Println("You're outdoors. Pass a room ID (e.g. 'room 29') to inspect one.");
			return;
		}
	}
	else
	{
		int parsed;
		if (!parseInt(args[1], parsed) || parsed < 0 || parsed >= NO_ROOM)
		{
			Console_Println(ST::format("'{}' is not a valid room ID (0..{}).", args[1], NO_ROOM - 1));
			return;
		}
		roomId = static_cast<UINT8>(parsed);
	}

	// Pass 1: bounding box + interior tile count. Rooms can be irregular
	// (L-shaped, with internal gaps), so the rectangle bound is an outer
	// envelope; we report interior count separately and flag mismatch.
	INT16 minCol = WORLD_COLS, maxCol = -1;
	INT16 minRow = WORLD_ROWS, maxRow = -1;
	int interior = 0;
	for (INT16 g = 0; g < WORLD_MAX; ++g)
	{
		if (GetRoom(g) != roomId) continue;
		const INT16 col = g % WORLD_COLS;
		const INT16 row = g / WORLD_COLS;
		if (col < minCol) minCol = col;
		if (col > maxCol) maxCol = col;
		if (row < minRow) minRow = row;
		if (row > maxRow) maxRow = row;
		++interior;
	}
	if (interior == 0)
	{
		Console_Println(ST::format("Room {}: not present in this sector.", roomId));
		return;
	}

	const INT16 w = static_cast<INT16>(maxCol - minCol + 1);
	const INT16 h = static_cast<INT16>(maxRow - minRow + 1);
	const INT16 nwGrid = static_cast<INT16>(minRow * WORLD_COLS + minCol);
	const INT16 seGrid = static_cast<INT16>(maxRow * WORLD_COLS + maxCol);
	const bool rectangular = (interior == static_cast<int>(w) * static_cast<int>(h));

	Console_Println(ST::format("Room {}: {} wide x {} tall, {} floor tiles{}.",
	                           roomId, w, h, interior,
	                           rectangular ? "" : " (irregular shape)"));
	Console_Println(ST::format("  NW corner at {}, SE corner at {}.",
	                           formatOffset(observer->sGridNo, nwGrid),
	                           formatOffset(observer->sGridNo, seGrid)));

	// Doors. Reuse the `nearby doors` enumeration so tag indices match
	// what the user sees in `nearby` — d2 in `room` is the same door as
	// d2 in `nearby doors`. A door is relevant to this room if either:
	//   (a) its tile is in the room (interior door), or
	//   (b) any 4-cardinal neighbor of its tile is in the room.
	// Walls and doors are stored on the south/east side of their seam
	// (`Explosion_Control.cc:508-532`), so a door between rooms 29 and 28
	// commonly lives on a room-28 tile while bordering room 29; (a)
	// alone misses every shared-wall door. The "other side" room — the
	// room (or outdoors) on the non-roomId neighbor — is annotated so
	// the user can see at a glance where each opening leads.
	{
		std::vector<ListedDoor> doors;
		enumerateDoors(*observer, doors);

		auto otherSideRoom = [&](INT16 g) -> int
		{
			// -1 = no qualifying neighbor (door isn't related to roomId);
			// NO_ROOM = the other side is outdoors.
			int hostNonRoomNeighbor = -1;
			bool hostIsRoom = (GetRoom(g) == roomId);
			for (UINT8 dir = 0; dir < NUM_WORLD_DIRECTIONS; dir += 2)
			{
				const INT16 ng = NewGridNo(g, DirIncrementer[dir]);
				if (ng == NOWHERE || ng == g) continue;
				const UINT8 nr = GetRoom(ng);
				if (hostIsRoom)
				{
					if (nr != roomId) { hostNonRoomNeighbor = nr; break; }
				}
				else if (nr == roomId)
				{
					return GetRoom(g); // the host tile is the "other side"
				}
			}
			return hostIsRoom ? hostNonRoomNeighbor : -1;
		};

		std::vector<std::size_t> relevant;
		for (std::size_t i = 0; i < doors.size(); ++i)
		{
			const int other = otherSideRoom(doors[i].gridno);
			if (other == -1 && GetRoom(doors[i].gridno) != roomId) continue;
			relevant.push_back(i);
		}

		if (!relevant.empty())
		{
			Console_Println(ST::format("  Doors ({}):", relevant.size()));
			for (std::size_t i : relevant)
			{
				const ListedDoor& d = doors[i];
				const char* state = d.perceivedKnown
					? (d.perceivedOpen ? "open" : "closed")
					: "state unknown";
				const char* lock;
				switch (d.perceivedLock)
				{
					case DOOR_PERCEIVED_LOCKED:   lock = ", locked";      break;
					case DOOR_PERCEIVED_UNLOCKED: lock = ", unlocked";    break;
					case DOOR_PERCEIVED_BROKEN:   lock = ", lock broken"; break;
					default:                      lock = "";              break;
				}
				const int other = otherSideRoom(d.gridno);
				ST::string leadsTo;
				if (other == NO_ROOM)              leadsTo = ", to outdoors";
				else if (other >= 0)               leadsTo = ST::format(", to room {}", other);
				// else: door's tile is in this room and no neighbor differs
				// (interior partition? rare) — no annotation.
				Console_Println(ST::format("    d{} {}: {}{}{}",
				                           i + 1,
				                           formatOffset(observer->sGridNo, d.gridno),
				                           state, lock, leadsTo));
			}
		}
	}

	// Occupants: every soldier whose tile is in this room. Include the
	// observer (the enumerators skip them by design — but "you are here"
	// is exactly the kind of orientation a room dump should confirm).
	{
		std::vector<ST::string> lines;
		auto consider = [&](SOLDIERTYPE& t, const char* tagPrefix, std::size_t tagN)
		{
			if (GetRoom(t.sGridNo) != roomId) return;
			ST::string label = tagPrefix
				? ST::format("{}{} {}", tagPrefix, tagN, t.name)
				: ST::format("{}", t.name);
			lines.push_back(ST::format("    {} ({}, {})",
			                           label,
			                           formatOffset(observer->sGridNo, t.sGridNo),
			                           stanceWord(t)));
		};

		// Observer first (no tag — they don't appear in `nearby`).
		if (GetRoom(observer->sGridNo) == roomId)
		{
			lines.push_back(ST::format("    {} ({}, {}) -- you",
			                           observer->name,
			                           formatOffset(observer->sGridNo, observer->sGridNo),
			                           stanceWord(*observer)));
		}

		std::vector<ListedSoldier> hostiles;   enumerateHostiles (*observer, hostiles);
		std::vector<ListedSoldier> teammates;  enumerateTeammates(*observer, teammates);
		for (std::size_t i = 0; i < hostiles.size();  ++i) consider(*hostiles[i].soldier,  "e", i + 1);
		for (std::size_t i = 0; i < teammates.size(); ++i) consider(*teammates[i].soldier, "m", i + 1);

		// Civilians: walk the same enumerateCivilians the nearby printer
		// uses, so the cN tags in this room dump match what `nearby c`
		// shows globally. Numbering by a room-local counter would break
		// `goto c3` cross-references between the two surfaces.
		std::vector<ListedSoldier> civs;
		enumerateCivilians(*observer, civs);
		for (std::size_t i = 0; i < civs.size(); ++i)
		{
			const ListedSoldier& ls = civs[i];
			if (GetRoom(ls.gridno) != roomId) continue;
			const char* stance = ls.stale
				? stanceWordForAnim(ls.animState)
				: stanceWord(*ls.soldier);
			ST::string suffix = ls.stale
				? lastSeenSuffix(ls.lastSeenMin)
				: ST::string();
			lines.push_back(ST::format("    c{} {} ({}, {}){}",
			                           i + 1, ls.soldier->name,
			                           formatOffset(observer->sGridNo, ls.gridno),
			                           stance, suffix));
		}

		if (!lines.empty())
		{
			Console_Println(ST::format("  Occupants ({}):", lines.size()));
			for (auto const& s : lines) Console_Println(s);
		}
	}

	// Visible item piles in this room.
	{
		std::vector<ItemPile> piles;
		enumerateVisibleItems(*observer, piles);
		std::size_t inRoom = 0;
		for (auto const& p : piles)
			if (GetRoom(p.gridno) == roomId) ++inRoom;
		if (inRoom > 0)
		{
			Console_Println(ST::format("  Visible items ({}):", inRoom));
			for (std::size_t i = 0; i < piles.size(); ++i)
			{
				if (GetRoom(piles[i].gridno) != roomId) continue;
				const ItemPile& p = piles[i];
				// Item-grouping mirrors formatItemPileLine; inlined to
				// drop the "[same room]" suffix that would clutter every
				// line in a room dump.
				std::vector<UINT16> kinds;
				std::vector<int>    counts;
				for (UINT16 ui : p.items)
				{
					std::size_t k = 0;
					for (; k < kinds.size(); ++k) if (kinds[k] == ui) break;
					if (k == kinds.size()) { kinds.push_back(ui); counts.push_back(1); }
					else                   { ++counts[k]; }
				}
				ST::string list;
				for (std::size_t k = 0; k < kinds.size(); ++k)
				{
					if (!list.empty()) list += ", ";
					list += counts[k] > 1
						? ST::format("{} x{}", itemName(kinds[k]), counts[k])
						: itemName(kinds[k]);
				}
				Console_Println(ST::format("    i{} {}: {}",
				                           i + 1,
				                           formatOffset(observer->sGridNo, p.gridno),
				                           list));
			}
		}
	}
}

void Cmd_Cth(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const sel = requireSelectedMerc();
	if (!sel) return;
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
	const ItemModel* const item = GCM->getItem(weapon);
	const UINT32           cls  = item->getItemClass();
	if (cls != IC_GUN && cls != IC_THROWING_KNIFE)
	{
		Console_Println(ST::format("Held item ({}) is not a gun or throwing knife.", item->getName()));
		return;
	}

	// Mirror what UI_Cursors.cc:244-269 computes for the cursor's CTH text:
	// base CTH × chance-to-get-through (cover/LOS), as a percent, plus
	// AP cost (CalcTotalAPsToAttack folds in turning + aim time, the same
	// way UI_Cursors.cc:191 computes the cursor display). For throwing
	// knives the engine swaps base CTH to CalcThrownChanceToHit
	// (Weapons.cc:647). Body part defaults to torso; tile shots use cube
	// level 2 (the value Handle_UI.cc:2192 sets for shoot-at-interactive-
	// tile, the closest thing the engine has to "centre of tile").
	const UINT8 part = AIM_SHOT_TORSO;
	const INT8  cube = 2;

	// CalcChanceToHitGun reads bAimShotLocation / bTargetLevel /
	// bTargetCubeLevel off the firer for its internal LOS test
	// (Weapons.cc:2124-2137). UI_Cursors.cc:253 writes these before
	// calling. Without them the LOS test queries whatever level/body
	// part the soldier struct happens to hold from the last UI action —
	// silently produces a wrong base in some cases. Set them to match
	// the same target we're querying about.
	sel->bAimShotLocation = part;
	sel->bTargetLevel     = tgt.soldier ? tgt.soldier->bLevel : (INT8)gsInterfaceLevel;
	sel->bTargetCubeLevel = cube;

	// chance-to-get-through doesn't depend on aim time — compute once.
	const UINT32 through = tgt.soldier
		? SoldierToSoldierBodyPartChanceToGetThrough(sel, tgt.soldier, part)
		: SoldierToLocationChanceToGetThrough(sel, tgt.gridno, gsInterfaceLevel, cube, nullptr);

	ST::string cells;
	for (int aim = 0; aim <= 4; ++aim)
	{
		const UINT32 base = (cls == IC_THROWING_KNIFE)
			? CalcThrownChanceToHit(sel, static_cast<INT16>(tgt.gridno),
			                        static_cast<UINT8>(aim), part)
			: CalcChanceToHitGun(sel, static_cast<UINT16>(tgt.gridno),
			                     static_cast<UINT8>(aim), part, FALSE);
		const int   pct = static_cast<int>(base * through / 100);
		const UINT8 ap  = CalcTotalAPsToAttack(sel, tgt.gridno, TRUE, static_cast<INT8>(aim));

		if (!cells.empty()) cells += "  ";
		cells += ST::format("aim {}: {}% / {} AP", aim, pct, ap);
	}

	const ST::string label = tgt.soldier ? tgt.soldier->name : ST::string("target tile");
	Console_Println(ST::format("CTH on {}:  {}", label, cells));

	// When the composed CTH lands at 0 across the board it's almost
	// always because LOS is blocked, not because skill is low — base
	// clamps at MINCHANCETOHIT. Surface the through% so the SR user
	// knows whether to move (LOS problem) vs aim more (skill problem).
	// The engine's own fire path refuses the shot at this threshold
	// (Handle_Items.cc:118 → QUOTE_NO_LINE_OF_FIRE).
	if (through < OK_CHANCE_TO_GET_THROUGH)
	{
		Console_Println(ST::format("Line of fire blocked ({}% through).", through));
	}
}

namespace
{
	// Bucket boundaries match the engine's hold-DELETE overlay
	// (DisplayCover.cc:142-147): five colour bands at 20/40/60/80. We
	// emit the same bands as English labels so the SR user gets the
	// same granularity a sighted player perceives — no precise percent,
	// because the cursor overlay never shows one either. The top bucket
	// reads "clear" rather than "safe" because the single-tile output
	// reserves "safe" for absence-of-threats messaging.
	const char* coverBucket(INT8 cover)
	{
		if (cover <= 20) return "exposed";
		if (cover <= 40) return "weak cover";
		if (cover <= 60) return "partial cover";
		if (cover <= 80) return "good cover";
		return "clear";
	}

	// Find the eN tag (1-based) for an enemy in the canonical hostiles
	// list, or 0 if not present. The list is ordered the same way
	// `nearby` shows it, so eN here matches the user's other surfaces.
	std::size_t findHostileTag(SOLDIERTYPE const* opp,
	                           std::vector<ListedSoldier> const& hostiles)
	{
		for (std::size_t i = 0; i < hostiles.size(); ++i)
		{
			if (hostiles[i].soldier == opp) return i + 1;
		}
		return 0;
	}

	ST::string formatEnemyLabel(SOLDIERTYPE const* opp,
	                            std::vector<ListedSoldier> const& hostiles)
	{
		std::size_t const tag = findHostileTag(opp, hostiles);
		return tag != 0
			? ST::format("{} (e{})", opp->name, tag)
			: ST::string(opp->name);
	}

	// One line per stance: split the known-opponents list into the
	// shooters who can hit you (threat > 0) and those who can't (gates
	// fail at this tile/stance), and render both halves so the SR user
	// can read off which stance change actually drops a specific threat.
	void emitStanceCoverLine(const char* stanceTitle,
	                          std::vector<CoverOpponent> const& list,
	                          std::vector<ListedSoldier> const& hostiles)
	{
		ST::string exposed, covered;
		for (CoverOpponent const& o : list)
		{
			ST::string& bucket = (o.threat > 0) ? exposed : covered;
			if (!bucket.empty()) bucket += ", ";
			bucket += formatEnemyLabel(o.opponent, hostiles);
		}

		ST::string body;
		if (exposed.empty())      body = ST::format("covered from {}", covered);
		else if (covered.empty()) body = ST::format("exposed to {}", exposed);
		else                      body = ST::format("exposed to {}; covered from {}", exposed, covered);
		Console_Println(ST::format("  {}: {}.", stanceTitle, body));
	}
}

namespace
{
	struct ScanCandidate
	{
		INT16 gridno;
		UINT8 dir;     // for the exposedByDir tally only; rendering uses formatOffset(gridno)
		INT8  cover;
		INT16 apCost;
	};
}

static void RunCoverScan(SOLDIERTYPE& sel, int radius);

void Cmd_Cover(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const sel = requireSelectedMerc();
	if (!sel) return;

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
		label  = formatOffset(sel->sGridNo, gridno);
	}

	// Per-stance threat decomposition. The opponent universe is
	// stance-independent (the gates up to LOS don't read bStance), so all
	// three vectors hold the same enemies — only the per-enemy `threat`
	// changes with stance. Reporting all three stances at once lets the
	// SR user compare without re-querying, and the per-enemy split tells
	// them *which* threats a stance change drops.
	std::vector<CoverOpponent> stand, crouch, prone;
	EvaluateCoverOpponentsAtGridNo(sel, gridno, ANIM_STAND,  stand);
	EvaluateCoverOpponentsAtGridNo(sel, gridno, ANIM_CROUCH, crouch);
	EvaluateCoverOpponentsAtGridNo(sel, gridno, ANIM_PRONE,  prone);

	if (stand.empty())
	{
		Console_Println(ST::format(
			"Cover at {}: clear. No known enemy can see or reach this tile.",
			label));
		return;
	}

	std::vector<ListedSoldier> hostiles;
	enumerateHostiles(*sel, hostiles);

	Console_Println(ST::format("Cover at {} vs {} known {}:",
	                           label, stand.size(),
	                           stand.size() == 1 ? "enemy" : "enemies"));
	emitStanceCoverLine("Standing", stand,  hostiles);
	emitStanceCoverLine("Crouched", crouch, hostiles);
	emitStanceCoverLine("Prone",    prone,  hostiles);
}

static void RunCoverScan(SOLDIERTYPE& sel, int radius)
{
	const INT8 stance = GetStance(sel);

	// Short-circuit when no known enemies could threaten anywhere — the
	// bucket distribution and "better moves" are meaningless when every
	// tile is trivially clear. The opponent universe is stance- and
	// tile-independent (the gates up to LOS test don't read either), so
	// asking at the merc's own tile in the merc's own stance is enough.
	std::vector<CoverOpponent> hereOpps;
	EvaluateCoverOpponentsAtGridNo(&sel, sel.sGridNo, stance, hereOpps);
	if (hereOpps.empty())
	{
		Console_Println(ST::format(
			"Cover scan within {}: no known enemies — every reachable tile is trivially clear.",
			radius));
		return;
	}
	const std::size_t threatCount = hereOpps.size();

	// Mirror DisplayCover.cc's overlay: paint cover only on tiles you can
	// actually walk to. LocalReachableTest sets MAPELEMENT_REACHABLE on
	// every tile within `radius` that has a foot path from the merc.
	LocalReachableTest(sel.sGridNo, static_cast<INT8>(radius));

	const INT16 maxLeft  = std::min<INT16>(radius,                  sel.sGridNo % MAXCOL);
	const INT16 maxRight = std::min<INT16>(radius, MAXCOL - 1     - sel.sGridNo % MAXCOL);
	const INT16 maxUp    = std::min<INT16>(radius,                  sel.sGridNo / MAXROW);
	const INT16 maxDown  = std::min<INT16>(radius, MAXROW - 1     - sel.sGridNo / MAXROW);

	std::vector<ScanCandidate> cands;
	cands.reserve(static_cast<size_t>((maxLeft + maxRight + 1) * (maxUp + maxDown + 1)));

	int bucketCounts[5] = { 0, 0, 0, 0, 0 }; // exposed, weak, partial, good, clear
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
		"Cover scan within {} vs {} known {} (stance {}): {} reachable — {} clear, {} good, {} partial, {} weak, {} exposed.",
		radius, threatCount, threatCount == 1 ? "enemy" : "enemies",
		stanceWord(sel), total,
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
			Console_Println(ST::format("  {}: {}, {} AP{}",
				formatOffset(sel.sGridNo, c.gridno),
				coverBucket(c.cover), c.apCost, overBudget));
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
	// Day/night/dusk descriptor from a tile's true light value.
	// NORMAL_LIGHTLEVEL_DAY=3 and NORMAL_LIGHTLEVEL_NIGHT=12 anchor the
	// engine's dark/light scale (Environment.h).
	const char* lightPhase(UINT8 level)
	{
		if (level <= NORMAL_LIGHTLEVEL_DAY)   return "Day";
		if (level >= NORMAL_LIGHTLEVEL_NIGHT) return "Night";
		return "Dusk/dawn";
	}

	// Active weather that actually moves sight. Rain/thunder applies a
	// ×0.7 multiplier inside AdjustMaxSightRangeForEnvEffects
	// (OppList.cc:240); other forecasts don't affect DistanceVisible.
	const char* sightAffectingWeather(UINT32 env)
	{
		if (env & WEATHER_FORECAST_THUNDERSHOWERS) return "thundershowers";
		if (env & WEATHER_FORECAST_SHOWERS)        return "showers";
		return nullptr;
	}

	const char* visionGearLabel(const SOLDIERTYPE& s)
	{
		if (IsWearingHeadGear(s, UVGOGGLES))    return "UV goggles";
		if (IsWearingHeadGear(s, NIGHTGOGGLES)) return "nightvision goggles";
		if (IsWearingHeadGear(s, SUNGOGGLES))   return "sunglasses";
		return "none";
	}

	// An in-bounds gridno one tile from the merc in `dir`, falling back to
	// the opposite direction if the primary lands off-map. DistanceVisible
	// uses this only for the light read at the subject tile and the
	// muzzleflash check — the cone bucket comes from `bSubjectDir` directly.
	// A non-self gridno is required to avoid the same-tile short-circuit
	// at OppList.cc:920 that would return MaxDistanceVisible.
	INT16 syntheticSubjectGridno(INT16 fromGridno, UINT8 dir)
	{
		INT16 syn = fromGridno + DirIncrementer[dir];
		if (syn >= 0 && syn < WORLD_MAX) return syn;
		syn = fromGridno + DirIncrementer[OppositeDirection(dir)];
		if (syn >= 0 && syn < WORLD_MAX) return syn;
		return fromGridno;
	}

	// Grid-step count from `from` to the map edge along `dir`. Computed
	// analytically from col/row rather than walked, so this is O(1).
	int stepsToMapEdge(GridNo from, UINT8 dir)
	{
		const int col        = from % WORLD_COLS;
		const int row        = from / WORLD_COLS;
		const int rightSpace = WORLD_COLS - 1 - col;
		const int leftSpace  = col;
		const int downSpace  = WORLD_ROWS - 1 - row;
		const int upSpace    = row;
		switch (dir)
		{
			case NORTH:     return upSpace;
			case NORTHEAST: return std::min(upSpace,   rightSpace);
			case EAST:      return rightSpace;
			case SOUTHEAST: return std::min(downSpace, rightSpace);
			case SOUTH:     return downSpace;
			case SOUTHWEST: return std::min(downSpace, leftSpace);
			case WEST:      return leftSpace;
			case NORTHWEST: return std::min(upSpace,   leftSpace);
		}
		return 0;
	}

	// Sight-blocker label for the tile where an LOS ray died. Walks
	// pStructureHead through the shared classifyStructure and renders
	// a lowercase noun phrase for the first matching structure. Doors
	// reaching this point are closed by definition (open doors don't
	// oppose LOS, so the ray would not have died here). "obstruction"
	// is reserved for the case where we found no classifiable structure
	// at the failing tile -- roof clipping, ground elevation, or a
	// blocker the classifier hasn't been taught about.
	ST::string describeSightBlocker(GridNo g)
	{
		if (g < 0 || g >= WORLD_MAX) return ST::string{"obstruction"};
		for (STRUCTURE* s = gpWorldLevelData[g].pStructureHead; s; s = s->pNext)
		{
			StructureKind kind;
			if (!classifyStructure(s, kind)) continue;
			switch (kind)
			{
				case StructureKind::Door:      return ST::string{"closed door"};
				case StructureKind::Explosive: return ST::string{"gas tank"};
				case StructureKind::Vehicle:   return ST::string{"vehicle"};
				case StructureKind::Tree:      return ST::string{"tree"};
				case StructureKind::Fence:     return ST::string{"fence"};
				case StructureKind::Window:    return ST::string{"window"};
				case StructureKind::Wall:
				{
					const UINT8 mat = (s->pDBStructureRef && s->pDBStructureRef->pDBStructure)
					                  ? s->pDBStructureRef->pDBStructure->ubArmour
					                  : MATERIAL_NOTHING;
					const char* const adj = destMaterialAdjective(mat);
					return adj[0] ? ST::format("{} wall", adj) : ST::string{"wall"};
				}
				case StructureKind::Switch:    return ST::string{"switch"};
				case StructureKind::Container: return ST::string{"container"};
				case StructureKind::Obstacle:  return ST::string{"obstacle"};
			}
		}
		return ST::string{"obstruction"};
	}

	struct SightBlocker
	{
		bool       blocked;
		int        first_blocked_step; // grid-steps from merc, 1-indexed
		GridNo     blocker_gridno;
		ST::string label;
	};

	// Binary-search the first blocked step along `dir`. Calls
	// SoldierTo3DLocationLineOfSightTest against progressively closer
	// targets; the function returns 0 for blocked, positive for the
	// cover-adjusted distance achieved when the ray reaches the target.
	// Cube level 2 (chest) matches what `cth` / `cover` use as a generic
	// "is there a standing-target lane here" probe. Aware=TRUE skips the
	// triple-cost-vegetation penalty the engine applies to unaware
	// lookers (LOS.cc:671) -- we're asking what the merc can deliberately
	// spot, not what they'd miss while doing something else.
	SightBlocker findSightBlocker(const SOLDIERTYPE& sel, UINT8 dir, INT16 envelope)
	{
		SightBlocker out{};
		if (envelope <= 0) return out;

		// Max grid-step count along this direction such that the target
		// stays within the Euclidean envelope. Diagonals cover sqrt(2)
		// Euclidean per grid step -- use 1414/1000 as the integer-math
		// approximation matching the engine's pathfinding diagonal-bias
		// (PathAI.cc:1338 uses *14/10 = 1.4 for the same reason).
		const bool isDiag = (dir & 1);
		int max_steps = isDiag ? (envelope * 1000) / 1414 : envelope;
		max_steps = std::min(max_steps, stepsToMapEdge(sel.sGridNo, dir));
		if (max_steps <= 0) return out;

		auto losAtStep = [&](int step) -> bool
		{
			const INT16 target = static_cast<INT16>(
				sel.sGridNo + step * DirIncrementer[dir]);
			const INT32 r = SoldierTo3DLocationLineOfSightTest(
				&sel, target, sel.bLevel, /*cube*/ 2,
				/*sight cap*/ 255, /*aware*/ TRUE);
			return r > 0;
		};

		// Fast path: LOS clear at the envelope edge -> no blocker.
		if (losAtStep(max_steps))
		{
			out.blocked = false;
			return out;
		}

		// Find the largest step count that still reads clear. We know
		// max_steps fails; lo=0 represents "even step 1 fails" (the
		// blocker is the merc's immediate neighbor).
		int lo = 0, hi = max_steps;
		while (lo < hi)
		{
			const int mid = (lo + hi + 1) / 2;
			if (losAtStep(mid)) lo = mid;
			else                hi = mid - 1;
		}

		out.blocked            = true;
		out.first_blocked_step = lo + 1;
		out.blocker_gridno     = static_cast<INT16>(
			sel.sGridNo + out.first_blocked_step * DirIncrementer[dir]);
		out.label              = describeSightBlocker(out.blocker_gridno);
		return out;
	}

	// Body cube for an LOS query. Soldier targets get cube 3 (head),
	// matching the engine's stock "can S see this soldier" probes
	// (OppList.cc:4892, Soldier_Ani.cc:2678, Interface_Panels.cc:386
	// all probe at cube 3). Tile targets get cube 2 (chest), matching
	// the generic "is there a standing-target lane here" probe `sight`
	// and `findSightBlocker` use. Different cubes because the question
	// is different: for a soldier, "can I spot/shoot the person";
	// for a bare tile, "is the lane clear at chest height".
	INT8 losTargetCube(const Target& tgt)
	{
		return tgt.soldier ? 3 : 2;
	}

	INT8 losTargetLevel(const Target& tgt)
	{
		if (tgt.soldier) return tgt.soldier->bLevel;
		return static_cast<INT8>(gsInterfaceLevel);
	}

	// Walk a straight line from `from` to `to`, parametrised by
	// step/totalSteps. step=0 returns `from`, step=totalSteps returns
	// `to`. Integer-truncated lerp on col/row; for the binary search
	// over the line this is precise enough to identify the breaking
	// tile in nearly all cases — the worst case is a 1-tile-off ambig
	// at the diagonal seam, which `describeSightBlocker` papers over
	// by falling back to "obstruction" on an empty structure list.
	GridNo lineStep(GridNo from, GridNo to, int step, int totalSteps)
	{
		if (totalSteps <= 0) return from;
		const int from_col = from % WORLD_COLS;
		const int from_row = from / WORLD_COLS;
		const int to_col   = to   % WORLD_COLS;
		const int to_row   = to   / WORLD_COLS;
		const int col = from_col + (to_col - from_col) * step / totalSteps;
		const int row = from_row + (to_row - from_row) * step / totalSteps;
		return static_cast<GridNo>(col + row * WORLD_COLS);
	}

	struct LosBlocker
	{
		int        first_blocked_step;  // 1-indexed; step where the ray dies
		int        total_steps;         // PythSpacesAway(sel, target)
		GridNo     gridno;              // tile at first_blocked_step
		ST::string label;
	};

	// Binary-search the first blocked step along the line from
	// `sel.sGridNo` to `target`. Precondition: the caller has already
	// observed that the lane to `target` is blocked (otherwise the
	// search degenerates to "no blocker found" and we'd label nothing).
	// Same shape as findSightBlocker but operating on an arbitrary line
	// rather than an axial direction — the per-direction `sight` verb
	// can address its targets via DirIncrementer[d]; `los` cannot,
	// because the target may sit at any cartesian offset.
	LosBlocker findLineBlocker(const SOLDIERTYPE& sel,
	                           GridNo             target,
	                           INT8               target_level,
	                           INT8               target_cube)
	{
		LosBlocker out{};
		out.total_steps = PythSpacesAway(sel.sGridNo, target);
		if (out.total_steps <= 0)
		{
			out.first_blocked_step = 0;
			out.gridno = sel.sGridNo;
			out.label  = ST::string{"obstruction"};
			return out;
		}

		auto losAtStep = [&](int step) -> bool
		{
			const GridNo g = lineStep(sel.sGridNo, target,
			                          step, out.total_steps);
			const INT32  r = SoldierTo3DLocationLineOfSightTest(
				&sel, g, target_level, target_cube,
				/*sight cap*/ 255, /*aware*/ TRUE);
			return r > 0;
		};

		// Find the largest step that's still clear; the next one is
		// where the ray dies. step=0 is trivially clear (same tile).
		int lo = 0, hi = out.total_steps;
		while (lo < hi)
		{
			const int mid = (lo + hi + 1) / 2;
			if (losAtStep(mid)) lo = mid;
			else                hi = mid - 1;
		}

		out.first_blocked_step = lo + 1;
		out.gridno = lineStep(sel.sGridNo, target,
		                     out.first_blocked_step, out.total_steps);
		out.label  = describeSightBlocker(out.gridno);
		return out;
	}
}

void Cmd_Sight(const std::vector<std::string>&)
{
	SOLDIERTYPE* const sel = requireSelectedMerc();
	if (!sel) return;

	const UINT8 light    = LightTrueLevel(sel->sGridNo, sel->bLevel);
	const int   lightPct = (light < 16) ? gbLightSighting[0][light] : 0;
	const char* weather  = sightAffectingWeather(guiEnvWeather);
	const char* gear     = visionGearLabel(*sel);

	if (weather != nullptr)
	{
		Console_Println(ST::format(
			"sight: {} facing {}, {}. {} (light {}, {}%), {}. Vision gear: {}.",
			sel->name, directionWord(sel->bDirection), stanceWord(*sel),
			lightPhase(light), static_cast<int>(light), lightPct, weather, gear));
	}
	else
	{
		Console_Println(ST::format(
			"sight: {} facing {}, {}. {} (light {}, {}%). Vision gear: {}.",
			sel->name, directionWord(sel->bDirection), stanceWord(*sel),
			lightPhase(light), static_cast<int>(light), lightPct, gear));
	}

	// Flashbang blindness short-circuits DistanceVisible to 0 in every
	// direction (OppList.cc:899) -- skip the per-direction loop, it would
	// just print blind eight times.
	if (sel->bBlindedCounter > 0)
	{
		Console_Println(ST::format(
			"  Blinded ({} more turns); sight 0 in all directions.",
			static_cast<int>(sel->bBlindedCounter)));
		return;
	}

	// Effective sight per direction. DistanceVisible folds the facing-cone
	// lookup (gbLookDistance), the OUR_TEAM ANGLE->STRAIGHT promotion, light
	// scaling, weather, vision gear, running penalty, and roof bonus into
	// one number.
	INT16 ranges[NUM_WORLD_DIRECTIONS] = { 0 };
	for (UINT8 d = 0; d < NUM_WORLD_DIRECTIONS; ++d)
	{
		const INT16 syn = syntheticSubjectGridno(sel->sGridNo, d);
		ranges[d] = DistanceVisible(sel, sel->bDirection, d, syn, sel->bLevel);
	}

	// Per-direction LOS scan: for every direction with positive envelope,
	// binary-search outward for the first blocked tile and identify the
	// structure there. Eight directions * ~log2(envelope) LOS calls each;
	// SoldierTo3DLocationLineOfSightTest walks the ray once per call.
	SightBlocker blockers[NUM_WORLD_DIRECTIONS] = {};
	for (UINT8 d = 0; d < NUM_WORLD_DIRECTIONS; ++d)
	{
		if (ranges[d] > 0) blockers[d] = findSightBlocker(*sel, d, ranges[d]);
	}

	// Output in three passes so the reader gets a stable structure:
	// (1) Clear lanes, grouped by envelope range, longest first.
	// (2) Blocked lanes, each on its own line, clockwise from N.
	// (3) Blind directions (rear arc), grouped on one line.
	bool printed[NUM_WORLD_DIRECTIONS] = { false };

	// Pass 1: clear groups, longest range first.
	for (;;)
	{
		bool  found = false;
		INT16 best  = 0;
		for (UINT8 d = 0; d < NUM_WORLD_DIRECTIONS; ++d)
		{
			if (printed[d] || ranges[d] == 0 || blockers[d].blocked) continue;
			if (!found || ranges[d] > best) { best = ranges[d]; found = true; }
		}
		if (!found) break;

		ST::string dirs;
		for (UINT8 d = 0; d < NUM_WORLD_DIRECTIONS; ++d)
		{
			if (printed[d] || ranges[d] != best || blockers[d].blocked) continue;
			if (!dirs.empty()) dirs += ", ";
			dirs += directionWord(d);
			printed[d] = true;
		}
		Console_Println(ST::format("  {}: {} tiles.", dirs, static_cast<int>(best)));
	}

	// Pass 2: blocked lanes in clockwise order.
	for (UINT8 d = 0; d < NUM_WORLD_DIRECTIONS; ++d)
	{
		if (printed[d] || !blockers[d].blocked) continue;
		const ST::string off = formatOffset(sel->sGridNo, blockers[d].blocker_gridno);
		Console_Println(ST::format("  {}: {} at {} (envelope {} tiles).",
			directionWord(d), blockers[d].label, off,
			static_cast<int>(ranges[d])));
		printed[d] = true;
	}

	// Pass 3: blind directions.
	ST::string blindDirs;
	for (UINT8 d = 0; d < NUM_WORLD_DIRECTIONS; ++d)
	{
		if (printed[d] || ranges[d] != 0) continue;
		if (!blindDirs.empty()) blindDirs += ", ";
		blindDirs += directionWord(d);
	}
	if (!blindDirs.empty())
	{
		Console_Println(ST::format("  {}: blind.", blindDirs));
	}
}

void Cmd_Los(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const sel = requireSelectedMerc();
	if (!sel) return;
	if (args.size() < 2)
	{
		Console_Println("usage: los <target>");
		return;
	}

	Target tgt;
	ST::string err;
	if (parseTarget(args, 1, sel, tgt, err) == 0) { Console_Println(err); return; }

	// Soldier-target visibility gate. parseTarget already filters
	// name- and tag-based soldier addresses by team-known sightings,
	// but a tag can decay between command parse and execution; this
	// second check keeps the position from leaking via the error
	// message ("Ivan is at 12 E 3 N" — no, we don't say that).
	if (tgt.soldier && !ConsoleVis::IsKnownSoldier(*tgt.soldier))
	{
		Console_Println(ST::format(
			"{} is not currently visible to your team.", tgt.soldier->name));
		return;
	}

	if (tgt.gridno == sel->sGridNo)
	{
		Console_Println("Target is your own tile.");
		return;
	}

	const ST::string label = tgt.soldier ? ST::string(tgt.soldier->name)
	                                     : ST::string("target tile");
	const ST::string off   = formatOffset(sel->sGridNo, tgt.gridno);
	const int        dist  = PythSpacesAway(sel->sGridNo, tgt.gridno);
	const INT8       cube  = losTargetCube(tgt);
	const INT8       lvl   = losTargetLevel(tgt);

	// Sight envelope along the bearing to the target, so we can
	// distinguish "wall in the way" from "too far / too dark / wrong
	// direction for this facing". Synthetic subject gridno avoids the
	// same-tile short-circuit at OppList.cc:920 — same trick `sight`
	// uses for its per-direction calls.
	const UINT8 dir_to   = static_cast<UINT8>(
		GetDirectionToGridNoFromGridNo(sel->sGridNo, tgt.gridno));
	const INT16 syn      = syntheticSubjectGridno(sel->sGridNo, dir_to);
	const INT16 envelope = DistanceVisible(sel, sel->bDirection,
	                                       dir_to, syn, sel->bLevel);

	// Raw physical lane: sight cap 255 means "ignore the envelope" so
	// we can answer the structural question (is there a wall?)
	// independently of the perceptual one (would this merc actually
	// spot a stationary target there right now?). The envelope above
	// answers the latter.
	const INT32 r = SoldierTo3DLocationLineOfSightTest(
		sel, tgt.gridno, lvl, cube, /*sight cap*/ 255, /*aware*/ TRUE);

	if (r > 0)
	{
		if (dist <= envelope)
		{
			Console_Println(ST::format(
				"los: {} at {}, {} tiles. Clear LOS.", label, off, dist));
		}
		else
		{
			Console_Println(ST::format(
				"los: {} at {}, {} tiles. Lane clear, but beyond sight range "
				"(envelope {} tiles {}).",
				label, off, dist,
				static_cast<int>(envelope), directionWord(dir_to)));
		}
		return;
	}

	const LosBlocker blk    = findLineBlocker(*sel, tgt.gridno, lvl, cube);
	const ST::string blkOff = formatOffset(sel->sGridNo, blk.gridno);
	Console_Println(ST::format(
		"los: {} at {}, {} tiles. Blocked by {} at {} (step {} of {}).",
		label, off, dist, blk.label, blkOff,
		blk.first_blocked_step, blk.total_steps));
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
		s = requireSelectedMerc();
		if (!s) return;
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
	SOLDIERTYPE* const s = requireSelectedMerc();
	if (!s) return;
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
