// Finding enemies and hurting them, for attacks the mods add (masenko.c,
// fusion_moves.c). The damage goes through the game's own "enemy takes damage"
// routine, so enemies flinch, die and give experience as they would for one of
// the game's attacks.
#include "lg2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRATCH_ROM 0x8F3000 // the hit handed to the game, in the unused top of the ROM

// Sprite definitions of the mods' own (bosses.c's Buus), to count as enemies too
static uint32_t ownDefs[8];
static int nOwnDefs;

void lg2RegisterSpriteDef(uint32_t def) {
	if (nOwnDefs < 8) {
		ownDefs[nOwnDefs++] = def;
	}
}

static int ownDef(uint32_t def) {
	int i;
	for (i = 0; i < nOwnDefs; ++i) {
		if (ownDefs[i] == def) {
			return 1;
		}
	}
	return 0;
}

// Enemies are found by what they look like in memory: an object drawn with one
// of the game's sprites that has an enemy type and health left.
int lg2FindEnemies(uint32_t player, uint32_t* found, int most) {
	const uint32_t* defs = (const uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
	const uint32_t* ram = modRam(0x02000000);
	int n = 0, i, k;
	for (i = 0x48 / 4; i + 1 < 0x40000 / 4 && n < most; ++i) {
		uint32_t def = ram[i], object = 0x02000000 + i * 4 - LG2_ACTOR_SPRITE_DEF;
		if ((def >> 24) != 8 || ram[i + 1] < def || ram[i + 1] >= def + LG2_SPRITE_DEF_ANIMATIONS + 32 * 16 || object == player) {
			continue;
		}
		for (k = 0; k < 157 && defs[k] != def; ++k) {
		}
		if ((k == 157 && !ownDef(def)) || object + 0x18C > 0x02040000 || (modRead32(object) >> 24) != 8) {
			continue;
		}
		uint32_t type = modRead32(object + LG2_ENEMY_TYPE);
		int health = (int32_t) modRead32(object + LG2_ENEMY_HP);
		if ((type >> 24) != 8 || type >= 0x08800000 || health <= 0 || health > (int32_t) modRead32(type + 4)) {
			continue;
		}
		found[n++] = object;
	}
	return n;
}

// Where an enemy stands on the screen (the middle of its feet) and how wide it is
void lg2EnemyPlace(uint32_t enemy, int* x, int* y, int* width) {
	uint32_t frameRecord = modRead32(enemy + LG2_ACTOR_FRAME_RECORD);
	int w = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 2) : 16, h = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 3) : 32;
	*x = (int) modRead32(enemy + LG2_ACTOR_SCREEN_X) + w / 2;
	*y = (int) modRead32(enemy + LG2_ACTOR_SCREEN_Y) + h - 6;
	*width = w;
}

// Hits asked for at a moment the game cannot be called into (it is inside an
// interrupt at the end of some frames) are kept and landed the next frame
static struct { uint32_t player, enemy; int basePower; } pending[16];
static int nPending;

static void land(uint32_t player, uint32_t enemy, int basePower);
int lg2HookSeen;

static void flush(void) {
	int i, n = nPending;
	if (!n || !modCanCall()) {
		return;
	}
	nPending = 0;
	for (i = 0; i < n; ++i) {
		if ((int32_t) modRead32(pending[i].enemy + LG2_ENEMY_HP) > 0 && lg2IsPlayer(pending[i].player)) {
			land(pending[i].player, pending[i].enemy, pending[i].basePower);
		}
	}
}

// An energy attack of the given base power (an energy blast is 1024) lands on
// an enemy. The damage is the game's own formula (see gohan_attacks.c).
void lg2HitEnemy(uint32_t player, uint32_t enemy, int basePower) {
	if (!modCanCall()) {
		modLog("hit on %08X put off: the game cannot be called into right now", enemy);
		if (nPending < 16) {
			pending[nPending].player = player;
			pending[nPending].enemy = enemy;
			pending[nPending++].basePower = basePower;
		}
		return;
	}
	land(player, enemy, basePower);
}

static void land(uint32_t player, uint32_t enemy, int basePower) {
	uint32_t alive[16];
	int n = lg2FindEnemies(player, alive, 16), k;
	for (k = 0; k < n && alive[k] != enemy; ++k) {
	}
	if (k == n) { // no longer an enemy with health (dead, gone, or its memory reused): nothing to hit
		modLog("hit on %08X not landed: it is not a living enemy now", enemy);
		return;
	}
	int power = modRead8(player + LG2_PLAYER_STATS + LG2_POW), endurance = modRead8(enemy + LG2_PLAYER_STATS + LG2_END);
	double f = (26.0 * power * power - 701.0 * power + 115618.0) * power / 256.0;
	double exact = (128 - endurance) * basePower / 128.0 * f / 262144.0;
	int damage = exact < 1 ? 1 : (int) exact;
	if (getenv("LG2_DEBUG")) {
		uint32_t type = modRead32(enemy + LG2_ENEMY_TYPE);
		fprintf(stderr, "frame %u: hit enemy %08X: power %d, its stat bytes %d %d %d %d %d, base %d -> damage %d; health %d of %d; type words:", modFrame(), enemy, power,
		        modRead8(enemy + LG2_PLAYER_STATS), modRead8(enemy + LG2_PLAYER_STATS + 1), modRead8(enemy + LG2_PLAYER_STATS + 2), modRead8(enemy + LG2_PLAYER_STATS + 3),
		        modRead8(enemy + LG2_PLAYER_STATS + 4), basePower, damage, (int) modRead32(enemy + LG2_ENEMY_HP), (int) modRead32(type + 4));
		for (int k = 0; k < 10; ++k) fprintf(stderr, " %d", (int) modRead32(type + k * 4));
		fprintf(stderr, "\n");
	}
	// A hit as the game builds them (see lg2.h): the routine is handed a
	// pointer 16 bytes in. The attack descriptor says "energy attack".
	uint8_t* scratch = modRom(SCRATCH_ROM);
	memset(scratch, 0, 128);
	scratch[64] = 1;
	*(int32_t*) (scratch + 68) = basePower;
	*(int32_t*) (scratch + 4) = 1024;
	*(uint32_t*) (scratch + 8) = 0x08000000 + SCRATCH_ROM + 64;
	*(uint32_t*) (scratch + 12) = player;
	*(int32_t*) (scratch + 20) = damage;
	*(int32_t*) (scratch + 24) = damage;
	int before = (int32_t) modRead32(enemy + LG2_ENEMY_HP);
	int seen = (int32_t) modRead32(0x08000000 + SCRATCH_ROM + 24); // what the game will read as the damage
	lg2HookSeen = 0;
	uint32_t result = modCall(LG2_FN_ENEMY_TAKE_DAMAGE | 1, enemy, 0x08000000 + SCRATCH_ROM + 16, 0, 0);
	modLog("hit enemy %08X: power %d vs endurance %d, base %d -> %d damage (game reads %d); call returned %u, hook ran %d; its health %d -> %d", enemy, power, endurance,
	       basePower, damage, seen, result, lg2HookSeen, before, (int32_t) modRead32(enemy + LG2_ENEMY_HP));
}

MOD_INIT(enemy_hits) { modOnFrame(flush); }
