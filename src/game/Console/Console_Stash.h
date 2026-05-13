#ifndef GAME_CONSOLE_STASH_H_
#define GAME_CONSOLE_STASH_H_

#include <string>
#include <vector>

/* `stash [<sector>] [hidden]` and `stash take <address> [merc]`.
 *
 * Listing form reads the seen-items list for any strategic sector — the
 * same data the mapscreen "Sector Inventory" panel shows. Bare form
 * targets the currently loaded sector (live `gWorldItems`); other
 * sectors are loaded from their temp item file without touching the
 * rendered world. The `hidden` flag swaps the listing to items the
 * player hasn't actually spotted yet (debug peek; symmetric with the
 * panel's highlight cycle). Each row is one pile in the world
 * (`usItem` × `sGridNo` × `ubLevel`) and is annotated with `@ <offset>`
 * — cartesian from the selected merc for the loaded sector, raw col,row
 * for remote sectors (the form the address grammar accepts after
 * entering the sector). For browsable, addressable item piles in the
 * loaded sector use `nearby items` — it prints the same data with the
 * `i<N>` tags that `stash take` and other action verbs accept.
 *
 * `stash take <address>` lifts every visible, reachable WORLDITEM at
 * the addressed (gridno, merc-level) tile and hands them to the merc
 * via AutoPlaceObject — tile-granular, matching `pickup`'s "grab what's
 * here" model rather than the GUI panel's per-slot click flow. The
 * address is whatever `parseTarget` accepts: `i<N>` tag from `nearby
 * items`, compass+steps (`e 5 n 3`), or `col,row`. Partial fits get
 * pushed back at the original tile via AddItemToPool so nothing
 * vanishes when pockets fill up. The reverse direction (merc → stash)
 * is just `drop <slot>`, since dropping at the merc's tile produces a
 * visible WORLDITEM the panel/listing surface immediately. */
void Cmd_Stash(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_STASH_H_
