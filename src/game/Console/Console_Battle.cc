#include "Console_Battle.h"

#include "Console.h"

#include "Accessibility.h"
#include "Map_Screen_Interface_Map.h"
#include "Overhead.h"
#include "PreBattle_Interface.h"
#include "Queen_Command.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "Strategic.h"
#include "StrategicMap.h"
#include "Town_Militia.h"

#include <cctype>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	const char* encounterName(UINT8 code)
	{
		switch (code)
		{
			case NO_ENCOUNTER_CODE:           return "no encounter";
			case ENEMY_INVASION_CODE:         return "enemy invasion";
			case ENEMY_ENCOUNTER_CODE:        return "enemy encounter";
			case ENEMY_AMBUSH_CODE:           return "enemy ambush";
			case ENTERING_ENEMY_SECTOR_CODE:  return "entering enemy-held sector";
			case CREATURE_ATTACK_CODE:        return "creature attack";
			case BLOODCAT_AMBUSH_CODE:        return "bloodcat ambush";
			case ENTERING_BLOODCAT_LAIR_CODE: return "entering bloodcat lair";
			case FIGHTING_CREATURES_CODE:     return "creatures in battle";
			case HOSTILE_CIVILIANS_CODE:      return "hostile civilians";
			case HOSTILE_BLOODCATS_CODE:      return "hostile bloodcats";
		}
		return "unknown encounter";
	}

	const char* edgeName(UINT8 code)
	{
		switch (code)
		{
			case INSERTION_CODE_NORTH:  return "north";
			case INSERTION_CODE_SOUTH:  return "south";
			case INSERTION_CODE_EAST:   return "east";
			case INSERTION_CODE_WEST:   return "west";
			case INSERTION_CODE_CENTER: return "center";
			case INSERTION_CODE_GRIDNO: return "specific tile";
		}
		return nullptr;
	}

	// Returns a non-empty edge name if every involved merc enters on
	// the same side; empty otherwise. Mercs arriving from different
	// sectors (rare but legal) split across edges.
	ST::string unanimousEdge()
	{
		UINT8 seen = 0xFF;
		bool any = false;
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (!PlayerMercInvolvedInThisCombat(*s)) continue;
			const UINT8 code = s->ubStrategicInsertionCode;
			if (!any) { seen = code; any = true; continue; }
			if (code != seen) return ST::string();
		}
		if (!any) return ST::string();
		const char* n = edgeName(seen);
		return n ? ST::string(n) : ST::string();
	}

	// Persistent dialog uses gubEnemyEncounterCode; mid-battle dialog
	// uses gubExplicitEnemyEncounterCode (a superset that adds a few
	// codes — e.g. hostile civilians — that can only arise mid-fight).
	UINT8 activeEncounterCode()
	{
		return gfPersistantPBI ? gubEnemyEncounterCode
		                       : (UINT8)gubExplicitEnemyEncounterCode;
	}

	// The gate logic below mirrors PreBattle_Interface.cc:443-519.
	// gfCantRetreatInPBI is read-then-cleared inside Init, so by the
	// time the verb runs that one branch is invisible to us; if the
	// engine's own enable check refuses the action anyway it will
	// silently no-op.

	ST::string autoresolveDisabledReason()
	{
		const UINT8 code = activeEncounterCode();
		if (gfPersistantPBI)
		{
			if (code == ENTERING_ENEMY_SECTOR_CODE ||
				code == ENTERING_BLOODCAT_LAIR_CODE)
				return ST::string("player is invading");
			if (code == ENEMY_AMBUSH_CODE ||
				code == BLOODCAT_AMBUSH_CODE)
				return ST::string("ambush");
			return ST::string();
		}
		if (code == ENEMY_ENCOUNTER_CODE ||
			code == ENEMY_INVASION_CODE  ||
			code == CREATURE_ATTACK_CODE)
			return ST::string();
		return ST::string("mid-battle restriction");
	}

	ST::string goToSectorDisabledReason()
	{
		if (gfAutomaticallyStartAutoResolve)
			return ST::string("auto-resolve starting");
		return ST::string();
	}

	ST::string retreatDisabledReason()
	{
		if (!gfPersistantPBI)
			return ST::string("battle already in progress");
		if (gfAutomaticallyStartAutoResolve)
			return ST::string("auto-resolve starting");
		const UINT8 code = activeEncounterCode();
		if (code == ENEMY_AMBUSH_CODE    ||
			code == BLOODCAT_AMBUSH_CODE ||
			code == CREATURE_ATTACK_CODE)
			return ST::string("surprised");
		return ST::string();
	}

	UINT32 countInvolvedMercs()
	{
		UINT32 n = 0;
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (PlayerMercInvolvedInThisCombat(*s)) ++n;
		}
		return n;
	}

	ST::string enemyLine()
	{
		switch (WhatPlayerKnowsAboutEnemiesInSector(gubPBSector))
		{
			case KNOWS_HOW_MANY:
				return ST::format("Enemies: {}.", NumEnemiesInSector(gubPBSector));
			case KNOWS_THEYRE_THERE:
				return ST::string("Enemies: present, count unknown.");
			case KNOWS_NOTHING:
			default:
				return ST::string("Enemies: unknown.");
		}
	}

	ST::string actionLine()
	{
		ST::string parts;
		auto append = [&](const char* label, const ST::string& disabled)
		{
			if (!parts.empty()) parts += "; ";
			parts += label;
			if (!disabled.empty())
			{
				parts += " (disabled — ";
				parts += disabled;
				parts += ")";
			}
		};
		append("battle go", goToSectorDisabledReason());
		append("battle auto", autoresolveDisabledReason());
		append("battle retreat", retreatDisabledReason());
		return ST::format("Actions: {}.", parts);
	}

	ST::string headerLine()
	{
		const char* kind = gfPersistantPBI ? "Pre-battle" : "Mid-battle";
		const ST::string sec = GetSectorIDString(gubPBSector, TRUE);
		return ST::format("{}: {}. {}.",
		                  kind, sec, encounterName(activeEncounterCode()));
	}

	ST::string defendersLine()
	{
		const UINT32 mercs = countInvolvedMercs();
		const UINT8  militia = CountAllMilitiaInSector(gubPBSector);
		if (militia)
			return ST::format("Defenders: {} merc{}, {} militia.",
			                  mercs, mercs == 1 ? "" : "s", militia);
		return ST::format("Defenders: {} merc{}.",
		                  mercs, mercs == 1 ? "" : "s");
	}

	ST::string approachLine()
	{
		if (!gfPersistantPBI) return ST::string();
		const ST::string edge = unanimousEdge();
		if (!edge.empty())
			return ST::format("Approach: from the {} edge.", edge);
		return ST::string("Approach: mercs entering from multiple edges.");
	}

	bool requireDialog()
	{
		if (!gfPreBattleInterfaceActive)
		{
			Console_Println("No pre-battle dialog open.");
			return false;
		}
		return true;
	}

	void printSummary()
	{
		Console_Println(headerLine());
		Console_Println(defendersLine());
		Console_Println(enemyLine());
		const ST::string approach = approachLine();
		if (!approach.empty()) Console_Println(approach);
		Console_Println(actionLine());
	}
}

void Cmd_Battle(const std::vector<std::string>& args)
{
	if (!requireDialog()) return;

	if (args.size() < 2)
	{
		printSummary();
		return;
	}

	std::string sub = args[1];
	for (char& c : sub) c = (char)std::tolower((unsigned char)c);

	if (sub == "go" || sub == "1")
	{
		const ST::string why = goToSectorDisabledReason();
		if (!why.empty())
		{
			Console_Println(ST::format("Cannot enter sector: {}.", why));
			return;
		}
		ActivatePreBattleEnterSectorAction();
		Console_Println("Entering sector.");
		return;
	}
	if (sub == "auto" || sub == "2")
	{
		const ST::string why = autoresolveDisabledReason();
		if (!why.empty())
		{
			Console_Println(ST::format("Cannot auto-resolve: {}.", why));
			return;
		}
		ActivatePreBattleAutoresolveAction();
		Console_Println("Auto-resolving.");
		return;
	}
	if (sub == "retreat" || sub == "3")
	{
		const ST::string why = retreatDisabledReason();
		if (!why.empty())
		{
			Console_Println(ST::format("Cannot retreat: {}.", why));
			return;
		}
		ActivatePreBattleRetreatAction();
		Console_Println("Retreating.");
		return;
	}
	Console_Println("usage: battle | battle go | battle auto | battle retreat");
}

void Console_AnnouncePreBattle()
{
	if (!gfPreBattleInterfaceActive) return;

	printSummary();

	// Speech: terse cue identifying the dialog category; detail is on
	// demand via `battle`. Mirrors the modal-dialog convention of
	// announcing the category along with body text.
	AX_Say(ST::format("{} Type 'battle' for details.", headerLine()), true);
}
