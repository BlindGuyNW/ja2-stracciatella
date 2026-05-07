#include "Console_Cheat.h"

#include "Finances.h"
#include "Game_Clock.h"
#include "Interface.h"
#include "Laptop.h"
#include "LaptopSave.h"
#include "Overhead.h"
#include "Player_Command.h"
#include "Queen_Command.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "Text.h"
#include "Types.h"

#include "Console.h"
#include "Console_Address.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	using ArgList = std::vector<std::string>;

	// Mirrors the engine's natural unlock at Player_Command.cc:91 (flag
	// flip on liberating the shipping destination), plus the bookmark add
	// that BobbyR.cc:226 performs the first time the player visits the
	// site. Without the bookmark, `web bobbyr` refuses to load the page.
	void cheatBobbyR(const ArgList&)
	{
		const bool wasOpen = LaptopSaveInfo.fBobbyRSiteCanBeAccessed;
		LaptopSaveInfo.fBobbyRSiteCanBeAccessed = TRUE;
		SetBookMark(BOBBYR_BOOKMARK);
		Console_Println(wasOpen
			? "Bobby Ray's was already accessible; bookmark refreshed."
			: "Bobby Ray's unlocked. Use 'web bobbyr' to open the site.");
	}

	// Wraps the laptop +/- shortcut at Laptop.cc:2789-2806, which posts
	// an ANONYMOUS_DEPOSIT to the finance ledger. Default top-up matches
	// the +/+= shortcut ($100k); negative amounts work the same as the
	// `-` shortcut.
	void cheatMoney(const ArgList& args)
	{
		long amount = 100000;
		if (args.size() > 2 && !parseInt(args[2], amount))
		{
			Console_Println(ST::format(
				"usage: cheat money [amount]; got: {}", args[2]));
			return;
		}
		AddTransactionToPlayersBook(ANONYMOUS_DEPOSIT, 0,
			GetWorldTotalMin(), static_cast<INT32>(amount));
		Console_Println(ST::format(
			"Deposited {}. Balance: {}.",
			SPrintMoney(static_cast<INT32>(amount)),
			SPrintMoney(LaptopSaveInfo.iCurrentBalance)));
	}

	// Time-warp the strategic clock, processing every queued strategic
	// event along the way (Bobby Ray shipment arrivals, contract expiries,
	// scheduled emails, mine income, daily updates). The Quest Debug
	// system uses this same call (Quest_Debug_System.cc:2449) for the
	// same purpose. Refused during combat — advancing time mid-combat
	// would desync the turn-based state.
	void cheatSkip(const ArgList& args)
	{
		if (args.size() < 3)
		{
			Console_Println("usage: cheat skip <hours>");
			return;
		}
		long hours;
		if (!parseInt(args[2], hours) || hours < 1 || hours > 720)
		{
			Console_Println(ST::format(
				"usage: cheat skip <hours> (1..720); got: {}", args[2]));
			return;
		}
		if (gTacticalStatus.uiFlags & INCOMBAT)
		{
			Console_Println(
				"refusing to skip time during combat — end the turn or "
				"resolve the fight first.");
			return;
		}

		const UINT32 beforeMin = GetWorldTotalMin();
		const UINT32 want      = static_cast<UINT32>(hours) * 60u;

		WarpGameTime(static_cast<UINT32>(hours) * 3600u,
			WARPTIME_PROCESS_EVENTS_NORMALLY);

		const UINT32 afterMin  = GetWorldTotalMin();
		const UINT32 elapsed   = afterMin - beforeMin;

		ST::string interrupted;
		if (elapsed < want)
		{
			interrupted = ST::format(
				" (interrupted at {} of {} requested minutes — "
				"a strategic event paused the warp)",
				elapsed, want);
		}
		Console_Println(ST::format(
			"Advanced to day {}, {}.{}",
			GetWorldDay(), gpGameClockString, interrupted));
	}

	// Wraps the Ctrl+U tactical shortcut (Turn_Based_Input.cc:1857-1871):
	// reset life / breath / bleeding for every living merc on OUR_TEAM.
	// Skips dead mercs — reviving them has cascading state issues (corpse
	// removal, death triggers) we don't want a cheat to silently mask.
	void cheatHeal(const ArgList&)
	{
		UINT32 healed = 0;
		FOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (s->bLife <= 0) continue;
			s->bLife     = s->bLifeMax;
			s->bBreath   = s->bBreathMax;
			s->bBleeding = 0;
			++healed;
		}
		fInterfacePanelDirty = DIRTYLEVEL2;
		Console_Println(healed == 0
			? ST::string("No living mercs to heal.")
			: ST::format("Healed {} merc{} to full life and breath.",
				healed, healed == 1 ? "" : "s"));
	}

	// Wraps the Alt+D in-combat shortcut (Turn_Based_Input.cc:2036-2055):
	// `CalcNewActionPoints` for every living merc. Useful for stress-
	// testing a combat-turn surface without burning the turn-end cycle.
	void cheatAp(const ArgList&)
	{
		UINT32 refreshed = 0;
		FOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (s->bLife <= 0) continue;
			CalcNewActionPoints(s);
			++refreshed;
		}
		fInterfacePanelDirty = DIRTYLEVEL2;
		Console_Println(refreshed == 0
			? ST::string("No living mercs to refresh.")
			: ST::format("Refreshed APs for {} merc{}.",
				refreshed, refreshed == 1 ? "" : "s"));
	}

	// Wraps SetThisSectorAsPlayerControlled, the engine's single funnel
	// for player-control flips. Fires the airport→Bobby Ray's unlock,
	// town loyalty bumps, mine activation, SAM site unlock, and
	// helicopter refuel-site assignments — i.e. *every* downstream effect
	// a sighted player gets from naturally clearing the sector. Refuses
	// if hostiles remain (the engine's own gate at
	// Player_Command.cc:82-85 returns FALSE in that case anyway, so
	// surface the reason).
	void cheatLiberate(const ArgList& args)
	{
		if (args.size() < 3)
		{
			Console_Println("usage: cheat liberate <sector>");
			return;
		}

		SGPSector sec = SGPSector::FromShortString(ST::string(args[2]), 0);
		if (!sec.IsValid())
		{
			Console_Println(ST::format(
				"invalid sector: {} (try a short string like 'a9' or 'h13')",
				args[2]));
			return;
		}

		const UINT8 hostiles = NumHostilesInSector(sec);
		if (hostiles > 0)
		{
			Console_Println(ST::format(
				"{} still has {} hostile{} — clear them first; the "
				"engine's liberation funnel refuses contested sectors.",
				sec.AsShortString(), hostiles,
				hostiles == 1 ? "" : "s"));
			return;
		}

		const BOOLEAN ok = SetThisSectorAsPlayerControlled(sec, FALSE);
		if (!ok)
		{
			Console_Println(ST::format(
				"engine refused to flip control for {} "
				"(meanwhile in progress?)",
				sec.AsShortString()));
			return;
		}
		Console_Println(ST::format(
			"{} is now player-controlled. "
			"Side effects fired (loyalty / mines / SAM / Bobby Ray's).",
			sec.AsShortString()));
	}

	struct Entry
	{
		const char* name;
		void      (*fn)(const ArgList&);
		const char* help;
	};

	const Entry kCheats[] =
	{
		{ "bobbyr",   &cheatBobbyR,   "unlock Bobby Ray's Guns and Things (skips the Drassen-airport gate)" },
		{ "money",    &cheatMoney,    "money [amount=100000] — deposit (or withdraw with a negative)" },
		{ "skip",     &cheatSkip,     "skip <hours> — advance the strategic clock, firing queued events" },
		{ "heal",     &cheatHeal,     "heal — full life/breath/bleeding reset for the living team" },
		{ "ap",       &cheatAp,       "ap — refill action points for the living team" },
		{ "liberate", &cheatLiberate, "liberate <sector> — flip a sector to player control (no hostiles)" },
	};

	void listCheats()
	{
		Console_Println("Cheats:");
		for (const auto& c : kCheats)
		{
			Console_Println(ST::format("  cheat {} — {}", c.name, c.help));
		}
	}
}

void Cmd_Cheat(const std::vector<std::string>& args)
{
	if (args.size() < 2) { listCheats(); return; }

	const std::string want = lower(args[1]);
	for (const auto& c : kCheats)
	{
		if (want == c.name) { c.fn(args); return; }
	}
	Console_Println(ST::format("unknown cheat: {} (try 'cheat')", args[1]));
}
