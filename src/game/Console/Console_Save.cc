#include "Console_Save.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "GameLoop.h"
#include "GameSettings.h"
#include "Game_Clock.h"
#include "Handle_UI.h"
#include "Interface_Dialogue.h"
#include "JAScreens.h"
#include "MainMenuScreen.h"
#include "Map_Information.h"
#include "Meanwhile.h"
#include "Options_Screen.h"
#include "Overhead.h"
#include "PreBattle_Interface.h"
#include "SaveLoadGame.h"
#include "SaveLoadScreen.h"
#include "ScreenIDs.h"
#include "StrategicMap.h"
#include "Timer_Control.h"
#include "Types.h"

#include "Console.h"
#include "Console_Address.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <utility>
#include <vector>

namespace
{
	// ----- text helpers ------------------------------------------------

	// Join args[from..end] with spaces, preserving original casing.
	// Save names are case-significant on disk on most filesystems and the
	// engine does case-insensitive lookup, so we keep what the user typed.
	std::string joinFrom(const std::vector<std::string>& args, std::size_t from)
	{
		std::string out;
		for (std::size_t i = from; i < args.size(); ++i)
		{
			if (i > from) out += " ";
			out += args[i];
		}
		return out;
	}

	// ----- gating ------------------------------------------------------

	// True if a campaign is in progress (any player merc on the team).
	// Saving from the main menu is meaningless; this gate matches the
	// inCampaignGate() pattern used elsewhere in the console module.
	bool campaignActive()
	{
		CFOR_EACH_IN_TEAM(s, OUR_TEAM) { (void)s; return true; }
		return false;
	}

	// Mirror DoDeadIsDeadSave's runtime quiescence check, plus
	// CanGameBeSaved() for Iron Man's "no save in combat / with enemies in
	// sector" rules. Returns an empty string when it's safe to save.
	ST::string saveSafetyCheck()
	{
		if (!CanGameBeSaved())
		{
			if (gGameOptions.ubGameSaveMode == DIF_IRON_MAN)
			{
				if (gTacticalStatus.uiFlags & INCOMBAT)
					return "Iron Man — cannot save during combat.";
				return "Iron Man — cannot save while enemies are in this sector.";
			}
			return "Engine refused to save.";
		}
		if (gTacticalStatus.ubCurrentTeam != OUR_TEAM)
			return "Not your turn — wait for the AI turn to end.";
		if (gfInTalkPanel)
			return "NPC dialogue is open — close it first.";
		if (gfInMeanwhile)
			return "A cutscene is playing — wait for it to end.";
		if (gfPreBattleInterfaceActive)
			return "Pre-battle interface is open — resolve it first.";
		if (gCurrentUIMode == LOCKUI_MODE)
			return "Game UI is locked — wait for the current action to finish.";
		return {};
	}

	// Looser check for load: same UI quiescence rules, but skip the
	// CanGameBeSaved() Iron-Man-specific tests (loading isn't blocked by
	// Iron Man, and from the main menu there's no game state to consult).
	ST::string loadSafetyCheck()
	{
		if (guiCurrentScreen == MAINMENU_SCREEN ||
		    guiCurrentScreen == INTRO_SCREEN ||
		    guiCurrentScreen == GAME_INIT_OPTIONS_SCREEN)
		{
			return {};
		}
		if (gTacticalStatus.ubCurrentTeam != OUR_TEAM)
			return "Not your turn — wait for the AI turn to end.";
		if (gfInTalkPanel)
			return "NPC dialogue is open — close it first.";
		if (gfInMeanwhile)
			return "A cutscene is playing — wait for it to end.";
		if (gfPreBattleInterfaceActive)
			return "Pre-battle interface is open — resolve it first.";
		if (gCurrentUIMode == LOCKUI_MODE)
			return "Game UI is locked — wait for the current action to finish.";
		return {};
	}

	bool isReservedName(const ST::string& name)
	{
		return IsQuickSaveName(name) || IsAutoSaveName(name) ||
		       IsErrorSaveName(name);
	}

	// ----- save list & lookups ----------------------------------------

	// Sort newest-first by file mtime, matching SaveLoadScreen.cc's
	// compareSaveGames so console rows line up with what a sighted player
	// would see.
	void sortByMtimeDesc(std::vector<SaveGameInfo>& v)
	{
		std::sort(v.begin(), v.end(),
			[](const SaveGameInfo& a, const SaveGameInfo& b) {
				auto ta = GCM->saveGameFiles()->getLastModifiedTime(GetSaveGamePath(a.name()));
				auto tb = GCM->saveGameFiles()->getLastModifiedTime(GetSaveGamePath(b.name()));
				return ta > tb;
			});
	}

	// User-facing label for a save. The GUI saves to a timestamped
	// filename (`2026-05-06T22-14-34Z-real`) but stores the human-typed
	// "real" as the description, so what the player thinks of as the
	// save name is `sSavedGameDesc`. Console-created saves use the same
	// string for both, so this also returns "real" for those.
	ST::string saveLabel(const SaveGameInfo& s)
	{
		const auto& desc = s.header().sSavedGameDesc;
		return desc.empty() ? s.name() : desc;
	}

	// Return all saves whose label or filename matches `tok`. We prefer
	// label (description) matches because that's what the user sees, but
	// filename matches are accepted as a fallback for users who already
	// know the on-disk name.
	std::vector<const SaveGameInfo*> findMatches(
		const std::vector<SaveGameInfo>& v, const ST::string& tok)
	{
		std::vector<const SaveGameInfo*> labelHits;
		std::vector<const SaveGameInfo*> nameHits;
		for (const auto& s : v)
		{
			if (saveLabel(s).compare(tok, ST::case_insensitive) == 0)
				labelHits.push_back(&s);
			else if (s.name().compare(tok, ST::case_insensitive) == 0)
				nameHits.push_back(&s);
		}
		if (!labelHits.empty()) return labelHits;
		return nameHits;
	}

	const SaveGameInfo* findByName(const std::vector<SaveGameInfo>& v,
		const ST::string& name)
	{
		auto hits = findMatches(v, name);
		return hits.empty() ? nullptr : hits.front();
	}

	// Parse "s\d+" → 1-based index into the user-saves list (the same
	// order cmdSaveList prints). Returns -1 if `tok` is not an s-tag.
	int parseSlotTag(const std::string& tok)
	{
		if (tok.size() < 2) return -1;
		if (tok[0] != 's' && tok[0] != 'S') return -1;
		int n = 0;
		for (std::size_t i = 1; i < tok.size(); ++i)
		{
			if (tok[i] < '0' || tok[i] > '9') return -1;
			n = n * 10 + (tok[i] - '0');
			if (n > 9999) return -1;
		}
		return n > 0 ? n : -1;
	}

	// Build the same user-saves list cmdSaveList prints (no quick/auto,
	// sorted newest-first). We rebuild on every lookup so a save written
	// since the last `save list` call still resolves predictably.
	std::vector<SaveGameInfo> userSavesSorted()
	{
		auto saves = GetValidSaveGames();
		std::vector<SaveGameInfo> out;
		out.reserve(saves.size());
		for (auto& s : saves)
		{
			if (IsQuickSaveName(s.name()) || IsAutoSaveName(s.name()))
				continue;
			out.push_back(std::move(s));
		}
		sortByMtimeDesc(out);
		return out;
	}

	// Resolve a token that may be either a save label/filename or an
	// s-tag. Returns the on-disk filename on success, empty on failure
	// (and prints the appropriate refusal). Slot tags only address user
	// saves — quick/auto go via dedicated subcommands.
	ST::string resolveSaveRef(const std::string& tok)
	{
		int slot = parseSlotTag(tok);
		if (slot > 0)
		{
			auto users = userSavesSorted();
			if (static_cast<std::size_t>(slot) > users.size())
			{
				Console_Println(ST::format(
					"No save at slot s{} (have {}).", slot, users.size()));
				return {};
			}
			return users[slot - 1].name();
		}
		ST::string ref(tok);
		auto saves = GetValidSaveGames();
		auto hits = findMatches(saves, ref);
		if (hits.empty())
		{
			Console_Println(ST::format(
				"No save named '{}' — try 'save list'.", ref));
			return {};
		}
		if (hits.size() > 1)
		{
			// Disambiguate by mtime descending — the newest match is
			// almost always what the user means. Surface the ambiguity
			// so they can pick a slot tag if it's the wrong one.
			std::sort(hits.begin(), hits.end(),
				[](const SaveGameInfo* a, const SaveGameInfo* b) {
					auto ta = GCM->saveGameFiles()->getLastModifiedTime(GetSaveGamePath(a->name()));
					auto tb = GCM->saveGameFiles()->getLastModifiedTime(GetSaveGamePath(b->name()));
					return ta > tb;
				});
			Console_Println(ST::format(
				"{} saves match '{}'; using the most recent. "
				"Use 'save list' + s<N> to pick a specific one.",
				hits.size(), ref));
		}
		return hits.front()->name();
	}

	// One-line summary — "Day 1, 07:00 — A9: Omerta — 3 mercs — $27,270".
	// Mirrors the row text the user pulled with `r list` in the screen.
	ST::string formatHeaderSummary(const SAVED_GAME_HEADER& h)
	{
		ST::string sector;
		if (h.sSector.IsValid())
			sector = GetSectorIDString(h.sSector, FALSE);
		else
			sector = "—";
		const char* mercWord = h.ubNumOfMercsOnPlayersTeam == 1 ? "merc" : "mercs";
		return ST::format(
			"Day {}, {02d}:{02d} — {} — {} {} — ${}",
			h.uiDay, h.ubHour, h.ubMin,
			sector,
			h.ubNumOfMercsOnPlayersTeam, mercWord,
			h.iCurrentBalance);
	}

	// ----- save handlers ----------------------------------------------

	void cmdSaveStatus()
	{
		auto saves = GetValidSaveGames();
		std::size_t userCount  = 0;
		std::size_t quickCount = 0;
		std::size_t autoCount  = 0;
		for (const auto& s : saves)
		{
			if (IsQuickSaveName(s.name()))      ++quickCount;
			else if (IsAutoSaveName(s.name()))  ++autoCount;
			else                                ++userCount;
		}

		const char* mode = "Save Anywhere";
		switch (gGameOptions.ubGameSaveMode)
		{
			case DIF_IRON_MAN:      mode = "Iron Man";      break;
			case DIF_DEAD_IS_DEAD:  mode = "Dead is Dead";  break;
			default: break;
		}

		Console_Println(ST::format(
			"Saves: {} user, {} quick, {} auto. Mode: {}.",
			userCount, quickCount, autoCount, mode));

		if (!gGameSettings.sCurrentSavedGameName.empty())
		{
			Console_Println(ST::format(
				"Current slot: '{}'.",
				gGameSettings.sCurrentSavedGameName));
		}
		ST::string err = saveSafetyCheck();
		if (err.empty())
		{
			Console_Println("Safe to save now.");
		}
		else
		{
			Console_Println(ST::format("Cannot save: {}", err));
		}
	}

	void cmdSaveList()
	{
		auto saves = GetValidSaveGames();
		sortByMtimeDesc(saves);

		std::vector<const SaveGameInfo*> user;
		std::vector<const SaveGameInfo*> special;
		for (const auto& s : saves)
		{
			if (IsQuickSaveName(s.name()) || IsAutoSaveName(s.name()))
				special.push_back(&s);
			else
				user.push_back(&s);
		}

		if (user.empty() && special.empty())
		{
			Console_Println("No saves on disk.");
			return;
		}

		if (!user.empty())
		{
			Console_Println(ST::format("Saves ({}):", user.size()));
			std::size_t i = 1;
			for (const auto* s : user)
			{
				Console_Println(ST::format("  s{}  {} — {}",
					i++, saveLabel(*s),
					formatHeaderSummary(s->header())));
			}
		}
		if (!special.empty())
		{
			Console_Println(ST::format("Special ({}):", special.size()));
			for (const auto* s : special)
			{
				Console_Println(ST::format("  {} — {}",
					saveLabel(*s),
					formatHeaderSummary(s->header())));
			}
		}
	}

	// `tok` is what the user typed: either a literal save name or an
	// s-tag (`s3`). For overwrite=true we resolve through the slot list;
	// for overwrite=false (new save), the token is always a literal name
	// — slot tags by definition refer to existing saves and would just
	// route to overwrite.
	void cmdSaveCreate(const std::string& tok, bool overwrite)
	{
		if (tok.empty())
		{
			Console_Println("usage: save <name>  (or 'save overwrite <name|s<N>>')");
			return;
		}
		if (gGameOptions.ubGameSaveMode == DIF_DEAD_IS_DEAD)
		{
			Console_Println(ST::format(
				"Dead is Dead — only 'save quick' is permitted (current slot: '{}').",
				gGameSettings.sCurrentSavedGameName));
			return;
		}
		if (!campaignActive())
		{
			Console_Println("No campaign loaded — start or load a game first.");
			return;
		}
		ST::string err = saveSafetyCheck();
		if (!err.empty())
		{
			Console_Println(ST::format("Cannot save: {}", err));
			return;
		}

		ST::string name;
		const SaveGameInfo* existing = nullptr;
		auto saves = GetValidSaveGames();

		if (overwrite)
		{
			name = resolveSaveRef(tok);
			if (name.empty()) return;            // resolver already printed
			existing = findByName(saves, name);  // exists by construction
		}
		else
		{
			// New save: token is the literal name. Reject s-tags here —
			// they imply an existing slot, which is overwrite territory.
			if (parseSlotTag(tok) > 0)
			{
				Console_Println(ST::format(
					"'{}' looks like a slot tag — use 'save overwrite {}' to replace, "
					"or pick a different name.", tok, tok));
				return;
			}
			name = ST::string(tok);
			if (isReservedName(name))
			{
				Console_Println(ST::format(
					"'{}' is a reserved save name — use 'save quick' instead.",
					name));
				return;
			}
			existing = findByName(saves, name);
			if (existing)
			{
				ST::string label = saveLabel(*existing);
				Console_Println(ST::format(
					"Save '{}' already exists — {}.",
					label,
					formatHeaderSummary(existing->header())));
				Console_Println(ST::format(
					"Use 'save overwrite {}' to replace it.", label));
				return;
			}
		}

		// Mirror DoDeadIsDeadSave's backup pass: keep the previous file
		// rotated out before we clobber it.
		if (existing) BackupSavedGame(name);

		// SaveGeneralInfo writes guiPreviousOptionScreen as the save's
		// "go to this screen after loading" field (SaveLoadGame.cc:1767).
		// Vanilla save paths always run after a transition into
		// OPTIONS_SCREEN that sets the global to wherever the player
		// came from; console saves skip that, so without this line the
		// global stays at its file-static default (OPTIONS_SCREEN, see
		// Options_Screen.cc:122) and every load lands on the in-game
		// Options panel.
		//
		// We can't naively mirror guiCurrentScreen — the user can save
		// from a screen that isn't a viable resume target (OPTIONS_SCREEN
		// itself, MAINMENU_SCREEN if a campaign is somehow active there,
		// SAVE_LOAD_SCREEN, etc.). When the current screen isn't a
		// gameplay screen, derive the resume target from world state:
		// tactical if the sector world is loaded, strategic otherwise.
		guiPreviousOptionScreen =
			(guiCurrentScreen == GAME_SCREEN || guiCurrentScreen == MAP_SCREEN)
				? guiCurrentScreen
				: (gfWorldLoaded ? GAME_SCREEN : MAP_SCREEN);

		BOOLEAN ok = SaveGame(name, name);
		if (!ok)
		{
			Console_Println("Save failed — engine reported an error.");
			return;
		}
		Console_Println(ST::format(
			existing ? "Overwrote save '{}'." : "Saved as '{}'.", name));
	}

	void cmdSaveQuick()
	{
		if (!campaignActive())
		{
			Console_Println("No campaign loaded — start or load a game first.");
			return;
		}
		ST::string err = saveSafetyCheck();
		if (!err.empty())
		{
			Console_Println(ST::format("Cannot save: {}", err));
			return;
		}
		// Vanilla quicksave call sites (Turn_Based_Input.cc:1964,
		// MapScreen.cc:2996) prep this before DoQuickSave; see the long
		// comment in cmdSaveCreate for why the console path has to do
		// the same and why we don't blindly mirror guiCurrentScreen.
		guiPreviousOptionScreen =
			(guiCurrentScreen == GAME_SCREEN || guiCurrentScreen == MAP_SCREEN)
				? guiCurrentScreen
				: (gfWorldLoaded ? GAME_SCREEN : MAP_SCREEN);
		// DoQuickSave handles the DIF_DEAD_IS_DEAD route internally and
		// shows its own error popup on failure; we let it.
		DoQuickSave();
		Console_Println("Quick save invoked.");
	}

	void cmdSaveDelete(const std::string& tok)
	{
		if (tok.empty())
		{
			Console_Println("usage: save delete <name|s<N>>");
			return;
		}
		ST::string name = resolveSaveRef(tok);
		if (name.empty()) return;
		try
		{
			GCM->saveGameFiles()->deleteFile(GetSaveGamePath(name));
		}
		catch (const std::runtime_error& e)
		{
			Console_Println(ST::format("Delete failed: {}", e.what()));
			return;
		}
		Console_Println(ST::format("Deleted save '{}'.", name));
	}

	// ----- load handlers ----------------------------------------------

	// Drive LoadSavedGame() directly. The SaveLoadScreen normally wraps
	// it in a fade transition (see DoneFadeOutForSaveLoadScreen at
	// SaveLoadScreen.cc:1176) — visual only, no functional effect. We
	// replicate the post-load pause/screen handoff inline.
	void doLoad(const ST::string& filename, const ST::string& label)
	{
		try
		{
			LoadSavedGame(filename);
		}
		catch (const std::runtime_error& e)
		{
			Console_Println(ST::format("Load failed: {}", e.what()));
			return;
		}

		ScreenID const dest = guiScreenToGotoAfterLoadingSavedGame;
		// SetPendingNewScreen jumps `guiCurrentScreen` straight to `dest`
		// from inside GameLoop, bypassing whatever screen we're leaving.
		// GameLoop's per-screen deinit switch handles MAP_SCREEN and
		// LAPTOP_SCREEN explicitly, but MAINMENU_SCREEN falls through —
		// `ExitMainMenu()` only runs from inside `MainMenuScreenHandle()`
		// when `gfMainMenuScreenExit` is set. Drive that flag instead so
		// the menu's buttons and background image are torn down. Without
		// this the menu's regions linger in tactical, showing up under
		// `g list` and narrating on hover via FastHelp.
		if (guiCurrentScreen == MAINMENU_SCREEN)
			SetMainMenuExitScreen(dest);
		else
			SetPendingNewScreen(dest);
		if (dest == MAP_SCREEN)
		{
			if (!gfPauseDueToPlayerGamePause)
			{
				UnLockPauseState();
				UnPauseGame();
			}
		}
		else
		{
			PauseTime(FALSE);
			if (GamePaused())
			{
				HandlePlayerPauseUnPauseOfGame();
				HandlePlayerPauseUnPauseOfGame();
			}
		}
		Console_Println(ST::format("Loaded '{}'.", label));
	}

	void cmdLoadName(const std::string& tok)
	{
		if (tok.empty())
		{
			Console_Println("usage: load <name|s<N>>  (or 'load quick')");
			return;
		}
		ST::string err = loadSafetyCheck();
		if (!err.empty())
		{
			Console_Println(ST::format("Cannot load: {}", err));
			return;
		}
		ST::string filename = resolveSaveRef(tok);
		if (filename.empty()) return;
		// Re-fetch to find the label for display. Cheap; the load
		// itself dwarfs the directory walk.
		auto saves = GetValidSaveGames();
		ST::string label = filename;
		for (const auto& s : saves)
			if (s.name() == filename) { label = saveLabel(s); break; }
		doLoad(filename, label);
	}

	void cmdLoadQuick()
	{
		ST::string err = loadSafetyCheck();
		if (!err.empty())
		{
			Console_Println(ST::format("Cannot load: {}", err));
			return;
		}
		auto saves = GetValidSaveGames();
		ST::string quickName = GetQuickSaveName();
		const SaveGameInfo* existing = findByName(saves, quickName);
		if (!existing)
		{
			Console_Println("No quick save on disk.");
			return;
		}
		doLoad(existing->name(), saveLabel(*existing));
	}
}

void Cmd_Save(const std::vector<std::string>& args)
{
	if (args.size() < 2) { cmdSaveStatus(); return; }
	const std::string sub = lower(args[1]);
	if (sub == "list")      { cmdSaveList(); return; }
	if (sub == "quick")     { cmdSaveQuick(); return; }
	if (sub == "delete")
	{
		cmdSaveDelete(joinFrom(args, 2));
		return;
	}
	if (sub == "overwrite")
	{
		cmdSaveCreate(joinFrom(args, 2), /*overwrite=*/true);
		return;
	}
	// Bare-name case: 'save real' → create new (refuse if exists).
	cmdSaveCreate(joinFrom(args, 1), /*overwrite=*/false);
}

void Cmd_Load(const std::vector<std::string>& args)
{
	if (args.size() < 2) { cmdSaveList(); return; }
	const std::string sub = lower(args[1]);
	if (sub == "list")  { cmdSaveList(); return; }
	if (sub == "quick") { cmdLoadQuick(); return; }
	cmdLoadName(joinFrom(args, 1));
}
