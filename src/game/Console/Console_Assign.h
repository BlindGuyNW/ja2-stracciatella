#ifndef GAME_CONSOLE_ASSIGN_H_
#define GAME_CONSOLE_ASSIGN_H_

#include <string>
#include <vector>

/* `assign` verb — task-first view onto the mapscreen assignment system.
 *
 *   assign                         Summary: counts by task type.
 *   assign list                    Per-merc roster grouped by sector.
 *   assign <name|tN>               Detailed readout for one merc.
 *   assign <name|tN> doctor        Field-heal teammates in the same sector.
 *   assign <name|tN> patient       Be healed by a doctor in the same sector.
 *   assign <name|tN> repair        Repair items (default target).
 *   assign <name|tN> repair robot
 *   assign <name|tN> repair vehicle <type>
 *   assign <name|tN> vehicle <type>        Board a vehicle in this sector.
 *   assign <name|tN> train self <stat>     Solo stat training.
 *   assign <name|tN> train town            Train militia (town/SAM sector).
 *   assign <name|tN> train teach <stat>    Teach the stat to teammates.
 *   assign <name|tN> train learn <stat>    Be a student of a teammate.
 *   assign <name|tN> squad <N>             Join (or move to) squad N.
 *
 * Eligibility mirrors the mapscreen popup: CanCharacterDoctor /
 * CanCharacterPatient / CanCharacterRepair / CanCharacterTrainStat /
 * CanCharacterVehicle / CanCharacterTrainMilitia are consulted before
 * dispatch, and the engine setters (SetSoldierAssignment*, PutSoldierIn-
 * Vehicle, AddCharacterToSquad) are called directly — no field-poking
 * and no menu-state fiddling. */

void Cmd_Assign(std::vector<std::string> const& args);

#endif // GAME_CONSOLE_ASSIGN_H_
