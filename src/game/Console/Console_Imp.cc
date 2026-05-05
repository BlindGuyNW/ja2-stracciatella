#include "Console_Imp.h"

#include "Button_System.h"
#include "MouseSystem.h"

#include "CharProfile.h"
#include "IMP_Begin_Screen.h"
#include "IMP_Confirm.h"
#include "IMP_MainPage.h"
#include "IMP_Personality_Quiz.h"
#include "IMP_Portraits.h"
#include "IMP_SkillTraits.h"
#include "IMP_Attribute_Selection.h"
#include "IMP_Text_System.h"
#include "IMP_Voices.h"
#include "Laptop.h"
#include "LaptopSave.h"
#include "Text.h"
#include "Text_Input.h"

#include "Console.h"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	// ----- Gates --------------------------------------------------------

	bool laptopGate()
	{
		if (guiCurrentLaptopMode == LAPTOP_MODE_CHAR_PROFILE) return true;
		Console_Println("Open the IMP web site first (laptop → IMP).");
		return false;
	}

	// page == one of the IMP_* enum values from CharProfile.h. label is for
	// the failure message (e.g. "personality quiz"). Returns false and
	// prints if iCurrentImpPage doesn't match.
	bool pageGate(INT32 page, const char* label)
	{
		if (iCurrentImpPage == page) return true;
		Console_Println(ST::format("not on the {} page (try 'imp goto …').", label));
		return false;
	}

	// Multi-page variant. Either of the two pages is acceptable. Useful
	// when a verb (e.g. `imp confirm`) is meaningful on more than one
	// sub-screen of the IMP flow.
	bool pageGate2(INT32 a, INT32 b, const char* label)
	{
		if (iCurrentImpPage == a || iCurrentImpPage == b) return true;
		Console_Println(ST::format("not on the {} page (try 'imp goto …').", label));
		return false;
	}

	// ----- Argument parsing --------------------------------------------

	bool parseInt(const std::string& s, INT32& out)
	{
		if (s.empty()) return false;
		char* end = nullptr;
		long v = std::strtol(s.c_str(), &end, 10);
		if (end == s.c_str() || *end != '\0') return false;
		out = static_cast<INT32>(v);
		return true;
	}

	// Accepts +N, -N, or N. Used by `imp stat <name> <±delta>`.
	bool parseDelta(const std::string& s, INT32& out)
	{
		if (s.empty()) return false;
		const char* p = s.c_str();
		if (*p == '+') ++p;
		char* end = nullptr;
		long v = std::strtol(p, &end, 10);
		if (end == p || *end != '\0') return false;
		out = static_cast<INT32>(v);
		return true;
	}

	std::string lower(std::string s)
	{
		for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}

	// Case-insensitive prefix match against a sequence of labels. Used by
	// `imp trait` (against gzIMPSkillTraitsText) and `imp stat` (against
	// kStatTable) — both want the same "exact match wins, otherwise unique
	// prefix wins, ambiguous returns -1" semantics. labelAt(i) returns the
	// label for index i; count is the number of labels.
	INT32 findByPrefix(const std::string& want, std::size_t count,
		const std::function<std::string(std::size_t)>& labelAt)
	{
		const std::string w = lower(want);
		INT32 hit = -1;
		INT32 hits = 0;
		for (std::size_t i = 0; i < count; ++i)
		{
			const std::string label = lower(labelAt(i));
			if (label == w) return static_cast<INT32>(i);
			if (label.rfind(w, 0) == 0) { hit = static_cast<INT32>(i); ++hits; }
		}
		return hits == 1 ? hit : -1;
	}

	// Joins arg tokens [from..end] with single spaces. Quoted-string
	// tokenisation isn't in the dispatcher, so we glue multi-word names
	// back together for `imp name`, `imp nickname`.
	ST::string joinFrom(const std::vector<std::string>& args, std::size_t from)
	{
		ST::string out;
		for (std::size_t i = from; i < args.size(); ++i)
		{
			if (i > from) out += " ";
			out += ST::string(args[i]);
		}
		return out;
	}

	// ----- Button click synthesis --------------------------------------

	// Fires a button's click callback the same way Console_Gui's `g click`
	// does — DWN then UP, with the region's mouse coords centred on the
	// button so position-sensitive callbacks see a real-looking click.
	void clickButton(GUI_BUTTON* b)
	{
		const INT16 cx = b->X() + b->W() / 2;
		const INT16 cy = b->Y() + b->H() / 2;
		b->Area.MouseXPos    = cx;
		b->Area.MouseYPos    = cy;
		b->Area.RelativeXPos = static_cast<INT16>(cx - b->X());
		b->Area.RelativeYPos = static_cast<INT16>(cy - b->Y());
		if (!b->ClickCallback) return;
		b->ClickCallback(b, MSYS_CALLBACK_REASON_LBUTTON_DWN);
		b->ClickCallback(b, MSYS_CALLBACK_REASON_LBUTTON_UP);
	}

	// ----- imp (orientation readout) -----------------------------------

	const char* pageName(INT32 page)
	{
		switch (page)
		{
			case IMP_HOME_PAGE:          return "home";
			case IMP_BEGIN:              return "begin (name/gender)";
			case IMP_FINISH:             return "finish";
			case IMP_MAIN_PAGE:          return "main";
			case IMP_PERSONALITY:        return "personality entrance";
			case IMP_PERSONALITY_QUIZ:   return "personality quiz";
			case IMP_SKILLTRAITS:        return "skill traits";
			case IMP_PERSONALITY_FINISH: return "personality finish";
			case IMP_ATTRIBUTE_ENTRANCE: return "attribute entrance";
			case IMP_ATTRIBUTE_PAGE:     return "attribute selection";
			case IMP_ATTRIBUTE_FINISH:   return "attribute finish";
			case IMP_PORTRAIT:           return "portrait";
			case IMP_VOICE:              return "voice";
			case IMP_ABOUT_US:           return "about us";
			case IMP_CONFIRM:            return "confirm hire";
			default:                     return "unknown";
		}
	}

	const char* profileModeName(INT32 mode)
	{
		switch (mode)
		{
			case 0: return "starting (name/gender pending)";
			case 1: return "name & gender done; personality next";
			case 2: return "personality done; attributes next";
			case 3: return "attributes done; portrait next";
			case 4: return "portrait done; voice next";
			case 5: return "all profiling complete; ready to hire";
			default: return "unknown";
		}
	}

	void cmdSummary()
	{
		Console_Println(ST::format("page: {} (id {})", pageName(iCurrentImpPage), iCurrentImpPage));
		Console_Println(ST::format("profile mode: {} ({})",
			static_cast<int>(iCurrentProfileMode), profileModeName(iCurrentProfileMode)));

		if (!pFullName.empty() || !pNickName.empty())
		{
			Console_Println(ST::format("name: \"{}\"  nickname: \"{}\"  gender: {}",
				pFullName, pNickName, fCharacterIsMale ? "male" : "female"));
		}

		if (LaptopSaveInfo.fIMPCompletedFlag)
		{
			Console_Println("IMP profile already completed (re-entry blocked).");
		}

		// Page-specific hint.
		switch (iCurrentImpPage)
		{
			case IMP_PERSONALITY_QUIZ:
				Console_Println(ST::format(
					"quiz: question {} of 16 (max reached: {}); "
					"current selection: {}",
					giCurrentPersonalityQuizQuestion + 1,
					giMaxPersonalityQuizQuestion + 1,
					iCurrentAnswer < 0 ? ST::string("none")
					                   : ST::format("{}", iCurrentAnswer + 1)));
				Console_Println("try 'imp question' to read it; 'imp answer <N>' to pick; 'imp confirm' to advance.");
				break;
			case IMP_BEGIN:
				Console_Println("try 'imp name <full name>', 'imp nickname <nick>', 'imp gender m|f', then 'imp done'.");
				break;
			case IMP_ATTRIBUTE_PAGE:
				Console_Println("try 'imp stats' to read or 'imp stat <name> <+/-N>' to change; 'imp done' when finished.");
				break;
			case IMP_SKILLTRAITS:
				Console_Println("try 'imp traits' or 'imp trait <name>'; 'imp done' when finished.");
				break;
			case IMP_PORTRAIT:
				Console_Println("try 'imp portrait' or 'imp portrait <N>'; 'imp done' when finished.");
				break;
			case IMP_VOICE:
				Console_Println("try 'imp voice' or 'imp voice <N>'; 'imp done' when finished.");
				break;
			case IMP_PERSONALITY:
			case IMP_ATTRIBUTE_ENTRANCE:
				Console_Println("try 'imp done' to begin this section.");
				break;
			case IMP_CONFIRM:
				Console_Println("try 'imp hire' to commit.");
				break;
			default: break;
		}
	}

	// ----- imp goto -----------------------------------------------------

	struct GotoEntry { const char* name; INT32 page; };
	const GotoEntry kGotoTable[] =
	{
		{ "home",        IMP_HOME_PAGE      },
		{ "main",        IMP_MAIN_PAGE      },
		{ "name",        IMP_BEGIN          },
		{ "begin",       IMP_BEGIN          },
		{ "personality", IMP_PERSONALITY    },
		{ "quiz",        IMP_PERSONALITY_QUIZ },
		{ "traits",      IMP_SKILLTRAITS    },
		{ "attributes",  IMP_ATTRIBUTE_PAGE },
		{ "stats",       IMP_ATTRIBUTE_PAGE },
		{ "portrait",    IMP_PORTRAIT       },
		{ "voice",       IMP_VOICE          },
		{ "finish",      IMP_FINISH         },
		{ "confirm",     IMP_CONFIRM        },
	};

	void cmdGoto(const std::vector<std::string>& args)
	{
		if (args.size() < 3)
		{
			Console_Println("usage: imp goto <home|main|name|personality|quiz|traits|attributes|portrait|voice|finish|confirm>");
			return;
		}
		const std::string want = lower(args[2]);
		for (const auto& e : kGotoTable)
		{
			if (want == e.name)
			{
				iCurrentImpPage = e.page;
				fButtonPendingFlag = TRUE;
				Console_Println(ST::format("goto {}: {}", e.name, pageName(e.page)));
				return;
			}
		}
		Console_Println(ST::format("unknown page: {}", args[2]));
	}

	// ----- imp gender / name / nickname --------------------------------

	void cmdGender(const std::vector<std::string>& args)
	{
		if (!pageGate(IMP_BEGIN, "begin")) return;
		if (args.size() < 3)
		{
			Console_Println("usage: imp gender m|f");
			return;
		}
		const std::string g = lower(args[2]);
		if (g == "m" || g == "male") { bGenderFlag = 1; }
		else if (g == "f" || g == "female") { bGenderFlag = 0; }
		else { Console_Println(ST::format("unknown gender: {} (use m or f)", args[2])); return; }
		fReDrawCharProfile = TRUE;
		Console_Println(ST::format("gender set to {}", bGenderFlag == 1 ? "male" : "female"));
	}

	void cmdName(const std::vector<std::string>& args, UINT8 fieldId, const char* label)
	{
		if (!pageGate(IMP_BEGIN, "begin")) return;
		if (args.size() < 3)
		{
			Console_Println(ST::format("current {}: \"{}\"", label, GetStringFromField(fieldId)));
			return;
		}
		const ST::string value = joinFrom(args, 2);
		SetInputFieldString(fieldId, value);
		Console_Println(ST::format("set {} to \"{}\"", label, GetStringFromField(fieldId)));
	}

	// ----- imp question / answer / confirm / prev / next ---------------

	ST::string quizPrompt(INT32 record)
	{
		if (!gImpText) return ST::string();
		return gImpText->at(record, 0);
	}

	void cmdQuestion()
	{
		if (!pageGate(IMP_PERSONALITY_QUIZ, "personality quiz")) return;

		const INT32 q       = giCurrentPersonalityQuizQuestion;
		const INT32 record  = IMP_Quiz_QuestionRecord(q);
		const INT32 nAnsw   = IMP_Quiz_AnswerCount(q);
		const INT32 saved   = iQuizAnswerList[q]; // -1 if unset on this question
		const INT32 selected = iCurrentAnswer >= 0 ? iCurrentAnswer : saved;

		Console_Println(ST::format("Question {}/16: {}", q + 1, quizPrompt(record)));
		for (INT32 i = 0; i < nAnsw; ++i)
		{
			const char* mark = (i == selected) ? "* " : "  ";
			Console_Println(ST::format("  {}{} — {}", mark, i + 1, quizPrompt(record + 1 + i)));
		}
		if (selected < 0)
		{
			Console_Println("(no answer selected; use 'imp answer <N>' then 'imp confirm')");
		}
	}

	void cmdAnswer(const std::vector<std::string>& args)
	{
		if (!pageGate(IMP_PERSONALITY_QUIZ, "personality quiz")) return;
		if (args.size() < 3) { Console_Println("usage: imp answer <N>"); return; }
		INT32 n;
		if (!parseInt(args[2], n)) { Console_Println(ST::format("not a number: {}", args[2])); return; }
		const INT32 nAnsw = IMP_Quiz_AnswerCount(giCurrentPersonalityQuizQuestion);
		if (n < 1 || n > nAnsw)
		{
			Console_Println(ST::format("answer {} out of range (1..{})", n, nAnsw));
			return;
		}
		iCurrentAnswer = n - 1;
		fReDrawCharProfile = TRUE;
		Console_Println(ST::format("selected answer {}", n));
	}

	void cmdConfirm()
	{
		if (!pageGate(IMP_PERSONALITY_QUIZ, "personality quiz")) return;
		if (iCurrentAnswer < 0)
		{
			Console_Println("no answer selected (use 'imp answer <N>' first).");
			return;
		}
		IMP_Quiz_ConfirmAnswer();
		Console_Println(ST::format("confirmed; now on question {}/16",
			giCurrentPersonalityQuizQuestion + 1));
	}

	void cmdPrev()
	{
		if (!pageGate(IMP_PERSONALITY_QUIZ, "personality quiz")) return;
		IMP_Quiz_PrevQuestion();
		Console_Println(ST::format("now on question {}/16", giCurrentPersonalityQuizQuestion + 1));
	}

	void cmdNext()
	{
		if (!pageGate(IMP_PERSONALITY_QUIZ, "personality quiz")) return;
		IMP_Quiz_NextQuestion();
		Console_Println(ST::format("now on question {}/16", giCurrentPersonalityQuizQuestion + 1));
	}

	// ----- imp traits / trait ------------------------------------------

	void cmdTraits()
	{
		if (!pageGate(IMP_SKILLTRAITS, "skill traits")) return;
		INT32 onCount = 0;
		for (UINT32 i = 0; i < IMP_SKILL_TRAITS_COUNT; ++i)
		{
			const bool on = gfSkillTraitQuestions[i] != FALSE;
			if (on && i < IMP_SKILL_TRAITS_COUNT - 1) ++onCount; // exclude NONE row
			Console_Println(ST::format("  [{}] {} — {}",
				on ? "x" : " ", i, gzIMPSkillTraitsText[i]));
		}
		Console_Println(ST::format("({} of 2 traits selected; index 14 is NONE)", onCount));
	}

	void cmdTrait(const std::vector<std::string>& args)
	{
		if (!pageGate(IMP_SKILLTRAITS, "skill traits")) return;
		if (args.size() < 3) { Console_Println("usage: imp trait <name>"); return; }
		const std::string name = joinFrom(args, 2).to_std_string();
		const INT32 idx = findByPrefix(name, IMP_SKILL_TRAITS_COUNT,
			[](std::size_t i) { return gzIMPSkillTraitsText[i].to_std_string(); });
		if (idx < 0)
		{
			Console_Println(ST::format("unknown or ambiguous trait: {} (try 'imp traits')", name));
			return;
		}
		HandleIMPSkillTraitAnswers(static_cast<UINT32>(idx), FALSE);
		Console_Println(ST::format("toggled {}: now {}",
			gzIMPSkillTraitsText[idx],
			gfSkillTraitQuestions[idx] ? "on" : "off"));
	}

	// ----- imp stats / stat --------------------------------------------

	struct StatEntry { const char* name; INT32 attr; };
	// Order matches the on-screen rows top-to-bottom on the attribute screen.
	const StatEntry kStatTable[] =
	{
		{ "health",       IMP_ATTR_HEALTH       },
		{ "dexterity",    IMP_ATTR_DEXTERITY    },
		{ "agility",      IMP_ATTR_AGILITY      },
		{ "strength",     IMP_ATTR_STRENGTH     },
		{ "wisdom",       IMP_ATTR_WISDOM       },
		{ "leadership",   IMP_ATTR_LEADERSHIP   },
		{ "marksmanship", IMP_ATTR_MARKSMANSHIP },
		{ "explosives",   IMP_ATTR_EXPLOSIVES   },
		{ "medical",      IMP_ATTR_MEDICAL      },
		{ "mechanical",   IMP_ATTR_MECHANICAL   },
	};

	void cmdStats(const std::vector<std::string>& args)
	{
		if (!pageGate(IMP_ATTRIBUTE_PAGE, "attribute selection")) return;
		// `imp stats` reads. `imp stat <name> <delta>` writes. The plural
		// vs singular tripped up the user: typing `imp stats health +30`
		// silently produced the read output and ignored the args. Catch
		// the mistake explicitly.
		if (args.size() >= 3)
		{
			Console_Println(ST::format(
				"'imp stats' is read-only; use 'imp stat {} {}' to change it.",
				args[2], args.size() >= 4 ? args[3] : std::string("+1")));
			return;
		}
		Console_Println(ST::format("bonus points remaining: {}", GetIMPCurrentBonusPoints()));
		for (const auto& s : kStatTable)
		{
			Console_Println(ST::format("  {} — {}", s.name, GetCurrentAttributeValue(s.attr)));
		}
	}

	void cmdStat(const std::vector<std::string>& args)
	{
		if (!pageGate(IMP_ATTRIBUTE_PAGE, "attribute selection")) return;
		if (args.size() < 3) { Console_Println("usage: imp stat <name> [+/-N] (delta defaults to +1)"); return; }
		const std::size_t count = sizeof(kStatTable) / sizeof(kStatTable[0]);
		const INT32 idx = findByPrefix(args[2], count,
			[](std::size_t i) { return std::string(kStatTable[i].name); });
		if (idx < 0)
		{
			Console_Println(ST::format("unknown or ambiguous stat: {} (try 'imp stats')", args[2]));
			return;
		}
		const INT32 attr = kStatTable[idx].attr;

		INT32 delta = 1;
		if (args.size() >= 4 && !parseDelta(args[3], delta))
		{
			Console_Println(ST::format("not a delta: {} (try +5, -3, or 1)", args[3]));
			return;
		}

		const INT32 before = GetCurrentAttributeValue(attr);
		if (delta >= 0) for (INT32 i = 0; i < delta;  ++i) IncrementStat(attr);
		else            for (INT32 i = 0; i < -delta; ++i) DecrementStat(attr);
		const INT32 after = GetCurrentAttributeValue(attr);

		fReDrawCharProfile = TRUE;
		Console_Println(ST::format(
			"{}: {} → {} (bonus left: {})",
			args[2], before, after, GetIMPCurrentBonusPoints()));
	}

	// ----- imp portrait / voice ----------------------------------------

	void cmdPortrait(const std::vector<std::string>& args)
	{
		if (!pageGate(IMP_PORTRAIT, "portrait")) return;
		// iLastPicture is 0-indexed (= 7) — there are 8 portraits.
		const INT32 total = iLastPicture + 1;
		if (args.size() < 2 + 1)
		{
			Console_Println(ST::format("portrait {} of {}",
				iCurrentPortrait + 1, total));
			return;
		}
		INT32 n;
		if (!parseInt(args[2], n) || n < 1 || n > total)
		{
			Console_Println(ST::format("portrait number out of range (1..{})", total));
			return;
		}
		iCurrentPortrait = n - 1;
		fReDrawPortraitScreenFlag = TRUE;
		fReDrawCharProfile = TRUE;
		Console_Println(ST::format("portrait set to {} of {}", n, total));
	}

	void cmdVoice(const std::vector<std::string>& args)
	{
		if (!pageGate(IMP_VOICE, "voice")) return;
		constexpr INT32 kVoiceCount = 3;
		if (args.size() < 2 + 1)
		{
			Console_Println(ST::format("voice {} of {}",
				iCurrentVoices + 1, kVoiceCount));
			IMP_Voices_PlayCurrentSample();
			return;
		}
		INT32 n;
		if (!parseInt(args[2], n) || n < 1 || n > kVoiceCount)
		{
			Console_Println(ST::format("voice number out of range (1..{})", kVoiceCount));
			return;
		}
		iCurrentVoices = n - 1;
		IMP_Voices_PlayCurrentSample();
		fReDrawCharProfile = TRUE;
		Console_Println(ST::format("voice set to {} of {}", n, kVoiceCount));
	}

	// ----- imp done -----------------------------------------------------

	// Picks the pImpButtonText[] entry that matches the "advance" button
	// shown on the current IMP sub-page. Returning nullptr means "no
	// single primary advance — use a more specific verb". The button's
	// label is the source of truth for click matching, since the engine
	// localises these and the strings compare directly to GUI_BUTTON's
	// codepoints field (set via SpecifyText / CreateIconAndTextButton).
	const ST::string* doneLabelForCurrentPage()
	{
		switch (iCurrentImpPage)
		{
			case IMP_BEGIN:               return &pImpButtonText[6];  // Done (begin screen)
			case IMP_PERSONALITY:         return &pImpButtonText[1];  // Begin / Begin Profiling
			case IMP_PERSONALITY_QUIZ:    return &pImpButtonText[8];  // Done (= confirm; prefer 'imp confirm')
			case IMP_PERSONALITY_FINISH:  return &pImpButtonText[24]; // OK
			case IMP_SKILLTRAITS:         return &pImpButtonText[11]; // Done
			case IMP_ATTRIBUTE_ENTRANCE:  return &pImpButtonText[1];  // Begin
			case IMP_ATTRIBUTE_PAGE:      return &pImpButtonText[11]; // Done
			case IMP_ATTRIBUTE_FINISH:    return &pImpButtonText[24]; // OK
			case IMP_PORTRAIT:            return &pImpButtonText[11]; // Done
			case IMP_VOICE:               return &pImpButtonText[11]; // Done
			case IMP_FINISH:              return &pImpButtonText[6];  // Done
			case IMP_CONFIRM:             return &pImpButtonText[16]; // Yes — but prefer 'imp hire'
			default:                       return nullptr;
		}
	}

	// Scan ButtonList for a visible enabled button whose codepoints match
	// `label`. If exactly one matches, click it; otherwise refuse with a
	// helpful message instead of guessing.
	void cmdDone()
	{
		const ST::string* label = doneLabelForCurrentPage();
		if (!label)
		{
			Console_Println(ST::format(
				"no obvious 'done' on page {}; use 'g list' / 'g click <id>'.",
				pageName(iCurrentImpPage)));
			return;
		}

		const ST::utf32_buffer want = label->to_utf32();
		GUI_BUTTON* hit  = nullptr;
		std::size_t hits = 0;
		for (INT32 i = 0; i < MAX_BUTTONS; ++i)
		{
			GUI_BUTTON* const b = ButtonList[i];
			if (!b) continue;
			if (b->uiFlags & BUTTON_DELETION_PENDING) continue;
			if (!(b->Area.uiFlags & MSYS_REGION_ENABLED)) continue;
			if (!b->Enabled()) continue;
			if (b->codepoints != want) continue;
			hit = b;
			++hits;
		}

		if (hits == 0)
		{
			Console_Println(ST::format("no enabled button labelled \"{}\" on this page.", *label));
			return;
		}
		if (hits > 1)
		{
			Console_Println(ST::format(
				"multiple buttons labelled \"{}\" — narrow with 'g list' / 'g click <id>'.",
				*label));
			return;
		}
		clickButton(hit);
		Console_Println(ST::format("clicked \"{}\".", *label));
	}

	// ----- imp hire -----------------------------------------------------

	void cmdHire()
	{
		if (!pageGate(IMP_CONFIRM, "confirm hire")) return;
		if (LaptopSaveInfo.fIMPCompletedFlag)
		{
			Console_Println("IMP profile already completed; cannot hire again.");
			return;
		}
		if (LaptopSaveInfo.iCurrentBalance < COST_OF_PROFILE)
		{
			Console_Println(ST::format(
				"insufficient funds (need {}, have {})",
				COST_OF_PROFILE, LaptopSaveInfo.iCurrentBalance));
			return;
		}
		GUI_BUTTON* yes = giIMPConfirmButton[0];
		if (!yes)
		{
			Console_Println("Yes button missing on confirm screen.");
			return;
		}
		clickButton(yes);
		Console_Println("clicked Yes — hiring underway.");
	}
}

void Cmd_Imp(const std::vector<std::string>& args)
{
	if (!laptopGate()) return;

	// `imp` bare — orientation readout.
	if (args.size() < 2) { cmdSummary(); return; }

	const std::string& sub = args[1];

	if (sub == "goto")     { cmdGoto(args);     return; }
	if (sub == "gender")   { cmdGender(args);   return; }
	if (sub == "name")     { cmdName(args, 0, "name");     return; }
	if (sub == "nickname") { cmdName(args, 1, "nickname"); return; }

	if (sub == "question") { cmdQuestion();     return; }
	if (sub == "answer")   { cmdAnswer(args);   return; }
	if (sub == "confirm")  { cmdConfirm();      return; }
	if (sub == "prev")     { cmdPrev();         return; }
	if (sub == "next")     { cmdNext();         return; }

	if (sub == "traits")   { cmdTraits();       return; }
	if (sub == "trait")    { cmdTrait(args);    return; }

	if (sub == "stats")    { cmdStats(args);     return; }
	if (sub == "stat")     { cmdStat(args);     return; }

	if (sub == "portrait") { cmdPortrait(args); return; }
	if (sub == "voice")    { cmdVoice(args);    return; }

	if (sub == "hire")     { cmdHire();         return; }
	if (sub == "done")     { cmdDone();         return; }

	// Fallthrough: accept `imp <pagename>` as a shortcut for
	// `imp goto <pagename>`. Saves the user from `imp begin` /
	// `imp main` failing — common-sense aliasing.
	const std::string want = lower(sub);
	for (const auto& e : kGotoTable)
	{
		if (want == e.name)
		{
			iCurrentImpPage = e.page;
			fButtonPendingFlag = TRUE;
			Console_Println(ST::format("goto {}: {}", e.name, pageName(e.page)));
			return;
		}
	}

	Console_Println(ST::format(
		"unknown subcommand: imp {} (try 'imp', 'imp goto …', "
		"'imp name/nickname/gender', 'imp question/answer/confirm/prev/next', "
		"'imp traits/trait', 'imp stats/stat', 'imp portrait/voice', "
		"'imp done', 'imp hire')",
		sub));
}
