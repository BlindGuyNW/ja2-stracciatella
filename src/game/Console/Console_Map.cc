#include "Console_Map.h"

#include "Assignments.h"
#include "Campaign_Types.h"
#include "ContentManager.h"
#include "Finances.h"
#include "Font.h"
#include "Font_Control.h"
#include "GameInstance.h"
#include "GameScreen.h"
#include "Game_Clock.h"
#include "Interface_Panels.h"
#include "JAScreens.h"
#include "MapScreen.h"
#include "Map_Screen_Interface.h"
#include "Map_Screen_Interface_Bottom.h"
#include "Map_Screen_Interface_Map.h"
#include "MineModel.h"
#include "SAM_Sites.h"
#include "Overhead.h"
#include "Soldier_Control.h"
#include "Soldier_Profile.h"
#include "Soldier_Profile_Type.h"
#include "Squads.h"
#include "ScreenIDs.h"
#include "Strategic_Mines.h"
#include "Strategic_Movement.h"
#include "Strategic_Pathing.h"
#include "Strategic_Town_Loyalty.h"
#include "StrategicMap.h"
#include "Text.h"
#include "TownModel.h"
#include "Types.h"

#include "Console.h"
#include "Console_Address.h"
#include "Console_Strategic.h"
#include "SGP.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	// ----- text helpers ------------------------------------------------

	std::string joinFrom(const std::vector<std::string>& args, std::size_t from)
	{
		std::string out;
		for (std::size_t i = from; i < args.size(); ++i)
		{
			if (i > from) out += " ";
			out += args[i];
		}
		return lower(out);
	}

	// Vehicle slots live at the tail of gCharactersList per the engine's
	// MAX_CHARACTER_COUNT / FIRST_VEHICLE convention. The other state
	// helpers (gate, mapscreen swap, tN tags, alive / in-transit / POW /
	// on-squad / on-assignment buckets, squad-number decode) all hoisted
	// to Console_Strategic so team and assign verbs share them.
	bool isVehicleSlot(INT8 slot) { return slot >= FIRST_VEHICLE; }

	enum class Urgency { Safe, Warning, Critical, NA };

	// Decode the colour signal from GetMapscreenMercDepartureString
	// without keeping its abbreviated text — we want full words.
	Urgency contractUrgency(const SOLDIERTYPE& s)
	{
		if ((s.ubWhatKindOfMercAmI != MERC_TYPE__AIM_MERC && s.ubProfile != SLAY) ||
		    s.bLife == 0)
			return Urgency::NA;
		UINT8 colour = FONT_LTGREEN;
		(void)GetMapscreenMercDepartureString(s, &colour);
		if (colour == FONT_LTGREEN) return Urgency::Safe;
		if (colour == FONT_WHITE)   return Urgency::Critical;  // flashing
		return Urgency::Warning;
	}

	// Verbose remaining-time string for the merc deep-dive readout.
	// Uses the same threshold (3 days) the engine paints across.
	ST::string verboseTimeRemaining(const SOLDIERTYPE& s)
	{
		if ((s.ubWhatKindOfMercAmI != MERC_TYPE__AIM_MERC && s.ubProfile != SLAY) ||
		    s.bLife == 0)
			return ST::string("n/a");
		const INT32 mins = s.iEndofContractTime - GetWorldTotalMin();
		if (mins <= 0) return ST::string("expired");
		const Urgency u = contractUrgency(s);
		const char* tag =
			u == Urgency::Critical ? " (URGENT — extend or lose)" :
			u == Urgency::Warning  ? " (warning)" :
			u == Urgency::Safe     ? "" : "";
		if (mins >= 3 * NUM_MIN_IN_DAY)
		{
			const INT32 days = mins / NUM_MIN_IN_DAY;
			return ST::format("{} day{} remaining{}", days, days == 1 ? "" : "s", tag);
		}
		const INT32 hrs = mins > 5 ? (mins + 59) / 60 : 0;
		return ST::format("{} hour{} remaining{}", hrs, hrs == 1 ? "" : "s", tag);
	}

	// ----- compact-row formatting -------------------------------------
	// One readable row per merc. Mirrors the GUI columns but flattens
	// them: name, location, assignment, contract, life.
	ST::string mercRow(INT8 slot, const SOLDIERTYPE& s)
	{
		const ST::string name = s.name;
		const ST::string loc  = GetMapscreenMercLocationString(s);
		// Sleep is a flag orthogonal to bAssignment — the engine's
		// assignment string would still say "Practice" / "Doctor" while
		// the merc is snoring. Suffix the row so the SR user hears the
		// disconnect instead of being misled.
		ST::string asg = GetMapscreenMercAssignmentString(s);
		if (Console_IsAsleep(s)) asg += " (asleep)";
		// Departure string — verbose word, not engine abbreviation.
		ST::string contract;
		if (s.ubWhatKindOfMercAmI == MERC_TYPE__AIM_MERC || s.ubProfile == SLAY)
		{
			const Urgency u = contractUrgency(s);
			const INT32 mins = s.iEndofContractTime - GetWorldTotalMin();
			if (mins <= 0)
				contract = ST::string("expired");
			else if (mins >= 3 * NUM_MIN_IN_DAY)
				contract = ST::format("{}d", mins / NUM_MIN_IN_DAY);
			else
				contract = ST::format("{}h{}",
					mins > 5 ? (mins + 59) / 60 : 0,
					u == Urgency::Critical ? "*" : "");
		}
		else
		{
			contract = ST::string("--");
		}
		return ST::format("  t{} {} — {}, {}, {}, life {}/{}",
			static_cast<int>(slot) + 1,
			name, loc, asg, contract, s.bLife, s.bLifeMax);
	}

	// ----- team summary (bare `team`) ---------------------------------

	void cmdTeamSummary()
	{
		if (!Console_RequireCampaign()) return;

		std::size_t total = 0, active = 0, transit = 0, asleep = 0;
		std::size_t assignment = 0, pow = 0;
		const SOLDIERTYPE* soonest = nullptr;
		INT32 soonestMins = 0;

		CFOR_EACH_IN_CHAR_LIST(c)
		{
			const SOLDIERTYPE& s = *c->merc;
			++total;
			if (!Console_IsAlive(s)) continue;
			if (Console_IsPOW(s)) { ++pow; continue; }
			if (Console_IsInTransit(s)) ++transit;
			else if (Console_IsAsleep(s)) ++asleep;
			else if (Console_IsOnNonCombatAssignment(s)) ++assignment;
			else ++active;

			if (s.ubWhatKindOfMercAmI == MERC_TYPE__AIM_MERC || s.ubProfile == SLAY)
			{
				const INT32 mins = s.iEndofContractTime - GetWorldTotalMin();
				if (mins > 0 && (!soonest || mins < soonestMins))
				{
					soonest = &s;
					soonestMins = mins;
				}
			}
		}

		Console_Println(ST::format(
			"{} mercs on team. {} on duty, {} on assignment, {} in transit, {} asleep{}.",
			total, active, assignment, transit, asleep,
			pow > 0 ? ST::format(", {} POW", pow) : ST::string()));

		if (soonest)
		{
			Console_Println(ST::format(
				"Soonest contract expiry: {}, {}.",
				soonest->name, verboseTimeRemaining(*soonest)));
		}
		Console_Println(
			"Use 'team list' to enumerate, 'team merc <name|tN>' for details.");
	}

	// ----- team list --------------------------------------------------

	enum class Filter { Active, Assigned, Asleep, Travelling, ContractSoon, All };

	bool parseFilter(const std::string& tok, Filter& out)
	{
		if      (tok == "active")        out = Filter::Active;
		else if (tok == "assigned")      out = Filter::Assigned;
		else if (tok == "asleep")        out = Filter::Asleep;
		else if (tok == "travelling" ||
		         tok == "in-transit" ||
		         tok == "transit")       out = Filter::Travelling;
		else if (tok == "contract-soon") out = Filter::ContractSoon;
		else if (tok == "all")           out = Filter::All;
		else return false;
		return true;
	}

	bool filterAccepts(Filter f, const SOLDIERTYPE& s)
	{
		switch (f)
		{
			case Filter::All:
				return true;
			case Filter::Active:
				return Console_IsAlive(s) && !Console_IsInTransit(s) && !Console_IsPOW(s);
			case Filter::Assigned:
				return Console_IsAlive(s) && Console_IsOnNonCombatAssignment(s);
			case Filter::Asleep:
				return Console_IsAlive(s) && Console_IsAsleep(s);
			case Filter::Travelling:
				return Console_IsAlive(s) && Console_IsInTransit(s);
			case Filter::ContractSoon:
				return Console_IsAlive(s) && contractUrgency(s) != Urgency::Safe &&
				       contractUrgency(s) != Urgency::NA;
		}
		return false;
	}

	const char* filterWord(Filter f)
	{
		switch (f)
		{
			case Filter::All:          return "all";
			case Filter::Active:       return "active";
			case Filter::Assigned:     return "assigned";
			case Filter::Asleep:       return "asleep";
			case Filter::Travelling:   return "travelling";
			case Filter::ContractSoon: return "with contract under 24 hours";
		}
		return "?";
	}

	enum class SortKey { Slot, Name, Sector, Contract, Life };

	bool parseSort(const std::string& tok, SortKey& out)
	{
		if      (tok == "slot")     out = SortKey::Slot;
		else if (tok == "name")     out = SortKey::Name;
		else if (tok == "sector")   out = SortKey::Sector;
		else if (tok == "contract") out = SortKey::Contract;
		else if (tok == "life")     out = SortKey::Life;
		else return false;
		return true;
	}

	void cmdTeamList(const std::vector<std::string>& args)
	{
		if (!Console_RequireCampaign()) return;

		Filter  filter = Filter::Active;
		SortKey sort   = SortKey::Slot;
		bool    ascend = true;

		for (std::size_t i = 2; i < args.size(); ++i)
		{
			const std::string a = lower(args[i]);
			Filter f; SortKey k;
			if (parseFilter(a, f)) { filter = f; continue; }
			if (parseSort(a, k))   { sort = k; continue; }
			if (a == "asc")  { ascend = true;  continue; }
			if (a == "desc") { ascend = false; continue; }
			Console_Println(ST::format(
				"unknown arg: {} (filters: active|assigned|asleep|travelling|"
				"contract-soon|all; sort: slot|name|sector|contract|life [asc|desc])",
				args[i]));
			return;
		}

		std::vector<INT8> slots;
		slots.reserve(MAX_CHARACTER_COUNT);
		for (INT8 i = 0; i < MAX_CHARACTER_COUNT; ++i)
		{
			const SOLDIERTYPE* const s = gCharactersList[i].merc;
			if (!s) continue;
			if (!filterAccepts(filter, *s)) continue;
			slots.push_back(i);
		}

		// Sort. SortKey::Slot is identity (slots already in order).
		auto cmp = [&](INT8 lhs, INT8 rhs) -> bool {
			const SOLDIERTYPE& a = *gCharactersList[lhs].merc;
			const SOLDIERTYPE& b = *gCharactersList[rhs].merc;
			switch (sort)
			{
				case SortKey::Slot:     return lhs < rhs;
				case SortKey::Name:     return a.name.compare(b.name) < 0;
				case SortKey::Sector:   return a.sSector < b.sSector;
				case SortKey::Contract:
				{
					const INT32 ax = a.iEndofContractTime;
					const INT32 bx = b.iEndofContractTime;
					return ax < bx;
				}
				case SortKey::Life:     return a.bLife < b.bLife;
			}
			return false;
		};
		std::sort(slots.begin(), slots.end(),
			[&](INT8 l, INT8 r) { return ascend ? cmp(l, r) : cmp(r, l); });

		Console_Println(ST::format(
			"{} merc{} ({}):",
			slots.size(), slots.size() == 1 ? "" : "s",
			filterWord(filter)));

		if (slots.empty())
		{
			Console_Println("(no matches)");
			return;
		}

		// Re-tag in display order. tN tags reflect the rows the user just
		// saw, which means subsequent `team merc t3` resolves against
		// what they heard. The tag table is shared with `assign list`
		// via Console_Strategic, so either roster wins the namespace.
		Console_RegisterTeamTags(slots);
		for (INT8 slot : slots)
		{
			Console_Println(mercRow(slot, *gCharactersList[slot].merc));
		}
	}

	// ----- team merc --------------------------------------------------

	const char* mercKindWord(const SOLDIERTYPE& s)
	{
		switch (s.ubWhatKindOfMercAmI)
		{
			case MERC_TYPE__PLAYER_CHARACTER: return "IMP";
			case MERC_TYPE__AIM_MERC:         return "AIM";
			case MERC_TYPE__MERC:             return "M.E.R.C.";
			case MERC_TYPE__NPC:              return "RPC";
			case MERC_TYPE__EPC:              return "EPC";
			case MERC_TYPE__VEHICLE:          return "vehicle";
			case MERC_TYPE__NPC_WITH_UNEXTENDABLE_CONTRACT: return "RPC (fixed contract)";
		}
		return "merc";
	}

	void cmdTeamMerc(const std::vector<std::string>& args)
	{
		if (!Console_RequireCampaign()) return;

		INT8 slot;
		if (args.size() < 3)
		{
			// Fall back to the info-pane focus, matching the GUI.
			SOLDIERTYPE* const focus = GetSelectedInfoChar();
			if (!focus)
			{
				Console_Println("usage: team merc <name|tN>");
				return;
			}
			slot = -1;
			for (INT8 i = 0; i < MAX_CHARACTER_COUNT; ++i)
				if (gCharactersList[i].merc == focus) { slot = i; break; }
			if (slot < 0)
			{
				Console_Println("usage: team merc <name|tN>");
				return;
			}
		}
		else
		{
			ST::string err;
			slot = Console_ResolveCharSlot(joinFrom(args, 2), err);
			if (slot < 0) { Console_Println(err); return; }
		}

		const SOLDIERTYPE& s = *gCharactersList[slot].merc;
		MERCPROFILESTRUCT const& p = GetProfile(s.ubProfile);

		// Header.
		ST::string header;
		if (!p.zNickname.empty() && p.zNickname != p.zName)
			header = ST::format("{} ({})", p.zNickname, p.zName);
		else
			header = p.zName.empty() ? s.name : p.zName;
		Console_Println(ST::format(
			"{}, level {}, {}.",
			header, s.bExpLevel, mercKindWord(s)));

		// Status line.
		const ST::string loc  = GetMapscreenMercLocationString(s);
		const ST::string asg  = GetMapscreenMercAssignmentString(s);
		const ST::string dest = GetMapscreenMercDestinationString(s);
		ST::string statusLine = ST::format("  Sector: {}.  ", loc);
		if (Console_IsOnSquad(s))
			statusLine += ST::format("Squad {}.  ", Console_SquadNumber(s));
		statusLine += ST::format("Assignment: {}.", asg);
		Console_Println(statusLine);
		if (!dest.empty() && dest != loc)
		{
			Console_Println(ST::format("  Destination: {}.", dest));
		}

		// Contract block — only AIM mercs and SLAY have an end-of-contract.
		if (s.ubWhatKindOfMercAmI == MERC_TYPE__AIM_MERC || s.ubProfile == SLAY)
		{
			Console_Println(ST::format("  Contract: {}. Daily {}.",
				verboseTimeRemaining(s), SPrintMoney(p.sSalary)));
			Console_Println(ST::format(
				"  Extend: 1 day {}, 1 week {}, 2 weeks {}.",
				SPrintMoney(p.sSalary),
				SPrintMoney(static_cast<INT32>(p.uiWeeklySalary)),
				SPrintMoney(static_cast<INT32>(p.uiBiWeeklySalary))));
			if (p.bMedicalDeposit)
				Console_Println(ST::format("  Medical deposit: {}.",
					SPrintMoney(p.sMedicalDepositAmount)));
		}
		else if (s.ubWhatKindOfMercAmI == MERC_TYPE__MERC)
		{
			Console_Println(ST::format("  M.E.R.C. merc. Daily {}.",
				SPrintMoney(p.sSalary)));
		}

		// Health block.
		Console_Println(ST::format(
			"  Health: life {}/{}, breath {}/{}. Morale: {}.",
			s.bLife, s.bLifeMax, s.bBreath, s.bBreathMax,
			GetMoraleString(s)));
		if (Console_IsAsleep(s))
			Console_Println("  Sleep: asleep.");
		else if (s.bBreathMax <= BREATHMAX_PRETTY_TIRED)
			Console_Println("  Sleep: tired (breath max reduced).");
	}

	// ============================================================
	//   `map` — strategic geography
	// ============================================================

	// ----- sector-id parser --------------------------------------------
	// Three forms: grid notation (A9, optional -1/-2/-3 underground
	// suffix), town name (Drassen → primary sector), or "here" (sSelMap).
	bool resolveSectorArg(const std::string& wantRaw, SGPSector& out, ST::string& err)
	{
		std::string want = lower(wantRaw);
		if (want.empty()) { err = ST::string("missing sector"); return false; }

		if (want == "here") { out = sSelMap; return true; }

		// Underground suffix -1 / -2 / -3 — split off before the rest.
		INT8 z = 0;
		const std::size_t dash = want.rfind('-');
		if (dash != std::string::npos && dash > 0)
		{
			long zv;
			if (parseInt(want.substr(dash + 1), zv) && zv >= 1 && zv <= 3)
			{
				z = static_cast<INT8>(zv);
				want = want.substr(0, dash);
			}
		}

		// Grid notation: leading letter A..P followed by digits.
		if (want.size() >= 2 && want[0] >= 'a' && want[0] <= 'p')
		{
			bool allDigits = true;
			for (std::size_t i = 1; i < want.size(); ++i)
				if (!std::isdigit(static_cast<unsigned char>(want[i])))
				{
					allDigits = false; break;
				}
			if (allDigits)
			{
				// FromShortString takes "A9" — reuse it but add z afterwards.
				out = SGPSector::FromShortString(ST::string(want.c_str()), z);
				if (!out.IsValid())
				{
					err = ST::format("sector {} out of range (A1..P16, optional -1..-3)",
						wantRaw);
					return false;
				}
				return true;
			}
		}

		// Town name lookup.
		for (auto const& kv : GCM->getTowns())
		{
			if (!kv.second) continue;
			if (lower(kv.second->name.to_std_string()) == want ||
			    lower(kv.second->internalName.to_std_string()) == want)
			{
				out = kv.second->getBaseSector();
				out.z = z;
				return true;
			}
		}

		err = ST::format("not a sector: {} (try grid e.g. C9, town name, or 'here')",
			wantRaw);
		return false;
	}

	// ----- map (current view) -----------------------------------------

	const char* compressWord(INT32 m)
	{
		switch (m)
		{
			case TIME_COMPRESS_X0:    return "paused";
			case TIME_COMPRESS_X1:    return "real time";
			case TIME_COMPRESS_5MINS: return "5-minute steps";
			case TIME_COMPRESS_30MINS:return "30-minute steps";
			case TIME_COMPRESS_60MINS:return "1-hour steps";
		}
		return "?";
	}

	// What the user actually wants to hear: the *effective* state, which
	// can be paused even when giTimeCompressMode still holds the previous
	// rate (StopTimeCompression deliberately preserves the rate so resume
	// goes back to the same speed). IsTimeBeingCompressed encodes the full
	// "is time advancing right now" check.
	ST::string compressionState()
	{
		if (!IsTimeBeingCompressed()) return ST::string("paused");
		return ST::string(compressWord(giTimeCompressMode));
	}

	void cmdMapView()
	{
		if (!Console_RequireCampaign()) return;
		const ST::string sel = GetSectorIDString(sSelMap, FALSE);
		Console_Println(ST::format(
			"Selected sector: {}. Z-level: {}. Time {02d}:{02d}, day {}. Compression: {}.",
			sel, static_cast<int>(sSelMap.z),
			guiHour, guiMin, guiDay,
			compressionState()));
		if (gsHighlightSector.IsValid() && gsHighlightSector != sSelMap)
		{
			Console_Println(ST::format("Highlighted (under cursor): {}.",
				GetSectorIDString(gsHighlightSector, FALSE)));
		}
	}

	// ----- militia helpers --------------------------------------------
	// The engine paints greens / regulars / elites separately on the
	// militia popup. The breakdown lives in the SECTORINFO array as
	// ubNumberOfCivsAtLevel[GREEN_MILITIA / REGULAR_MILITIA / ELITE_MILITIA].
	struct MilitiaCount { int green = 0, regular = 0, elite = 0; int total() const { return green + regular + elite; } };

	MilitiaCount militiaInSector(const SGPSector& sector)
	{
		MilitiaCount c{};
		if (sector.z != 0) return c;
		SECTORINFO& si = SectorInfo[sector.AsByte()];
		c.green   = si.ubNumberOfCivsAtLevel[GREEN_MILITIA];
		c.regular = si.ubNumberOfCivsAtLevel[REGULAR_MILITIA];
		c.elite   = si.ubNumberOfCivsAtLevel[ELITE_MILITIA];
		return c;
	}

	ST::string militiaDescription(const MilitiaCount& c)
	{
		if (c.total() == 0) return ST::string("no militia");
		ST::string parts;
		if (c.green)   parts += ST::format("{} green", c.green);
		if (c.regular) parts += ST::format("{}{} regular", parts.empty() ? "" : ", ", c.regular);
		if (c.elite)   parts += ST::format("{}{} elite", parts.empty() ? "" : ", ", c.elite);
		return parts;
	}

	// ----- intel / enemies -------------------------------------------

	ST::string enemyIntelString(const SGPSector& sector)
	{
		SECTORINFO const& si = SectorInfo[sector.AsByte()];
		const UINT32 known = WhatPlayerKnowsAboutEnemiesInSector(sector);
		switch (known)
		{
			case KNOWS_NOTHING:     return ST::string("no known enemies");
			case KNOWS_THEYRE_THERE: return ST::string("enemies present (count unknown)");
			case KNOWS_HOW_MANY:
			{
				const int n =
					si.ubNumAdmins + si.ubNumTroops + si.ubNumElites;
				return ST::format("{} enemies", n);
			}
		}
		return ST::string("unknown");
	}

	// ----- map sector -------------------------------------------------

	void printMineLine(INT8 mineId);  // forward

	void cmdMapSector(const std::vector<std::string>& args)
	{
		if (!Console_RequireCampaign()) return;
		if (args.size() < 3)
		{
			Console_Println("usage: map sector <id|town|here>");
			return;
		}
		SGPSector sec;
		ST::string err;
		if (!resolveSectorArg(joinFrom(args, 2), sec, err))
		{
			Console_Println(err);
			return;
		}

		Console_Println(ST::format("{}{}",
			GetSectorIDString(sec, FALSE),
			sec.z != 0 ? ST::format(" (level {})", static_cast<int>(sec.z)) : ST::string()));

		// Surface control.
		if (sec.z == 0)
		{
			const auto& el = StrategicMap[sec.AsStrategicIndex()];
			Console_Println(ST::format("  Control: {}.",
				el.fEnemyControlled ? "enemy" : "player"));
		}

		// Town membership.
		const INT8 townId = GetTownIdForSector(sec);
		if (townId != BLANK_SECTOR)
		{
			const TownModel* const town = GCM->getTown(townId);
			ST::string line = ST::format("  Town: {}.", town ? town->name : ST::string("?"));
			if (gfTownUsesLoyalty[townId])
				line += ST::format(" Loyalty {}%.", gTownLoyalty[townId].ubRating);
			line += ST::format(" {} of {} sectors held.",
				GetTownSectorsUnderControl(townId), GetTownSectorSize(townId));
			Console_Println(line);
		}

		// Militia (surface only).
		if (sec.z == 0)
		{
			const MilitiaCount c = militiaInSector(sec);
			Console_Println(ST::format("  Militia: {}.", militiaDescription(c)));
		}

		// Enemy intel (surface only — underground intel lives elsewhere).
		if (sec.z == 0)
			Console_Println(ST::format("  Enemy intel: {}.", enemyIntelString(sec)));

		// Mine status.
		const INT8 mineId = GetIdOfMineForSector(sec);
		if (mineId >= 0)
		{
			Console_Println("  Mine:");
			printMineLine(mineId);
		}

		// SAM site.
		if (sec.z == 0 && IsThisSectorASAMSector(sec))
		{
			const auto& el = StrategicMap[sec.AsStrategicIndex()];
			Console_Println(ST::format(
				"  SAM site: condition {}/100. {}",
				el.bSAMCondition,
				IsThereAFunctionalSAMSiteInSector(sec) ? "Functional." : "Non-functional."));
		}

		// Mercs in or transiting through this sector.
		std::vector<const SOLDIERTYPE*> here;
		CFOR_EACH_IN_CHAR_LIST(c)
		{
			const SOLDIERTYPE& s = *c->merc;
			if (s.sSector == sec) here.push_back(&s);
		}
		if (!here.empty())
		{
			ST::string names;
			for (const SOLDIERTYPE* m : here)
			{
				if (!names.empty()) names += ", ";
				names += m->name;
			}
			Console_Println(ST::format("  Mercs: {}.", names));
		}
	}

	// ----- map list ---------------------------------------------------

	void printMineLine(INT8 mineId)
	{
		const auto& mines = GCM->getMines();
		if (mineId < 0 || static_cast<std::size_t>(mineId) >= mines.size())
		{
			Console_Println("    (mine id out of range)");
			return;
		}
		const MineModel* const m = mines[mineId];
		const auto& st = gMineStatus[mineId];
		const SGPSector entrance = SGPSector(m->entranceSector);
		ST::string head = ST::format("    {} ({}).",
			GetSectorIDString(entrance, FALSE),
			m->mineType == GOLD_MINE ? "gold" : "silver");
		if (st.fShutDown)
			head += ST::format(" SHUT DOWN{}.",
				st.fShutDownIsPermanent ? " (permanent)" : "");
		else if (st.fEmpty)
			head += ST::string(" Empty.");
		Console_Println(head);

		if (st.fSpokeToHeadMiner && !st.fEmpty && !st.fShutDown)
		{
			Console_Println(ST::format(
				"    Predicted income: {}/day, max {}/period.",
				SPrintMoney(static_cast<INT32>(PredictDailyIncomeFromAMine(mineId))),
				SPrintMoney(static_cast<INT32>(GetMaxPeriodicRemovalFromMine(mineId)))));
			Console_Println(ST::format("    Total left: {}.",
				SPrintMoney(GetTotalLeftInMine(mineId))));
		}
		else if (!st.fSpokeToHeadMiner)
		{
			Console_Println("    Speak to the head miner to get production estimates.");
		}

		const INT8 townId = GetTownAssociatedWithMine(mineId);
		if (townId != BLANK_SECTOR && gfTownUsesLoyalty[townId])
		{
			Console_Println(ST::format("    Associated town loyalty: {}%.",
				gTownLoyalty[townId].ubRating));
		}
	}

	void cmdMapListTowns()
	{
		if (!Console_RequireCampaign()) return;
		Console_Println("Towns:");
		for (auto const& kv : GCM->getTowns())
		{
			const TownModel* const t = kv.second;
			if (!t) continue;
			ST::string line = ST::format("  {}: {} of {} sectors held",
				t->name,
				GetTownSectorsUnderControl(t->townId),
				GetTownSectorSize(t->townId));
			if (gfTownUsesLoyalty[t->townId])
				line += ST::format(", loyalty {}%",
					gTownLoyalty[t->townId].ubRating);
			Console_Println(line + ST::string("."));
		}
	}

	void cmdMapListMines()
	{
		if (!Console_RequireCampaign()) return;
		const auto& mines = GCM->getMines();
		Console_Println(ST::format("Mines ({}):", mines.size()));
		for (std::size_t i = 0; i < mines.size(); ++i)
			printMineLine(static_cast<INT8>(i));
	}

	void cmdMapListSams()
	{
		if (!Console_RequireCampaign()) return;
		// SAM sectors aren't listed centrally — iterate the surface grid.
		Console_Println("SAM sites:");
		std::size_t shown = 0;
		for (INT16 y = 1; y <= 16; ++y)
		for (INT16 x = 1; x <= 16; ++x)
		{
			SGPSector s(x, y);
			if (!IsThisSectorASAMSector(s)) continue;
			++shown;
			const auto& el = StrategicMap[s.AsStrategicIndex()];
			Console_Println(ST::format(
				"  {}: condition {}/100, {}, {}.",
				GetSectorIDString(s, FALSE),
				el.bSAMCondition,
				el.fEnemyControlled ? "enemy" : "player",
				IsThereAFunctionalSAMSiteInSector(s) ? "functional" : "non-functional"));
		}
		if (shown == 0) Console_Println("  (none).");
	}

	void cmdMapListMilitia()
	{
		if (!Console_RequireCampaign()) return;
		Console_Println("Sectors with militia:");
		std::size_t shown = 0;
		for (INT16 y = 1; y <= 16; ++y)
		for (INT16 x = 1; x <= 16; ++x)
		{
			SGPSector s(x, y);
			const MilitiaCount c = militiaInSector(s);
			if (c.total() == 0) continue;
			++shown;
			Console_Println(ST::format("  {}: {}.",
				GetSectorIDString(s, FALSE), militiaDescription(c)));
		}
		if (shown == 0) Console_Println("  (none).");
	}

	void cmdMapListEnemies()
	{
		if (!Console_RequireCampaign()) return;
		std::vector<SGPSector> known, partial;
		for (INT16 y = 1; y <= 16; ++y)
		for (INT16 x = 1; x <= 16; ++x)
		{
			SGPSector s(x, y);
			const UINT32 k = WhatPlayerKnowsAboutEnemiesInSector(s);
			if (k == KNOWS_HOW_MANY) known.push_back(s);
			else if (k == KNOWS_THEYRE_THERE) partial.push_back(s);
		}

		Console_Println(ST::format(
			"Known enemies on the map ({} sectors with confirmed counts, {} suspected):",
			known.size(), partial.size()));
		for (const SGPSector& s : known)
			Console_Println(ST::format("  {}: {}.",
				GetSectorIDString(s, FALSE), enemyIntelString(s)));
		for (const SGPSector& s : partial)
			Console_Println(ST::format("  {}: enemies present (count unknown).",
				GetSectorIDString(s, FALSE)));
		if (known.empty() && partial.empty())
			Console_Println("  (no enemy intel).");
	}

	// ----- map town / map mine ----------------------------------------

	void cmdMapTown(const std::vector<std::string>& args)
	{
		if (!Console_RequireCampaign()) return;
		if (args.size() < 3)
		{
			Console_Println("usage: map town <name>");
			return;
		}
		const std::string want = joinFrom(args, 2);
		const TownModel* found = nullptr;
		for (auto const& kv : GCM->getTowns())
		{
			if (!kv.second) continue;
			if (lower(kv.second->name.to_std_string()) == want ||
			    lower(kv.second->internalName.to_std_string()) == want)
			{
				found = kv.second; break;
			}
		}
		if (!found)
		{
			Console_Println(ST::format("no town named: {}", joinFrom(args, 2)));
			return;
		}
		Console_Println(ST::format("{} ({} sector{}):",
			found->name,
			found->sectorIDs.size(),
			found->sectorIDs.size() == 1 ? "" : "s"));
		Console_Println(ST::format(
			"  Sectors held: {} of {}.{}",
			GetTownSectorsUnderControl(found->townId),
			GetTownSectorSize(found->townId),
			gfTownUsesLoyalty[found->townId]
				? ST::format(" Loyalty: {}%.", gTownLoyalty[found->townId].ubRating)
				: ST::string()));
		if (!found->isMilitiaTrainingAllowed)
			Console_Println("  Militia training: not available.");

		// Per-sector roll-up.
		MilitiaCount totalMil{};
		for (UINT8 sid : found->sectorIDs)
		{
			SGPSector s(sid);
			const auto& el = StrategicMap[s.AsStrategicIndex()];
			const MilitiaCount mc = militiaInSector(s);
			totalMil.green   += mc.green;
			totalMil.regular += mc.regular;
			totalMil.elite   += mc.elite;
			Console_Println(ST::format("  {}: {}, {}.",
				GetSectorIDString(s, FALSE),
				el.fEnemyControlled ? "enemy" : "player",
				militiaDescription(mc)));
		}
		Console_Println(ST::format("  Total militia: {}.", militiaDescription(totalMil)));
	}

	void cmdMapMine(const std::vector<std::string>& args)
	{
		if (!Console_RequireCampaign()) return;
		if (args.size() < 3)
		{
			Console_Println("usage: map mine <town>");
			return;
		}
		const std::string want = joinFrom(args, 2);

		// Resolve "drassen" / "alma" / etc. to the mine via its associated town.
		INT8 mineId = -1;
		const auto& mines = GCM->getMines();
		for (std::size_t i = 0; i < mines.size(); ++i)
		{
			const TownModel* const t = GCM->getTown(mines[i]->associatedTownId);
			if (!t) continue;
			if (lower(t->name.to_std_string()) == want ||
			    lower(t->internalName.to_std_string()) == want)
			{
				mineId = static_cast<INT8>(i); break;
			}
		}
		if (mineId < 0)
		{
			Console_Println(ST::format("no mine for: {}", joinFrom(args, 2)));
			return;
		}
		const TownModel* const t = GCM->getTown(mines[mineId]->associatedTownId);
		Console_Println(ST::format("{} mine:",
			t ? t->name : ST::string("?")));
		printMineLine(mineId);
	}

	// ----- map dispatch -----------------------------------------------

	void cmdMapList(const std::vector<std::string>& args)
	{
		if (args.size() < 3)
		{
			Console_Println("usage: map list towns|mines|sams|militia|enemies");
			return;
		}
		const std::string what = lower(args[2]);
		if      (what == "towns")   cmdMapListTowns();
		else if (what == "mines")   cmdMapListMines();
		else if (what == "sams")    cmdMapListSams();
		else if (what == "militia") cmdMapListMilitia();
		else if (what == "enemies") cmdMapListEnemies();
		else Console_Println(ST::format(
			"unknown list: {} (try towns|mines|sams|militia|enemies)", args[2]));
	}

	// ============================================================
	//   `team sleep`
	// ============================================================

	void cmdTeamSleep(const std::vector<std::string>& args)
	{
		if (!Console_RequireCampaign()) return;
		if (args.size() < 4)
		{
			Console_Println("usage: team sleep <name|tN> <on|off>");
			return;
		}
		ST::string err;
		const INT8 slot = Console_ResolveCharSlot(args[2], err);
		if (slot < 0) { Console_Println(err); return; }
		SOLDIERTYPE& s = *gCharactersList[slot].merc;

		const std::string what = lower(args.back());
		bool wantSleep;
		if      (what == "on" || what == "asleep" || what == "sleep") wantSleep = true;
		else if (what == "off" || what == "awake" || what == "wake")  wantSleep = false;
		else
		{
			Console_Println(ST::format("unknown arg: {} (try on|off)", args.back()));
			return;
		}

		if (!CanChangeSleepStatusForSoldier(&s))
		{
			Console_Println(ST::format("{}: sleep state can't change right now.", s.name));
			return;
		}

		bool ok;
		if (wantSleep)
		{
			ok = SetMercAsleep(s, /*give_warning=*/false);
		}
		else
		{
			ok = SetMercAwake(&s, /*fGiveWarning=*/FALSE, /*fForceHim=*/FALSE) != FALSE;
		}

		if (!ok)
		{
			Console_Println(ST::format("{}: refused (engine declined the change).", s.name));
			return;
		}
		Console_Println(ST::format("{}: now {}.",
			s.name, wantSleep ? "asleep" : "awake"));
	}

	// ============================================================
	//   `map level`
	// ============================================================

	void cmdMapLevel(const std::vector<std::string>& args)
	{
		if (!Console_RequireCampaign()) return;
		if (args.size() < 3)
		{
			Console_Println(ST::format(
				"usage: map level <0|1|2|3>  (current level: {})",
				static_cast<int>(iCurrentMapSectorZ)));
			return;
		}
		long z;
		if (!parseInt(args[2], z) || z < 0 || z > 3)
		{
			Console_Println(ST::format(
				"unknown level: {} (try 0 surface, 1..3 underground)", args[2]));
			return;
		}
		JumpToLevel(static_cast<INT32>(z));
		Console_Println(ST::format("Map level: {}.", static_cast<int>(z)));
	}

	// ============================================================
	//   `map move` / `map cancel`
	// ============================================================

	// Translate MoveError to the same string the engine paints in
	// ReportMapScreenMovementError. pMapErrorString carries pre-localized
	// strings that may include %ls placeholders for soldier name; we don't
	// substitute them, so messages with placeholders may look raw — that's
	// rare in practice (most are unconditional like "the merc is in transit").
	ST::string moveErrorString(MoveError code, const SOLDIERTYPE& s)
	{
		if (code <= ME_OK) return ST::string();
		const auto idx = static_cast<int>(code);
		ST::string raw = pMapErrorString[idx];
		if (raw.empty())
		{
			return ST::format("{}: can't move (engine code {}).",
				s.name, idx);
		}
		return ST::format("{}: {}", s.name, raw);
	}

	// Resolve which merc to use as the movement-group handle. Strategic
	// movement operates on squads, not mercs; we just need any merc in the
	// target squad to feed PlotPathForCharacter and the engine moves the
	// whole squad. Order:
	//   - explicit `from <name|tN>` → that merc's squad
	//   - else the info-pane focus (bSelectedInfoChar) → its squad
	//   - else error
	// Vehicles are allowed (vehicles are slots t18..t20 and move via
	// PlotPathForCharacter the same way mercs do).
	INT8 resolveMoveHandle(const std::vector<std::string>& args, ST::string& err)
	{
		for (std::size_t i = 2; i + 1 < args.size(); ++i)
		{
			if (lower(args[i]) == "from")
			{
				return Console_ResolveCharSlot(args[i + 1], err);
			}
		}
		const SOLDIERTYPE* const focus = GetSelectedInfoChar();
		if (focus)
		{
			for (INT8 i = 0; i < MAX_CHARACTER_COUNT; ++i)
				if (gCharactersList[i].merc == focus) return i;
		}
		err = ST::string(
			"no merc focused. Use 'team merc <name|tN>' to focus one, "
			"or 'map move <sector> from <name>'.");
		return -1;
	}

	// Print one path's route + ETA. PathSt nodes carry strategic indices
	// in uiSectorId, which round-trip through SGPSector::FromStrategicIndex.
	// ETA comes from CalculateTravelTimeOfGroup against the group's
	// waypoints, not GetPathTravelTimeDuringPlotting — the latter bails
	// early when bSelectedDestChar/fTempPathAlreadyDrawn aren't set, both
	// of which are GUI-mode flags the console never touches.
	void printPathSummary(const SOLDIERTYPE& s)
	{
		PathSt* head = GetSoldierMercPathPtr(&s);
		if (!head)
		{
			Console_Println(ST::format("  {}: no path.", s.name));
			return;
		}
		ST::string route;
		std::size_t hops = 0;
		for (PathSt* n = head; n; n = n->pNext, ++hops)
		{
			if (!route.empty()) route += " -> ";
			route += GetSectorIDString(
				SGPSector::FromStrategicIndex(static_cast<UINT16>(n->uiSectorId)),
				FALSE);
		}
		const GROUP* const grp = GetGroup(s.ubGroupID);
		const INT32 mins = grp ? CalculateTravelTimeOfGroup(grp) : 0;
		const INT32 hrs  = mins / 60;
		const INT32 rem  = mins % 60;
		ST::string eta;
		if (hrs > 0)
			eta = ST::format("{}h{02d}m", hrs, rem);
		else
			eta = ST::format("{}m", rem);
		// hops counts entries including the start sector; movement legs
		// are hops-1.
		Console_Println(ST::format("  {}: {} ({} sector{}, ~{}).",
			s.name, route,
			hops > 0 ? hops - 1 : 0,
			hops == 2 ? "" : "s",
			eta));
	}

	void cmdMapMove(const std::vector<std::string>& args)
	{
		if (!Console_RequireCampaign()) return;
		if (args.size() < 3)
		{
			Console_Println(
				"usage: map move <sector> [from <name|tN>] [keep-path]");
			return;
		}

		// Pull flags + remaining args. The destination is the first arg
		// that isn't a flag and isn't part of `from <name>`.
		bool keepPath = false;
		std::string destRaw;
		for (std::size_t i = 2; i < args.size(); ++i)
		{
			const std::string a = lower(args[i]);
			if (a == "keep-path")            { keepPath = true; continue; }
			if (a == "from" && i + 1 < args.size()) { ++i; continue; }
			if (destRaw.empty()) destRaw = args[i];
		}
		if (destRaw.empty())
		{
			Console_Println(
				"usage: map move <sector> [from <name|tN>] [keep-path]");
			return;
		}

		SGPSector dest;
		ST::string err;
		if (!resolveSectorArg(destRaw, dest, err))
		{
			Console_Println(err);
			return;
		}
		if (dest.z != 0)
		{
			Console_Println(ST::format(
				"can't plot to underground ({}). Exit underground via tactical first.",
				GetSectorIDString(dest, FALSE)));
			return;
		}

		const INT8 handle = resolveMoveHandle(args, err);
		if (handle < 0) { Console_Println(err); return; }
		SOLDIERTYPE& s = *gCharactersList[handle].merc;

		const MoveError code = CanEntireMovementGroupMercIsInMove(s);
		if (code != ME_OK)
		{
			Console_Println(moveErrorString(code, s));
			return;
		}
		if (s.sSector == dest)
		{
			Console_Println(ST::format("{}: already in {}.",
				s.name, GetSectorIDString(dest, FALSE)));
			return;
		}

		// Plot. Replace semantics by default (CancelPathForCharacter
		// before plotting); keep-path appends. PlotPathForCharacter only
		// touches pMercPath; the strategic clock acts on the *group's*
		// waypoint list, so without RebuildWayPointsForGroupPath the path
		// is dead data and the group never moves.
		if (!keepPath) CancelPathForCharacter(&s);
		PlotPathForCharacter(s, dest, /*tactical_traversal=*/false);
		PathSt* const head = GetSoldierMercPathPtr(&s);
		if (!head)
		{
			Console_Println(
				"Plotting failed (no path attached). Underground or unreachable destination?");
			return;
		}
		GROUP* const grp = GetGroup(s.ubGroupID);
		if (grp) RebuildWayPointsForGroupPath(head, *grp);

		// Name the squad and its members so the user can hear *who* is
		// travelling. The vanilla model (manual p.41): "send an entire
		// squad to a far-off sector — choose any merc in that squad."
		// The handle merc is a stand-in; the whole squad goes.
		ST::string who;
		if (Console_IsOnSquad(s))
		{
			const int sq = Console_SquadNumber(s);
			ST::string members;
			// Squad[] entries can be null — the engine's FOR_EACH_IN_SQUAD
			// macro guards on `if (!*iter) continue` for the same reason.
			FOR_EACH_IN_SQUAD(it, sq - 1)
			{
				if (!members.empty()) members += ", ";
				members += (*it)->name;
			}
			who = ST::format("Squad {} ({})", sq, members);
		}
		else
		{
			who = s.name;
		}

		Console_Println(ST::format("Plotted route to {} for {}.",
			GetSectorIDString(dest, FALSE), who));
		printPathSummary(s);
		if (giTimeCompressMode == TIME_COMPRESS_X0)
			Console_Println(
				"Time is paused — use 'compress fast' to start moving.");
	}

	void cmdMapCancel(const std::vector<std::string>& args)
	{
		if (!Console_RequireCampaign()) return;

		// No arg: cancel for the focused merc's squad (mirrors map move's
		// default-handle model). With <name>: cancel for that merc's squad.
		INT8 slot;
		if (args.size() < 3)
		{
			const SOLDIERTYPE* const focus = GetSelectedInfoChar();
			if (!focus)
			{
				Console_Println(
					"no merc focused. Use 'team merc <name|tN>' first, "
					"or 'map cancel <name|tN>'.");
				return;
			}
			slot = -1;
			for (INT8 i = 0; i < MAX_CHARACTER_COUNT; ++i)
				if (gCharactersList[i].merc == focus) { slot = i; break; }
			if (slot < 0)
			{
				Console_Println("focused merc not found in mapscreen list.");
				return;
			}
		}
		else
		{
			ST::string err;
			slot = Console_ResolveCharSlot(joinFrom(args, 2), err);
			if (slot < 0) { Console_Println(err); return; }
		}

		SOLDIERTYPE& s = *gCharactersList[slot].merc;
		if (!GetSoldierMercPathPtr(&s))
		{
			Console_Println(ST::format("{}: no path to cancel.", s.name));
			return;
		}
		CancelPathForCharacter(&s);
		Console_Println(Console_IsOnSquad(s)
			? ST::format("Squad {}: path cancelled.", Console_SquadNumber(s))
			: ST::format("{}: path cancelled.", s.name));
	}

	// ============================================================
	//   `compress` / `tactical` / `log` / `quit`
	// ============================================================

	void cmdCompress(const std::vector<std::string>& args)
	{
		if (!Console_RequireCampaign()) return;
		if (args.size() < 2)
		{
			Console_Println(ST::format(
				"compression: {}. Use 'compress <off|fast|faster|fastest>'.",
				compressionState()));
			return;
		}
		const std::string what = lower(args[1]);
		INT32 want;
		if      (what == "off"     || what == "stop" || what == "pause")  want = TIME_COMPRESS_X0;
		else if (what == "x1"      || what == "real" || what == "normal") want = TIME_COMPRESS_X1;
		else if (what == "fast"    || what == "5"    || what == "5min")   want = TIME_COMPRESS_5MINS;
		else if (what == "faster"  || what == "30"   || what == "30min")  want = TIME_COMPRESS_30MINS;
		else if (what == "fastest" || what == "60"   || what == "hour")   want = TIME_COMPRESS_60MINS;
		else
		{
			Console_Println(ST::format(
				"unknown speed: {} (try off|fast|faster|fastest)", args[1]));
			return;
		}

		// X1 (real time) only exists in tactical. SetGameTimeCompressionLevel
		// silently remaps X1→X0 in mapscreen; surfacing that as a no-op is
		// confusing, so name it.
		if (want == TIME_COMPRESS_X1 && guiCurrentScreen != GAME_SCREEN)
		{
			Console_Println(
				"x1 (real time) is only available in tactical. "
				"On mapscreen use off | fast | faster | fastest.");
			return;
		}

		if (want == TIME_COMPRESS_X0)
		{
			// Preserves giTimeCompressMode for resume; flips
			// gfTimeCompressionOn off.
			StopTimeCompression();
		}
		else
		{
			// Direct set is the right entry — engine handles
			// AllowedToTimeCompress check, prebattle/combat blocks, and
			// the X1 remap. Step-loop via Increase/Decrease can't reach
			// arbitrary targets in mapscreen because both helpers skip X1.
			SetGameTimeCompressionLevel(static_cast<UINT32>(want));
		}
		Console_Println(ST::format("Compression: {}.", compressionState()));
	}

	void cmdTactical()
	{
		switch (guiCurrentScreen)
		{
			case GAME_SCREEN:
				Console_Println("Already in tactical.");
				return;
			case MAP_SCREEN:
				if (!AllowedToExitFromMapscreenTo(MAP_EXIT_TO_TACTICAL))
				{
					Console_Println(
						"Can't switch to tactical from this screen state.");
					return;
				}
				RequestTriggerExitFromMapscreen(MAP_EXIT_TO_TACTICAL);
				Console_Println("Switching to tactical.");
				return;
			case LAPTOP_SCREEN:
				// Laptop's exit machinery is file-static; from outside we
				// can't drive it cleanly. Send the user back through
				// mapscreen, where the same verb works.
				Console_Println(
					"Close laptop first (click its Exit button via 'g click', "
					"or use the Exit option in the laptop UI), then run 'tactical'.");
				return;
			default:
				Console_Println("Can't switch to tactical from this screen.");
				return;
		}
	}

	void cmdQuit()
	{
		// Exit the application — same path the OS window's close button
		// takes. requestGameExit posts SDL_QUIT, which the main loop
		// catches and routes through deinitGameAndExit (the Dead-is-Dead
		// auto-save and engine teardown live there). No confirm: the user
		// typed the verb, that is the confirmation.
		Console_Println("Exiting.");
		requestGameExit();
	}

	void cmdLog(const std::vector<std::string>& args)
	{
		std::size_t n = MAX_MESSAGES_ON_MAP_BOTTOM;
		if (args.size() >= 2)
		{
			long v;
			if (!parseInt(args[1], v) || v <= 0)
			{
				Console_Println(ST::format(
					"usage: log [N]  (default {})", MAX_MESSAGES_ON_MAP_BOTTOM));
				return;
			}
			n = static_cast<std::size_t>(v);
		}
		std::size_t shown = 0;
		Console_ForEachRecentMapMessage(n,
			[&](const ST::string& s) {
				Console_Println(ST::format("  {}", s));
				++shown;
			});
		if (shown == 0)
		{
			Console_Println("No strategic messages yet.");
		}
		else
		{
			Console_Println(ST::format("({} message{} shown.)",
				shown, shown == 1 ? "" : "s"));
		}
	}
}

void Cmd_Team(const std::vector<std::string>& args)
{
	Console_EnsureMapscreen();
	if (args.size() < 2) { cmdTeamSummary(); return; }
	const std::string sub = lower(args[1]);
	if (sub == "list")   { cmdTeamList(args);   return; }
	if (sub == "merc")   { cmdTeamMerc(args);   return; }
	if (sub == "sleep")  { cmdTeamSleep(args);  return; }

	Console_Println(ST::format(
		"unknown subcommand: team {} (try 'team', 'team list', "
		"'team merc <name|tN>', 'team sleep <name|tN> <on|off>')",
		args[1]));
}

void Cmd_Laptop(const std::vector<std::string>&)
{
	switch (guiCurrentScreen)
	{
		case LAPTOP_SCREEN:
			Console_Println("Already in laptop.");
			return;
		case GAME_SCREEN:
			LeaveTacticalScreen(LAPTOP_SCREEN);
			Console_Println("Opening laptop.");
			return;
		case MAP_SCREEN:
			RequestTriggerExitFromMapscreen(MAP_EXIT_TO_LAPTOP);
			Console_Println("Opening laptop.");
			return;
		default:
			Console_Println("Can't open the laptop from this screen.");
			return;
	}
}

void Cmd_Map(const std::vector<std::string>& args)
{
	Console_EnsureMapscreen();
	if (args.size() < 2) { cmdMapView(); return; }
	const std::string sub = lower(args[1]);
	if (sub == "sector") { cmdMapSector(args); return; }
	if (sub == "list")   { cmdMapList(args);   return; }
	if (sub == "town")   { cmdMapTown(args);   return; }
	if (sub == "mine")   { cmdMapMine(args);   return; }
	if (sub == "level")  { cmdMapLevel(args);  return; }
	if (sub == "move")   { cmdMapMove(args);   return; }
	if (sub == "cancel") { cmdMapCancel(args); return; }

	// Shorthand: `map b9`, `map drassen`, `map here` — treat the arg as
	// a sector id and dispatch to `map sector`. Lets the user skip the
	// `sector` filler word once they've heard about a sector.
	{
		SGPSector probe;
		ST::string sectorErr;
		if (resolveSectorArg(joinFrom(args, 1), probe, sectorErr))
		{
			std::vector<std::string> rewritten = { args[0], "sector" };
			rewritten.insert(rewritten.end(), args.begin() + 1, args.end());
			cmdMapSector(rewritten);
			return;
		}
	}

	Console_Println(ST::format(
		"unknown subcommand: map {} (try 'map', 'map sector <id>', "
		"'map list <towns|mines|sams|militia|enemies>', 'map town <name>', 'map mine <town>', "
		"'map level <0..3>', 'map move <sector> [from <name>] [keep-path]', 'map cancel [<name>]')",
		args[1]));
}

void Cmd_Compress(const std::vector<std::string>& args)
{
	cmdCompress(args);
}

void Cmd_Tactical(const std::vector<std::string>&)
{
	cmdTactical();
}

void Cmd_Quit(const std::vector<std::string>&)
{
	cmdQuit();
}

void Cmd_Log(const std::vector<std::string>& args)
{
	cmdLog(args);
}
