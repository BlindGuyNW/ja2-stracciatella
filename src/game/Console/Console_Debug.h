#ifndef GAME_CONSOLE_DEBUG_H_
#define GAME_CONSOLE_DEBUG_H_

#include <string>
#include <vector>

/* One-shot diagnostics for the loaded tactical world. Used to triage
 * "why is enumerator X returning zero?" without resorting to ad-hoc
 * Console_Println calls scattered through query code. */
void Cmd_Debug(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_DEBUG_H_
