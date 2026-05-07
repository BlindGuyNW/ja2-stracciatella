#ifndef GAME_CONSOLE_SAVE_H_
#define GAME_CONSOLE_SAVE_H_

#include <string>
#include <vector>

/* Save / Load console verbs. Bypass the SaveLoadScreen entirely — its text
 * input field for the savename isn't surfaced to TextCapture, so the SR
 * user can't type a slot name through the rendered UI. We drive
 * SaveGame() / LoadSavedGame() directly and render the slot list as
 * console rows that match what a sighted player would read off the
 * screen. */

void Cmd_Save(const std::vector<std::string>& args);
void Cmd_Load(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_SAVE_H_
