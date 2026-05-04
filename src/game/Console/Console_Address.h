#ifndef GAME_CONSOLE_ADDRESS_H_
#define GAME_CONSOLE_ADDRESS_H_

#include "JA2Types.h"
#include "Types.h"

#include <cstddef>
#include <string>
#include <string_theory/string>
#include <vector>

/* Console address parser.
 *
 * Resolves a user-typed target spec into either a soldier handle or a
 * tile gridno, anchored on `observer` (typically the selected merc) for
 * relative directions. Four address forms are supported:
 *
 *   <name>          one token. Case-insensitive prefix match across
 *                   teammates and known hostiles. Soldier+gridno set.
 *
 *   e<N> / m<N>     positional index — the Nth visible hostile (e) or
 *                   teammate (m) in distance order from the observer.
 *                   Disambiguates duplicate names. Computed on demand,
 *                   so the mapping is deterministic at command time but
 *                   not stable across turns.
 *
 *   <dir> <steps>   two tokens. Compass word (n, ne, ..., nw) plus an
 *                   integer step count from the observer's tile. Gridno
 *                   set; soldier null.
 *
 *   <col>,<row>     one token containing a comma. Map coordinate as a
 *                   fallback escape hatch. Gridno set; soldier null.
 *
 * Raw integer gridnos are not accepted on purpose: they're meaningless
 * to a player who can't see the world. */

struct Target
{
	SOLDIERTYPE* soldier;  // nullable; set only when the address was a name
	INT16        gridno;   // always valid on success
};

/** Parse an address starting at args[start]. On success returns the
 *  number of tokens consumed (1 or 2). On failure returns 0 and writes
 *  a one-line user-facing reason into `err`. */
int parseTarget(const std::vector<std::string>& args,
                std::size_t                     start,
                const SOLDIERTYPE*              observer,
                Target&                         out,
                ST::string&                     err);

/* Shared enumeration so 'nearby' and address parsing agree on the
 * meaning of e<N> / m<N>. Both lists are sorted by ascending tile
 * distance from observer; ties broken by soldier ID for determinism. */
struct ListedSoldier
{
	SOLDIERTYPE* soldier;
	INT16        distance;
};

void enumerateHostiles (const SOLDIERTYPE& observer, std::vector<ListedSoldier>& out);
void enumerateTeammates(const SOLDIERTYPE& observer, std::vector<ListedSoldier>& out);

#endif // GAME_CONSOLE_ADDRESS_H_
