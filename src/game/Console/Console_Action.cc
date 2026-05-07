#include "Console_Action.h"
#include "Console_Address.h"
#include "Console_Query.h"

#include "Console.h"

#include "ScreenIDs.h"

#include "AI.h"
#include "Animation_Control.h"
#include "Arms_Dealer_Init.h"
#include "CalibreModel.h"
#include "Civ_Quotes.h"
#include "ContentManager.h"
#include "Dialogue_Control.h"
#include "Explosion_Control.h"
#include "ExplosiveModel.h"
#include "Faces.h"
#include "GameInstance.h"
#include "GameScreen.h"
#include "Handle_Doors.h"
#include "Handle_Items.h"
#include "Handle_UI.h"
#include "Interactive_Tiles.h"
#include "Interface.h"
#include "Interface_Control.h"
#include "Interface_Dialogue.h"
#include "Interface_Panels.h"
#include "Isometric_Utils.h"
#include "ItemModel.h"
#include "Item_Types.h"
#include "Items.h"
#include "JAScreens.h"
#include "LOS.h"
#include "Map_Information.h"
#include "MercProfile.h"
#include "NPC.h"
#include "OppList.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "PathAI.h"
#include "Points.h"
#include "QArray.h"
#include "ShopKeeper_Interface.h"
#include "Soldier.h"
#include "Soldier_Add.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "Soldier_Profile.h"
#include "StrategicMap.h"
#include "Strategic_Movement.h"
#include "Squads.h"
#include "Structure.h"
#include "Structure_Internals.h"
#include "WeaponModels.h"
#include "Weapons.h"
#include "World_Items.h"
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

	// On the tactical screen, route through the engine's SelectSoldier so
	// squad / panel / level sync, the merc attn voice line, and the
	// "is unavailable" feedback all fire — same path as a keyboard F-key.
	// SelectSoldier early-returns on LAPTOP/MAP screens (Overhead.cc:1986),
	// so for those we keep the bare setter; the merc summary still prints.
	if (guiCurrentScreen == GAME_SCREEN)
	{
		SelectSoldier(s, SELSOLDIER_FROM_UI | SELSOLDIER_ACKNOWLEDGE | SELSOLDIER_FORCE_RESELECT);
		if (GetSelectedMan() != s) return; // SelectSoldier rejected; it already screen-msg'd why.
	}
	else
	{
		SetSelectedMan(s);
	}

	PrintMercSummary(*s);
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
			const INT16 dist = SpacesAway(sel->sGridNo, tgt.gridno);
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

namespace
{
	// Mirrors the realtime branch of UIHandleCMoveMerc (Handle_UI.cc:1700):
	// in real time, the click handler refreshes usUIMovementMode based on
	// current stance every time a move is issued (prone→crawl, crouch→swat,
	// stand→walk-or-run-by-fast-flag). Combat preserves whatever was set so
	// AP cost resolves against the chosen mode.
	void refreshMoveModeForRealtime(SOLDIERTYPE* s)
	{
		if (gTacticalStatus.uiFlags & INCOMBAT) return;
		s->usUIMovementMode = GetMoveStateBasedOnStance(
			s, gAnimControl[s->usAnimState].ubEndHeight);
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
		Console_Println("usage: move <target> [run]");
		Console_Println("  targets: <name> | <dir> <steps> | <col,row>");
		return;
	}

	Target tgt;
	ST::string err;
	const int consumed = parseTarget(args, 1, sel, tgt, err);
	if (consumed == 0)
	{
		Console_Println(err);
		return;
	}

	// Optional trailing "run" flag. Per-action like the engine's shift+click,
	// not a persistent mode — pace only matters for this one move.
	bool wantRun = false;
	const std::size_t after = static_cast<std::size_t>(1 + consumed);
	if (after < args.size())
	{
		if (args[after] == "run")
		{
			wantRun = true;
			if (after + 1 < args.size())
			{
				Console_Println(ST::format("unexpected token '{}' after 'run'", args[after + 1]));
				return;
			}
		}
		else
		{
			Console_Println(ST::format("unexpected token '{}' (try 'run')", args[after]));
			return;
		}
	}

	if (tgt.gridno == sel->sGridNo)
	{
		Console_Println("Already there.");
		return;
	}

	refreshMoveModeForRealtime(sel);
	if (wantRun)
	{
		// Mirror Handle_UI.cc:1417-1425 — fast bit applies to any stance,
		// but only WALKING promotes to RUNNING. SWAT and CRAWL keep their
		// mode (the engine can't actually speed them up; the bit is a no-op
		// there but harmless, and matches what the run-menu button does).
		sel->fUIMovementFast = TRUE;
		if (sel->usUIMovementMode == WALKING)
		{
			sel->usUIMovementMode = RUNNING;
		}
	}
	else
	{
		sel->fUIMovementFast = FALSE;
	}
	Soldier{sel}.removePendingAction();

	// If the destination tile carries an openable structure (door, locker,
	// switch), don't try to walk onto it — find an adjacent approach tile
	// and queue the open/interact for arrival. Mirrors Handle_UI.cc:1708.
	INT16      destGridNo  = tgt.gridno;
	STRUCTURE* intStruct   = FindStructure(tgt.gridno, STRUCTURE_OPENABLE);
	UINT8      intDir      = sel->bDirection;
	bool       willInteract = false;

	if (intStruct)
	{
		const INT16 approach = (intStruct->fFlags & (STRUCTURE_ANYDOOR | STRUCTURE_SWITCH))
			? FindAdjacentGridExAdvanced(sel, *intStruct, tgt.gridno, &intDir)
			: FindAdjacentGridEx(sel, tgt.gridno, &intDir, nullptr, FALSE, TRUE);

		if (approach == -1)
		{
			Console_Println("No path to that structure.");
			return;
		}
		destGridNo   = approach;
		willInteract = true;

		// Already adjacent — interact immediately, no path needed. Mirrors
		// the same-tile shortcut in Handle_UI.cc:1729.
		if (sel->sGridNo == approach)
		{
			StartInteractiveObject(tgt.gridno, *intStruct, *sel, intDir);
			InteractWithOpenableStruct(*sel, *intStruct, intDir);
			Console_Println("Interacting.");
			return;
		}
	}

	EVENT_InternalGetNewSoldierPath(sel, static_cast<UINT16>(destGridNo),
	                                sel->usUIMovementMode, TRUE,
	                                sel->fNoAPToFinishMove);

	if (willInteract)
	{
		// Pending action fires when the merc reaches the approach tile.
		StartInteractiveObject(tgt.gridno, *intStruct, *sel, intDir);
		Console_Println(wantRun ? "Running to interact." : "Approaching to interact.");
		return;
	}

	const char* const verb = wantRun ? "Running" : "Moving";
	if (tgt.soldier)
	{
		Console_Println(ST::format("{} toward {}.", verb, tgt.soldier->name));
	}
	else
	{
		Console_Println(ST::format("{}.", verb));
	}
}

void Cmd_MoveAll(const std::vector<std::string>& args)
{
	if (gTacticalStatus.uiFlags & INCOMBAT)
	{
		Console_Println("move-all only works in real time. In combat, move each merc individually.");
		return;
	}

	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel)
	{
		Console_Println("No merc selected to anchor 'move-all' on.");
		return;
	}
	if (args.size() < 2)
	{
		Console_Println("usage: move-all <target> [run]");
		Console_Println("  targets: <name> | <dir> <steps> | <col,row>");
		return;
	}

	Target tgt;
	ST::string err;
	const int consumed = parseTarget(args, 1, sel, tgt, err);
	if (consumed == 0)
	{
		Console_Println(err);
		return;
	}

	bool wantRun = false;
	const std::size_t after = static_cast<std::size_t>(1 + consumed);
	if (after < args.size())
	{
		if (args[after] == "run")
		{
			wantRun = true;
			if (after + 1 < args.size())
			{
				Console_Println(ST::format("unexpected token '{}' after 'run'", args[after + 1]));
				return;
			}
		}
		else
		{
			Console_Println(ST::format("unexpected token '{}' (try 'run')", args[after]));
			return;
		}
	}

	const INT32 squad = CurrentSquad();
	if (squad == NO_CURRENT_SQUAD)
	{
		Console_Println("No active squad.");
		return;
	}

	// Mirrors the group-move branch of UIHandleCMoveMerc (Handle_UI.cc:1640).
	// Path-through-people lets squadmates resolve through one another instead
	// of blocking on the leader's tile when they all start clustered.
	gfGetNewPathThroughPeople = TRUE;

	int moved = 0;
	int skipped = 0;
	FOR_EACH_IN_TEAM(s, OUR_TEAM)
	{
		if (!OK_CONTROLLABLE_MERC(s))     { ++skipped; continue; }
		if (s->bAssignment != squad)      continue;
		if (s->fMercAsleep)               { ++skipped; continue; }
		if (s->uiStatusFlags & SOLDIER_ROBOT && !CanRobotBeControlled(s))
		                                  { ++skipped; continue; }

		AdjustNoAPToFinishMove(s, FALSE);
		// Mirror the all-move-fast branch in Handle_UI.cc:1657-1666: when the
		// player picks the run variant, every squad member gets the fast bit
		// and standers promote to RUNNING. Crouched/prone squadmates keep
		// their movement mode (the fast bit is a no-op there but matches
		// engine behavior).
		s->usUIMovementMode = GetMoveStateBasedOnStance(
			s, gAnimControl[s->usAnimState].ubEndHeight);
		if (wantRun)
		{
			s->fUIMovementFast = TRUE;
			if (s->usUIMovementMode == WALKING) s->usUIMovementMode = RUNNING;
		}
		else
		{
			s->fUIMovementFast = FALSE;
		}

		Soldier{s}.removePendingAction();

		if (EVENT_InternalGetNewSoldierPath(s, static_cast<UINT16>(tgt.gridno),
		                                    s->usUIMovementMode, TRUE, FALSE))
		{
			++moved;
		}
		else
		{
			++skipped;
		}
	}

	gfGetNewPathThroughPeople = FALSE;

	Console_Println(ST::format("Group {} queued: {} {}, {} skipped.",
	                           wantRun ? "run" : "move",
	                           moved,
	                           wantRun ? "running" : "moving",
	                           skipped));
}

void Cmd_Climb(const std::vector<std::string>&)
{
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel) { Console_Println("No merc selected."); return; }

	// Mirror BtnClimbCallback (Interface_Panels.cc:2086): try down → up →
	// fence in priority order. Same dispatch the panel "Climb" button uses,
	// so 'climb' is the one verb for roof access (both directions) and
	// fence-hopping. The engine's Find* helpers auto-detect direction.
	UINT8 dir;

	if (FindLowerLevel(sel, &dir))
	{
		const INT8 ap = GetAPsToClimbRoof(sel, TRUE);
		if (!EnoughPoints(sel, ap, 0, TRUE))
		{
			Console_Println(ST::format(
				"{} needs {} AP to climb down but has {}.",
				sel->name, ap, sel->bActionPoints));
			return;
		}
		BeginSoldierClimbDownRoof(sel);
		Console_Println("Climbing down.");
		return;
	}

	if (FindHigherLevel(sel, &dir))
	{
		const INT8 ap = GetAPsToClimbRoof(sel, FALSE);
		if (!EnoughPoints(sel, ap, 0, TRUE))
		{
			Console_Println(ST::format(
				"{} needs {} AP to climb up but has {}.",
				sel->name, ap, sel->bActionPoints));
			return;
		}
		BeginSoldierClimbUpRoof(sel);
		Console_Println("Climbing up.");
		return;
	}

	if (FindFenceJumpDirection(sel, &dir))
	{
		const INT8 ap = GetAPsToJumpFence(sel);
		if (!EnoughPoints(sel, ap, 0, TRUE))
		{
			Console_Println(ST::format(
				"{} needs {} AP to hop the fence but has {}.",
				sel->name, ap, sel->bActionPoints));
			return;
		}
		BeginSoldierClimbFence(sel);
		Console_Println("Hopping fence.");
		return;
	}

	Console_Println(ST::format(
		"{} has no roof access or fence to climb here.", sel->name));
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

void Cmd_Reload(const std::vector<std::string>& args)
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

	OBJECTTYPE& hand = s->inv[HANDPOS];
	if (hand.usItem == 0)
	{
		Console_Println(ST::format("{} has no weapon in hand.", s->name));
		return;
	}
	const ItemModel* const item = GCM->getItem(hand.usItem);
	if (!item->isGun())
	{
		Console_Println(ST::format("{}'s held item ({}) is not a gun.",
		                           s->name, item->getName()));
		return;
	}
	if (hand.usItem == ROCKET_LAUNCHER)
	{
		Console_Println("The LAW cannot be reloaded.");
		return;
	}

	// Mirror what AutoReload would search for, so we can give a clear
	// "no compatible ammo" message before any AP is spent. AutoReload's
	// own slot search uses FindAmmoToReload (Items.cc:1429), so calling
	// the same predicate here keeps the verdicts consistent.
	if (FindAmmoToReload(s, HANDPOS, NO_SLOT) == NO_SLOT)
	{
		const WeaponModel* const w = GCM->getWeapon(hand.usItem);
		const ST::string cal = (w && w->calibre)
			? w->calibre->getName() : ST::string("compatible");
		Console_Println(ST::format("No {} ammo on this merc.", cal));
		return;
	}

	// AP preview — only relevant in combat. Outside combat the engine
	// charges nothing for reloads (Items.cc:1096-1104), so don't gate the
	// command on the merc's current AP pool when the turn-based flag is off.
	const bool inCombat = (gTacticalStatus.uiFlags & INCOMBAT) != 0;
	if (inCombat)
	{
		const INT8 apCost = GetAPsToAutoReload(s);
		if (apCost > s->bActionPoints)
		{
			Console_Println(ST::format(
				"{} needs {} AP to reload but has {}.",
				s->name, apCost, s->bActionPoints));
			return;
		}
	}

	// Snapshot before/after for an unambiguous "0 → 30 shots" report.
	// AutoReload itself drives ScreenMsg/sound side effects via ReloadGun,
	// so the player still hears the chamber-action sound and sees any
	// engine-issued status text.
	const UINT8 shotsBefore = hand.ubGunShotsLeft;

	if (!AutoReload(s))
	{
		Console_Println("Reload failed.");
		return;
	}

	const UINT8 shotsAfter = hand.ubGunShotsLeft;
	Console_Println(ST::format("Reloaded {}: {} → {} shots.",
	                           item->getName(), shotsBefore, shotsAfter));
	// Note: if the merc was dual-wielding the off-hand gun is also reloaded;
	// AutoReload handles that itself. Inventory readout will reflect both.
}

void Cmd_Pickup(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel) { Console_Println("No merc selected."); return; }

	// No arg: pick up at the merc's current tile (walked-onto-it case).
	// Otherwise parse a target tile (compass+steps, col,row, or a known soldier
	// — handy for "the tile that hostile is on" after they drop).
	INT16 gridno = sel->sGridNo;
	if (args.size() >= 2)
	{
		Target tgt;
		ST::string err;
		if (parseTarget(args, 1, sel, tgt, err) == 0) { Console_Println(err); return; }
		gridno = tgt.gridno;
	}

	if (AM_AN_EPC(sel))
	{
		Console_Println(ST::format("{} is an escort and refuses to handle items.", sel->name));
		return;
	}

	// Pre-check the item pool so we can give a clear "nothing here" message
	// instead of the engine's silent BATTLE_SOUND_NOTHING grunt.
	ITEM_POOL const* const pool = GetItemPool(gridno, sel->bLevel);
	if (!pool || !IsItemPoolVisible(pool))
	{
		Console_Println("No visible items at that tile.");
		return;
	}

	// Mirror the click handler at Turn_Based_Input.cc:3235-3238 and at
	// Handle_UI.cc HandleMoveModeInteractiveClick:3326-3344. UIOkForItemPickup
	// gates on path + AP and (on success) deducts the AP cost; SoldierPickupItem
	// then sets up the pending pickup action and pathfinds.
	//
	// We pass ITEM_PICKUP_ACTION_ALL / ITEM_IGNORE_Z_LEVEL — same combo
	// Interface_Dialogue.cc:3001 uses for the body-search "take all" button.
	// This deliberately bypasses the multi-item pickup popup
	// (Handle_Items.cc:1350 InitializeItemPickupMenu), which is a graphical
	// menu we can't drive from the console.
	if (!UIOkForItemPickup(sel, gridno))
	{
		Console_Println("Can't pick up: no path or not enough APs.");
		return;
	}

	SoldierPickupItem(sel, ITEM_PICKUP_ACTION_ALL, gridno, ITEM_IGNORE_Z_LEVEL);

	if (sel->sGridNo == gridno)
	{
		Console_Println("Picking up.");
	}
	else
	{
		const INT16 dist = SpacesAway(sel->sGridNo, gridno);
		Console_Println(ST::format("Approaching {} tile{} to pick up.",
		                           dist, dist == 1 ? "" : "s"));
	}
}

void Cmd_Bandage(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel) { Console_Println("No merc selected."); return; }

	// Default target = self (most common case: bandage yourself).
	SOLDIERTYPE* patient = sel;
	if (args.size() >= 2)
	{
		Target tgt;
		ST::string err;
		if (parseTarget(args, 1, sel, tgt, err) == 0) { Console_Println(err); return; }
		if (!tgt.soldier)
		{
			Console_Println("Bandage target must be a soldier (name, mN, eN, or cN).");
			return;
		}
		patient = tgt.soldier;
	}

	// Refuse hostiles up front. EVENT_SoldierBeginFirstAid would refuse too
	// (Soldier_Control.cc:6923) but we can give a clear, non-engine message.
	if (patient->bTeam != OUR_TEAM && !patient->bNeutral)
	{
		Console_Println(ST::format("Cannot bandage {}: hostile.", patient->name));
		return;
	}

	if (patient->bBleeding == 0 && patient->bLife >= patient->bLifeMax)
	{
		Console_Println(ST::format("{} doesn't need bandaging.", patient->name));
		return;
	}

	// FIRSTAIDKIT and MEDICKIT both share IC_MEDKIT class. The engine
	// treats them identically here — first aid kits stop bleeding faster
	// per "kit point", medical kits also restore life. Either works.
	const INT8 kitSlot = FindObjClass(sel, IC_MEDKIT);
	if (kitSlot == NO_SLOT)
	{
		Console_Println(ST::format(
			"{} has no first aid kit or medical kit.", sel->name));
		return;
	}

	const UINT16 kitItem = sel->inv[kitSlot].usItem;

	// HandlePlayerServices reads HANDPOS directly while the GIVING_AID
	// animation plays (Overhead.cc:3841 OBJECTTYPE& in_hand = s.inv[HANDPOS]),
	// so the kit must be in hand for the bandage to actually consume points
	// and apply healing. Mirror the AI's swap-in pattern from Medical.cc:402.
	//
	// Caveat: outside autobandage mode the engine does *not* swap the
	// previous hand item back when the bandage finishes. The displaced
	// weapon ends up where the kit used to be — call `inventory` to see
	// the new layout, and re-equip with whatever swap verbs we add later.
	if (kitSlot != HANDPOS)
	{
		SwapObjs(&sel->inv[HANDPOS], &sel->inv[kitSlot]);
		Console_Println(ST::format(
			"Moved {} into main hand.", GCM->getItem(kitItem)->getName()));
	}

	const INT8 level = static_cast<INT8>(gsInterfaceLevel);
	const ItemHandleResult r = HandleItem(sel, patient->sGridNo, level,
	                                       kitItem, TRUE);

	const ST::string who = (patient == sel) ? ST::string("self") : patient->name;
	switch (r)
	{
		case ITEM_HANDLE_OK:
			Console_Println(ST::format("Bandaging {}.", who));
			break;
		case ITEM_HANDLE_NOAPS:
			Console_Println("Not enough APs to begin bandaging.");
			break;
		case ITEM_HANDLE_CANNOT_GETTO_LOCATION:
			Console_Println(ST::format("Can't reach {}.", who));
			break;
		case ITEM_HANDLE_REFUSAL:
			Console_Println(ST::format("{} refused first aid.", who));
			break;
		case ITEM_HANDLE_UNCONSCIOUS:
			Console_Println(ST::format("{} is unconscious.", sel->name));
			break;
		default:
			Console_Println(ST::format("Bandage issued (code {}).",
			                           static_cast<int>(r)));
			break;
	}
}

namespace
{
	// Combat-only AP cost gate. Real-time the engine doesn't charge for
	// inventory shuffling, and `Ctrl+Q` (the keyboard swap-hands binding)
	// is free even in combat. We mirror that: free in real time, free for
	// hand swap, AP_PICKUP_ITEM (3) for combat drops and combat slot moves.
	bool deductIfCombat(SOLDIERTYPE* s, INT8 ap)
	{
		if (!(gTacticalStatus.uiFlags & INCOMBAT)) return true;
		if (!EnoughPoints(s, ap, 0, TRUE))         return false;
		DeductPoints(s, ap, 0);
		return true;
	}
}

void Cmd_SwapHands(const std::vector<std::string>&)
{
	SOLDIERTYPE* const s = GetSelectedMan();
	if (!s) { Console_Println("No merc selected."); return; }
	if (AM_A_ROBOT(s))
	{
		Console_Println(ST::format("{} cannot manipulate items.", s->name));
		return;
	}

	const UINT16 oldHand = s->inv[HANDPOS].usItem;
	const UINT16 oldOff  = s->inv[SECONDHANDPOS].usItem;
	if (oldHand == NOTHING && oldOff == NOTHING)
	{
		Console_Println(ST::format("{}'s hands are both empty.", s->name));
		return;
	}

	// Mirror the keyboard binding's path (Turn_Based_Input.cc:3559).
	// SwapHandItems handles the bare-hand promotion case (off → main)
	// and the two-handed off-hand displacement case the keyboard path
	// special-cases inline.
	SwapHandItems(s);
	ReLoadSoldierAnimationDueToHandItemChange(s, oldHand, s->inv[HANDPOS].usItem);
	fInterfacePanelDirty = DIRTYLEVEL2;

	const ItemModel* const newItem = GCM->getItem(s->inv[HANDPOS].usItem);
	const ItemModel* const oldItem = GCM->getItem(oldHand);
	if (s->inv[HANDPOS].usItem == NOTHING)
	{
		Console_Println(ST::format("{} unreadied {}.", s->name, oldItem->getName()));
	}
	else if (oldHand == NOTHING)
	{
		Console_Println(ST::format("{} readied {}.", s->name, newItem->getName()));
	}
	else
	{
		Console_Println(ST::format("{} swapped {} for {}.",
		                           s->name, oldItem->getName(), newItem->getName()));
	}
}

void Cmd_Swap(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const s = GetSelectedMan();
	if (!s) { Console_Println("No merc selected."); return; }
	if (AM_A_ROBOT(s))
	{
		Console_Println(ST::format("{} cannot manipulate items.", s->name));
		return;
	}
	if (args.size() < 3)
	{
		Console_Println("usage: swap <slot> <slot>  (e.g. 'swap s8 s1')");
		return;
	}

	const INT8 a = parseSlotTag(args[1]);
	const INT8 b = parseSlotTag(args[2]);
	if (a < 0 || b < 0)
	{
		Console_Println("slots must be s1..s19; see 'inventory'.");
		return;
	}
	if (a == b)
	{
		Console_Println("Source and destination are the same slot.");
		return;
	}

	OBJECTTYPE& objA = s->inv[a];
	OBJECTTYPE& objB = s->inv[b];
	if (objA.usItem == NOTHING && objB.usItem == NOTHING)
	{
		Console_Println("Both slots are empty.");
		return;
	}

	// Hand <-> hand goes through SwapHandItems instead of raw SwapObjs:
	// the helper relocates the main-hand item into a pocket if the
	// off-hand item is two-handed, where SwapObjs would fail silently.
	if ((a == HANDPOS && b == SECONDHANDPOS) ||
	    (a == SECONDHANDPOS && b == HANDPOS))
	{
		Cmd_SwapHands({});
		return;
	}

	// Validate placement in both directions. CanItemFitInPosition gates
	// armor-slot affinity (helmet must accept a helmet, etc.), face-slot
	// affinity, and per-pocket size. fDoingPlacement=FALSE so the
	// validator doesn't side-effect; we only swap once both directions
	// are clean.
	if (objA.usItem != NOTHING && !CanItemFitInPosition(s, &objA, b, FALSE))
	{
		Console_Println(ST::format(
			"{} doesn't fit in {} ({}).",
			GCM->getItem(objA.usItem)->getName(), slotTag(b), slotLabel(b)));
		return;
	}
	if (objB.usItem != NOTHING && !CanItemFitInPosition(s, &objB, a, FALSE))
	{
		Console_Println(ST::format(
			"{} doesn't fit in {} ({}).",
			GCM->getItem(objB.usItem)->getName(), slotTag(a), slotLabel(a)));
		return;
	}

	if (!deductIfCombat(s, AP_PICKUP_ITEM))
	{
		Console_Println(ST::format(
			"{} needs {} AP to move items but has {}.",
			s->name, AP_PICKUP_ITEM, s->bActionPoints));
		return;
	}

	const UINT16 oldHand = s->inv[HANDPOS].usItem;
	SwapObjs(&objA, &objB);

	// If a hand changed contents, refresh soldier animation so ready-time
	// and held-item visuals update — same call the inventory drag path
	// makes in Interface_Panels.cc:3791.
	const bool handTouched = (a == HANDPOS || b == HANDPOS);
	if (handTouched)
	{
		ReLoadSoldierAnimationDueToHandItemChange(
			s, oldHand, s->inv[HANDPOS].usItem);
	}
	DirtyMercPanelInterface(s, DIRTYLEVEL2);
	fInterfacePanelDirty = DIRTYLEVEL2;

	Console_Println(ST::format("Swapped {} ({}) and {} ({}).",
	                           slotTag(a), slotLabel(a),
	                           slotTag(b), slotLabel(b)));
}

void Cmd_Drop(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const s = GetSelectedMan();
	if (!s) { Console_Println("No merc selected."); return; }
	if (AM_AN_EPC(s))
	{
		Console_Println(ST::format("{} is an escort and refuses to handle items.", s->name));
		return;
	}
	if (args.size() < 2)
	{
		Console_Println("usage: drop <slot>  (slot is s1..s19; see 'inventory')");
		return;
	}

	const INT8 slot = parseSlotTag(args[1]);
	if (slot < 0)
	{
		Console_Println("slot must be s1..s19; see 'inventory'.");
		return;
	}
	OBJECTTYPE& src = s->inv[slot];
	if (src.usItem == NOTHING)
	{
		Console_Println(ST::format("{} {}: empty.", slotTag(slot), slotLabel(slot)));
		return;
	}

	// OBJECT_UNDROPPABLE marks story / quest items the engine refuses to
	// drop (Item_Types.h:55). Honour it before any AP charge.
	if (src.fFlags & OBJECT_UNDROPPABLE)
	{
		Console_Println(ST::format(
			"{} cannot be dropped.", GCM->getItem(src.usItem)->getName()));
		return;
	}

	if (!deductIfCombat(s, AP_PICKUP_ITEM))
	{
		Console_Println(ST::format(
			"{} needs {} AP to drop items but has {}.",
			s->name, AP_PICKUP_ITEM, s->bActionPoints));
		return;
	}

	// We skip SoldierDropItem's animation path because it relies on the
	// cursor pTempObject flow (Handle_Items.cc:1097); the crouch/prone
	// branch in HandleSoldierThrowItem (Handle_Items.cc:1037) already
	// uses AddItemToPool directly when there's no animation to play, so
	// taking the same shortcut here is consistent with engine practice.
	// NotifySoldiersToLookforItems wakes nearby AI to spot the drop.
	OBJECTTYPE temp = src;
	const UINT16 wasInHand = (slot == HANDPOS) ? src.usItem : NOTHING;
	const ST::string droppedName = GCM->getItem(src.usItem)->getName();

	DeleteObj(&src);
	AddItemToPool(s->sGridNo, &temp, VISIBLE, s->bLevel, 0, -1);
	NotifySoldiersToLookforItems();

	if (wasInHand != NOTHING)
	{
		ReLoadSoldierAnimationDueToHandItemChange(s, wasInHand, NOTHING);
	}
	DirtyMercPanelInterface(s, DIRTYLEVEL2);
	fInterfacePanelDirty = DIRTYLEVEL2;

	Console_Println(ST::format("Dropped {} from {} ({}).",
	                           droppedName,
	                           slotTag(slot), slotLabel(slot)));
}

void Cmd_Give(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const giver = GetSelectedMan();
	if (!giver) { Console_Println("No merc selected."); return; }
	if (AM_A_ROBOT(giver))
	{
		Console_Println(ST::format("{} cannot hand items to anyone.", giver->name));
		return;
	}
	if (AM_AN_EPC(giver))
	{
		Console_Println(ST::format("{} is an escort and refuses to handle items.", giver->name));
		return;
	}
	if (args.size() < 3)
	{
		Console_Println("usage: give <slot> <target>  (target is a name, mN/eN/cN, or col,row)");
		return;
	}

	const INT8 slot = parseSlotTag(args[1]);
	if (slot < 0)
	{
		Console_Println("first arg must be a slot s1..s19; see 'inventory'.");
		return;
	}
	OBJECTTYPE& src = giver->inv[slot];
	if (src.usItem == NOTHING)
	{
		Console_Println(ST::format("{} {}: empty.", slotTag(slot), slotLabel(slot)));
		return;
	}
	if (src.fFlags & OBJECT_UNDROPPABLE)
	{
		Console_Println(ST::format(
			"{} cannot be handed over.", GCM->getItem(src.usItem)->getName()));
		return;
	}

	// Reuse the address parser: it understands names, mN/eN/cN, and col,row.
	Target tgt;
	ST::string err;
	if (parseTarget(args, 2, giver, tgt, err) == 0) { Console_Println(err); return; }
	if (!tgt.soldier)
	{
		Console_Println("'give' target must be a soldier (a name, mN, eN, or cN), not a tile.");
		return;
	}
	SOLDIERTYPE* const recipient = tgt.soldier;
	if (recipient == giver)
	{
		Console_Println("Can't give to self.");
		return;
	}

	// Match the click-to-give UI's eligibility check exactly
	// (Interface_Items.cc:3185). IsValidTalkableNPC(fGive=TRUE,
	// fAllowMercs=TRUE, fCheckCollapsed=TRUE) accepts: teammates,
	// non-recruited NPCs/RPCs, EPCs, robots (the engine treats give-
	// to-robot as the reload-robot path), and visible hostiles only
	// when intentional. Refuses dead, collapsed, vehicles, hidden
	// hostiles, and the engine's other internal disqualifiers.
	//
	// Caveat the user should know: handing an item to an arms dealer
	// transitions into the shopkeeper screen (Handle_Items.cc:2367),
	// which is currently inaccessible. That's a global limitation of
	// the shop UI surface, not something `give` should gate on — the
	// player can always cancel out of the dialog if they hit it.
	if (!IsValidTalkableNPC(recipient, TRUE, TRUE, TRUE))
	{
		Console_Println(ST::format(
			"Can't give to {}: not a valid recipient.", recipient->name));
		return;
	}
	if (recipient->uiStatusFlags & SOLDIER_ENGAGEDINACTION)
	{
		Console_Println(ST::format("{} is busy with another action.", recipient->name));
		return;
	}

	// SoldierGiveItem doesn't return success/failure; it silently no-ops
	// if FindAdjacentGridEx fails. Run the same check up front so we can
	// give the player a clean reason instead of a quiet failure.
	UINT8 dummyDir;
	INT16 dummyAdj;
	const INT16 actionGridNo = FindAdjacentGridEx(
		giver, recipient->sGridNo, &dummyDir, &dummyAdj, TRUE, FALSE);
	if (actionGridNo == -1)
	{
		Console_Println(ST::format("No adjacent path from {} to {}.",
		                           giver->name, recipient->name));
		return;
	}

	// AP cost is deducted by SoldierGiveItemFromAnimation when the give
	// completes (Handle_Items.cc:2330), not at queue time, so we don't
	// pre-deduct here. Combat affordability is still worth checking so
	// the engine doesn't queue an action the merc can't pay for.
	if ((gTacticalStatus.uiFlags & INCOMBAT) &&
	    !EnoughPoints(giver, AP_PICKUP_ITEM, 0, TRUE))
	{
		Console_Println(ST::format(
			"{} needs {} AP to give items but has {}.",
			giver->name, AP_PICKUP_ITEM, giver->bActionPoints));
		return;
	}

	const ST::string itemName = GCM->getItem(src.usItem)->getName();
	SoldierGiveItem(giver, recipient, &src, slot);

	// Mirror the click UI's "lock conversation" step for off-team
	// recipients (Interface_Items.cc:3311). Prevents the give from
	// silently competing with a dialog that the engine is about to
	// open in response to the handoff.
	if (recipient->ubProfile != NO_PROFILE &&
	    !MercProfile(recipient->ubProfile).isPlayerMerc() &&
	    !RPC_RECRUITED(recipient))
	{
		SetEngagedInConvFromPCAction(giver);
	}

	if (giver->sGridNo == actionGridNo)
	{
		Console_Println(ST::format("Giving {} to {}.", itemName, recipient->name));
	}
	else
	{
		const INT16 dist = SpacesAway(giver->sGridNo, actionGridNo);
		Console_Println(ST::format(
			"Approaching {} ({} tile{}) to hand over {}.",
			recipient->name, dist, dist == 1 ? "" : "s", itemName));
	}
}

// Interface_Dialogue.cc owns these and re-uses Quests.cc's local extern.
// Re-declare here for the same reason: there's no header-exposed accessor
// and we need both to mirror TalkPanelClickCallback's give branch.
extern SOLDIERTYPE* gpSrcSoldier;
extern SOLDIERTYPE* gpDestSoldier;

namespace
{
	// Subcommand keywords for an active talk panel. Compared
	// case-insensitively against the first arg. If a match hits, the
	// caller is asking us to drive the talkbox (pick an approach, skip
	// the current quote, list options, etc.) — not to start a new
	// conversation. Anything else (a name, mN/eN/cN, col,row) initiates.
	bool eqCI(const std::string& a, const char* b)
	{
		std::size_t i = 0;
		for (; i < a.size() && b[i]; ++i)
		{
			if (std::tolower(static_cast<unsigned char>(a[i])) !=
			    std::tolower(static_cast<unsigned char>(b[i]))) return false;
		}
		return i == a.size() && b[i] == '\0';
	}

	bool isTalkSubcommand(const std::string& tok)
	{
		const char* const kKeywords[] = {
			"options", "list",
			"friendly", "direct", "threaten", "recruit", "repeat",
			"give", "buysell",
			"done", "cancel", "leave",
			"skip", "shutup",
		};
		for (const char* k : kKeywords) if (eqCI(tok, k)) return true;
		return false;
	}

	const char* approachLabel(Approach a)
	{
		switch (a)
		{
			case APPROACH_FRIENDLY: return "Friendly";
			case APPROACH_DIRECT:   return "Direct";
			case APPROACH_THREATEN: return "Threaten";
			case APPROACH_RECRUIT:  return "Recruit";
			case APPROACH_REPEAT:   return "Repeat (\"Come Again?\")";
			case APPROACH_BUYSELL:  return "Give / Buy-sell";
			default:                return "?";
		}
	}

	// Mirror TalkPanelClickCallback's "give" branch (Interface_Dialogue.cc:803).
	// Dealer with unused buy-sell records → fire that approach as a quote.
	// Dealer without records → close the panel and enter the shop screen
	// (still inaccessible, but matches the click UI; the user knows they're
	// hitting that limitation by typing 'talk give' anyway). Non-dealer →
	// close the panel, set the NPC to wait, and tell the user to use the
	// standalone `give <slot> <name>` verb to complete the handoff.
	void handleTalkGive()
	{
		const UINT8 ch = gTalkPanel.ubCharNum;
		if (IsMercADealer(ch))
		{
			if (NPCHasUnusedRecordWithGivenApproach(ch, APPROACH_BUYSELL))
			{
				TriggerNPCWithGivenApproach(ch, APPROACH_BUYSELL);
				Console_Println(ST::format(
					"Asking {} about buying / selling.", GetProfile(ch).zNickname));
			}
			else
			{
				DeleteTalkingMenu();
				EnterShopKeeperInterfaceScreen(ch);
				Console_Println(ST::format(
					"Closed talkbox, opened shop with {}. (Shop UI is currently inaccessible.)",
					GetProfile(ch).zNickname));
			}
			return;
		}

		// Non-dealer give: same state shuffle the click UI does, minus the
		// inventory-cursor trick (we don't drive a cursor — the user will
		// type 'give <slot> <name>' explicitly).
		gTalkPanel.fHandled              = TRUE;
		gTalkPanel.fHandledTalkingVal    = gTalkPanel.face->fTalking;
		gTalkPanel.fHandledCanDeleteVal  = TRUE;

		if (gpDestSoldier)
		{
			gpDestSoldier->bNextAction       = AI_ACTION_WAIT;
			gpDestSoldier->usNextActionData  = 10000;
			if (gpDestSoldier->bAction != AI_ACTION_PENDING_ACTION)
			{
				CancelAIAction(gpDestSoldier);
			}
		}

		Console_Println(ST::format(
			"Closed talkbox; {} is waiting. Use 'give <slot> {}' to hand them an item.",
			GetProfile(gTalkPanel.ubCharNum).zNickname,
			GetProfile(gTalkPanel.ubCharNum).zNickname));
	}

	void runTalkSubcommand(const std::string& sub)
	{
		if (!gfInTalkPanel)
		{
			Console_Println(ST::format(
				"No talk panel active. Start one with 'talk <name>' first."));
			return;
		}

		if (eqCI(sub, "options") || eqCI(sub, "list"))
		{
			Console_Println(ST::format("Talking to {}. Approaches:",
			                           GetProfile(gTalkPanel.ubCharNum).zNickname));
			Console_Println("  talk friendly | direct | threaten | recruit | repeat | give");
			Console_Println("  talk skip      — interrupt the current quote");
			Console_Println("  talk done      — close the panel");
			if (gTalkPanel.face && gTalkPanel.face->fTalking)
			{
				Console_Println("  (Currently speaking. Use 'talk skip' or wait, then pick an approach.)");
			}
			return;
		}

		if (eqCI(sub, "skip") || eqCI(sub, "shutup"))
		{
			if (gTalkPanel.face && gTalkPanel.face->fTalking)
			{
				InternalShutupaYoFace(gTalkPanel.face, FALSE);
				Console_Println("Skipped current quote.");
			}
			else
			{
				Console_Println("Nothing to skip.");
			}
			return;
		}

		if (eqCI(sub, "done") || eqCI(sub, "cancel") || eqCI(sub, "leave"))
		{
			// Mirror DoneTalkingButtonClickCallback (Interface_Dialogue.cc:500).
			gTalkPanel.fHandled              = TRUE;
			gTalkPanel.fHandledTalkingVal    = gTalkPanel.face->fTalking;
			gTalkPanel.fHandledCanDeleteVal  = TRUE;
			Console_Println("Closed talkbox.");
			return;
		}

		// Approach selection. Refuse mid-quote, matching the click UI's
		// `if (!gTalkPanel.face->fTalking)` guard. The user explicitly
		// types 'talk skip' to interrupt — making that an explicit step
		// keeps SR pacing predictable and avoids stomping mid-line.
		if (gTalkPanel.face && gTalkPanel.face->fTalking)
		{
			Console_Println(ST::format(
				"{} is still speaking. Use 'talk skip' to interrupt, or wait.",
				GetProfile(gTalkPanel.ubCharNum).zNickname));
			return;
		}

		Approach appr = APPROACH_NONE;
		if      (eqCI(sub, "friendly")) appr = APPROACH_FRIENDLY;
		else if (eqCI(sub, "direct"))   appr = APPROACH_DIRECT;
		else if (eqCI(sub, "threaten")) appr = APPROACH_THREATEN;
		else if (eqCI(sub, "recruit"))  appr = APPROACH_RECRUIT;
		else if (eqCI(sub, "repeat"))   appr = APPROACH_REPEAT;
		else if (eqCI(sub, "give") || eqCI(sub, "buysell"))
		{
			handleTalkGive();
			return;
		}

		if (appr == APPROACH_NONE)
		{
			Console_Println(ST::format("unknown talk subcommand '{}'.", sub));
			return;
		}

		// Converse routes through the engine's quote system, which will
		// emit the NPC's response via TacticalCharacterDialogue → ScreenMsg
		// (we hooked MSG_DIALOG into AX_Say so subtitles narrate). If
		// the NPC has no record matching this approach, the engine
		// silently drops it; surface a hint so the user isn't left
		// guessing.
		Converse(gTalkPanel.ubCharNum, gubSrcSoldierProfile, appr);
		Console_Println(ST::format("Approach: {}.", approachLabel(appr)));
	}
}

void Cmd_Talk(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		if (gfInTalkPanel)
		{
			runTalkSubcommand("options");
			return;
		}
		Console_Println("usage: talk <target>  (a name, mN/eN/cN, or col,row)");
		Console_Println("       talk <approach>  (when a talk panel is open)");
		Console_Println("       talk options     to list approach keywords");
		return;
	}

	// Subcommand keywords short-circuit *unconditionally* — even when no
	// talk panel is up. Otherwise typing 'talk options' on a refusenik
	// NPC (one whose only response is a quote, like Pacos) falls through
	// to parseTarget, which reports "no known soldier matches 'options'"
	// — confusing because the user isn't trying to address a soldier.
	// Better to recognize the keyword and explain the panel isn't open.
	if (isTalkSubcommand(args[1]))
	{
		if (args.size() > 2)
		{
			Console_Println(ST::format(
				"unexpected extra arg '{}' after 'talk {}'.", args[2], args[1]));
			return;
		}
		runTalkSubcommand(args[1]);
		return;
	}

	if (args.size() > 2)
	{
		// Reject `talk pacos friendly` etc. — staging is required so the
		// user can hear the opening quote (and confirm a panel actually
		// opened) before picking an approach.
		Console_Println(ST::format(
			"unexpected extra arg '{}'. Use 'talk <name>' first, then 'talk <approach>'.",
			args[2]));
		return;
	}

	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel) { Console_Println("No merc selected."); return; }
	if (AM_A_ROBOT(sel))
	{
		Console_Println(ST::format("{} cannot talk to anyone.", sel->name));
		return;
	}

	Target tgt;
	ST::string err;
	if (parseTarget(args, 1, sel, tgt, err) == 0) { Console_Println(err); return; }
	if (!tgt.soldier)
	{
		Console_Println("'talk' target must be a soldier (a name, mN, eN, or cN), not a tile.");
		return;
	}
	SOLDIERTYPE* const target = tgt.soldier;
	if (target == sel)
	{
		Console_Println("Can't talk to self.");
		return;
	}

	// Mirror the click UI eligibility (Handle_UI.cc:4671): fGive=FALSE
	// (talk, not give), fAllowMercs=TRUE (teammate chatter is allowed),
	// fCheckCollapsed=FALSE (we check collapsed inline like the engine
	// does, so we can give a clearer message).
	if (!IsValidTalkableNPC(target, FALSE, TRUE, FALSE))
	{
		Console_Println(ST::format(
			"Can't talk to {}: not a valid conversation target.", target->name));
		return;
	}
	if (target->bCollapsed)
	{
		Console_Println(ST::format("{} is collapsed.", target->name));
		return;
	}

	// Same-team non-EPC: this is the social-chatter shortcut
	// (Handle_UI.cc:4708). The click UI fires a randomized
	// QUOTE_NEGATIVE_COMPANY / QUOTE_PASSING_DISLIKE / QUOTE_SOCIAL_TRAIT
	// based on attitude, no conversation popup. The chatter line lands
	// in ScreenMsg → AX_Say so the SR user hears it.
	if (target->bTeam == OUR_TEAM && !AM_AN_EPC(target))
	{
		if (target->ubProfile == DIMITRI)
		{
			Console_Println(ST::format(
				"{} doesn't talk much.", target->name));
			return;
		}
		const UINT8 dieMax = (target->ubProfile != NO_PROFILE &&
			gMercProfiles[target->ubProfile].bAttitude != ATT_NORMAL) ? 3 : 2;
		UINT8 dice = (UINT8)Random(dieMax);
		if (target->ubWhatKindOfMercAmI == MERC_TYPE__PLAYER_CHARACTER) dice = 0;

		UINT16 quote = QUOTE_NEGATIVE_COMPANY;
		if (dice == 1)
		{
			quote = QuoteExp_PassingDislike[target->ubProfile]
				? QUOTE_PASSING_DISLIKE : QUOTE_NEGATIVE_COMPANY;
		}
		else if (dice == 2)
		{
			quote = QUOTE_SOCIAL_TRAIT;
		}
		if (target->ubProfile == IRA) quote = QUOTE_PASSING_DISLIKE;

		TacticalCharacterDialogue(target, quote);
		Console_Println(ST::format("{} chats with {}.", sel->name, target->name));
		return;
	}

	// LOS check before initiating — the click UI emits a localized
	// "no LOS" ScreenMsg here (Handle_UI.cc:4680). We rely on the same
	// ScreenMsg path to surface that message via AX_Say if the engine
	// emits it, but pre-check ourselves so we can also fail cleanly
	// with a console-side message.
	const INT16 distVisible = DistanceVisible(
		sel, DIRECTION_IRRELEVANT, DIRECTION_IRRELEVANT,
		target->sGridNo, target->bLevel);
	if (!SoldierTo3DLocationLineOfSightTest(
		sel, target->sGridNo, target->bLevel, 3, distVisible, TRUE))
	{
		Console_Println(ST::format(
			"{} has no line of sight to {}.", sel->name, target->name));
		return;
	}

	const UINT32 range = GetRangeFromGridNoDiff(sel->sGridNo, target->sGridNo);

	if (range > NPC_TALK_RADIUS)
	{
		// Walk-up case (Handle_UI.cc:4787). Find an adjacent destination,
		// validate the path, queue MERC_TALK so PlayerSoldierStartTalking
		// fires on arrival.
		const INT16 actionGridNo = FindAdjacentGridEx(
			sel, target->sGridNo, NULL, NULL, FALSE, TRUE);
		if (actionGridNo == -1)
		{
			Console_Println(ST::format(
				"No path from {} to {}.", sel->name, target->name));
			return;
		}
		if (UIPlotPath(sel, actionGridNo, NO_COPYROUTE, FALSE,
		               sel->usUIMovementMode, sel->bActionPoints) == 0)
		{
			Console_Println(ST::format(
				"No path from {} to {}.", sel->name, target->name));
			return;
		}

		gfNPCCircularDistLimit = TRUE;
		UINT8 newDir;
		const INT16 sweetSpot = FindGridNoFromSweetSpotWithStructData(
			sel, sel->usUIMovementMode, target->sGridNo,
			NPC_TALK_RADIUS - 1, &newDir, TRUE);
		gfNPCCircularDistLimit = FALSE;

		if ((gTacticalStatus.uiFlags & INCOMBAT) &&
		    !EnoughPoints(sel, AP_TALK, 0, TRUE))
		{
			Console_Println(ST::format(
				"{} needs {} AP to start talking but has {}.",
				sel->name, AP_TALK, sel->bActionPoints));
			return;
		}

		Soldier{sel}.setPendingAction(MERC_TALK);
		sel->uiPendingActionData1 = target->ubID;
		EVENT_InternalGetNewSoldierPath(sel, sweetSpot, sel->usUIMovementMode,
		                                TRUE, sel->fNoAPToFinishMove);

		const INT16 dist = SpacesAway(sel->sGridNo, sweetSpot);
		Console_Println(ST::format(
			"Approaching {} ({} tile{}) to talk.",
			target->name, dist, dist == 1 ? "" : "s"));
		return;
	}

	// Adjacent case (Handle_UI.cc:4828): kick the conversation directly.
	// PlayerSoldierStartTalking deducts AP_TALK itself (Soldier_Control.cc:8574),
	// so we only need to gate combat affordability.
	if ((gTacticalStatus.uiFlags & INCOMBAT) &&
	    !EnoughPoints(sel, AP_TALK, 0, TRUE))
	{
		Console_Println(ST::format(
			"{} needs {} AP to talk but has {}.",
			sel->name, AP_TALK, sel->bActionPoints));
		return;
	}

	PlayerSoldierStartTalking(sel, target->ubID, FALSE);
	Console_Println(ST::format("{} talks to {}.", sel->name, target->name));
}

void Cmd_Exit(const std::vector<std::string>& args)
{
	if (!gfWorldLoaded)
	{
		Console_Println("Not in a sector.");
		return;
	}
	if (args.size() < 2)
	{
		Console_Println("usage: exit <n|s|e|w>");
		Console_Println("(See 'nearby exits' for valid sides in this sector.)");
		return;
	}

	// Restrict to cardinals: strategic move codes are N/S/E/W only.
	// EXITGRID transitions (basements, building entrances) go through a
	// different path — `move <col,row>` onto the grid tile already does
	// the right thing, so we don't expose them here.
	const INT8 cardinal = parseCompass(args[1]);
	INT8 strategicDir;
	switch (cardinal)
	{
		case NORTH: strategicDir = NORTH_STRATEGIC_MOVE; break;
		case EAST:  strategicDir = EAST_STRATEGIC_MOVE;  break;
		case SOUTH: strategicDir = SOUTH_STRATEGIC_MOVE; break;
		case WEST:  strategicDir = WEST_STRATEGIC_MOVE;  break;
		default:
			Console_Println(ST::format(
				"exit takes a cardinal direction (n, s, e, w); got '{}'.", args[1]));
			return;
	}

	// Mirror the engine's pre-flight: OKForSectorExit returns 0 (no),
	// 1 (only the selected merc qualifies), or 2 (whole squad qualifies).
	// It also sets a few reason globals on failure that we read back to
	// give a useful message instead of a generic refusal.
	gfInvalidTraversal              = FALSE;
	gfLoneEPCAttemptingTraversal    = FALSE;
	gubLoneMercAttemptingToAbandonEPCs = 0;

	UINT32 traverseTime = 0;
	const UINT8 ok = static_cast<UINT8>(
		OKForSectorExit(strategicDir, 0, &traverseTime));

	if (!ok)
	{
		if (gfInvalidTraversal)
		{
			Console_Println(
				"That direction has no valid traversal route from this sector.");
		}
		else if (gfLoneEPCAttemptingTraversal)
		{
			Console_Println(
				"EPCs cannot leave a sector alone — escort them with a merc.");
		}
		else if (gubLoneMercAttemptingToAbandonEPCs)
		{
			Console_Println(ST::format(
				"Cannot leave: would abandon {} EPC{} in this sector.",
				gubLoneMercAttemptingToAbandonEPCs,
				gubLoneMercAttemptingToAbandonEPCs == 1 ? "" : "s"));
		}
		else
		{
			Console_Println(
				"Cannot exit that side. Move closer to the edge first.");
		}
		return;
	}

	// 1 = only the selected merc made it close enough; 2 = whole squad.
	// Both load the new sector immediately (the *_LOAD_NEW jump codes).
	// The dialog also offers a *_NO_LOAD pair that just leaves the merc
	// queued in the strategic group; for SR play, going straight in is
	// almost always what the user wants. If we ever need the no-load
	// variant we can add 'exit <dir> noload'.
	const UINT8 jumpCode = (ok == 1) ? JUMP_SINGLE_LOAD_NEW : JUMP_ALL_LOAD_NEW;

	JumpIntoAdjacentSector(static_cast<UINT8>(cardinal), jumpCode, 0);

	const char* dirWord;
	switch (cardinal)
	{
		case NORTH: dirWord = "north"; break;
		case EAST:  dirWord = "east";  break;
		case SOUTH: dirWord = "south"; break;
		case WEST:  dirWord = "west";  break;
		default:    dirWord = "?";     break;
	}
	Console_Println(ST::format("Exiting {} ({} merc{}).",
	                           dirWord,
	                           ok == 1 ? "single" : "whole squad",
	                           ok == 1 ? "" : ""));
}

void Cmd_Throw(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel) { Console_Println("No merc selected."); return; }
	if (args.size() < 3)
	{
		Console_Println("usage: throw <slot> <target>  (slot is s1..s19; target is name, any nearby tag, dir steps, or col,row)");
		return;
	}

	const INT8 slot = parseSlotTag(args[1]);
	if (slot < 0)
	{
		Console_Println("first arg must be a slot s1..s19; see 'inventory'.");
		return;
	}
	// HandleItem operates on the held weapon (HANDPOS) and assumes the
	// throw animation will be played from the main hand. Refusing here
	// keeps the verb honest about that — the user can `swap` the
	// grenade into s1 and retry, mirroring the click-to-throw UI's
	// drag-to-hand requirement.
	if (slot != HANDPOS)
	{
		Console_Println(ST::format(
			"Throw requires the grenade in s1 (in hand). Try 'swap {} s1' first.",
			slotTag(slot)));
		return;
	}

	OBJECTTYPE& src = sel->inv[slot];
	if (src.usItem == NOTHING) { Console_Println("Hand is empty."); return; }

	const ItemModel* const it  = GCM->getItem(src.usItem);
	const UINT32           cls = it->getItemClass();
	if (cls != IC_GRENADE && cls != IC_THROWN)
	{
		Console_Println(ST::format("{} is not throwable.", it->getName()));
		return;
	}
	// Launched grenades (40mm GL rounds, mortar shells) are ammunition,
	// not hand-throwables; chucking one bare just drops it. Refuse with
	// a hint so the player doesn't waste a turn.
	if (const ExplosiveModel* const e = it->asExplosive(); e && e->isLaunchable())
	{
		Console_Println(ST::format(
			"{} is launched ammunition — load it into a launcher first.",
			it->getName()));
		return;
	}

	Target tgt;
	ST::string err;
	if (parseTarget(args, 2, sel, tgt, err) == 0) { Console_Println(err); return; }

	// CalcMaxTossRange returns merc-specific reach in tiles based on
	// strength + the explosive's weight (Weapons.cc:3509). Beyond that,
	// the throw lands short — refuse cleanly so the player doesn't burn
	// APs on a wasted toss.
	const INT32 maxRange = CalcMaxTossRange(sel, src.usItem, TRUE);
	const INT16 dist     = SpacesAway(sel->sGridNo, tgt.gridno);
	if (dist > maxRange)
	{
		Console_Println(ST::format(
			"Out of throw range: {} tiles, max {} for {}.",
			dist, maxRange, it->getName()));
		return;
	}

	// HandleItem(...IC_GRENADE...) at Handle_Items.cc:856 routes through
	// FireWeapon / SendBeginFireWeaponEvent for us — same path the
	// click-to-throw UI uses. AP cost (MinAPsToAttack) and animation
	// are the engine's responsibility from here.
	const INT8             level = static_cast<INT8>(gsInterfaceLevel);
	const ItemHandleResult r     = HandleItem(sel, tgt.gridno, level, src.usItem, TRUE);

	const ST::string who = tgt.soldier ? tgt.soldier->name
		: ST::format("({},{})", tgt.gridno % WORLD_COLS, tgt.gridno / WORLD_COLS);
	switch (r)
	{
		case ITEM_HANDLE_OK:
			Console_Println(ST::format("Throwing {} at {}.", it->getName(), who));
			break;
		case ITEM_HANDLE_UNCONSCIOUS:           Console_Println("Merc is unconscious.");          break;
		case ITEM_HANDLE_NOAPS:                 Console_Println("Not enough APs to throw.");      break;
		case ITEM_HANDLE_BROKEN:                Console_Println("Item is broken or jammed.");     break;
		case ITEM_HANDLE_NOROOM:                Console_Println("Can't throw from here.");        break;
		case ITEM_HANDLE_REFUSAL:               Console_Println("Merc refused.");                 break;
		case ITEM_HANDLE_CANNOT_GETTO_LOCATION: Console_Println("Can't reach a throwing position."); break;
		default:                                Console_Println("Throw issued.");                 break;
	}
}

void Cmd_Attach(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const s = GetSelectedMan();
	if (!s) { Console_Println("No merc selected."); return; }
	if (AM_AN_EPC(s) || AM_A_ROBOT(s))
	{
		Console_Println(ST::format("{} cannot manipulate attachments.", s->name));
		return;
	}
	if (args.size() < 3)
	{
		Console_Println("usage: attach <slot> detonator|remotedet  (slot is s1..s19; pulls the detonator from this merc's inventory)");
		return;
	}

	const INT8 slot = parseSlotTag(args[1]);
	if (slot < 0)
	{
		Console_Println("first arg must be a slot s1..s19; see 'inventory'.");
		return;
	}
	OBJECTTYPE& bomb = s->inv[slot];
	if (bomb.usItem == NOTHING)
	{
		Console_Println(ST::format("{} {}: empty.", slotTag(slot), slotLabel(slot)));
		return;
	}
	const ItemModel* const bombItem = GCM->getItem(bomb.usItem);
	if (bombItem->getItemClass() != IC_BOMB)
	{
		Console_Println(ST::format(
			"{} is not a plantable explosive — only IC_BOMB items take detonators.",
			bombItem->getName()));
		return;
	}
	if (bomb.fFlags & OBJECT_ARMED_BOMB)
	{
		Console_Println(ST::format(
			"{} is already armed; can't change attachments.", bombItem->getName()));
		return;
	}

	UINT16 want = NOTHING;
	const std::string& kind = args[2];
	if      (kind == "detonator" || kind == "timed")  want = DETONATOR;
	else if (kind == "remotedet" || kind == "remote") want = REMDETONATOR;
	else
	{
		Console_Println(ST::format(
			"unknown attachment '{}' (try 'detonator' or 'remotedet')", kind));
		return;
	}

	if (FindAttachment(&bomb, want) != ITEM_NOT_FOUND)
	{
		Console_Println(ST::format(
			"{} already has a {} attached.",
			bombItem->getName(), GCM->getItem(want)->getName()));
		return;
	}
	if (!ValidAttachment(want, bomb.usItem))
	{
		Console_Println(ST::format(
			"{} cannot accept a {}.",
			bombItem->getName(), GCM->getItem(want)->getName()));
		return;
	}

	const INT8 attachSlot = FindObj(s, want);
	if (attachSlot == NO_SLOT)
	{
		Console_Println(ST::format(
			"No {} in {}'s inventory.",
			GCM->getItem(want)->getName(), s->name));
		return;
	}

	// AttachObject moves one item out of the source slot (or decrements
	// a stack) and into the bomb's attach array. It also runs the
	// explosives skill check (ATTACHING_DETONATOR_CHECK / _REMOTE_),
	// which is why we pass the SOLDIERTYPE so XP and chat-of-failure
	// can fire just like the inventory drag path.
	if (!AttachObject(s, &bomb, &s->inv[attachSlot], 0))
	{
		Console_Println("Attach failed.");
		return;
	}

	DirtyMercPanelInterface(s, DIRTYLEVEL2);
	fInterfacePanelDirty = DIRTYLEVEL2;

	Console_Println(ST::format("Attached {} to {} ({}).",
	                           GCM->getItem(want)->getName(),
	                           bombItem->getName(),
	                           slotTag(slot)));
}

void Cmd_Arm(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const s = GetSelectedMan();
	if (!s) { Console_Println("No merc selected."); return; }
	if (args.size() < 2)
	{
		Console_Println("usage: arm <slot> [turns <N> | freq <F> | pressure]  (slot is s1..s19)");
		return;
	}

	const INT8 slot = parseSlotTag(args[1]);
	if (slot < 0)
	{
		Console_Println("first arg must be a slot s1..s19; see 'inventory'.");
		return;
	}
	OBJECTTYPE& bomb = s->inv[slot];
	if (bomb.usItem == NOTHING)
	{
		Console_Println(ST::format("{} {}: empty.", slotTag(slot), slotLabel(slot)));
		return;
	}
	const ItemModel*      const it = GCM->getItem(bomb.usItem);
	const ExplosiveModel* const e  = it->asExplosive();
	if (!e || it->getItemClass() != IC_BOMB)
	{
		Console_Println(ST::format("{} is not an arm-able explosive.", it->getName()));
		return;
	}
	if (bomb.fFlags & OBJECT_ARMED_BOMB)
	{
		Console_Println(ST::format("{} is already armed.", it->getName()));
		return;
	}

	// Mode auto-detection mirrors ArmBomb (Items.cc:2664-2708): the
	// attached detonator (or pressureActivated flag) decides timed /
	// remote / pressure. We let the user override only to disambiguate
	// or to confirm — never to override the engine's reading of the
	// attachments, since ArmBomb itself wouldn't honour that.
	const bool hasTimed  = FindAttachment(&bomb, DETONATOR)    != ITEM_NOT_FOUND;
	const bool hasRemote = FindAttachment(&bomb, REMDETONATOR) != ITEM_NOT_FOUND;
	const bool pressure  = e->isPressureTriggered();

	INT8 setting = 0;
	if (args.size() >= 3)
	{
		const std::string& mode = args[2];
		if (mode == "turns" || mode == "timer")
		{
			if (!hasTimed)
			{
				Console_Println("Needs a DETONATOR attached for a timed fuse — try 'attach' first.");
				return;
			}
			int n;
			if (args.size() < 4 || !parseInt(args[3], n) || n < 1 || n > 99)
			{
				Console_Println("usage: arm <slot> turns <N>  (N is 1..99 turns)");
				return;
			}
			setting = static_cast<INT8>(n);
		}
		else if (mode == "freq" || mode == "frequency" || mode == "remote")
		{
			if (!hasRemote)
			{
				Console_Println("Needs a REMDETONATOR attached for a remote trigger — try 'attach' first.");
				return;
			}
			int n;
			if (args.size() < 4 || !parseInt(args[3], n) || n < 1 || n >= PANIC_FREQUENCY)
			{
				Console_Println(ST::format(
					"usage: arm <slot> freq <F>  (F is 1..{})", PANIC_FREQUENCY - 1));
				return;
			}
			setting = static_cast<INT8>(n);
		}
		else if (mode == "pressure" || mode == "trip")
		{
			if (!pressure)
			{
				Console_Println(ST::format(
					"{} is not pressure-triggered.", it->getName()));
				return;
			}
			setting = 0;
		}
		else
		{
			Console_Println(ST::format(
				"unknown arm mode '{}' (try 'turns N', 'freq F', or 'pressure')", mode));
			return;
		}
	}
	else
	{
		// Bare 'arm <slot>' is only enough for pressure mines (no
		// setting needed). For everything else the engine needs a
		// timer or frequency — ask explicitly so we never silently
		// pick a default the player didn't intend.
		if (pressure && !hasTimed && !hasRemote)
		{
			setting = 0;
		}
		else if (hasTimed)
		{
			Console_Println(ST::format(
				"{} has a timed detonator. Specify: 'arm {} turns <N>'.",
				it->getName(), slotTag(slot)));
			return;
		}
		else if (hasRemote)
		{
			Console_Println(ST::format(
				"{} has a remote detonator. Specify: 'arm {} freq <F>'.",
				it->getName(), slotTag(slot)));
			return;
		}
		else
		{
			Console_Println(ST::format(
				"{} has no detonator. Try 'attach {} detonator' or 'attach {} remotedet' first.",
				it->getName(), slotTag(slot), slotTag(slot)));
			return;
		}
	}

	if (!ArmBomb(&bomb, setting))
	{
		Console_Println(ST::format("Failed to arm {}.", it->getName()));
		return;
	}

	DirtyMercPanelInterface(s, DIRTYLEVEL2);
	fInterfacePanelDirty = DIRTYLEVEL2;

	switch (bomb.bDetonatorType)
	{
		case BOMB_TIMED:
			Console_Println(ST::format(
				"Armed {}: timed fuse, {} turn{}.",
				it->getName(), bomb.bDelay, bomb.bDelay == 1 ? "" : "s"));
			break;
		case BOMB_REMOTE:
			Console_Println(ST::format(
				"Armed {}: remote, frequency {}.",
				it->getName(), bomb.bFrequency));
			break;
		case BOMB_PRESSURE:
			Console_Println(ST::format("Armed {}: pressure trigger.", it->getName()));
			break;
		default:
			Console_Println(ST::format("Armed {}.", it->getName()));
			break;
	}
}

void Cmd_Plant(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const s = GetSelectedMan();
	if (!s) { Console_Println("No merc selected."); return; }
	if (AM_AN_EPC(s))
	{
		Console_Println(ST::format("{} is an escort and refuses to handle items.", s->name));
		return;
	}
	if (args.size() < 2)
	{
		Console_Println("usage: plant <slot> [target]  (slot is s1..s19; target must be the merc's tile or adjacent)");
		return;
	}

	const INT8 slot = parseSlotTag(args[1]);
	if (slot < 0)
	{
		Console_Println("first arg must be a slot s1..s19; see 'inventory'.");
		return;
	}
	OBJECTTYPE& bomb = s->inv[slot];
	if (bomb.usItem == NOTHING)
	{
		Console_Println(ST::format("{} {}: empty.", slotTag(slot), slotLabel(slot)));
		return;
	}
	const ItemModel*      const it = GCM->getItem(bomb.usItem);
	const ExplosiveModel* const e  = it->asExplosive();
	if (!e || it->getItemClass() != IC_BOMB)
	{
		Console_Println(ST::format("{} is not plantable.", it->getName()));
		return;
	}

	// Default plant location is the merc's own tile (matching the
	// engine's place-on-self behavior). Allow an adjacent target so a
	// player can plant a tripwire one step away and walk back; anything
	// farther needs `move` first because the place path doesn't pathfind.
	INT16 dest = s->sGridNo;
	if (args.size() >= 3)
	{
		Target tgt;
		ST::string err;
		if (parseTarget(args, 2, s, tgt, err) == 0) { Console_Println(err); return; }
		const INT16 d = SpacesAway(s->sGridNo, tgt.gridno);
		if (d > 1)
		{
			Console_Println(ST::format(
				"Target is {} tiles away — move adjacent first.", d));
			return;
		}
		dest = tgt.gridno;
	}

	// Pressure mines auto-arm with setting 0 here, mirroring
	// HandleSoldierDropBomb (Handle_Items.cc:947). Bombs needing a
	// timer or frequency must be `arm`ed first — we won't pick a
	// default for them.
	if (!(bomb.fFlags & OBJECT_ARMED_BOMB))
	{
		if (e->isPressureTriggered())
		{
			if (!ArmBomb(&bomb, 0))
			{
				Console_Println(ST::format("Failed to arm {}.", it->getName()));
				return;
			}
		}
		else
		{
			Console_Println(ST::format(
				"{} is not armed. Try 'arm {} turns <N>' or 'arm {} freq <F>' first.",
				it->getName(), slotTag(slot), slotTag(slot)));
			return;
		}
	}

	if (!deductIfCombat(s, AP_DROP_BOMB))
	{
		Console_Println(ST::format(
			"{} needs {} AP to plant but has {}.",
			s->name, AP_DROP_BOMB, s->bActionPoints));
		return;
	}

	// Shared engine helper — same place recipe HandleSoldierDropBomb
	// uses (XP, trap-detect difficulty, owner stamp, MAPELEMENT_PLAYER_
	// MINE_PRESENT, BURIED + WORLD_ITEM_ARMED_BOMB pool entry).
	PlaceArmedBombInWorld(s, &bomb, dest);

	DirtyMercPanelInterface(s, DIRTYLEVEL2);
	fInterfacePanelDirty = DIRTYLEVEL2;

	if (dest == s->sGridNo)
	{
		Console_Println(ST::format("Planted {} at your feet.", it->getName()));
	}
	else
	{
		Console_Println(ST::format("Planted {} on adjacent tile.", it->getName()));
	}
}

void Cmd_Detonate(const std::vector<std::string>& args)
{
	SOLDIERTYPE* const s = GetSelectedMan();
	if (!s) { Console_Println("No merc selected."); return; }
	if (args.size() < 2)
	{
		Console_Println(ST::format(
			"usage: detonate <freq>  (freq is 1..{}; 'nearby bombs' shows your placed remote bombs)",
			PANIC_FREQUENCY - 1));
		return;
	}

	int freq;
	if (!parseInt(args[1], freq) || freq < 1 || freq >= PANIC_FREQUENCY)
	{
		Console_Println(ST::format(
			"freq must be 1..{}.", PANIC_FREQUENCY - 1));
		return;
	}

	// Walk gWorldBombs first to give a confirming "triggered N bombs"
	// message — SetOffBombsByFrequency itself returns nothing, so without
	// this the user has no way to tell whether their button-press did
	// anything until the explosions actually fire (or don't).
	int matches = 0;
	CFOR_EACH_WORLD_BOMB(wb)
	{
		const WORLDITEM& wi = GetWorldItem(wb.iItemIndex);
		if (!wi.fExists)                                continue;
		if (!(wi.o.fFlags & OBJECT_ARMED_BOMB))         continue;
		if (wi.o.fFlags & OBJECT_DISABLED_BOMB)         continue;
		if (wi.o.bDetonatorType != BOMB_REMOTE)         continue;
		if (wi.o.bFrequency     != static_cast<INT8>(freq)) continue;
		++matches;
	}
	if (matches == 0)
	{
		Console_Println(ST::format(
			"No armed remote bombs on frequency {}.", freq));
		return;
	}

	SetOffBombsByFrequency(s, static_cast<INT8>(freq));
	Console_Println(ST::format(
		"Triggered {} bomb{} on frequency {}.",
		matches, matches == 1 ? "" : "s", freq));
}
