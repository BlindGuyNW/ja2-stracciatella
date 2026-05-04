#ifndef GAME_CONSOLE_DISPATCH_H_
#define GAME_CONSOLE_DISPATCH_H_

/* Per-frame console pump. Drains pending command lines from the console
 * worker thread, parses each, and calls the matching handler on the
 * game thread. Must be called exactly once per game cycle. */
void ConsoleDispatch_Tick(void);

#endif // GAME_CONSOLE_DISPATCH_H_
