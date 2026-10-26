// Fighting extras:
//
//   - a health bar over each enemy you have hit, for a few seconds after the hit
//   - the damage of each hit floating up from whoever took it (white on
//     enemies, red on you)
//   - zenie: enemies drop money when beaten (spent in shop.c, key P)
//   - enemy toughness, for when the game gets too easy. On the Start menu's
//     Extras page it goes through:
//       NORMAL   the game as it is
//       SCALED   enemies weaker than you are brought up towards your level
//       TOUGH    every enemy has 1.5 times the health and hits 1.25 times as hard
//       BRUTAL   2.5 times the health, 1.75 times as hard
//     The choice is remembered.
//
// Enemy health is not actually changed: the damage you deal is divided
// instead, which comes to the same thing and needs no bookkeeping per enemy.
#include "lg2.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { NORMAL, SCALED, TOUGH, BRUTAL, MODES };
static const char* const modeNames[MODES] = { "NORMAL", "SCALED", "TOUGH", "BRUTAL" };
static int mode;

int lg2DamageDealtPercent = 100, lg2DamageTakenPercent = 100, lg2ZeniePercent = 100;

const char* lg2ToughnessName(void) {
	return modeNames[mode];
}

void lg2NextToughness(void) {
	mode = (mode + 1) % MODES;
	modSetSetting("enemy_toughness", mode);
}

// ---- zenie -----------------------------------------------------------------------------
static int zenie;

int lg2Zenie(void) {
	return zenie;
}

void lg2AddZenie(int amount) {
	zenie += amount;
	if (zenie < 0) {
		zenie = 0;
	}
	if (zenie > 9999999) {
		zenie = 9999999;
	}
	modSetSetting("zenie", zenie);
}

// ---- how tough an enemy is --------------------------------------------------------------
// The level an enemy was made for, judged by the experience it gives: the
// level at which about twenty of them fill the bar.
static int enemyLevel(int reward) {
	const uint32_t* xp = (const uint32_t*) modRom(LG2_ROM_XP_TABLE);
	int level;
	for (level = 1; level < 49; ++level) {
		if ((int) (xp[level] - xp[level - 1]) / 20 >= reward) {
			break;
		}
	}
	return level;
}

// A boss: an enemy type with this much health or more (Cell and his juniors,
// the androids, Frieza's family, Cooler...); the ordinary enemies stop at 600
#define BOSS_HEALTH 1000
#define BOSS_HEALTH_CAP 2.0   // the most a boss is ever toughened, whatever the mode
#define BOSS_STRENGTH_CAP 1.4

static int isBoss(uint32_t enemy) {
	uint32_t type = enemy ? modRead32(enemy + LG2_ENEMY_TYPE) : 0;
	return (type >> 24) == 8 && (int32_t) modRead32(type + 4) >= BOSS_HEALTH;
}

static void toughnessOfMode(uint32_t enemy, double* health, double* strength);

// How many times its normal health an enemy has, and how much harder it hits
static void toughness(uint32_t enemy, double* health, double* strength) {
	toughnessOfMode(enemy, health, strength);
	if (isBoss(enemy)) {
		if (*health > BOSS_HEALTH_CAP) {
			*health = BOSS_HEALTH_CAP;
		}
		if (*strength > BOSS_STRENGTH_CAP) {
			*strength = BOSS_STRENGTH_CAP;
		}
	}
}

static void toughnessOfMode(uint32_t enemy, double* health, double* strength) {
	*health = *strength = 1.0;
	if (mode == TOUGH) {
		*health = 1.5;
		*strength = 1.25;
	} else if (mode == BRUTAL) {
		*health = 2.5;
		*strength = 1.75;
	} else if (mode == SCALED && enemy) {
		uint32_t type = modRead32(enemy + LG2_ENEMY_TYPE);
		int behind = lg2Level(lg2Char(lg2CurrentChar())) - enemyLevel((int32_t) modRead32(type));
		if (behind > 0) {
			*health = pow(1.10, behind);
			if (*health > 8.0) {
				*health = 8.0;
			}
			*strength = sqrt(*health);
		}
	}
}

// ---- what is shown ------------------------------------------------------------------------
#define BAR_FRAMES 240
#define NUMBER_FRAMES 45

static struct Bar { uint32_t enemy, type; unsigned until; } bars[12];
static struct Number { int x, y, amount; unsigned born; uint32_t owner; uint32_t color; } numbers[16];
static uint32_t lastEnemyHit;

static void actorTop(uint32_t actor, int* x, int* y) {
	uint32_t frameRecord = modRead32(actor + LG2_ACTOR_FRAME_RECORD);
	int w = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 2) : 16;
	*x = (int) modRead32(actor + LG2_ACTOR_SCREEN_X) + w / 2;
	*y = (int) modRead32(actor + LG2_ACTOR_SCREEN_Y);
}

// Hits in quick succession on the same target (a beam) add up into one number
static void showNumber(uint32_t owner, int amount, uint32_t color) {
	unsigned now = modFrame(), i, oldest = 0;
	if (amount <= 0) {
		return;
	}
	for (i = 0; i < 16; ++i) {
		if (numbers[i].owner == owner && numbers[i].color == color && now - numbers[i].born < 20) {
			numbers[i].amount += amount;
			numbers[i].born = now;
			return;
		}
		if (numbers[i].born < numbers[oldest].born) {
			oldest = i;
		}
	}
	actorTop(owner, &numbers[oldest].x, &numbers[oldest].y);
	numbers[oldest].x += (int) modRandom(9) - 4;
	numbers[oldest].amount = amount;
	numbers[oldest].born = now;
	numbers[oldest].owner = owner;
	numbers[oldest].color = color;
}

// ---- hooks ----------------------------------------------------------------------------------
// An enemy is about to lose health: r5 = enemy, r6 = the hit
static int onEnemyHit(uint32_t* r) {
	if (getenv("LG2_DEBUG_HITS")) {
		fprintf(stderr, "frame %u: enemy %08X hit: descriptor %08X owner %08X damage %d\n", modFrame(), r[5], modRead32(r[6] - 8), modRead32(r[6] - 4), (int32_t) modRead32(r[6] + 8));
	}
	static double owed; // fractions of a point carried to the next hit
	uint32_t enemy = r[5], hit = r[6];
	int32_t damage = (int32_t) modRead32(hit + 8);
	lg2HookSeen = damage ? damage : -1;
	double health, strength;
	unsigned i, slot = 0;
	if (damage > 0) {
		toughness(enemy, &health, &strength);
		// The player's big attacks hit harder as they grow with level (see
		// gohan_attacks.c): 1.25 times from level 25, 1.5 times from 50
		int tier = modRead32(hit - 4) == modRead32(LG2_RAM_PLAYER_PTR) ? lg2GrowingAttackTier(modRead32(hit - 8)) : 0;
		double grown = tier == 2 ? 1.5 : tier == 1 ? 1.25 : 1.0;
		double exact = damage * grown * (lg2DamageDealtPercent / 100.0) / health + owed;
		int before = damage;
		if (tier) {
			modLog("  grown attack (tier %d): %d becomes %d", tier, damage, (int) (damage * grown));
		}
		damage = (int32_t) exact;
		owed = exact - damage;
		if (health != 1.0) {
			modLog("  enemies %s: %d becomes %d", modeNames[mode], before, damage);
		}
		if (getenv("LG2_DEBUG_ONE_HIT")) { // test aid: every hit is a knock-out
			damage = 999999;
		}
		modWrite32(hit + 8, (uint32_t) damage);
	}
	showNumber(enemy, damage, 0xFFFFFF);
	for (i = 0; i < 12; ++i) {
		if (bars[i].enemy == enemy) {
			slot = i;
			break;
		}
		if (bars[i].until < bars[slot].until) {
			slot = i;
		}
	}
	bars[slot].enemy = lastEnemyHit = enemy;
	bars[slot].type = modRead32(enemy + LG2_ENEMY_TYPE);
	bars[slot].until = modFrame() + BAR_FRAMES;
	return MOD_CONTINUE;
}

// The player is about to lose health
static void onPlayerHit(int32_t* damage) {
	double health, strength;
	if (*damage <= 0) {
		return;
	}
	toughness(lastEnemyHit && modRead32(lastEnemyHit + LG2_ENEMY_HP) ? lastEnemyHit : 0, &health, &strength);
	*damage = (int32_t) (*damage * strength * (lg2DamageTakenPercent / 100.0) + 0.5);
	if (*damage < 1) {
		*damage = 1;
	}
	showNumber(modRead32(LG2_RAM_PLAYER_PTR), *damage, 0x4040FF);
}

// An enemy has been beaten: r0 = enemy
static int onEnemyKilled(uint32_t* r) {
	uint32_t type = modRead32(r[0] + LG2_ENEMY_TYPE);
	int reward = (int32_t) modRead32(type);
	if (getenv("LG2_DEBUG_ONE_HIT")) {
		fprintf(stderr, "enemy beaten: experience %d, made for level %d, max health %d\n", reward, enemyLevel(reward), (int32_t) modRead32(type + 4));
	}
	if (reward > 0) {
		int amount = (reward / 4 + 1 + (int) modRandom(reward / 8 + 2)) * lg2ZeniePercent / 100;
		lg2AddZenie(amount);
		showNumber(r[0], amount, 0x40E0FF);
	}
	return MOD_CONTINUE;
}

// The Start menu, recognised by its frame: a handful of pixels that are the
// same on all four of its pages (status, items, options, journal) and nowhere
// else. { x, y, colour }
static int startMenuShowing(void) {
	static const struct { uint8_t x, y; uint32_t color; } marks[] = {
		// all on the bottom bar: the panel above it changes shape with the page
		// and, on the status page, with whose page it is
		{ 13, 150, 0x7B7B7B }, { 65, 136, 0x0063CE }, { 73, 136, 0x0063CE }, { 168, 136, 0x0063CE }, { 219, 137, 0xC6C6C6 },
	};
	unsigned i;
	for (i = 0; i < sizeof(marks) / sizeof(marks[0]); ++i) {
		if ((modScreen()[marks[i].y * 240 + marks[i].x] & 0xFFFFFF) != marks[i].color) {
			return 0;
		}
	}
	return 1;
}

// The zenie total as the Start menu shows it: a small plaque in the menu's own
// colours at the top, between the two arrow buttons, with a coin and the
// amount in gold.
static void px(int x, int y, uint32_t color) {
	if (x >= 0 && x < 240 && y >= 0 && y < 160) {
		modScreen()[y * 240 + x] = color;
	}
}

// One 3x5 glyph, drawn double size with a dark edge to its lower right like the menu's own lettering
static void glyph(int x, int y, const char* rows, uint32_t color) {
	int pass, row, col, i, j;
	for (pass = 0; pass < 2; ++pass) { // the edge first, then the glyph over it
		for (row = 0; row < 5; ++row) {
			for (col = 0; col < 3; ++col) {
				if ((rows[row] - '0') & (4 >> col)) {
					for (j = 0; j < 2; ++j) {
						for (i = 0; i < 2; ++i) {
							px(x + col * 2 + i + !pass, y + row * 2 + j + !pass, pass ? color : 0x301008);
						}
					}
				}
			}
		}
	}
}

static void drawMenuZenie(void) {
	static const char* const digits[10] = { "75557", "26227", "71747", "71717", "55711", "74717", "74757", "71111", "75757", "75717" };
	enum { LEFT = 134, TOP = 3, WIDTH = 78, HEIGHT = 15 };
	char text[12];
	int x, y, n, i;
	// the plaque: light rim, deep blue inside, corners cut
	for (y = 0; y < HEIGHT; ++y) {
		for (x = 0; x < WIDTH; ++x) {
			int cut = (x == 0 || x == WIDTH - 1) + (y == 0 || y == HEIGHT - 1);
			int inset = (x <= 1 || x >= WIDTH - 2) + (y <= 1 || y >= HEIGHT - 2);
			if (cut == 2) {
				continue;
			}
			int rim = x == 0 || x == WIDTH - 1 || y == 0 || y == HEIGHT - 1 || inset == 2;
			px(LEFT + x, TOP + y, rim ? 0xEFDEB5 : y < HEIGHT / 2 ? 0x8C3918 : 0x732908);
		}
	}
	// the coin
	for (y = -4; y <= 4; ++y) {
		for (x = -4; x <= 4; ++x) {
			int d = x * x + y * y;
			if (d <= 18) {
				px(LEFT + 9 + x, TOP + 7 + y, d > 11 ? 0x0070B8 : (x + y < -1 ? 0x80F0FF : 0x00C8FF));
			}
		}
	}
	for (i = 0; i < 3; ++i) { // a Z on it
		px(LEFT + 8 + i, TOP + 5, 0x004890);
		px(LEFT + 10 - i, TOP + 6 + i, 0x004890);
		px(LEFT + 8 + i, TOP + 9, 0x004890);
	}
	// the amount, right-aligned
	n = snprintf(text, sizeof(text), "%d", zenie);
	for (i = 0; i < n; ++i) {
		glyph(LEFT + WIDTH - 5 - (n - i) * 8, TOP + 3, digits[text[i] - '0'], 0x00BDFF);
	}
}

// ---- every frame ------------------------------------------------------------------------------
static void frame(void) {
	static unsigned noteUntil;
	unsigned now = modFrame(), i;
	if (modPaused()) {
		return;
	}
	(void) noteUntil;
	if (!modRead32(LG2_RAM_PLAYER_PTR) && startMenuShowing()) {
		drawMenuZenie();
	}
	if ((modRead32(LG2_RAM_PLAYER_PTR) >> 24) != 2) {
		memset(bars, 0, sizeof(bars)); // a menu, or the area changed: those enemies are gone
		memset(numbers, 0, sizeof(numbers));
		lastEnemyHit = 0;
		return;
	}
	for (i = 0; i < 12; ++i) {
		struct Bar* b = &bars[i];
		if (!b->enemy || now >= b->until || modRead32(b->enemy + LG2_ENEMY_TYPE) != b->type) {
			b->enemy = 0;
			continue;
		}
		int hp = (int32_t) modRead32(b->enemy + LG2_ENEMY_HP), max = (int32_t) modRead32(b->type + 4);
		int x, y;
		if (hp <= 0 || max <= 0) {
			continue;
		}
		actorTop(b->enemy, &x, &y);
		int filled = hp >= max ? 22 : 1 + hp * 21 / max;
		modRect(x - 12, y - 6, 24, 4, 0x000000);
		modRect(x - 11, y - 5, 22, 2, 0x202060);
		modRect(x - 11, y - 5, filled, 2, hp * 4 <= max ? 0x3030F0 : hp * 2 <= max ? 0x30C0F0 : 0x40E040);
	}
	for (i = 0; i < 16; ++i) {
		struct Number* n = &numbers[i];
		unsigned age = now - n->born;
		if (!n->amount || age >= NUMBER_FRAMES) {
			n->amount = 0;
			continue;
		}
		int digits = n->amount >= 10000 ? 5 : n->amount >= 1000 ? 4 : n->amount >= 100 ? 3 : n->amount >= 10 ? 2 : 1;
		if (n->color == 0x40E0FF) { // money: a little later and to the side, so it does not sit on the last hit
			modText(n->x - digits * 2 - 2, n->y - 16 - (int) age / 3, n->color, "+%dZ", n->amount);
		} else {
			modText(n->x - digits * 2, n->y - 8 - (int) age / 2, n->color, "%d", n->amount);
		}
	}
	if (now < noteUntil) {
		modTextScale(2);
		modText(8, 58, 0x40FFFF, "ENEMIES: %s", modeNames[mode]);
		modTextScale(1);
	}
}

MOD_INIT(combat) {
	zenie = modGetSetting("zenie", 0);
	mode = modGetSetting("enemy_toughness", NORMAL) % MODES;
	modHook(LG2_FN_ENEMY_TAKE_DAMAGE_HIT, onEnemyHit);
	modHook(LG2_FN_ENEMY_KILLED, onEnemyKilled);
	lg2AddPlayerHitFilter(onPlayerHit);
	modOnFrame(frame);
}
