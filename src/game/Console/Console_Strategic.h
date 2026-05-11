#ifndef GAME_CONSOLE_STRATEGIC_H_
#define GAME_CONSOLE_STRATEGIC_H_

#include "JA2Types.h"
#include "Types.h"

#include <cstddef>
#include <string>
#include <string_theory/string>
#include <vector>

/* Shared plumbing for strategic-side console verbs (team, map, assign, ...).
 *
 * What lives here is the cross-verb state that was originally trapped in
 * Console_Map.cc's anonymous namespace:
 *
 *   1. The campaign / screen gates — `team`, `map`, `assign` all want them.
 *   2. The `tN` tag table — both `team list` and `assign list` populate it,
 *      both of `team merc`, `map move`, `team sleep`, and `assign <merc>`
 *      consume it. Sharing the table means `t3` resolves against whichever
 *      list the user heard last, regardless of which verb produced it.
 *   3. Assignment-bucket predicates over SOLDIERTYPE — the engine's enum
 *      mixes squad slots (SQUAD_1..20) with task assignments (DOCTOR,
 *      REPAIR, TRAIN_*, ...). Naming the buckets in one place keeps every
 *      caller's `isOnSquad(s)` / `isOnNonCombatAssignment(s)` reading the
 *      same boundaries. */

/** Gate: require a started/loaded campaign. Returns false (and prints a
 *  reason) if no merc exists on OUR_TEAM yet. Also refreshes the
 *  mapscreen's character cache (`gCharactersList`) so callers reading
 *  through it see what OUR_TEAM holds. */
bool Console_RequireCampaign();

/** Switch to mapscreen if currently in tactical. Other screens (laptop,
 *  prebattle) keep their own modal flows; strategic verbs still print
 *  their readout, only the screen swap is conditional. */
void Console_EnsureMapscreen();

/* tN tag table — register, look up, count. The table is module-local;
 * verbs that produce a roster ("team list", "assign list") call
 * Console_RegisterTeamTags() with the slots in display order, and
 * subsequent verbs resolve `t<N>` against that table via
 * Console_ResolveCharSlot or Console_LookupTeamTag. */
void   Console_RegisterTeamTags(std::vector<INT8> const& slotsInDisplayOrder);
INT8   Console_LookupTeamTag(long n);
std::size_t Console_TeamTagCount();

/** Resolve `<name|tN|N>` to a slot index in gCharactersList. Match order:
 *  tN tag (or bare N as a tag), exact name, prefix name. On failure
 *  returns -1 and writes a one-line user-facing reason into `err`. */
INT8 Console_ResolveCharSlot(std::string const& want, ST::string& err);

/* Assignment-bucket predicates over SOLDIERTYPE. Each is a small inline
 * test of the engine's bAssignment / state fields; the predicates are
 * named so verb logic reads as intent rather than as a magic-number
 * comparison ("isOnSquad(s)" not "s.bAssignment <= ON_DUTY"). */
bool Console_IsAlive(SOLDIERTYPE const& s);
bool Console_IsInTransit(SOLDIERTYPE const& s);
bool Console_IsPOW(SOLDIERTYPE const& s);
bool Console_IsAsleep(SOLDIERTYPE const& s);
bool Console_IsOnSquad(SOLDIERTYPE const& s);
bool Console_IsOnNonCombatAssignment(SOLDIERTYPE const& s);

/** Squad number 1..20 for a soldier whose bAssignment is a squad slot.
 *  Callers must check Console_IsOnSquad() first. */
int  Console_SquadNumber(SOLDIERTYPE const& s);

#endif // GAME_CONSOLE_STRATEGIC_H_
