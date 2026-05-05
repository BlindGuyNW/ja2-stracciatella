#include "Console_Aim.h"

#include "AIM.h"
#include "AIMListingModel.h"
#include "AIMMembers.h"
#include "Assignments.h"
#include "ContentManager.h"
#include "Finances.h"
#include "GameInstance.h"
#include "ItemModel.h"
#include "JAScreens.h"
#include "Laptop.h"
#include "LaptopSave.h"
#include "ScreenIDs.h"
#include "Merc_Hiring.h"
#include "Soldier_Control.h"
#include "Soldier_Profile.h"
#include "Soldier_Profile_Type.h"
#include "Text.h"
#include "WordWrap.h"

#include "Console.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	// ----- Status classification ---------------------------------------
	// One enum per AIM-listing state we want to surface separately.
	// Reused by `aim list` (filter) and `aim merc` (display).
	enum StatusCat
	{
		ST_AVAILABLE,
		ST_ON_TEAM,
		ST_ARRIVING,
		ST_AWAY,
		ST_RETURNING,
		ST_DEAD,
		ST_POW,
		ST_ANNOYED_CONTACT,
		ST_ANNOYED_NO_CONTACT,
		ST_UNAVAILABLE,
	};

	struct StatusWord
	{
		StatusCat   cat;
		ST::string  text;
	};

	StatusWord aimStatusWord(MERCPROFILESTRUCT const& p)
	{
		SOLDIERTYPE const* const s = FindSoldierByProfileIDOnPlayerTeam(p.ubFaceIndex);

		if (IsMercDead(p))
			return { ST_DEAD, ST::string("dead") };

		if (p.bMercStatus == MERC_FIRED_AS_A_POW ||
			(s && s->bAssignment == ASSIGNMENT_POW))
			return { ST_POW, ST::string("POW") };

		if (s != NULL)
		{
			if (p.bMercStatus == MERC_HIRED_BUT_NOT_ARRIVED_YET)
				return { ST_ARRIVING, ST::string("arriving") };
			return { ST_ON_TEAM, ST::string("on team") };
		}

		if (p.bMercStatus == MERC_RETURNING_HOME)
			return { ST_RETURNING, ST::string("returning home") };

		if (p.bMercStatus == MERC_WORKING_ELSEWHERE || p.bMercStatus > 0)
		{
			if (p.uiDayBecomesAvailable > 0)
			{
				return { ST_AWAY,
					ST::format("away (back day {})", p.uiDayBecomesAvailable) };
			}
			return { ST_AWAY, ST::string("away") };
		}

		if (p.bMercStatus == MERC_ANNOYED_BUT_CAN_STILL_CONTACT)
			return { ST_ANNOYED_CONTACT, ST::string("annoyed (will still contact)") };

		if (p.bMercStatus == MERC_ANNOYED_WONT_CONTACT)
			return { ST_ANNOYED_NO_CONTACT, ST::string("annoyed (no contact)") };

		if (!IsMercHireable(p))
			return { ST_UNAVAILABLE, ST::string("unavailable") };

		return { ST_AVAILABLE, ST::string("available") };
	}

	// ----- Argument helpers --------------------------------------------

	std::string lower(std::string s)
	{
		for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}

	// Joins arg tokens [from..end] with single spaces. Used so multi-word
	// names ("mr. magic") survive the dispatcher's whitespace tokenization.
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

	bool parseInt(const std::string& s, long& out)
	{
		if (s.empty()) return false;
		char* end = nullptr;
		long v = std::strtol(s.c_str(), &end, 10);
		if (end == s.c_str() || *end != '\0') return false;
		out = v;
		return true;
	}

	// ----- aN tag table ------------------------------------------------
	// Populated by `aim list`, consumed by name-resolving subcommands.
	// Stable within a single `aim list` invocation; the next `aim list`
	// re-tags. Indexed 0..N-1 (a1 is index 0).
	std::vector<UINT8> g_lastTags;

	// ----- Gate --------------------------------------------------------
	// AimMercArray is populated by the engine on entry to the Members
	// Sort page (AIMSort.cc). Empty means the player hasn't navigated
	// there yet — same gate a sighted player passes by clicking the
	// Members card on the AIM home page. We honour that gate rather
	// than bypassing it via GCM->aimListings() directly.
	bool membersLoadedGate()
	{
		if (!AimMercArray.empty()) return true;
		Console_Println(
			"AIM Members section not loaded yet — "
			"a sighted player would click the Members card on the AIM home page.");
		Console_Println(
			"Run 'aim members' (laptop only) to do the same, then re-run this verb.");
		return false;
	}

	// ----- Name / aN resolution ----------------------------------------
	// Returns the profile id matching `want`, or -1 on miss/ambiguity.
	// Match order:
	//   "aN"        -> g_lastTags[N-1] (only valid after `aim list`)
	//   exact zNickname (case-insensitive) — always wins
	//   exact zName     (case-insensitive)
	//   prefix zNickname (unique)
	//   prefix zName     (unique)
	// Ambiguous prefixes return -1 and the caller prints a hint.
	INT32 resolveProfile(const std::string& wantRaw, ST::string& howFailed)
	{
		const std::string want = lower(wantRaw);
		if (want.empty())
		{
			howFailed = ST::string("missing name (try 'aim list' for tags)");
			return -1;
		}

		// aN
		if (want[0] == 'a' && want.size() >= 2)
		{
			long n;
			if (parseInt(want.substr(1), n))
			{
				if (n < 1 || static_cast<std::size_t>(n) > g_lastTags.size())
				{
					howFailed = ST::format(
						"tag {} out of range (run 'aim list' first; 1..{} valid)",
						wantRaw,
						g_lastTags.size());
					return -1;
				}
				return g_lastTags[n - 1];
			}
		}

		// Walk only AIM-listed profiles, not all 170.
		INT32  exactNickPid = -1;
		INT32  exactNamePid = -1;
		INT32  prefixNickPid = -1; std::size_t prefixNickHits = 0;
		INT32  prefixNamePid = -1; std::size_t prefixNameHits = 0;

		for (UINT8 pid : AimMercArray)
		{
			MERCPROFILESTRUCT const& p = GetProfile(pid);
			const std::string nick = lower(p.zNickname.to_std_string());
			const std::string name = lower(p.zName.to_std_string());

			if (!nick.empty() && nick == want)        exactNickPid = pid;
			if (!name.empty() && name == want)        exactNamePid = pid;
			if (!nick.empty() && nick.rfind(want, 0) == 0)
				{ prefixNickPid = pid; ++prefixNickHits; }
			if (!name.empty() && name.rfind(want, 0) == 0)
				{ prefixNamePid = pid; ++prefixNameHits; }
		}

		if (exactNickPid >= 0) return exactNickPid;
		if (exactNamePid >= 0) return exactNamePid;
		if (prefixNickHits == 1) return prefixNickPid;
		if (prefixNameHits == 1) return prefixNamePid;

		if (prefixNickHits > 1 || prefixNameHits > 1)
		{
			howFailed = ST::format("ambiguous name: {} (try 'aim list')", wantRaw);
		}
		else
		{
			howFailed = ST::format("no AIM merc named: {} (try 'aim list')", wantRaw);
		}
		return -1;
	}

	// ----- Sort -------------------------------------------------------

	struct SortSpec
	{
		int  key;     // 0..5 matching gubCurrentSortMode
		bool ascend;
	};

	// Default: experience desc.
	bool parseSort(const std::vector<std::string>& args, std::size_t at, SortSpec& spec)
	{
		spec.key = 1;        // experience
		spec.ascend = false; // descending

		if (args.size() <= at) return true;

		const std::string key = lower(args[at]);
		if      (key == "price")         spec.key = 0;
		else if (key == "experience")    spec.key = 1;
		else if (key == "marksmanship")  spec.key = 2;
		else if (key == "medical")       spec.key = 3;
		else if (key == "explosives")    spec.key = 4;
		else if (key == "mechanical")    spec.key = 5;
		else
		{
			Console_Println(ST::format(
				"unknown sort key: {} (try price|experience|marksmanship|medical|explosives|mechanical)",
				args[at]));
			return false;
		}

		if (args.size() > at + 1)
		{
			const std::string dir = lower(args[at + 1]);
			if      (dir == "asc")  spec.ascend = true;
			else if (dir == "desc") spec.ascend = false;
			else
			{
				Console_Println(ST::format(
					"unknown sort direction: {} (try asc|desc)", args[at + 1]));
				return false;
			}
		}
		return true;
	}

	INT32 sortValue(MERCPROFILESTRUCT const& p, int key)
	{
		switch (key)
		{
			case 0: return static_cast<INT32>(p.uiWeeklySalary);
			case 1: return p.bExpLevel;
			case 2: return p.bMarksmanship;
			case 3: return p.bMedical;
			case 4: return p.bExplosive;
			case 5: return p.bMechanical;
		}
		return 0;
	}

	// ----- Filter -----------------------------------------------------

	enum FilterMode
	{
		FILT_AVAILABLE,
		FILT_ALL,
		FILT_DEAD,
		FILT_AWAY,
		FILT_TEAM,
	};

	bool parseFilter(const std::vector<std::string>& args, FilterMode& mode, std::size_t& after)
	{
		mode = FILT_AVAILABLE;
		after = 2;
		if (args.size() <= 2) return true;

		const std::string a = lower(args[2]);
		// Only consume args[2] as a filter if it looks like one. Otherwise
		// parseSort will look at it next.
		if      (a == "available") mode = FILT_AVAILABLE;
		else if (a == "all")       mode = FILT_ALL;
		else if (a == "dead")      mode = FILT_DEAD;
		else if (a == "away")      mode = FILT_AWAY;
		else if (a == "team")      mode = FILT_TEAM;
		else { return true; /* leave at=2, parseSort handles it */ }
		after = 3;
		return true;
	}

	bool filterAccepts(FilterMode mode, StatusCat cat)
	{
		switch (mode)
		{
			case FILT_ALL: return true;
			case FILT_DEAD: return cat == ST_DEAD;
			case FILT_AWAY: return cat == ST_AWAY || cat == ST_RETURNING;
			case FILT_TEAM: return cat == ST_ON_TEAM || cat == ST_ARRIVING || cat == ST_POW;
			case FILT_AVAILABLE:
				return cat == ST_AVAILABLE || cat == ST_ANNOYED_CONTACT;
		}
		return false;
	}

	const char* filterName(FilterMode m)
	{
		switch (m)
		{
			case FILT_AVAILABLE: return "available";
			case FILT_ALL:       return "all";
			case FILT_DEAD:      return "dead";
			case FILT_AWAY:      return "away";
			case FILT_TEAM:      return "team";
		}
		return "?";
	}

	const char* sortKeyName(int k)
	{
		static const char* kNames[6] = {
			"weekly salary", "experience", "marksmanship",
			"medical", "explosives", "mechanical"
		};
		return (k >= 0 && k < 6) ? kNames[k] : "?";
	}

	// ----- aim (summary) ----------------------------------------------

	// ----- aim members (navigate) -------------------------------------
	// Equivalent to clicking the Members image card on the AIM home page
	// (AIM.cc::SelectMemberCardRegionCallBack). The card is a
	// MOUSE_REGION, not a GUI_BUTTON, so `g list` cannot surface it —
	// this verb is the only way an SR user can perform that click.
	void cmdMembers()
	{
		if (guiCurrentScreen != LAPTOP_SCREEN)
		{
			Console_Println("Open your laptop first.");
			return;
		}
		// Accept any AIM page as the launch point; sighted players reach
		// the Members card by clicking the AIM bookmark or the AIM site.
		switch (guiCurrentLaptopMode)
		{
			case LAPTOP_MODE_AIM:
			case LAPTOP_MODE_AIM_MEMBERS:
			case LAPTOP_MODE_AIM_MEMBERS_FACIAL_INDEX:
			case LAPTOP_MODE_AIM_MEMBERS_SORTED_FILES:
			case LAPTOP_MODE_AIM_MEMBERS_SORTED_FILES_VIDEO:
			case LAPTOP_MODE_AIM_MEMBERS_ARCHIVES:
			case LAPTOP_MODE_AIM_POLICIES:
			case LAPTOP_MODE_AIM_HISTORY:
			case LAPTOP_MODE_AIM_LINKS:
				break;
			default:
				Console_Println("Open the AIM site first (try 'web aim').");
				return;
		}
		guiCurrentLaptopMode = LAPTOP_MODE_AIM_MEMBERS_SORTED_FILES;
		Console_Println("Loading AIM Members …");
	}

	void cmdSummary()
	{
		if (!membersLoadedGate()) return;
		std::size_t hireable = 0;
		std::array<std::size_t, 11> byTier{};   // bExpLevel is 1..10
		UINT32 minSalary = 0xFFFFFFFFu;
		UINT32 maxSalary = 0;

		for (UINT8 pid : AimMercArray)
		{
			MERCPROFILESTRUCT const& p = GetProfile(pid);
			const StatusWord sw = aimStatusWord(p);
			if (sw.cat != ST_AVAILABLE && sw.cat != ST_ANNOYED_CONTACT) continue;
			++hireable;
			const int t = std::min<int>(10, std::max<int>(0, p.bExpLevel));
			++byTier[t];
			minSalary = std::min<UINT32>(minSalary, p.uiWeeklySalary);
			maxSalary = std::max<UINT32>(maxSalary, p.uiWeeklySalary);
		}

		if (hireable == 0)
		{
			Console_Println("AIM: no hireable mercs right now.");
			Console_Println("Use 'aim list all' to see the full roster.");
			return;
		}

		Console_Println(ST::format(
			"AIM: {} hireable mercs available, salary range {}-{} per week.",
			hireable,
			SPrintMoney(static_cast<INT32>(minSalary)),
			SPrintMoney(static_cast<INT32>(maxSalary))));

		// Tier breakdown only mentions tiers that have entries, kept on one line.
		ST::string tierLine;
		for (int t = 1; t <= 10; ++t)
		{
			if (byTier[t] == 0) continue;
			if (!tierLine.empty()) tierLine += ", ";
			tierLine += ST::format("L{}: {}", t, byTier[t]);
		}
		if (!tierLine.empty())
			Console_Println(ST::format("By experience tier — {}.", tierLine));

		Console_Println("Use 'aim list' to browse, 'aim merc <name>' for bios.");
	}

	// ----- aim list ---------------------------------------------------

	void cmdList(const std::vector<std::string>& args)
	{
		if (!membersLoadedGate()) return;
		FilterMode  filter;
		std::size_t after;
		if (!parseFilter(args, filter, after)) return;

		SortSpec sort;
		if (!parseSort(args, after, sort)) return;

		// Collect profiles passing the filter, paired with their cached status.
		std::vector<std::pair<UINT8, StatusWord>> rows;
		rows.reserve(AimMercArray.size());
		for (UINT8 pid : AimMercArray)
		{
			MERCPROFILESTRUCT const& p = GetProfile(pid);
			StatusWord sw = aimStatusWord(p);
			if (!filterAccepts(filter, sw.cat)) continue;
			rows.emplace_back(pid, std::move(sw));
		}

		std::sort(rows.begin(), rows.end(),
			[&](const auto& l, const auto& r)
		{
			const INT32 lv = sortValue(GetProfile(l.first), sort.key);
			const INT32 rv = sortValue(GetProfile(r.first), sort.key);
			return sort.ascend ? lv < rv : lv > rv;
		});

		// Update the tag table for subsequent `aim merc aN` lookups.
		g_lastTags.clear();
		g_lastTags.reserve(rows.size());

		Console_Println(ST::format(
			"{} mercs ({}), sorted by {} {}:",
			filterName(filter),
			rows.size(),
			sortKeyName(sort.key),
			sort.ascend ? "ascending" : "descending"));

		if (rows.empty())
		{
			Console_Println("(no matches)");
			return;
		}

		std::size_t i = 1;
		for (auto& row : rows)
		{
			MERCPROFILESTRUCT const& p = GetProfile(row.first);
			g_lastTags.push_back(row.first);

			ST::string tail;
			if (row.second.cat != ST_AVAILABLE)
			{
				tail = ST::format(" — {}", row.second.text);
			}

			Console_Println(ST::format(
				"  a{}  {} — Lvl {} — {}/wk{}",
				i++,
				p.zNickname.empty() ? p.zName : p.zNickname,
				p.bExpLevel,
				SPrintMoney(static_cast<INT32>(p.uiWeeklySalary)),
				tail));
		}
	}

	// ----- aim merc ---------------------------------------------------

	const char* skillTraitText(INT8 trait)
	{
		if (trait <= NO_SKILLTRAIT || trait >= NUM_SKILLTRAITS) return nullptr;
		return gzMercSkillText[trait].c_str();
	}

	// Best-effort inventory summary for the bio readout. Walks the merc's
	// inv[] picking up every non-empty slot, names and counts. The AIM GUI
	// shows the first 8 slots as icons; the console can show all 19, so
	// we do.
	ST::string gearSummary(MERCPROFILESTRUCT const& p)
	{
		ST::string out;
		bool first = true;
		for (UINT8 i = 0; i < NUM_INV_SLOTS; ++i)
		{
			const UINT16 usItem = p.inv[i];
			if (usItem == NOTHING) continue;
			const ItemModel* const item = GCM->getItem(usItem);
			if (!item) continue;
			if (!first) out += ", ";
			first = false;
			if (p.bInvNumber[i] > 1)
				out += ST::format("{}x {}", p.bInvNumber[i], item->getShortName());
			else
				out += item->getShortName();
		}
		return out;
	}

	void cmdMerc(const std::vector<std::string>& args)
	{
		if (!membersLoadedGate()) return;
		if (args.size() < 3)
		{
			Console_Println("usage: aim merc <name|aN>");
			return;
		}

		ST::string err;
		const INT32 pid = resolveProfile(joinFrom(args, 2), err);
		if (pid < 0)
		{
			Console_Println(err);
			return;
		}

		MERCPROFILESTRUCT const& p = GetProfile(static_cast<UINT8>(pid));
		const StatusWord sw = aimStatusWord(p);

		// Header
		ST::string header;
		if (!p.zNickname.empty() && p.zNickname != p.zName)
			header = ST::format("{} ({})", p.zNickname, p.zName);
		else
			header = p.zName;

		Console_Println(ST::format("{}, level {}, {}.",
			header,
			p.bExpLevel,
			p.bSex == FEMALE ? "female" : "male"));

		// Stats — two-column flow flattened to one line each, paired logically.
		Console_Println(ST::format(
			"  Health {} max, Agility {}, Dexterity {}, Strength {}, Wisdom {}, Leadership {}.",
			p.bLifeMax, p.bAgility, p.bDexterity, p.bStrength,
			p.bWisdom, p.bLeadership));
		Console_Println(ST::format(
			"  Marksmanship {}, Mechanical {}, Explosives {}, Medical {}.",
			p.bMarksmanship, p.bMechanical, p.bExplosive, p.bMedical));

		// Salary triple
		Console_Println(ST::format(
			"  Salary: {}/day, {}/week, {}/biweek.",
			SPrintMoney(p.sSalary),
			SPrintMoney(static_cast<INT32>(p.uiWeeklySalary)),
			SPrintMoney(static_cast<INT32>(p.uiBiWeeklySalary))));

		if (p.bMedicalDeposit)
		{
			Console_Println(ST::format("  Medical deposit: {}.",
				SPrintMoney(p.sMedicalDepositAmount)));
		}

		// Optional gear
		if (p.usOptionalGearCost > 0)
		{
			ST::string gear = gearSummary(p);
			if (!gear.empty())
			{
				Console_Println(ST::format("  Optional gear: {} — {}.",
					SPrintMoney(p.usOptionalGearCost), gear));
			}
			else
			{
				Console_Println(ST::format("  Optional gear: {}.",
					SPrintMoney(p.usOptionalGearCost)));
			}
		}

		// Skill traits
		const char* t1 = skillTraitText(p.bSkillTrait);
		const char* t2 = skillTraitText(p.bSkillTrait2);
		if (t1 || t2)
		{
			ST::string traitLine;
			if (t1 && t2 && p.bSkillTrait == p.bSkillTrait2)
			{
				// Same trait twice = expert
				traitLine = ST::format("{} (expert)", t1);
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

		// Status (always print so the user knows whether they can hire).
		Console_Println(ST::format("  Status: {}.", sw.text));

		// Bio prose. Pulled from the externalized AIMListing description /
		// additionalInformation pair (the same data the AIM Members page
		// renders). Email-style control codes shouldn't appear here in
		// practice, but strip just in case.
		auto* listing = GCM->aimListings()->optionalById(static_cast<uint8_t>(pid));
		if (listing)
		{
			if (!listing->description.empty())
			{
				Console_Println("");
				Console_Println(CleanOutControlCodesFromString(listing->description));
			}
			if (!listing->additionalInformation.empty())
			{
				Console_Println("");
				Console_Println(ST::format("Additional info: {}",
					CleanOutControlCodesFromString(listing->additionalInformation)));
			}
		}
	}

	// ----- aim show ---------------------------------------------------
	// Drives the GUI: lands on the target merc's profile page, switching
	// to LAPTOP_MODE_AIM_MEMBERS if currently elsewhere in AIM. Replaces
	// up to MAX_NUMBER_MERCS-1 prev/next clicks with one verb.
	bool inAimSection()
	{
		if (guiCurrentScreen != LAPTOP_SCREEN) return false;
		switch (guiCurrentLaptopMode)
		{
			case LAPTOP_MODE_AIM:
			case LAPTOP_MODE_AIM_MEMBERS:
			case LAPTOP_MODE_AIM_MEMBERS_FACIAL_INDEX:
			case LAPTOP_MODE_AIM_MEMBERS_SORTED_FILES:
			case LAPTOP_MODE_AIM_MEMBERS_SORTED_FILES_VIDEO:
			case LAPTOP_MODE_AIM_MEMBERS_ARCHIVES:
			case LAPTOP_MODE_AIM_POLICIES:
			case LAPTOP_MODE_AIM_HISTORY:
			case LAPTOP_MODE_AIM_LINKS:
				return true;
			default:
				return false;
		}
	}

	void cmdShow(const std::vector<std::string>& args)
	{
		if (!inAimSection())
		{
			Console_Println("Open the AIM site first (try 'web aim').");
			return;
		}
		if (!membersLoadedGate()) return;

		if (args.size() < 3)
		{
			Console_Println("usage: aim show <name|aN>");
			return;
		}

		ST::string err;
		const INT32 pid = resolveProfile(joinFrom(args, 2), err);
		if (pid < 0) { Console_Println(err); return; }

		if (!AIMMembers_ShowProfile(static_cast<UINT8>(pid)))
		{
			Console_Println(ST::format(
				"merc id {} is not in the live AIM listing", pid));
			return;
		}

		// Mirrors the Sort page's "To Member Stats" navigation; the
		// engine re-derives gbCurrentSoldier from gbCurrentIndex on
		// EnterAIMMembers, so the index we just set carries over.
		if (guiCurrentLaptopMode != LAPTOP_MODE_AIM_MEMBERS)
		{
			guiCurrentLaptopMode = LAPTOP_MODE_AIM_MEMBERS;
		}

		MERCPROFILESTRUCT const& p = GetProfile(static_cast<UINT8>(pid));
		Console_Println(ST::format("Showing {}'s profile.",
			p.zNickname.empty() ? p.zName : p.zNickname));
	}

	// ----- popup helpers ----------------------------------------------

	bool onMembersGate()
	{
		if (guiCurrentScreen != LAPTOP_SCREEN ||
			guiCurrentLaptopMode != LAPTOP_MODE_AIM_MEMBERS)
		{
			Console_Println(
				"Open a merc's profile first (try 'aim show <name>').");
			return false;
		}
		return true;
	}

	const char* lengthName(UINT8 len)
	{
		switch (len)
		{
			case 0: return "1 day";
			case 1: return "1 week";
			case 2: return "2 weeks";
		}
		return "?";
	}

	const char* modeWord(AIMVideoMode m)
	{
		switch (m)
		{
			case AIM_VIDEO_NOT_DISPLAYED_MODE:          return "no popup";
			case AIM_VIDEO_POPUP_MODE:                  return "title bar opening";
			case AIM_VIDEO_INIT_MODE:                   return "connecting (snowy)";
			case AIM_VIDEO_FIRST_CONTACT_MERC_MODE:     return "first contact";
			case AIM_VIDEO_HIRE_MERC_MODE:              return "hire screen";
			case AIM_VIDEO_MERC_ANSWERING_MACHINE_MODE: return "answering machine";
			case AIM_VIDEO_MERC_UNAVAILABLE_MODE:       return "merc unavailable";
			case AIM_VIDEO_POPDOWN_MODE:                return "title bar closing";
		}
		return "?";
	}

	// ----- aim length -------------------------------------------------
	void cmdLength(const std::vector<std::string>& args)
	{
		if (!onMembersGate()) return;
		if (args.size() < 3)
		{
			Console_Println("usage: aim length day | week | biweek");
			return;
		}
		UINT8 length;
		const std::string s = lower(args[2]);
		if      (s == "day"    || s == "1day"   || s == "1d") length = 0;
		else if (s == "week"   || s == "1week"  || s == "1w") length = 1;
		else if (s == "biweek" || s == "2weeks" || s == "2w") length = 2;
		else
		{
			Console_Println(
				"unknown length (try 'day', 'week', 'biweek')");
			return;
		}
		if (!AIMMembers_SetContractLength(length))
		{
			Console_Println(
				"Hire screen not open yet — try 'aim status' to see what stage the popup is in.");
			return;
		}
		const auto st = AIMMembers_GetPopupState();
		Console_Println(ST::format(
			"Length: {}. Total: {}.",
			lengthName(st.contractLength),
			SPrintMoney(st.contractAmount)));
	}

	// ----- aim gear ---------------------------------------------------
	void cmdGear(const std::vector<std::string>& args)
	{
		if (!onMembersGate()) return;
		if (args.size() < 3)
		{
			Console_Println("usage: aim gear on | off");
			return;
		}
		bool buy;
		const std::string s = lower(args[2]);
		if      (s == "on"  || s == "yes" || s == "include")  buy = true;
		else if (s == "off" || s == "no"  || s == "skip")     buy = false;
		else
		{
			Console_Println("unknown value (try 'on' or 'off')");
			return;
		}
		if (!AIMMembers_SetBuyEquipment(buy))
		{
			const auto st = AIMMembers_GetPopupState();
			if (buy && !st.gearAvailable)
			{
				Console_Println(
					"This merc has no optional gear package.");
			}
			else
			{
				Console_Println(
					"Hire screen not open yet — try 'aim status'.");
			}
			return;
		}
		const auto st = AIMMembers_GetPopupState();
		Console_Println(ST::format(
			"Gear: {}. Total: {}.",
			st.buyEquipment ? "included" : "not included",
			SPrintMoney(st.contractAmount)));
	}

	// ----- aim status -------------------------------------------------
	void cmdStatus()
	{
		if (!onMembersGate()) return;
		const auto st = AIMMembers_GetPopupState();
		const UINT8 pid = AIMMembers_CurrentProfile();
		MERCPROFILESTRUCT const& p = GetProfile(pid);
		const ST::string mercName = p.zNickname.empty() ? p.zName : p.zNickname;

		if (st.mode == AIM_VIDEO_NOT_DISPLAYED_MODE)
		{
			Console_Println(ST::format(
				"On {}'s profile. No popup open. Use 'aim contact' to start a call.",
				mercName));
			return;
		}

		Console_Println(ST::format(
			"Calling {}. Stage: {}.", mercName, modeWord(st.mode)));

		if (st.mode == AIM_VIDEO_HIRE_MERC_MODE)
		{
			Console_Println(ST::format(
				"Length: {}. Gear: {}. Total: {}. Balance: {}.",
				lengthName(st.contractLength),
				st.buyEquipment ? "included" : "not included",
				SPrintMoney(st.contractAmount),
				SPrintMoney(LaptopSaveInfo.iCurrentBalance)));
		}

		if (st.mercTalking)
		{
			Console_Println(
				"Merc is still talking — wait or click their face to interrupt.");
		}
	}

	// ----- aim authorize ----------------------------------------------
	void cmdAuthorize()
	{
		if (!onMembersGate()) return;
		const auto st = AIMMembers_GetPopupState();
		if (st.mode != AIM_VIDEO_FIRST_CONTACT_MERC_MODE &&
			st.mode != AIM_VIDEO_HIRE_MERC_MODE)
		{
			Console_Println(ST::format(
				"Cannot authorize from this stage ({}). Try 'aim status'.",
				modeWord(st.mode)));
			return;
		}
		if (!AIMMembers_Authorize())
		{
			Console_Println("Authorize button not available right now.");
			return;
		}
		// In FIRST_CONTACT mode this advances to HIRE; in HIRE it commits.
		// Let the engine speak the result (popup box + merc voice).
		if (st.mode == AIM_VIDEO_FIRST_CONTACT_MERC_MODE)
		{
			Console_Println("Advancing to hire screen.");
		}
		else
		{
			Console_Println("Transferring funds.");
		}
	}

	// ----- aim cancel -------------------------------------------------
	void cmdCancel()
	{
		if (!onMembersGate()) return;
		const auto st = AIMMembers_GetPopupState();
		if (st.mode != AIM_VIDEO_FIRST_CONTACT_MERC_MODE &&
			st.mode != AIM_VIDEO_HIRE_MERC_MODE)
		{
			Console_Println(ST::format(
				"Cannot cancel from this stage ({}). Try 'aim status'.",
				modeWord(st.mode)));
			return;
		}
		if (!AIMMembers_CancelAuthorize())
		{
			Console_Println("Cancel button not available right now.");
			return;
		}
		if (st.mode == AIM_VIDEO_FIRST_CONTACT_MERC_MODE)
		{
			Console_Println("Hanging up.");
		}
		else
		{
			Console_Println("Returning to first-contact screen.");
		}
	}

	// ----- aim contact ------------------------------------------------
	void cmdContact()
	{
		if (guiCurrentScreen != LAPTOP_SCREEN ||
			guiCurrentLaptopMode != LAPTOP_MODE_AIM_MEMBERS)
		{
			Console_Println(
				"Open a merc's profile first (try 'aim show <name>').");
			return;
		}

		const UINT8 pid = AIMMembers_CurrentProfile();
		MERCPROFILESTRUCT const& p = GetProfile(pid);

		if (IsMercDead(p))
		{
			Console_Println(ST::format(
				"{} is dead — cannot contact.",
				p.zNickname.empty() ? p.zName : p.zNickname));
			return;
		}

		// Engine refuses to re-enter the popup if one is already up;
		// surface that here rather than silently no-op.
		if (gubVideoConferencingMode != AIM_VIDEO_NOT_DISPLAYED_MODE)
		{
			Console_Println("Contact popup already open.");
			return;
		}

		AIMMembers_StartContact();
		Console_Println(ST::format("Contacting {} …",
			p.zNickname.empty() ? p.zName : p.zNickname));
	}
}

void Cmd_Aim(const std::vector<std::string>& args)
{
	if (args.size() < 2) { cmdSummary(); return; }

	const std::string sub = lower(args[1]);
	if (sub == "members")   { cmdMembers(); return; }
	if (sub == "list")      { cmdList(args); return; }
	if (sub == "merc")      { cmdMerc(args); return; }
	if (sub == "show")      { cmdShow(args); return; }
	if (sub == "contact")   { cmdContact(); return; }
	if (sub == "length")    { cmdLength(args); return; }
	if (sub == "gear")      { cmdGear(args); return; }
	if (sub == "status")    { cmdStatus(); return; }
	if (sub == "authorize") { cmdAuthorize(); return; }
	if (sub == "cancel")    { cmdCancel(); return; }

	Console_Println(ST::format(
		"unknown subcommand: aim {} "
		"(try 'aim', 'aim members', 'aim list', 'aim merc <name>', "
		"'aim show <name>', 'aim contact', 'aim length', 'aim gear', "
		"'aim status', 'aim authorize', 'aim cancel')",
		args[1]));
}
