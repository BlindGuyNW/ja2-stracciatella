#include "Console_Action.h"
#include "Console_Address.h"

#include "Console.h"

#include "Animation_Control.h"
#include "CalibreModel.h"
#include "ContentManager.h"
#include "Dialogue_Control.h"
#include "GameInstance.h"
#include "GameScreen.h"
#include "Handle_Doors.h"
#include "Handle_Items.h"
#include "Handle_UI.h"
#include "Interactive_Tiles.h"
#include "Interface.h"
#include "Interface_Panels.h"
#include "Isometric_Utils.h"
#include "ItemModel.h"
#include "Item_Types.h"
#include "Items.h"
#include "MercProfile.h"
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
		Console_Println("usage: give <slot> <target>  (target is a name, mN/eN, or col,row)");
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

	// Reuse the address parser: it understands names, mN, eN, and col,row.
	Target tgt;
	ST::string err;
	if (parseTarget(args, 2, giver, tgt, err) == 0) { Console_Println(err); return; }
	if (!tgt.soldier)
	{
		Console_Println("'give' target must be a soldier (a name, mN, or eN), not a tile.");
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
		const INT16 dist = PythSpacesAway(giver->sGridNo, actionGridNo);
		Console_Println(ST::format(
			"Approaching {} ({} tile{}) to hand over {}.",
			recipient->name, dist, dist == 1 ? "" : "s", itemName));
	}
}
