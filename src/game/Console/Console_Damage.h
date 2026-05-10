#ifndef GAME_CONSOLE_DAMAGE_H_
#define GAME_CONSOLE_DAMAGE_H_

#include <string>
#include <vector>

/* `damage` verb: review the recent damage events ring buffer populated
 * by hooks at EVENT_SoldierGotHit / SoldierTakeDamage. The buffer holds
 * the last 32 records so a player can answer "what just hit my merc?"
 * and "did that grenade kill anyone?" without having to parse the
 * scrolling tactical text. */

void Cmd_Damage(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_DAMAGE_H_
