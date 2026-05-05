#include "Console_Action.h"
#include "Console_Address.h"

#include "Console.h"

#include "Animation_Control.h"
#include "CalibreModel.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "GameScreen.h"
#include "Handle_Doors.h"
#include "Handle_Items.h"
#include "Handle_UI.h"
#include "Interactive_Tiles.h"
#include "Interface.h"
#include "Isometric_Utils.h"
#include "ItemModel.h"
#include "Item_Types.h"
#include "Items.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Points.h"
#include "Soldier.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "Squads.h"
#include "Structure.h"
#include "Structure_Internals.h"
#include "WeaponModels.h"
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

	refreshMoveModeForRealtime(sel);
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
		Console_Println("Approaching to interact.");
		return;
	}

	if (tgt.soldier)
	{
		Console_Println(ST::format("Moving toward {}.", tgt.soldier->name));
	}
	else
	{
		Console_Println("Moving.");
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
		Console_Println("usage: move-all <name> | move-all <dir> <steps> | move-all <col,row>");
		return;
	}

	Target tgt;
	ST::string err;
	if (parseTarget(args, 1, sel, tgt, err) == 0)
	{
		Console_Println(err);
		return;
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
		s->fUIMovementFast  = FALSE;
		s->usUIMovementMode = GetMoveStateBasedOnStance(
			s, gAnimControl[s->usAnimState].ubEndHeight);

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

	Console_Println(ST::format("Group move queued: {} moving, {} skipped.",
	                           moved, skipped));
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
		const INT16 dist = PythSpacesAway(sel->sGridNo, gridno);
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
			Console_Println("Bandage target must be a soldier (name, mN, or eN).");
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
