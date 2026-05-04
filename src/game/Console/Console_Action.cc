#include "Console_Action.h"
#include "Console_Address.h"

#include "Console.h"

#include "Animation_Control.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "GameScreen.h"
#include "Handle_Items.h"
#include "Handle_UI.h"
#include "Interface.h"
#include "Isometric_Utils.h"
#include "ItemModel.h"
#include "Item_Types.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Points.h"
#include "Soldier_Control.h"
#include "WorldDef.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
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

	bool parseInt(const std::string& s, int& out)
	{
		if (s.empty()) return false;
		char* end = nullptr;
		long v = std::strtol(s.c_str(), &end, 10);
		if (end == s.c_str() || *end != '\0') return false;
		out = static_cast<int>(v);
		return true;
	}

	INT8 parseCompass(const std::string& tok)
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

	// Equivalent to Handle_UI.cc::MakeSoldierTurn (file-static there).
	// Routes through the same primitives the player UI uses on a "look at"
	// click: GetAPsToLook for cost, EnoughPoints for affordability, and
	// SendSoldierSetDesiredDirectionEvent to drive the pivot animation,
	// sight extension, and turning lock.
	bool turnSoldierToward(SOLDIERTYPE* s, INT8 facing)
	{
		if (facing == s->bDirection) return true;
		const INT16 apCost = GetAPsToLook(s);
		if (!EnoughPoints(s, apCost, 0, TRUE)) return false;
		if (s->fNoAPToFinishMove) SoldierGotoStationaryStance(s);
		SendSoldierSetDesiredDirectionEvent(s, static_cast<UINT16>(facing));
		s->bTurningFromUI = TRUE;
		return true;
	}
}

void Cmd_Select(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		Console_Println("usage: select <name>");
		return;
	}
	ST::string err;
	SOLDIERTYPE* const s = findTeammateByName(args[1], err);
	if (!s) { Console_Println(err); return; }

	SetSelectedMan(s);
	Console_Println(ST::format("Selected: {}.", s->name));
}

void Cmd_EndTurn(const std::vector<std::string>&)
{
	gfBeginEndTurn = TRUE;
	Console_Println("End-turn queued.");
}

void Cmd_Stance(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		Console_Println("usage: stance <p|c|s>");
		return;
	}
	if (!GetSelectedMan())
	{
		Console_Println("No merc selected.");
		return;
	}

	UINT8 newStance;
	const char ch = static_cast<char>(std::tolower(static_cast<unsigned char>(args[1][0])));
	switch (ch)
	{
		case 'p': newStance = ANIM_PRONE;  break;
		case 'c': newStance = ANIM_CROUCH; break;
		case 's': newStance = ANIM_STAND;  break;
		default:
			Console_Println(ST::format("unknown stance '{}' (use p, c, or s)", args[1]));
			return;
	}

	HandleStanceChangeFromUIKeys(newStance);
	Console_Println("Stance change queued.");
}

void Cmd_Turn(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel)
	{
		Console_Println("No merc selected.");
		return;
	}
	if (args.size() < 2)
	{
		Console_Println("usage: turn <n|ne|e|...|nw> | turn <name> | turn <col,row>");
		return;
	}

	// `turn <compass>` is a self-contained facing change — no steps. The
	// general address parser would demand a step count for any direction
	// word, so we special-case compass-only here before delegating.
	const INT8 compass = parseCompass(args[1]);
	INT8 facing;
	ST::string label;
	if (compass >= 0)
	{
		facing = compass;
		label  = args[1].c_str();
	}
	else
	{
		Target tgt;
		ST::string err;
		if (parseTarget(args, 1, sel, tgt, err) == 0)
		{
			Console_Println(err);
			return;
		}
		if (tgt.gridno == sel->sGridNo)
		{
			Console_Println("Target is on the same tile; nothing to face.");
			return;
		}
		facing = static_cast<INT8>(GetDirectionToGridNoFromGridNo(sel->sGridNo, tgt.gridno));
		if (tgt.soldier)
		{
			label = tgt.soldier->name;
		}
		else
		{
			const INT16 dist = PythSpacesAway(sel->sGridNo, tgt.gridno);
			const UINT8 dir  = static_cast<UINT8>(GetDirectionToGridNoFromGridNo(sel->sGridNo, tgt.gridno));
			const char* dw;
			switch (dir)
			{
				case NORTH: dw = "N"; break; case NORTHEAST: dw = "NE"; break;
				case EAST:  dw = "E"; break; case SOUTHEAST: dw = "SE"; break;
				case SOUTH: dw = "S"; break; case SOUTHWEST: dw = "SW"; break;
				case WEST:  dw = "W"; break; case NORTHWEST: dw = "NW"; break;
				default:    dw = "?"; break;
			}
			label = ST::format("{} tiles {}", dist, dw);
		}
	}

	if (turnSoldierToward(sel, facing))
	{
		Console_Println(ST::format("Turning to face {}.", label));
	}
	else
	{
		Console_Println("Not enough APs to turn.");
	}
}

void Cmd_Move(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel)
	{
		Console_Println("No merc selected.");
		return;
	}
	if (args.size() < 2)
	{
		Console_Println("usage: move <name> | move <dir> <steps> | move <col,row>");
		return;
	}

	Target tgt;
	ST::string err;
	if (parseTarget(args, 1, sel, tgt, err) == 0)
	{
		Console_Println(err);
		return;
	}
	if (tgt.gridno == sel->sGridNo)
	{
		Console_Println("Already there.");
		return;
	}

	EVENT_GetNewSoldierPath(sel, static_cast<UINT16>(tgt.gridno), sel->usUIMovementMode);

	if (tgt.soldier)
	{
		Console_Println(ST::format("Moving toward {}.", tgt.soldier->name));
	}
	else
	{
		Console_Println("Moving.");
	}
}

void Cmd_Fire(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel)
	{
		Console_Println("No merc selected.");
		return;
	}
	if (args.size() < 2)
	{
		Console_Println("usage: fire <target> [aim 0-4]");
		return;
	}

	Target tgt;
	ST::string err;
	const int consumed = parseTarget(args, 1, sel, tgt, err);
	if (consumed == 0) { Console_Println(err); return; }

	int aim = 0;
	const std::size_t i = static_cast<std::size_t>(1 + consumed);
	if (i < args.size() && args[i] == "aim")
	{
		if (i + 1 >= args.size() || !parseInt(args[i + 1], aim) || aim < 0 || aim > 4)
		{
			Console_Println("aim must be 0..4 (0 = snap, 4 = full).");
			return;
		}
	}
	else if (i < args.size())
	{
		Console_Println(ST::format("unexpected token '{}' (try 'aim N')", args[i]));
		return;
	}

	// Friendly-fire guard: the click-to-fire UI pops a confirm dialog when
	// targeting our own team or militia (Handle_UI.cc:2286). The console
	// has no dialog, so for now refuse outright; a future explicit
	// override verb can revisit.
	if (tgt.soldier &&
	    (tgt.soldier->bTeam == OUR_TEAM || tgt.soldier->bTeam == MILITIA_TEAM))
	{
		Console_Println(ST::format(
			"Refusing: {} is on our side.", tgt.soldier->name));
		return;
	}

	const UINT16 weapon = sel->inv[HANDPOS].usItem;
	if (weapon == 0)
	{
		Console_Println("No weapon in main hand.");
		return;
	}
	const ItemModel* const item = GCM->getItem(weapon);
	if (item->getItemClass() != IC_GUN)
	{
		Console_Println(ST::format("Held item ({}) is not a gun.", item->getName()));
		return;
	}

	// Encode aim into the soldier's UI-aim fields the way the click
	// handler does (Handle_UI.cc:2140). bShownAimTime is in 0..8 steps
	// (REFINE_AIM_1..5); bAimTime is bShownAimTime/2. The MID odd values
	// are animation interstitials and we never want them.
	sel->bShownAimTime = static_cast<INT8>(aim * 2);
	sel->bAimTime      = static_cast<INT8>(aim);

	const INT8 level = static_cast<INT8>(gsInterfaceLevel);
	const ItemHandleResult r = HandleItem(sel, tgt.gridno, level, weapon, TRUE);

	const ST::string who = tgt.soldier ? tgt.soldier->name : ST::string("target");
	switch (r)
	{
		case ITEM_HANDLE_OK:
			Console_Println(aim == 0
				? ST::format("Firing at {}.",        who)
				: ST::format("Firing at {} (aim {}).", who, aim));
			break;
		case ITEM_HANDLE_RELOADING:           Console_Println("Reloading first.");              break;
		case ITEM_HANDLE_UNCONSCIOUS:         Console_Println("Merc is unconscious.");          break;
		case ITEM_HANDLE_NOAPS:               Console_Println("Not enough APs to fire.");       break;
		case ITEM_HANDLE_NOAMMO:              Console_Println("Out of ammo.");                  break;
		case ITEM_HANDLE_CANNOT_GETTO_LOCATION:Console_Println("Can't reach a firing position.");break;
		case ITEM_HANDLE_BROKEN:              Console_Println("Weapon is broken or jammed.");   break;
		case ITEM_HANDLE_NOROOM:              Console_Println("Can't fire from here.");         break;
		case ITEM_HANDLE_REFUSAL:             Console_Println("Merc refused.");                 break;
		default:                              Console_Println("Fire issued.");                  break;
	}
}
