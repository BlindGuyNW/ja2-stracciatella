#include "Console_Strategic.h"

#include "Assignments.h"
#include "GameScreen.h"
#include "Interface_Panels.h"
#include "JAScreens.h"
#include "MapScreen.h"
#include "Map_Screen_Interface.h"
#include "Overhead.h"
#include "ScreenIDs.h"
#include "Soldier_Control.h"

#include "Console.h"
#include "Console_Address.h"

#include <algorithm>
#include <cctype>
#include <string_theory/format>

namespace
{
	// Module-local tag table. Rebuilt by Console_RegisterTeamTags every
	// time a verb prints a roster; consumed by Console_ResolveCharSlot /
	// Console_LookupTeamTag. INT8 because gCharactersList is sized
	// MAX_CHARACTER_COUNT and slot indices fit comfortably.
	std::vector<INT8> g_teamTags;
}

bool Console_RequireCampaign()
{
	// Engine truth is OUR_TEAM in Menptr; gCharactersList is a UI cache the
	// mapscreen builds on entry. Mid-tactical it can be empty even with a
	// full team, so we gate on OUR_TEAM and then refresh the cache so the
	// rest of the verb (which reads through gCharactersList) sees the same
	// state the mapscreen would.
	bool any = false;
	CFOR_EACH_IN_TEAM(s, OUR_TEAM) { any = true; break; }
	if (!any)
	{
		Console_Println("No campaign loaded — start or load a game first.");
		return false;
	}
	ReBuildCharactersList();
	return true;
}

void Console_EnsureMapscreen()
{
	if (guiCurrentScreen == GAME_SCREEN)
	{
		// GoToMapScreenFromTactical wraps the boxing-in-progress check; it
		// pops the abandon dialog rather than tearing out of a fight.
		GoToMapScreenFromTactical();
	}
}

void Console_RegisterTeamTags(std::vector<INT8> const& slotsInDisplayOrder)
{
	g_teamTags = slotsInDisplayOrder;
}

INT8 Console_LookupTeamTag(long n)
{
	if (n < 1 || static_cast<std::size_t>(n) > g_teamTags.size()) return -1;
	return g_teamTags[n - 1];
}

std::size_t Console_TeamTagCount()
{
	return g_teamTags.size();
}

INT8 Console_ResolveCharSlot(std::string const& wantRaw, ST::string& err)
{
	std::string const want = lower(wantRaw);
	if (want.empty())
	{
		err = ST::string("missing name (try 'team list' for tags)");
		return -1;
	}

	// tN — and bare digits (`1`, `2`...) so users who heard the tag in a
	// list can echo it back without remembering the `t`.
	{
		std::string digits;
		if (want[0] == 't' && want.size() >= 2)
			digits = want.substr(1);
		else
			digits = want;
		bool isDigits = !digits.empty();
		for (char c : digits)
		{
			if (!std::isdigit(static_cast<unsigned char>(c))) { isDigits = false; break; }
		}
		long n;
		if (isDigits && parseInt(digits, n))
		{
			INT8 const slot = Console_LookupTeamTag(n);
			if (slot < 0)
			{
				err = ST::format(
					"tag {} out of range (run 'team list' first; 1..{} valid)",
					wantRaw, Console_TeamTagCount());
				return -1;
			}
			return slot;
		}
	}

	INT8        exactSlot  = -1;
	INT8        prefixSlot = -1;
	std::size_t prefixHits = 0;

	for (INT8 i = 0; i < MAX_CHARACTER_COUNT; ++i)
	{
		SOLDIERTYPE const* const s = gCharactersList[i].merc;
		if (!s) continue;
		std::string const n = lower(s->name.to_std_string());
		if (n.empty()) continue;
		if (n == want) { exactSlot = i; }
		if (n.rfind(want, 0) == 0)
		{
			prefixSlot = i;
			++prefixHits;
		}
	}

	if (exactSlot >= 0) return exactSlot;
	if (prefixHits == 1) return prefixSlot;
	if (prefixHits > 1)
		err = ST::format("ambiguous name: {} (try 'team list')", wantRaw);
	else
		err = ST::format("no merc named: {} (try 'team list')", wantRaw);
	return -1;
}

bool Console_IsAlive(SOLDIERTYPE const& s)
{
	return s.bLife > 0 && s.bAssignment != ASSIGNMENT_DEAD;
}

bool Console_IsInTransit(SOLDIERTYPE const& s)
{
	return s.bAssignment == IN_TRANSIT;
}

bool Console_IsPOW(SOLDIERTYPE const& s)
{
	return s.bAssignment == ASSIGNMENT_POW;
}

bool Console_IsAsleep(SOLDIERTYPE const& s)
{
	return s.fMercAsleep;
}

// "On a squad" in the user's mental model: assigned to a squad slot, or
// to ON_DUTY itself (the engine's "in a squad but not yet placed").
bool Console_IsOnSquad(SOLDIERTYPE const& s)
{
	return s.bAssignment >= SQUAD_1 && s.bAssignment <= ON_DUTY;
}

// Non-combat assignment: anything past ON_DUTY in the assignment enum
// that isn't a transient status (IN_TRANSIT, DEAD, POW). DOCTOR / PATIENT
// / VEHICLE / REPAIR / TRAIN_* / HOSPITAL all qualify.
bool Console_IsOnNonCombatAssignment(SOLDIERTYPE const& s)
{
	return s.bAssignment > ON_DUTY              &&
	       s.bAssignment != IN_TRANSIT          &&
	       s.bAssignment != ASSIGNMENT_DEAD     &&
	       s.bAssignment != ASSIGNMENT_POW;
}

int Console_SquadNumber(SOLDIERTYPE const& s)
{
	return s.bAssignment - SQUAD_1 + 1;
}
