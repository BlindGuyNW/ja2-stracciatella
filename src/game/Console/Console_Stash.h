#ifndef GAME_CONSOLE_STASH_H_
#define GAME_CONSOLE_STASH_H_

#include <string>
#include <vector>

/* `stash [<sector>] [hidden]`
 *
 * Reads the seen-items list for any strategic sector — the same data the
 * mapscreen "Sector Inventory" panel shows. Bare form targets the
 * currently loaded sector (live `gWorldItems`); other sectors are loaded
 * from their temp item file without touching the rendered world. The
 * `hidden` flag swaps the listing to items the player hasn't actually
 * spotted yet (debug peek; symmetric with the panel's highlight cycle). */
void Cmd_Stash(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_STASH_H_
