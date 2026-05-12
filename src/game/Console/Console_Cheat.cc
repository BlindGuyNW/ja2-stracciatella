#include "Console_Cheat.h"

#include "ContentManager.h"
#include "EMail.h"
#include "Explosion_Control.h"
#include "Finances.h"
#include "GameInstance.h"
#include "Game_Clock.h"
#include "Interface.h"
#include "ItemModel.h"
#include "Item_Types.h"
#include "Items.h"
#include "Laptop.h"
#include "LaptopSave.h"
#include "Overhead.h"
#include "Player_Command.h"
#include "Queen_Command.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "TeamTurns.h"
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

	// Mirrors the body of the EVENT_DAY2_ADD_EMAIL_FROM_IMP handler at
	// Game_Event_Hook.cc:127 — drops the personality / attitude / skills
	// profile email into the inbox immediately, instead of waiting for
	// 07:00 on (IMP creation day + 2). Gates on (a) an IMP actually
	// having been created (the email's body reads from gMercProfiles[
	// PLAYER_GENERATED_CHARACTER_ID + iVoiceId] and would otherwise
	// dump garbage), and (b) the email not already being in the inbox.
	void cheatImpEmail(const ArgList&)
	{
		if (!LaptopSaveInfo.fIMPCompletedFlag)
		{
			Console_Println(
				"No IMP character has been created. Complete the IMP "
				"questionnaire first (web imp).");
			return;
		}

		for (const Email* m = pEmailList; m; m = m->Next)
		{
			if (m->usOffset == IMP_EMAIL_PROFILE_RESULTS)
			{
				Console_Println(
					"IMP profile-results email is already in the inbox; "
					"open it with 'email <id>'.");
				return;
			}
		}

		AddEmail(IMP_EMAIL_PROFILE_RESULTS, IMP_EMAIL_PROFILE_RESULTS_LENGTH,
		         IMP_PROFILE_RESULTS, GetWorldTotalMin());
		Console_Println(
			"IMP profile-results email delivered. Open it with "
			"'email unread' then 'email <id>'.");
	}

	// Resolve an item argument by case-insensitive prefix match against
	// ItemModel::getInternalName (the uppercase-snake-case names from
	// items.json: HAND_GRENADE, SHAPED_CHARGE, GLOCK_17, ...). A numeric
	// arg resolves to the bare item ID. Returns NOTHING and writes a
	// one-line reason on failure: not-found, ambiguous (lists matches),
	// or out-of-range. Limits the ambiguous-match list to 8 names to
	// keep the failure terse.
	UINT16 resolveItemArg(const std::string& arg, ST::string& errOut)
	{
		long asNum;
		if (parseInt(arg, asNum))
		{
			if (asNum < 1 || asNum >= MAXITEMS)
			{
				errOut = ST::format(
					"item id {} out of range (1..{})", asNum, MAXITEMS - 1);
				return NOTHING;
			}
			const ItemModel* it = GCM->getItem(static_cast<uint16_t>(asNum), ItemSystem::nothrow);
			if (it == nullptr)
			{
				errOut = ST::format("no item with id {}", asNum);
				return NOTHING;
			}
			return static_cast<UINT16>(asNum);
		}

		const std::string needle = lower(arg);
		std::vector<UINT16> matches;
		std::vector<ST::string> matchNames;
		for (UINT16 i = 1; i < MAXITEMS; ++i)
		{
			const ItemModel* it = GCM->getItem(i, ItemSystem::nothrow);
			if (it == nullptr) continue;
			const ST::string& iname = it->getInternalName();
			if (iname.empty()) continue;
			if (startsWithCI(iname, needle))
			{
				matches.push_back(i);
				matchNames.push_back(iname);
				if (matches.size() == 1 && iname.size() == needle.size())
				{
					// Exact case-insensitive hit shortcuts ambiguity.
					return i;
				}
			}
		}
		if (matches.empty())
		{
			errOut = ST::format("no item matches '{}'", arg);
			return NOTHING;
		}
		if (matches.size() == 1) return matches[0];

		ST::string sample;
		const std::size_t n = std::min<std::size_t>(matchNames.size(), 8);
		for (std::size_t i = 0; i < n; ++i)
		{
			if (i > 0) sample += ", ";
			sample += matchNames[i];
		}
		if (matchNames.size() > n) sample += ST::format(", +{} more", matchNames.size() - n);
		errOut = ST::format("'{}' matches {} items: {}", arg, matchNames.size(), sample);
		return NOTHING;
	}

	// Materialize an item in the selected merc's inventory. The engine
	// has CreateItem (factory: usItem -> OBJECTTYPE) and AutoPlaceObject
	// (place into first viable slot, honoring slot affinity / pocket
	// size); together they're the same path the editor's "give item"
	// uses. Status defaults to 100. Count > 1 stacks for stackable items
	// (ammo, grenades) and creates separate objects otherwise (one
	// AutoPlaceObject call per object).
	void cheatGive(const ArgList& args)
	{
		if (args.size() < 3)
		{
			Console_Println(
				"usage: cheat give <item-name|id> [count]");
			return;
		}
		SOLDIERTYPE* const sel = GetSelectedMan();
		if (sel == nullptr)
		{
			Console_Println("No merc selected.");
			return;
		}

		ST::string err;
		const UINT16 idx = resolveItemArg(args[2], err);
		if (idx == NOTHING)
		{
			Console_Println(err);
			return;
		}

		long count = 1;
		if (args.size() >= 4 && (!parseInt(args[3], count) || count < 1 || count > 250))
		{
			Console_Println(ST::format(
				"usage: cheat give <item> [count=1..250]; got: {}", args[3]));
			return;
		}

		const ItemModel* model = GCM->getItem(idx);
		const ST::string name  = model ? model->getInternalName() : ST::string("?");

		// CreateItems handles internal stacking for stackables (ammo,
		// throwables); for non-stackables count > 1 still produces one
		// object with bStatus=100. To get N distinct copies of a non-
		// stackable, loop AutoPlaceObject N times.
		UINT32 placed = 0;
		for (long i = 0; i < count; ++i)
		{
			OBJECTTYPE obj{};
			CreateItem(idx, 100, &obj);
			if (AutoPlaceObject(sel, &obj, FALSE)) ++placed;
		}
		Console_Println(placed == 0
			? ST::format("Couldn't place {} on {} — inventory full?", name, sel->name)
			: ST::format("Gave {} x {} to {}.", name, placed, sel->name));
	}

	// Direct IgniteExplosion at a target tile, with selected merc as
	// owner so attribution works through the damage log. Mostly a test
	// tool for the structure / window / lock-blow paths so an SR
	// developer can exercise them without first equipping inventory.
	// Defaults to HAND_GRENADE; any IC_GRENADE / IC_BOMB item is
	// accepted. Refuses non-explosives (rifle, medkit, etc.) so a
	// typo doesn't crash IgniteExplosion's internal lookups.
	void cheatBoom(const ArgList& args)
	{
		if (args.size() < 3)
		{
			Console_Println(
				"usage: cheat boom <target> [item-name|id]");
			return;
		}
		SOLDIERTYPE* const sel = GetSelectedMan();
		if (sel == nullptr)
		{
			Console_Println("No merc selected.");
			return;
		}

		// Reuse the standard address parser. Note: parseTarget consumes
		// 1 or 2 tokens depending on grammar (name, tag, dir+steps,
		// dir+steps+dir+steps, col,row), so the optional item arg sits
		// after whatever it consumed.
		Target tgt{};
		ST::string err;
		const int consumed = parseTarget(args, 2, sel, tgt, err);
		if (consumed == 0)
		{
			Console_Println(err);
			return;
		}

		UINT16 itemIdx = HAND_GRENADE;
		if (args.size() > static_cast<std::size_t>(2 + consumed))
		{
			ST::string ierr;
			const UINT16 i = resolveItemArg(args[2 + consumed], ierr);
			if (i == NOTHING)
			{
				Console_Println(ierr);
				return;
			}
			itemIdx = i;
		}

		const ItemModel* model = GCM->getItem(itemIdx);
		if (model == nullptr || !model->isExplosive())
		{
			Console_Println(ST::format(
				"'{}' is not an IC_GRENADE / IC_BOMB item; refusing to "
				"detonate (would crash the explosion lookup).",
				model ? model->getInternalName() : ST::string("?")));
			return;
		}

		const INT8 level = static_cast<INT8>(gsInterfaceLevel);
		IgniteExplosion(sel, 0, tgt.gridno, itemIdx, level);
		Console_Println(ST::format(
			"Boom: {} at gridno {} (owner: {}, level: {}).",
			model->getInternalName(), tgt.gridno, sel->name,
			level == 0 ? "ground" : "roof"));
	}

	// Synthesize an our-team interrupt against the target so the
	// StartInterrupt narration hook can be exercised without depending
	// on the AI rolling a winning duel score (rookie enemies almost
	// never beat experienced mercs in CalcInterruptDuelPts). Uses the
	// engine's public funnel (AddToIntList + DoneAddingToIntList) so
	// the queue state matches what ResolveInterruptsVs would produce
	// naturally. Gated on INCOMBAT (the engine itself ignores
	// interrupts outside combat) and on the target being a different
	// team + alive.
	void cheatInterrupt(const ArgList& args)
	{
		if (!(gTacticalStatus.uiFlags & INCOMBAT))
		{
			Console_Println(
				"refusing to synthesize interrupt outside combat — "
				"the engine only resolves interrupts INCOMBAT.");
			return;
		}
		SOLDIERTYPE* const sel = GetSelectedMan();
		if (sel == nullptr)
		{
			Console_Println("No merc selected.");
			return;
		}
		if (args.size() < 3)
		{
			Console_Println("usage: cheat interrupt <target-soldier>");
			return;
		}

		Target tgt{};
		ST::string err;
		if (parseTarget(args, 2, sel, tgt, err) == 0)
		{
			Console_Println(err);
			return;
		}
		if (tgt.soldier == nullptr)
		{
			Console_Println(
				"interrupt target must resolve to a soldier "
				"(name or eN/cN/mN tag).");
			return;
		}
		if (tgt.soldier->bTeam == sel->bTeam)
		{
			Console_Println(
				"interrupt target must be on a different team than "
				"the selected merc.");
			return;
		}
		if (tgt.soldier->bLife < OKLIFE)
		{
			Console_Println(
				ST::format("{} is incapacitated; can't interrupt.",
					tgt.soldier->name));
			return;
		}

		// Target loses control; selected merc gains it.
		AddToIntList(tgt.soldier, FALSE, TRUE);
		AddToIntList(sel, TRUE, TRUE);
		DoneAddingToIntList();
		Console_Println(ST::format(
			"Interrupt queued: {} caught {}.",
			sel->name, tgt.soldier->name));
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
		{ "impemail", &cheatImpEmail, "impemail — deliver the IMP profile-results email now (normally arrives day+2 at 07:00)" },
		{ "give",     &cheatGive,     "give <item-name|id> [count] — materialize an item in the selected merc's inventory" },
		{ "boom",     &cheatBoom,     "boom <target> [item-name|id] — ignite an explosion at a tile (default: HAND_GRENADE); selected merc as owner" },
		{ "interrupt",&cheatInterrupt,"interrupt <target> — synthesize an our-team interrupt against the target (exercises the narration hook)" },
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
