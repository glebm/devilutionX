/**
 * @file dead.cpp
 *
 * Implementation of functions for placing dead monsters.
 */
#include "dead.h"

#include "diablo.h"
#include "levels/gendung.h"
#include "lighting.h"
#include "misdat.h"
#include "monster.h"

namespace devilution {

Corpse Corpses[MaxCorpses];
int8_t stonendx;

namespace {
void InitDeadAnimationFromMonster(Corpse &corpse, const CMonster &mon)
{
	const AnimStruct &animData = mon.getAnimData(MonsterGraphic::Death);
	if (animData.sprites) {
		corpse.sprites.emplace(*animData.sprites);
	} else {
		corpse.sprites = std::nullopt;
	}
	corpse.trn = mon.getTrn(MonsterGraphic::Death);
	corpse.frame = animData.frames - 1;
	corpse.width = animData.width;
}
} // namespace

void InitCorpses()
{
	int8_t mtypes[MaxMonsters] = {};

	int8_t nd = 0;

	for (size_t i = 0; i < LevelMonsterTypeCount; i++) {
		CMonster &monsterType = LevelMonsterTypes[i];
		if (mtypes[monsterType.type] != 0)
			continue;

		InitDeadAnimationFromMonster(Corpses[nd], monsterType);
		nd++;

		monsterType.corpseId = nd;
		mtypes[monsterType.type] = nd;
	}

	nd++; // Unused blood spatter

	if (!HeadlessMode)
		Corpses[nd].sprites.emplace(*MissileSpriteData[MFILE_SHATTER1].sprites);
	Corpses[nd].frame = 11;
	Corpses[nd].width = 128;
	nd++;

	stonendx = nd;

	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		auto &monster = Monsters[ActiveMonsters[i]];
		if (monster.isUnique()) {
			InitDeadAnimationFromMonster(Corpses[nd], monster.type());
			Corpses[nd].trn = Monsters[ActiveMonsters[i]].uniqueMonsterTRN.get();
			nd++;

			monster.corpseId = nd;
		}
	}

	assert(static_cast<unsigned>(nd) <= MaxCorpses);
}

void AddCorpse(Point tilePosition, int8_t dv, Direction ddir)
{
	dCorpse[tilePosition.x][tilePosition.y] = (dv & 0x1F) + (static_cast<int>(ddir) << 5);
}

void SyncUniqDead()
{
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		auto &monster = Monsters[ActiveMonsters[i]];
		if (!monster.isUnique())
			continue;
		for (int dx = 0; dx < MAXDUNX; dx++) {
			for (int dy = 0; dy < MAXDUNY; dy++) {
				if ((dCorpse[dx][dy] & 0x1F) == monster.corpseId)
					ChangeLightXY(monster.lightId, { dx, dy });
			}
		}
	}
}

} // namespace devilution
