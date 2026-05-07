#ifndef GAME_CONSOLE_TAGS_H_
#define GAME_CONSOLE_TAGS_H_

#include "Types.h"

/* Positional tile-tag resolver.
 *
 * `nearby` prints each entry with a single-letter + index tag (e.g.
 * `i3` for the third item pile, `d5` for the fifth door). This
 * namespace turns those tags back into the tile they describe, so
 * action verbs can address what `nearby` listed.
 *
 * Indices align with `nearby` by construction: both call the same
 * enumerator with the same observer, so `i3` here is the same pile
 * `nearby items` showed at line 3. The lookup re-enumerates on every
 * call (no cache) — for static map features that's functionally
 * stable; for moving entities (hostiles, teammates, civilians) it
 * tracks current state, same caveat e<N>/m<N> always carried.
 *
 * Slot tags `s<N>` are a different system — they address inventory
 * positions, not tiles. See parseSlotTag in Console_Address.h. */

struct SOLDIERTYPE;

namespace ConsoleTags
{
	struct Target
	{
		INT16        gridno;   // valid on Resolved
		SOLDIERTYPE* soldier;  // nullable; non-null only for e/m/c
	};

	enum Result
	{
		kNotATag         = 0,   // prefix not recognized
		kResolved        = 1,
		kIndexOutOfRange = -1,  // prefix valid; idx outside [1..listSize]
	};

	/** Resolve a tag's prefix letter + 1-based index to a tile.
	 *  On kIndexOutOfRange, listSize holds the actual category count
	 *  so callers can produce a precise error. On kResolved, out is
	 *  filled. On kNotATag both outputs are unspecified. */
	Result resolve(char               prefix,
	               int                idx,
	               const SOLDIERTYPE& observer,
	               Target&            out,
	               int&               listSize);

	/** Human-readable category name for error messages, or nullptr if
	 *  the prefix isn't a recognized tag prefix. */
	const char* categoryName(char prefix);

	/** True iff `c` is a recognized single-letter tag prefix. The
	 *  address parser uses this to decide whether to attempt tag
	 *  lookup before falling through to compass / name. */
	bool isTagPrefix(char c);
}

#endif // GAME_CONSOLE_TAGS_H_
