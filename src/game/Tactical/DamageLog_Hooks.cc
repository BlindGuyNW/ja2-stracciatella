#include "DamageLog_Hooks.h"

#include "Console_Visibility.h"
#include "Game_Clock.h"
#include "Isometric_Utils.h"
#include "OppList.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Soldier_Control.h"
#include "Structure_Internals.h"

namespace DamageLog
{

namespace
{
	// Same-structure dedup window. A 3x3 vehicle in a grenade radius
	// generates one DamageStructure call per sub-tile in range; without
	// this, we'd record up to nine entries for one destruction. Also
	// collapses burst-fire peppering a single wall. 500 ms is loose
	// enough to catch a queued explosion sweep but tight enough that a
	// re-destruction across turns (rare) still registers.
	constexpr UINT32 kDedupWindowMs = 500;

	bool VisibilityKnown(const SOLDIERTYPE* s)
	{
		if (s == nullptr)         return false;
		if (s->bTeam == OUR_TEAM) return true;
		if (!s->bActive)          return false;
		if (!s->bInSector)        return false;
		return gbPublicOpplist[OUR_TEAM][s->ubID] != NOT_HEARD_OR_SEEN;
	}

	NounClass ClassifyNoun(STRUCTURE const* s)
	{
		if (s == nullptr) return NOUN_OTHER;
		UINT32 const flags = s->fFlags;

		// STRUCTURE_EXPLOSIVE is checked first because gas tanks/red
		// barrels can also carry STRUCTURE_WALLSTUFF or _OPENABLE bits
		// in the engine data, and the explosive nature is the headline.
		if (flags & STRUCTURE_EXPLOSIVE)  return NOUN_EXPLOSIVE;
		if (flags & STRUCTURE_ANYDOOR)    return NOUN_DOOR;
		if (flags & STRUCTURE_VEHICLE)    return NOUN_VEHICLE;
		if (flags & STRUCTURE_TREE)       return NOUN_TREE;
		if (flags & (STRUCTURE_FENCE | STRUCTURE_WIREFENCE)) return NOUN_FENCE;
		if (flags & STRUCTURE_WALLSTUFF)  return NOUN_WALL;
		if (flags & STRUCTURE_SWITCH)     return NOUN_SWITCH;
		if (flags & STRUCTURE_LIGHTSOURCE) return NOUN_LIGHT;
		if (flags & STRUCTURE_OPENABLE)   return NOUN_CONTAINER;
		// FURNITURE has no flag; classify from material as a fallback.
		if (s->pDBStructureRef != nullptr &&
			s->pDBStructureRef->pDBStructure != nullptr &&
			s->pDBStructureRef->pDBStructure->ubArmour == MATERIAL_FURNITURE)
			return NOUN_FURNITURE;
		return NOUN_OTHER;
	}

	UINT8 MaterialOf(STRUCTURE const* s)
	{
		if (s == nullptr) return MATERIAL_NOTHING;
		if (s->pDBStructureRef == nullptr) return MATERIAL_NOTHING;
		if (s->pDBStructureRef->pDBStructure == nullptr) return MATERIAL_NOTHING;
		return s->pDBStructureRef->pDBStructure->ubArmour;
	}

	bool IsRecentDuplicateStructure(UINT16 structureId, UINT32 nowMs)
	{
		const Record* const last = PeekNewest();
		if (last == nullptr) return false;
		if (last->kind != KIND_STRUCTURE) return false;
		if (last->structure_id != structureId) return false;
		if (nowMs < last->timestamp_ms) return false;
		return (nowMs - last->timestamp_ms) < kDedupWindowMs;
	}

	bool IsRecentDuplicateWindow(GridNo gridno, UINT32 nowMs)
	{
		// Windows have a usStructureID but it changes across the
		// intact->cracked->shattered partner-swap chain, so dedup by
		// gridno+level instead. The same explosion sweep can hit the
		// same window from multiple directions (see Explosion_Control
		// 1164/1182/1198) and the second swap is a no-op for state but
		// still ends up calling our push.
		const Record* const last = PeekNewest();
		if (last == nullptr) return false;
		if (last->kind != KIND_WINDOW) return false;
		if (last->target_gridno != gridno) return false;
		if (nowMs < last->timestamp_ms) return false;
		return (nowMs - last->timestamp_ms) < kDedupWindowMs;
	}
}

void PushSoldierHit(
	const SOLDIERTYPE* tgt, INT8 lifeBefore, INT8 breathBefore,
	const SOLDIERTYPE* att, UINT16 weapon, UINT8 reason,
	UINT8 hitLoc, UINT8 special)
{
	if (tgt == nullptr) return;

	Record rec{};
	rec.timestamp_ms     = GetJA2Clock();
	rec.kind             = KIND_SOLDIER_HIT;
	rec.target_name      = tgt->name;
	rec.target_team      = tgt->bTeam;
	rec.target_visible   = VisibilityKnown(tgt);
	rec.target_gridno    = tgt->sGridNo;
	rec.target_level     = tgt->bLevel;
	rec.life_before      = lifeBefore;
	rec.life_after       = tgt->bLife;
	rec.life_max         = tgt->bLifeMax;
	rec.breath_before    = breathBefore;
	rec.breath_after     = tgt->bBreath;
	rec.breath_max       = tgt->bBreathMax;
	rec.damage_life      = static_cast<INT16>(lifeBefore   - tgt->bLife);
	rec.damage_breath    = static_cast<INT16>(breathBefore - tgt->bBreath);
	rec.weapon_index     = weapon;
	rec.reason           = reason;
	rec.hit_location     = hitLoc;
	rec.special          = special;
	rec.killed           = (tgt->bLife == 0 && lifeBefore > 0);
	rec.knocked_out      = (tgt->bLife > 0 && tgt->bLife < CONSCIOUSNESS && lifeBefore >= CONSCIOUSNESS);
	if (att != nullptr)
	{
		rec.attacker_name    = att->name;
		rec.attacker_team    = att->bTeam;
		rec.attacker_visible = VisibilityKnown(att);
		rec.attacker_gridno  = att->sGridNo;
	}
	else
	{
		rec.attacker_team    = -1;
		rec.attacker_visible = false;
		rec.attacker_gridno  = -1;
	}
	Push(rec);
}

void PushStructure(
	STRUCTURE* s, GridNo gridno, INT8 level,
	StructureDamageReason reason, StructureDamageResult result,
	const SOLDIERTYPE* owner, UINT16 weapon_or_item)
{
	if (s == nullptr) return;
	if (result == STRUCTURE_NOT_DAMAGED) return;

	// Multi-tile structures (vehicles, double doors) get DamageStructure
	// called per sub-tile in the explosion radius; only log on the base
	// tile to record one event per structure.
	STRUCTURE* const base = FindBaseStructure(s);
	if (base == nullptr) return;
	if (base != s) return;

	const UINT16 structure_id = base->usStructureID;
	const UINT32 nowMs        = GetJA2Clock();
	if (IsRecentDuplicateStructure(structure_id, nowMs)) return;

	Record rec{};
	rec.timestamp_ms     = nowMs;
	rec.kind             = KIND_STRUCTURE;
	rec.target_team      = -1;
	rec.target_gridno    = gridno;
	rec.target_level     = level;
	rec.target_visible   = ConsoleVis::IsKnownTile(gridno, level);
	rec.weapon_index     = weapon_or_item;
	rec.disp             = (result == STRUCTURE_DESTROYED) ? DISP_DESTROYED : DISP_DAMAGED;
	rec.cause            = (reason == STRUCTURE_DAMAGE_EXPLOSION) ? CAUSE_EXPLOSION : CAUSE_GUNFIRE;
	rec.structure_id     = structure_id;
	rec.noun_class       = ClassifyNoun(base);
	rec.material         = MaterialOf(base);
	if (owner != nullptr)
	{
		rec.attacker_name    = owner->name;
		rec.attacker_team    = owner->bTeam;
		rec.attacker_visible = VisibilityKnown(owner);
		rec.attacker_gridno  = owner->sGridNo;
	}
	else
	{
		rec.attacker_team    = -1;
		rec.attacker_visible = false;
		rec.attacker_gridno  = -1;
	}
	Push(rec);
}

void PushStructureIgnited(
	STRUCTURE* s, GridNo gridno, INT8 level,
	const SOLDIERTYPE* owner, UINT16 weapon)
{
	if (s == nullptr) return;

	STRUCTURE* const base = FindBaseStructure(s);
	if (base == nullptr) return;

	const UINT16 structure_id = base->usStructureID;
	const UINT32 nowMs        = GetJA2Clock();
	if (IsRecentDuplicateStructure(structure_id, nowMs)) return;

	Record rec{};
	rec.timestamp_ms     = nowMs;
	rec.kind             = KIND_STRUCTURE;
	rec.target_team      = -1;
	rec.target_gridno    = gridno;
	rec.target_level     = level;
	rec.target_visible   = ConsoleVis::IsKnownTile(gridno, level);
	rec.weapon_index     = weapon;
	rec.disp             = DISP_IGNITED;
	rec.cause            = CAUSE_GUNFIRE;
	rec.structure_id     = structure_id;
	rec.noun_class       = NOUN_EXPLOSIVE;
	rec.material         = MaterialOf(base);
	if (owner != nullptr)
	{
		rec.attacker_name    = owner->name;
		rec.attacker_team    = owner->bTeam;
		rec.attacker_visible = VisibilityKnown(owner);
		rec.attacker_gridno  = owner->sGridNo;
	}
	else
	{
		rec.attacker_team    = -1;
		rec.attacker_visible = false;
		rec.attacker_gridno  = -1;
	}
	Push(rec);
}

void PushWindow(
	GridNo gridno, INT8 level, bool shattered,
	Cause cause, const SOLDIERTYPE* actor, UINT16 weapon_or_item)
{
	const UINT32 nowMs = GetJA2Clock();
	if (IsRecentDuplicateWindow(gridno, nowMs)) return;

	Record rec{};
	rec.timestamp_ms     = nowMs;
	rec.kind             = KIND_WINDOW;
	rec.target_team      = -1;
	rec.target_gridno    = gridno;
	rec.target_level     = level;
	rec.target_visible   = ConsoleVis::IsKnownTile(gridno, level);
	rec.weapon_index     = weapon_or_item;
	rec.disp             = shattered ? DISP_SHATTERED : DISP_CRACKED;
	rec.cause            = cause;
	if (actor != nullptr)
	{
		rec.attacker_name    = actor->name;
		rec.attacker_team    = actor->bTeam;
		rec.attacker_visible = VisibilityKnown(actor);
		rec.attacker_gridno  = actor->sGridNo;
	}
	else
	{
		rec.attacker_team    = -1;
		rec.attacker_visible = false;
		rec.attacker_gridno  = -1;
	}
	Push(rec);
}

void PushLockEvent(
	GridNo gridno, INT8 level, Disposition disp,
	Cause cause, const SOLDIERTYPE* actor)
{
	Record rec{};
	rec.timestamp_ms     = GetJA2Clock();
	rec.kind             = KIND_LOCK;
	rec.target_team      = -1;
	rec.target_gridno    = gridno;
	rec.target_level     = level;
	rec.target_visible   = ConsoleVis::IsKnownTile(gridno, level);
	rec.weapon_index     = NOTHING;
	rec.disp             = disp;
	rec.cause            = cause;
	rec.noun_class       = NOUN_DOOR;
	if (actor != nullptr)
	{
		rec.attacker_name    = actor->name;
		rec.attacker_team    = actor->bTeam;
		rec.attacker_visible = VisibilityKnown(actor);
		rec.attacker_gridno  = actor->sGridNo;
	}
	else
	{
		rec.attacker_team    = -1;
		rec.attacker_visible = false;
		rec.attacker_gridno  = -1;
	}
	Push(rec);
}

} // namespace DamageLog
