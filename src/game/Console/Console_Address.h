#ifndef GAME_CONSOLE_ADDRESS_H_
#define GAME_CONSOLE_ADDRESS_H_

#include "JA2Types.h"
#include "Types.h"

#include <cctype>
#include <cstddef>
#include <cstdlib>
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
 *   <tag><N>        positional tag — the Nth entry in one of `nearby`'s
 *                   distance-sorted category lists. Single-letter prefix
 *                   per category: e=hostiles, m=teammates, c=civilians,
 *                   i=item piles, d=doors, k=containers, x=exits,
 *                   z=hazards, b=mines, p=placed bombs, u=unexplored
 *                   frontiers. (Authoritative list: ConsoleTags.) Soldier
 *                   tags (e/m/c) set both fields; tile tags set gridno
 *                   only. The mapping re-enumerates on each call so it's
 *                   deterministic at command time but not stable across
 *                   turns — a relevant caveat only for moving entities;
 *                   static map features (i/d/k/x/z/b/p/u) don't shift on
 *                   their own between a `nearby` and the next verb.
 *
 *   <dir>/<steps>   two tokens, either order. A compass word (n, ne,
 *                   ..., nw) and a positive integer step count from the
 *                   observer's tile, in whichever order the user typed:
 *                   `tile e 8` and `tile 8 e` are equivalent. Gridno
 *                   set; soldier null. Optionally chained with a second
 *                   segment (four tokens total) so cartesian offsets
 *                   from `nearby` round-trip exactly: a door shown as
 *                   `8 E 3 N` can be addressed as `tile 8 e 3 n` (the
 *                   form that mirrors `nearby`'s rendering), `tile e 8
 *                   n 3`, or any other per-segment permutation. The
 *                   second segment is only consumed when both extra
 *                   tokens unambiguously parse as a segment, so
 *                   trailing verb arguments (e.g. `move e 5 run`) still
 *                   flow through to the caller. No JA2 name is purely
 *                   numeric, so the integer vs name disambiguation is
 *                   safe in practice.
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

/* Shared enumeration for soldier-typed positional tags (e<N>, m<N>).
 * Both lists are sorted by ascending tile distance from observer; ties
 * broken by soldier ID for determinism. Non-soldier tags (i/d/k/...)
 * have their own enumerators in Console_Query.cc and are reached via
 * ConsoleTags::resolve. */
struct ListedSoldier
{
	SOLDIERTYPE* soldier;
	INT16        distance;
	// Tile to address this entry against — typically the soldier's
	// current position (`soldier->sGridNo`). For decayed-out civilians
	// we surface the last-known tile here so `cN` resolves to where the
	// player last saw them, not the engine's current authoritative
	// position. Default of -1 is a sentinel for "unset"; every
	// enumerator must fill this field.
	INT16        gridno      = -1;
	// Civilians-only. True when this entry comes from
	// ConsoleVis::CivilianEverSeenHere — i.e., the player remembers
	// them but the public opplist has decayed. Hostiles and teammates
	// leave these defaults.
	bool         stale       = false;
	UINT32       lastSeenMin = 0;
	UINT16       animState   = 0;
};

void enumerateHostiles (const SOLDIERTYPE& observer, std::vector<ListedSoldier>& out);
void enumerateTeammates(const SOLDIERTYPE& observer, std::vector<ListedSoldier>& out);

/* Slot tags ("s1".."s19"). The mapping is fixed, "most actionable
 * first": s1 = HANDPOS, s2 = SECONDHANDPOS, then worn slots, then
 * face slots, then big pockets, then small pockets — the same order
 * Cmd_Inventory prints. Putting the hands at s1/s2 means the most
 * common reference is the shortest tag. */
static constexpr int kSlotTagCount = 19;

/** Return the InvSlotPos a "s<N>" tag refers to, or -1 if invalid.
 *  Accepts "s1".."s19" case-insensitively. */
INT8 parseSlotTag(const std::string& tok);

/** Inverse of parseSlotTag: 4-character buffer like "s12" for an
 *  InvSlotPos. Returns "?" for out-of-range. */
const char* slotTag(INT8 invPos);

/** Display label for an InvSlotPos ("In hand", "Big pocket 1", ...). */
const char* slotLabel(INT8 invPos);

/** The canonical inventory iteration order matching s1..s19. */
extern const INT8 kSlotOrder[kSlotTagCount];

/* Small string / parsing utilities shared by every console verb file.
 * Defined here (header / .cc) instead of duplicated per file in
 * unnamed namespaces. */

/** Case-insensitive prefix match: true if `needle` is a prefix of
 *  `haystack` ignoring ASCII case. */
bool startsWithCI(const ST::string& haystack, const std::string& needle);

/** Lowercase ASCII copy. Non-ASCII bytes are passed through unchanged. */
inline std::string lower(std::string s)
{
	for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

/** Parse a decimal integer that consumes the entire string. Returns
 *  false on empty input, leading/trailing junk, or pure overflow of
 *  long; on success writes the value into `out` (with the same
 *  static_cast-truncation each verb file used to do by hand). The
 *  template lets each call site declare `int` / `long` / `INT32` to
 *  match the field it's writing into. */
template <typename T>
bool parseInt(const std::string& s, T& out)
{
	if (s.empty()) return false;
	char* end = nullptr;
	long v = std::strtol(s.c_str(), &end, 10);
	if (end == s.c_str() || *end != '\0') return false;
	out = static_cast<T>(v);
	return true;
}

/** Parse a compass word — "n"/"north", "ne"/"northeast", ..., "nw"/
 *  "northwest". Case-insensitive. Returns the matching direction enum
 *  (NORTH..NORTHWEST) or -1 if the token isn't a compass word. */
INT8 parseCompass(const std::string& tok);

/** Loss-free relative offset rendering: "3 E 4 N", "5 W", "here".
 *  Replacement for "<n> tiles <dir>" pairs that snapped the bearing to
 *  one of 8 compass octants via atan8 and collapsed distance through
 *  SpacesAway's max(|dx|,|dy|) — the projection round-tripped only on
 *  exactly axial offsets, so a door at offset (5,3) read as "5 NE" but
 *  `tile ne 5` walked (5,5) and missed it. The cartesian form is
 *  unambiguous and round-trips: the same components feed back into the
 *  chained-segment address form (`tile e 5 n 3`).
 *
 *  East = increasing col; North = decreasing row (matches the engine's
 *  atan8 sign convention in Soldier_Control.cc). Zero components are
 *  omitted; east/west prints first when both axes are non-zero. */
ST::string formatOffset(INT16 origin, INT16 dest);

/** Find a single own-team merc by case-insensitive name prefix.
 *  Skips inactive / dead / out-of-sector mercs so console commands
 *  don't accidentally address someone who isn't on the board. On
 *  ambiguity or no match, returns nullptr and writes a one-line
 *  reason into `errorOut`. */
SOLDIERTYPE* findTeammateByName(const std::string& needle, ST::string& errorOut);

/** Verb prologue: returns the currently selected merc, or nullptr after
 *  printing `msg` to the console. The default message matches the
 *  bare-bones "No merc selected." line every action verb shares; pass a
 *  more specific message when the verb's context warrants one (e.g.
 *  "No merc selected to anchor 'nearby' on."). Use as:
 *      SOLDIERTYPE* const s = requireSelectedMerc();
 *      if (!s) return;
 *  Callers that don't need the soldier handle can still call this for
 *  the print-and-gate side effect; the return is intentionally not
 *  [[nodiscard]]. */
SOLDIERTYPE* requireSelectedMerc(const char* msg = "No merc selected.");

#endif // GAME_CONSOLE_ADDRESS_H_
