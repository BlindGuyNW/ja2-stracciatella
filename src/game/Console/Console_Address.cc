#include "Console_Address.h"
#include "Console_Visibility.h"

#include "Isometric_Utils.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "WorldDef.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_theory/format>

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

	bool parseInt(const std::string& s, int& out)
	{
		if (s.empty()) return false;
		char* end = nullptr;
		long v = std::strtol(s.c_str(), &end, 10);
		if (end == s.c_str() || *end != '\0') return false;
		out = static_cast<int>(v);
		return true;
	}

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

	// Recognize positional index tokens like "e3" or "m1": one letter from
	// {e, m} followed by one or more digits, the whole token. Anything
	// else (e.g. a name "Ed" or "Mike") parses as not-an-index, so the
	// caller can fall through to name lookup.
	bool parseIndexTag(const std::string& tok, char& cat, int& idx)
	{
		if (tok.size() < 2) return false;
		const char first = static_cast<char>(std::tolower(static_cast<unsigned char>(tok[0])));
		if (first != 'e' && first != 'm') return false;
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

void enumerateHostiles(const SOLDIERTYPE& observer, std::vector<ListedSoldier>& out)
{
	FOR_EACH_MERC(it)
	{
		SOLDIERTYPE* const t = *it;
		if (t->ubID == observer.ubID) continue;
		if (!IsHostileToOurTeam(*t))   continue;
		if (!ConsoleVis::IsKnownSoldier(*t)) continue;
		out.push_back({ t, PythSpacesAway(observer.sGridNo, t->sGridNo) });
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
		out.push_back({ t, PythSpacesAway(observer.sGridNo, t->sGridNo) });
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

	// Positional-index form: e<N> / m<N>. Resolved against the same
	// distance-sorted lists `nearby` displays, so the indices the user
	// sees there are exactly what they can address.
	{
		char cat;
		int  idx;
		if (parseIndexTag(tok, cat, idx))
		{
			if (!observer)
			{
				err = "no merc selected to anchor positional index";
				return 0;
			}
			std::vector<ListedSoldier> list;
			if (cat == 'e') enumerateHostiles(*observer, list);
			else            enumerateTeammates(*observer, list);
			if (idx > static_cast<int>(list.size()))
			{
				err = ST::format("only {} {} known; can't address '{}'",
				                 list.size(),
				                 cat == 'e' ? "hostiles" : "teammates",
				                 tok);
				return 0;
			}
			out.soldier = list[idx - 1].soldier;
			out.gridno  = out.soldier->sGridNo;
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
		err = ST::format("'{}' is ambiguous; use a longer prefix or an index (eN/mN — see 'nearby')", tok);
		return 0;
	}
	out.soldier = s;
	out.gridno  = s->sGridNo;
	return 1;
}
