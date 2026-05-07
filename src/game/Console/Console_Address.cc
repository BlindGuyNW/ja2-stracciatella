#include "Console_Address.h"
#include "Console_Tags.h"
#include "Console_Visibility.h"

#include "Console.h"
#include "Isometric_Utils.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "WorldDef.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_theory/format>

// startsWithCI / parseCompass / parseInt live in Console_Address.h
// so every verb file can call them without redeclaring the same body.

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

SOLDIERTYPE* requireSelectedMerc(const char* msg)
{
	SOLDIERTYPE* const s = GetSelectedMan();
	if (!s) Console_Println(msg);
	return s;
}

namespace
{
	bool parseCoord(const std::string& tok, INT16& gridnoOut)
	{
		const auto comma = tok.find(',');
		if (comma == std::string::npos) return false;
		int col, row;
		if (!parseInt(tok.substr(0, comma), col))     return false;
		if (!parseInt(tok.substr(comma + 1), row))    return false;
		if (col < 0 || col >= WORLD_COLS) return false;
		if (row < 0 || row >= WORLD_ROWS) return false;
		gridnoOut = static_cast<INT16>(row * WORLD_COLS + col);
		return true;
	}

	// Walk `steps` tiles in a direction from origin, stopping cleanly at
	// the map edge. Returns NOWHERE if the first step is already off-map.
	INT16 walkSteps(INT16 origin, INT8 dir, INT16 steps)
	{
		INT16 cur = origin;
		for (INT16 i = 0; i < steps; ++i)
		{
			INT16 next = NewGridNo(cur, DirIncrementer[dir]);
			if (next == NOWHERE || next == cur) return cur;
			cur = next;
		}
		return cur;
	}

	// Match a name against any soldier the player can address — own team
	// (always known) or hostile that's been spotted by the team. Returns
	// pointer to the unique match or nullptr; sets *count to total hits.
	SOLDIERTYPE* findAddressableSoldier(const std::string& needle, int& count)
	{
		SOLDIERTYPE* match = nullptr;
		count = 0;
		FOR_EACH_MERC(it)
		{
			SOLDIERTYPE* const s = *it;
			if (!s->bActive)   continue;
			if (s->bLife <= 0) continue;
			if (!s->bInSector) continue;

			const bool ownTeam = (s->bTeam == OUR_TEAM);
			const bool knownHostile = !ownTeam && ConsoleVis::IsKnownSoldier(*s);
			if (!ownTeam && !knownHostile) continue;

			if (!startsWithCI(s->name, needle)) continue;
			match = s;
			++count;
		}
		return match;
	}

	// Recognize positional index tokens — one of the category letters
	// `nearby` prints (see ConsoleTags::isTagPrefix) followed by one or
	// more digits, the whole token. Anything else (e.g. a name "Ed" or
	// "Mike") parses as not-an-index, so the caller can fall through to
	// compass / name lookup.
	bool parseIndexTag(const std::string& tok, char& cat, int& idx)
	{
		if (tok.size() < 2) return false;
		const char first = static_cast<char>(std::tolower(static_cast<unsigned char>(tok[0])));
		if (!ConsoleTags::isTagPrefix(first)) return false;
		for (std::size_t i = 1; i < tok.size(); ++i)
		{
			if (!std::isdigit(static_cast<unsigned char>(tok[i]))) return false;
		}
		cat = first;
		idx = std::atoi(tok.c_str() + 1);
		return idx > 0;
	}

	void sortByDistance(std::vector<ListedSoldier>& list)
	{
		std::sort(list.begin(), list.end(),
			[](const ListedSoldier& a, const ListedSoldier& b)
			{
				if (a.distance != b.distance) return a.distance < b.distance;
				return a.soldier->ubID < b.soldier->ubID;
			});
	}
}

const INT8 kSlotOrder[kSlotTagCount] =
{
	HANDPOS, SECONDHANDPOS,
	HELMETPOS, VESTPOS, LEGPOS, HEAD1POS, HEAD2POS,
	BIGPOCK1POS, BIGPOCK2POS, BIGPOCK3POS, BIGPOCK4POS,
	SMALLPOCK1POS, SMALLPOCK2POS, SMALLPOCK3POS, SMALLPOCK4POS,
	SMALLPOCK5POS, SMALLPOCK6POS, SMALLPOCK7POS, SMALLPOCK8POS,
};
static_assert(NUM_INV_SLOTS == kSlotTagCount,
	"slot-tag table must enumerate every InvSlotPos exactly once");

INT8 parseSlotTag(const std::string& tok)
{
	if (tok.size() < 2) return -1;
	const char first = static_cast<char>(std::tolower(static_cast<unsigned char>(tok[0])));
	if (first != 's') return -1;
	for (std::size_t i = 1; i < tok.size(); ++i)
	{
		if (!std::isdigit(static_cast<unsigned char>(tok[i]))) return -1;
	}
	const int n = std::atoi(tok.c_str() + 1);
	if (n < 1 || n > kSlotTagCount) return -1;
	return kSlotOrder[n - 1];
}

const char* slotTag(INT8 invPos)
{
	// One thread-local buffer per call site is overkill — the tag table
	// is small, fixed, and only consumed by formatting paths that copy
	// the result into ST::format. A static lookup table keeps things
	// simple and avoids any per-call formatting.
	static const char* const kTags[kSlotTagCount] =
	{
		"s1",  "s2",  "s3",  "s4",  "s5",
		"s6",  "s7",  "s8",  "s9",  "s10",
		"s11", "s12", "s13", "s14", "s15",
		"s16", "s17", "s18", "s19",
	};
	for (int i = 0; i < kSlotTagCount; ++i)
	{
		if (kSlotOrder[i] == invPos) return kTags[i];
	}
	return "?";
}

const char* slotLabel(INT8 invPos)
{
	switch (invPos)
	{
		case HELMETPOS:     return "Helmet";
		case VESTPOS:       return "Vest";
		case LEGPOS:        return "Legs";
		case HEAD1POS:      return "Head 1";
		case HEAD2POS:      return "Head 2";
		case HANDPOS:       return "In hand";
		case SECONDHANDPOS: return "Off hand";
		case BIGPOCK1POS:   return "Big pocket 1";
		case BIGPOCK2POS:   return "Big pocket 2";
		case BIGPOCK3POS:   return "Big pocket 3";
		case BIGPOCK4POS:   return "Big pocket 4";
		case SMALLPOCK1POS: return "Small pocket 1";
		case SMALLPOCK2POS: return "Small pocket 2";
		case SMALLPOCK3POS: return "Small pocket 3";
		case SMALLPOCK4POS: return "Small pocket 4";
		case SMALLPOCK5POS: return "Small pocket 5";
		case SMALLPOCK6POS: return "Small pocket 6";
		case SMALLPOCK7POS: return "Small pocket 7";
		case SMALLPOCK8POS: return "Small pocket 8";
	}
	return "?";
}

void enumerateHostiles(const SOLDIERTYPE& observer, std::vector<ListedSoldier>& out)
{
	FOR_EACH_MERC(it)
	{
		SOLDIERTYPE* const t = *it;
		if (t->ubID == observer.ubID) continue;
		if (!IsHostileToOurTeam(*t))   continue;
		if (!ConsoleVis::IsKnownSoldier(*t)) continue;
		out.push_back({ t, SpacesAway(observer.sGridNo, t->sGridNo) });
	}
	sortByDistance(out);
}

void enumerateTeammates(const SOLDIERTYPE& observer, std::vector<ListedSoldier>& out)
{
	FOR_EACH_MERC(it)
	{
		SOLDIERTYPE* const t = *it;
		if (t->ubID == observer.ubID)                       continue;
		if (t->bTeam != OUR_TEAM)                            continue;
		if (!t->bActive || t->bLife <= 0 || !t->bInSector)   continue;
		out.push_back({ t, SpacesAway(observer.sGridNo, t->sGridNo) });
	}
	sortByDistance(out);
}

int parseTarget(const std::vector<std::string>& args,
                std::size_t                     start,
                const SOLDIERTYPE*              observer,
                Target&                         out,
                ST::string&                     err)
{
	if (start >= args.size())
	{
		err = "missing target";
		return 0;
	}

	const std::string& tok = args[start];

	// Coordinate form: contains a comma.
	if (tok.find(',') != std::string::npos)
	{
		INT16 g;
		if (!parseCoord(tok, g))
		{
			err = ST::format("'{}' is not a valid col,row coordinate", tok);
			return 0;
		}
		out.soldier = nullptr;
		out.gridno  = g;
		return 1;
	}

	// Positional-tag form: e<N> / m<N> / c<N> / i<N> / d<N> / k<N> /
	// x<N> / z<N> / b<N> / p<N> / u<N>. Resolved against the same
	// distance-sorted lists `nearby` displays, so the indices the user
	// sees there are exactly what they can address. Tags whose category
	// resolves to a soldier (e/m/c) set out.soldier; the rest set only
	// out.gridno.
	{
		char cat;
		int  idx;
		if (parseIndexTag(tok, cat, idx))
		{
			if (!observer)
			{
				err = "no merc selected to anchor positional tag";
				return 0;
			}
			ConsoleTags::Target tgt;
			int listSize;
			const auto r = ConsoleTags::resolve(cat, idx, *observer, tgt, listSize);
			if (r == ConsoleTags::kIndexOutOfRange)
			{
				err = ST::format("only {} {} known; can't address '{}'",
				                 listSize, ConsoleTags::categoryName(cat), tok);
				return 0;
			}
			// kNotATag is impossible: parseIndexTag already vetted the prefix.
			out.soldier = tgt.soldier;
			out.gridno  = tgt.gridno;
			return 1;
		}
	}

	// Direction + steps form.
	const INT8 dir = parseCompass(tok);
	if (dir >= 0)
	{
		if (!observer)
		{
			err = "no merc selected to anchor relative direction";
			return 0;
		}
		if (start + 1 >= args.size())
		{
			err = ST::format("'{}' needs a step count (e.g. '{} 5')", tok, tok);
			return 0;
		}
		int steps;
		if (!parseInt(args[start + 1], steps) || steps <= 0)
		{
			err = ST::format("step count must be a positive integer, got '{}'", args[start + 1]);
			return 0;
		}
		out.soldier = nullptr;
		out.gridno  = walkSteps(observer->sGridNo, dir, static_cast<INT16>(steps));
		return 2;
	}

	// Name form.
	int count;
	SOLDIERTYPE* const s = findAddressableSoldier(tok, count);
	if (count == 0)
	{
		err = ST::format("no known soldier matches '{}'", tok);
		return 0;
	}
	if (count > 1)
	{
		err = ST::format("'{}' is ambiguous; use a longer prefix or a tag from 'nearby' (eN/mN/cN/...)", tok);
		return 0;
	}
	out.soldier = s;
	out.gridno  = s->sGridNo;
	return 1;
}
