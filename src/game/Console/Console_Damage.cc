#include "Console_Damage.h"

#include "Console.h"
#include "Console_Address.h"
#include "DamageLog.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "Game_Clock.h"
#include "ItemModel.h"
#include "Items.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Soldier_Control.h"
#include "Structure.h"
#include "Text.h"
#include "WorldDef.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_theory/format>
#include <string_theory/string>

namespace
{
	// Maps the damage `reason` to a present-tense action verb. The action
	// is phrased so it reads naturally with or without an attacker:
	//   "Hans shot Ivan ..."          (attacker present)
	//   "Hans bleeding ..."           (no attacker)
	const char* actionWord(UINT8 reason, bool hasAttacker)
	{
		switch (reason)
		{
			case TAKE_DAMAGE_GUNFIRE:             return "shot";
			case TAKE_DAMAGE_BLADE:               return "stabbed";
			case TAKE_DAMAGE_HANDTOHAND:          return "punched";
			case TAKE_DAMAGE_EXPLOSION:           return hasAttacker ? "blasted" : "caught the blast";
			case TAKE_DAMAGE_STRUCTURE_EXPLOSION: return "caught the structure blast";
			case TAKE_DAMAGE_BLOODLOSS:           return "bleeding";
			case TAKE_DAMAGE_GAS:                 return "gassed";
			case TAKE_DAMAGE_TENTACLES:           return "grabbed by tentacles";
			case TAKE_DAMAGE_OBJECT:              return hasAttacker ? "hit with a thrown object" : "struck by an object";
			case TAKE_DAMAGE_FALLROOF:            return "fell";
			case TAKE_DAMAGE_ELECTRICITY:         return "shocked";
			default:                              return "hit";
		}
	}

	// AIM_SHOT_HEAD=1 / TORSO=2 / LEGS=3 map to the engine's localized
	// hit-location strings (the same TacticalStr entries the targeting
	// cursor uses). 0 (AIM_SHOT_RANDOM) leaves no qualifier -- splash
	// damage and indirect causes don't have a meaningful body part.
	ST::string bodyPartWord(UINT8 hitLocation)
	{
		switch (hitLocation)
		{
			case AIM_SHOT_HEAD:  return TacticalStr[HEAD_HIT_LOCATION_STR];
			case AIM_SHOT_TORSO: return TacticalStr[TORSO_HIT_LOCATION_STR];
			case AIM_SHOT_LEGS:  return TacticalStr[LEGS_HIT_LOCATION_STR];
			default:             return ST::string{};
		}
	}

	ST::string weaponWord(UINT16 weaponIndex)
	{
		if (weaponIndex == NOTHING) return ST::string{};
		const ItemModel* item = GCM->getItem(weaponIndex);
		return item != nullptr ? item->getShortName() : ST::string{};
	}

	ST::string formatElapsed(UINT32 nowMs, UINT32 thenMs)
	{
		const UINT32 dms = nowMs >= thenMs ? nowMs - thenMs : 0;
		const UINT32 secs = dms / 1000;
		if (secs < 60)   return ST::format("{}s ago",  secs);
		if (secs < 3600) return ST::format("{}m{}s ago", secs / 60, secs % 60);
		return ST::format("{}h{}m ago", secs / 3600, (secs % 3600) / 60);
	}

	// Visibility-gating policy for default listing:
	//   - target on OUR_TEAM           -> always show (player's merc was hit)
	//   - target visible               -> show (we saw the merc / tile)
	//   - attacker visible to OUR_TEAM -> show (we saw who fired)
	// otherwise the event happened entirely off-screen for the player and
	// is suppressed unless `damage all` is used. The tile-visibility check
	// inside the wrapper covers non-soldier kinds for free (target_visible
	// is filled from ConsoleVis::IsKnownTile at push time).
	bool shouldList(const DamageLog::Record& r, bool listAll)
	{
		if (listAll) return true;
		if (r.target_team == OUR_TEAM) return true;
		if (r.target_visible)          return true;
		if (r.attacker_visible)        return true;
		return false;
	}

	ST::string actorPhrase(const ST::string& name, bool visible, INT8 team)
	{
		if (visible && !name.empty()) return name;
		if (team == OUR_TEAM)         return name.empty() ? ST::string{"a teammate"} : name;
		if (team < 0)                 return ST::string{};
		return ST::string{"an unseen attacker"};
	}

	ST::string targetPhrase(const ST::string& name, bool visible, INT8 team)
	{
		if (visible && !name.empty()) return name;
		if (team == OUR_TEAM)         return name.empty() ? ST::string{"a teammate"} : name;
		return ST::string{"an unseen target"};
	}

	// Cartesian offset from the selected merc to the event tile. Returns
	// "(unknown)" when there is no selected merc (out of combat, in a
	// menu) so the line still parses for the user. The address grammar
	// in Console_Address.h:formatOffset gives forms like "8 E 3 N" that
	// round-trip into `tile 8 e 3 n` etc.
	ST::string offsetFromSelected(INT16 destGridno)
	{
		if (destGridno < 0 || destGridno >= WORLD_MAX) return ST::string{"(no tile)"};
		const SOLDIERTYPE* const sel = GetSelectedMan();
		if (sel == nullptr) return ST::string{"(no observer)"};
		return formatOffset(sel->sGridNo, destGridno);
	}

	// Material -> adjective. Most structure nouns carry their own
	// classification ("Wire fence" doesn't need "metal"); we add an
	// adjective only when the material discriminates within the noun
	// class ("Wooden wall" vs "Stone wall"). Empty string skips the
	// adjective.
	const char* materialAdjective(UINT8 material)
	{
		switch (material)
		{
			case MATERIAL_WOOD_WALL:    return "Wooden";
			case MATERIAL_PLYWOOD_WALL: return "Plywood";
			case MATERIAL_FURNITURE:    return "Wooden";
			case MATERIAL_PORCELAIN:    return "Porcelain";
			case MATERIAL_STONE:        return "Stone";
			case MATERIAL_CONCRETE1:    return "Concrete";
			case MATERIAL_CONCRETE2:    return "Concrete";
			case MATERIAL_ROCK:         return "Rock";
			case MATERIAL_LIGHT_METAL:    return "Metal";
			case MATERIAL_THICKER_METAL:  return "Metal";
			case MATERIAL_HEAVY_METAL:    return "Heavy metal";
			default:                    return "";
		}
	}

	// Noun word -- the headline category.
	const char* nounWord(DamageLog::NounClass n)
	{
		switch (n)
		{
			case DamageLog::NOUN_WALL:      return "wall";
			case DamageLog::NOUN_FENCE:     return "fence";
			case DamageLog::NOUN_DOOR:      return "door";
			case DamageLog::NOUN_TREE:      return "tree";
			case DamageLog::NOUN_VEHICLE:   return "vehicle";
			case DamageLog::NOUN_CONTAINER: return "container";
			case DamageLog::NOUN_FURNITURE: return "furniture";
			case DamageLog::NOUN_SWITCH:    return "switch";
			case DamageLog::NOUN_LIGHT:     return "light";
			case DamageLog::NOUN_EXPLOSIVE: return "explosive prop";
			default:                        return "structure";
		}
	}

	// Disposition verb. "Tree felled" / "Wall destroyed" / "Fence cut"
	// would be nicer per-noun, but a single verb keeps the formatter
	// readable. The noun makes the action obvious in context.
	const char* dispVerb(DamageLog::Disposition d)
	{
		switch (d)
		{
			case DamageLog::DISP_DAMAGED:       return "damaged";
			case DamageLog::DISP_DESTROYED:     return "destroyed";
			case DamageLog::DISP_IGNITED:       return "ignited";
			case DamageLog::DISP_CRACKED:       return "cracked";
			case DamageLog::DISP_SHATTERED:     return "shattered";
			case DamageLog::DISP_LOCK_SMASHED:  return "lock smashed";
			case DamageLog::DISP_LOCK_BLOWN:    return "lock blown";
			case DamageLog::DISP_LOCK_SHOT:     return "lock destroyed";
			default:                            return "changed";
		}
	}

	// Cause clause -- the trailing "Explosion by Mike (hand grenade)"
	// or "Hans (kicked)" half. Three shapes:
	//   - attributed gunfire/explosion : "<cause> by <actor> (<weapon>)"
	//   - unattributed gunfire/explosion : "<cause> (<weapon>)"
	//   - melee/charge with actor only : "<actor> (<verb>)"
	ST::string causeClause(const DamageLog::Record& r)
	{
		const ST::string actor = actorPhrase(r.attacker_name, r.attacker_visible, r.attacker_team);
		const ST::string weapon = weaponWord(r.weapon_index);

		switch (r.cause)
		{
			case DamageLog::CAUSE_GUNFIRE:
				if (!actor.empty() && !weapon.empty())
					return ST::format("Gunfire by {} ({})", actor, weapon);
				if (!actor.empty()) return ST::format("Gunfire by {}", actor);
				if (!weapon.empty()) return ST::format("Gunfire ({})", weapon);
				return ST::string{"Gunfire"};
			case DamageLog::CAUSE_EXPLOSION:
				if (!actor.empty() && !weapon.empty())
					return ST::format("Explosion by {} ({})", actor, weapon);
				if (!actor.empty()) return ST::format("Explosion by {}", actor);
				if (!weapon.empty()) return ST::format("Explosion ({})", weapon);
				return ST::string{"Explosion"};
			case DamageLog::CAUSE_THROWN:
				if (!actor.empty() && !weapon.empty())
					return ST::format("Thrown {} by {}", weapon, actor);
				if (!actor.empty()) return ST::format("Thrown by {}", actor);
				return ST::string{"Thrown object"};
			case DamageLog::CAUSE_KICK:
				return actor.empty() ? ST::string{"Kicked"} : ST::format("{} (kicked)", actor);
			case DamageLog::CAUSE_CROWBAR:
				return actor.empty() ? ST::string{"Pried"} : ST::format("{} (pried)", actor);
			case DamageLog::CAUSE_SHAPED_CHARGE:
				return actor.empty()
					? ST::string{"Shaped charge"}
					: ST::format("{} (shaped charge)", actor);
			default:
				return ST::string{};
		}
	}

	void printSoldierOneLiner(UINT32 slotN, UINT32 nowMs, const DamageLog::Record& r)
	{
		const ST::string when     = formatElapsed(nowMs, r.timestamp_ms);
		const bool       hasAtt   = (r.attacker_team >= 0);
		const ST::string attStr   = actorPhrase(r.attacker_name, r.attacker_visible, r.attacker_team);
		const ST::string tgtStr   = targetPhrase(r.target_name, r.target_visible, r.target_team);
		const ST::string action   = actionWord(r.reason, hasAtt);
		const ST::string part     = bodyPartWord(r.hit_location);
		const ST::string weapon   = weaponWord(r.weapon_index);

		// Lead clause: "<actor> <action> <target>" or "<target> <action>"
		// for non-attack reasons (bleed / fall / gas with no attacker).
		ST::string lead;
		if (hasAtt && !attStr.empty())
		{
			lead = ST::format("{} {} {}", attStr, action, tgtStr);
		}
		else
		{
			lead = ST::format("{} {}", tgtStr, action);
		}

		// Qualifier: "(in the head, with M14)" / "(with M14)" / "(in the head)"
		ST::string qualifier;
		if (!part.empty() && !weapon.empty())
		{
			qualifier = ST::format(" (in the {}, with {})", part, weapon);
		}
		else if (!part.empty())
		{
			qualifier = ST::format(" (in the {})", part);
		}
		else if (!weapon.empty())
		{
			qualifier = ST::format(" (with {})", weapon);
		}

		// Offset from selected merc to where the hit happened.
		const ST::string off = offsetFromSelected(r.target_gridno);

		// Damage and life/breath state.
		ST::string dmg;
		if (r.damage_breath != 0)
		{
			dmg = ST::format("{} dmg, {} breath", static_cast<int>(r.damage_life),
				static_cast<int>(r.damage_breath));
		}
		else
		{
			dmg = ST::format("{} dmg", static_cast<int>(r.damage_life));
		}

		ST::string outcome;
		if (r.killed)            outcome = ", killed";
		else if (r.knocked_out)  outcome = ", knocked out";

		Console_Println(ST::format("{}. {}: {}{}, {}. {}. Life {}/{}{}.",
			slotN, when, lead, qualifier, off, dmg,
			static_cast<int>(r.life_after), static_cast<int>(r.life_max),
			outcome));
	}

	// Headline noun phrase for structure events:
	//   "Wooden wall destroyed" / "Tree destroyed" / "Gas tank ignited"
	ST::string structureHeadline(const DamageLog::Record& r)
	{
		const char* const adj  = materialAdjective(r.material);
		const char* const noun = nounWord(r.noun_class);
		const char* const verb = dispVerb(r.disp);
		// Capitalize the first letter of the headline by relying on the
		// adjective when present, the noun otherwise. nounWord returns
		// lowercase nouns so the result is consistent with the rest of
		// the verb output.
		if (adj[0] != '\0')
		{
			return ST::format("{} {} {}", adj, noun, verb);
		}
		ST::string capNoun(noun);
		if (!capNoun.empty())
		{
			char buf[2] = { static_cast<char>(std::toupper(static_cast<unsigned char>(capNoun.c_str()[0]))), '\0' };
			capNoun = ST::string(buf) + capNoun.substr(1);
		}
		return ST::format("{} {}", capNoun, verb);
	}

	void printStructureOneLiner(UINT32 slotN, UINT32 nowMs, const DamageLog::Record& r)
	{
		const ST::string when     = formatElapsed(nowMs, r.timestamp_ms);
		const ST::string headline = structureHeadline(r);
		const ST::string off      = offsetFromSelected(r.target_gridno);
		const ST::string cause    = causeClause(r);
		if (cause.empty())
		{
			Console_Println(ST::format("{}. {}: {}, {}.", slotN, when, headline, off));
		}
		else
		{
			Console_Println(ST::format("{}. {}: {}, {}. {}.", slotN, when, headline, off, cause));
		}
	}

	void printWindowOneLiner(UINT32 slotN, UINT32 nowMs, const DamageLog::Record& r)
	{
		const ST::string when  = formatElapsed(nowMs, r.timestamp_ms);
		const char* const verb = (r.disp == DamageLog::DISP_SHATTERED) ? "shattered" : "cracked";
		const ST::string off   = offsetFromSelected(r.target_gridno);
		const ST::string cause = causeClause(r);
		if (cause.empty())
		{
			Console_Println(ST::format("{}. {}: Window {}, {}.", slotN, when, verb, off));
		}
		else
		{
			Console_Println(ST::format("{}. {}: Window {}, {}. {}.", slotN, when, verb, off, cause));
		}
	}

	void printLockOneLiner(UINT32 slotN, UINT32 nowMs, const DamageLog::Record& r)
	{
		const ST::string when  = formatElapsed(nowMs, r.timestamp_ms);
		const char* const verb = dispVerb(r.disp);
		const ST::string off   = offsetFromSelected(r.target_gridno);
		const ST::string cause = causeClause(r);
		if (cause.empty())
		{
			Console_Println(ST::format("{}. {}: Door {}, {}.", slotN, when, verb, off));
		}
		else
		{
			Console_Println(ST::format("{}. {}: Door {}, {}. {}.", slotN, when, verb, off, cause));
		}
	}

	void printOneLiner(UINT32 slotN, UINT32 nowMs, const DamageLog::Record& r)
	{
		switch (r.kind)
		{
			case DamageLog::KIND_SOLDIER_HIT: printSoldierOneLiner(slotN, nowMs, r); return;
			case DamageLog::KIND_STRUCTURE:   printStructureOneLiner(slotN, nowMs, r); return;
			case DamageLog::KIND_WINDOW:      printWindowOneLiner(slotN, nowMs, r); return;
			case DamageLog::KIND_LOCK:        printLockOneLiner(slotN, nowMs, r); return;
		}
	}

	void printSoldierDetail(UINT32 slotN, UINT32 nowMs, const DamageLog::Record& r)
	{
		const ST::string when    = formatElapsed(nowMs, r.timestamp_ms);
		const ST::string attStr  = actorPhrase(r.attacker_name, r.attacker_visible, r.attacker_team);
		const ST::string tgtStr  = targetPhrase(r.target_name, r.target_visible, r.target_team);
		const ST::string action  = actionWord(r.reason, r.attacker_team >= 0);
		const ST::string part    = bodyPartWord(r.hit_location);
		const ST::string weapon  = weaponWord(r.weapon_index);

		Console_Println(ST::format("damage #{} ({}):", slotN, when));
		if (r.attacker_team >= 0)
		{
			Console_Println(ST::format("  attacker: {}", attStr));
		}
		Console_Println(ST::format("  target: {}", tgtStr));
		Console_Println(ST::format("  at: {}", offsetFromSelected(r.target_gridno)));
		Console_Println(ST::format("  action: {}", action));
		if (!weapon.empty()) Console_Println(ST::format("  weapon: {}", weapon));
		if (!part.empty())   Console_Println(ST::format("  body part: {}", part));
		Console_Println(ST::format("  life: {} -> {} (max {})",
			static_cast<int>(r.life_before),
			static_cast<int>(r.life_after),
			static_cast<int>(r.life_max)));
		Console_Println(ST::format("  breath: {} -> {} (max {})",
			static_cast<int>(r.breath_before),
			static_cast<int>(r.breath_after),
			static_cast<int>(r.breath_max)));
		if (r.killed)       Console_Println("  killed.");
		if (r.knocked_out)  Console_Println("  knocked out.");
	}

	// Post-event tile-state line. The buffer freezes attribution at push
	// time, but movement-cost / line-of-sight changes are observable on
	// gpWorldLevelData *now* -- so for non-soldier records the detail
	// block reads current state to answer "is the lane clear?".
	ST::string tileStateNow(const DamageLog::Record& r)
	{
		if (r.target_gridno < 0 || r.target_gridno >= WORLD_MAX) return ST::string{};
		const UINT32 flags = gpWorldLevelData[r.target_gridno].uiFlags;
		const bool damaged = (flags & MAPELEMENT_STRUCTURE_DAMAGED) != 0;
		switch (r.kind)
		{
			case DamageLog::KIND_STRUCTURE:
				if (r.disp == DamageLog::DISP_DESTROYED)
					return ST::string{"Now clear: movement and line of sight pass through this tile."};
				if (r.disp == DamageLog::DISP_DAMAGED)
					return damaged
						? ST::string{"Still standing; visibly damaged."}
						: ST::string{"Repaired or replaced since."};
				if (r.disp == DamageLog::DISP_IGNITED)
					return ST::string{"Detonated. Secondary explosion events follow."};
				return ST::string{};
			case DamageLog::KIND_WINDOW:
				return r.disp == DamageLog::DISP_SHATTERED
					? ST::string{"Line of sight now clear; movement still blocked by the wall."}
					: ST::string{"Line of sight still blocked; sound carries."};
			case DamageLog::KIND_LOCK:
				return ST::string{"Door now opens and closes normally; lock is gone."};
			default:
				return ST::string{};
		}
	}

	void printGenericDetail(UINT32 slotN, UINT32 nowMs, const DamageLog::Record& r)
	{
		const ST::string when    = formatElapsed(nowMs, r.timestamp_ms);
		const ST::string cause   = causeClause(r);
		const ST::string state   = tileStateNow(r);

		Console_Println(ST::format("damage #{} ({}):", slotN, when));
		switch (r.kind)
		{
			case DamageLog::KIND_STRUCTURE:
				Console_Println(ST::format("  event: {}", structureHeadline(r)));
				break;
			case DamageLog::KIND_WINDOW:
				Console_Println(ST::format("  event: Window {}",
					r.disp == DamageLog::DISP_SHATTERED ? "shattered" : "cracked"));
				break;
			case DamageLog::KIND_LOCK:
				Console_Println(ST::format("  event: Door {}", dispVerb(r.disp)));
				break;
			default:
				break;
		}
		Console_Println(ST::format("  at: {}", offsetFromSelected(r.target_gridno)));
		if (r.target_level > 0)
		{
			Console_Println("  level: roof");
		}
		if (!cause.empty()) Console_Println(ST::format("  cause: {}", cause));
		if (!state.empty()) Console_Println(ST::format("  now: {}", state));
	}

	void printDetail(UINT32 slotN, UINT32 nowMs, const DamageLog::Record& r)
	{
		switch (r.kind)
		{
			case DamageLog::KIND_SOLDIER_HIT: printSoldierDetail(slotN, nowMs, r); return;
			case DamageLog::KIND_STRUCTURE:
			case DamageLog::KIND_WINDOW:
			case DamageLog::KIND_LOCK:        printGenericDetail(slotN, nowMs, r); return;
		}
	}

	bool parseSlot(const std::string& s, UINT32& out)
	{
		if (s.empty()) return false;
		for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
		try
		{
			out = static_cast<UINT32>(std::stoul(s));
			return true;
		}
		catch (...) { return false; }
	}
}

void Cmd_Damage(const std::vector<std::string>& args)
{
	const UINT32 size = DamageLog::Size();
	if (size == 0)
	{
		Console_Println("No damage recorded yet.");
		return;
	}

	const UINT32 nowMs = GetJA2Clock();
	bool listAll = false;

	if (args.size() >= 2)
	{
		std::string sub = args[1];
		std::transform(sub.begin(), sub.end(), sub.begin(),
			[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
		if (sub == "all")
		{
			listAll = true;
		}
		else
		{
			UINT32 slot = 0;
			if (parseSlot(sub, slot) && slot >= 1 && slot <= size)
			{
				const DamageLog::Record* r = DamageLog::Get(slot - 1);
				if (r != nullptr)
				{
					printDetail(slot, nowMs, *r);
					return;
				}
			}
			Console_Println(ST::format(
				"damage: unknown argument '{}' (try 'damage', 'damage all', 'damage <N>' where N is 1..{})",
				args[1], static_cast<int>(size)));
			return;
		}
	}

	UINT32 listed = 0;
	for (UINT32 i = 0; i < size; ++i)
	{
		const DamageLog::Record* r = DamageLog::Get(i);
		if (r == nullptr) continue;
		if (!shouldList(*r, listAll)) continue;
		printOneLiner(i + 1, nowMs, *r);
		++listed;
	}

	if (listed == 0)
	{
		Console_Println(listAll
			? "No damage records to show."
			: "No visible damage events. Try 'damage all' to include off-screen events.");
	}
}
