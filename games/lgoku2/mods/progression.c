// Buu's Fury style progression for Legacy of Goku II:
//   1. Levelling up gives stat points to spend yourself, instead of random growth.
//   2. The level cap is raised from 50 to LEVEL_CAP.
//   3. The story's three-year time skip counts as training: every character you
//      have at that point gains TIMESKIP_LEVELS levels (with the stat points
//      to spend). The original game hands out new moves there but no levels.
//
// The stat menu is on the Start menu's Extras page (L+R together also opens it).
#include "lg2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEVEL_CAP 100          // the original game stops at 50
#define POINTS_PER_LEVEL 4     // original growth averages about 4 a level (Hercule: 1.5)
#define POINTS_PER_LEVEL_LATE 2 // past level 50
#define STAT_MAX LG2_STAT_CAP
#define TIMESKIP_LEVELS 5

enum { KEY_F9 = 66 }; // SDL scancode (test aid only)
enum { BTN_A = 1, BTN_B = 2, BTN_RIGHT = 16, BTN_LEFT = 32, BTN_UP = 64, BTN_DOWN = 128, BTN_R = 256, BTN_L = 512 };

// ---- experience and levels ----------------------------------------------------
static uint32_t xpTable[LEVEL_CAP]; // xpTable[n]: total experience needed for level n+1

static void buildXpTable(void) {
	const uint32_t* rom = (const uint32_t*) modRom(LG2_ROM_XP_TABLE);
	int i;
	for (i = 0; i < 50 && i < LEVEL_CAP; ++i) {
		xpTable[i] = rom[i];
	}
	// Past 50: each level costs 6% more than the one before
	double step = (double) rom[49] - rom[48];
	for (; i < LEVEL_CAP; ++i) {
		step *= 1.06;
		double next = xpTable[i - 1] + step;
		xpTable[i] = next > 4000000000.0 ? 4000000000U : (uint32_t) next;
	}
}

int lg2Level(const struct LG2Char* c);
uint32_t lg2XpForLevel(int level);

static int levelOf(const struct LG2Char* c) {
	int i = LEVEL_CAP - 1;
	while (i > 0 && c->xp < xpTable[i]) {
		--i;
	}
	return i + 1;
}

static uint32_t xpForLevel(int level) {
	if (level < 1) {
		level = 1;
	}
	if (level > LEVEL_CAP) {
		level = LEVEL_CAP;
	}
	return xpTable[level - 1];
}

// For other mods (declared in lg2.h)
int lg2Level(const struct LG2Char* c) {
	return levelOf(c);
}

uint32_t lg2XpForLevel(int level) {
	return xpForLevel(level);
}

// The status page takes the level once, and at exactly 50 (the old cap) leaves
// out the NEXT line. It is told 49 then, and the one number it prints from
// that, "LEVEL: 49", is put right as it is formatted (below).
#define LG2_STATUS_LEVEL_CHECK 0x0800E43C  // return address of the status page's level call
#define LG2_STATUS_LEVEL_FORMAT 0x0800E47E // return address of its formatting of the level
static int statusToldLess;

static int hookGetLevel(uint32_t* r) {
	int level = levelOf(modRam(r[0]));
	uint32_t from = r[14] & ~1U;
	// The enemy-killed code gives no experience when this returns exactly 50,
	// the old cap. Tell that one caller 49 so levelling carries on past 50.
	if (level == 50 && LEVEL_CAP > 50 && (from == LG2_ENEMY_KILLED_LEVEL_CHECK || from == LG2_STATUS_LEVEL_CHECK)) {
		level = 49;
		statusToldLess = from == LG2_STATUS_LEVEL_CHECK;
	}
	r[0] = level;
	return MOD_RETURN;
}

static int hookXpToNext(uint32_t* r) {
	const struct LG2Char* c = modRam(r[0]);
	int level = levelOf(c);
	r[0] = level >= LEVEL_CAP ? 0 : xpTable[level] - c->xp;
	return MOD_RETURN;
}

static int hookXpIntoLevel(uint32_t* r) {
	const struct LG2Char* c = modRam(r[0]);
	r[0] = c->xp - xpForLevel(r[1]);
	return MOD_RETURN;
}

static int hookXpForLevel(uint32_t* r) {
	r[0] = xpForLevel(r[0]);
	return MOD_RETURN;
}

static int hookSetLevelXp(uint32_t* r) {
	struct LG2Char* c = modRam(r[0]);
	c->xp = xpForLevel(r[1]);
	return MOD_RETURN;
}

// ---- stat points ----------------------------------------------------------------
static int pointsForLevel(int who, int level) {
	int points = level > 50 ? POINTS_PER_LEVEL_LATE : POINTS_PER_LEVEL;
	return who == LG2_HERCULE ? points / 2 : points; // he is meant to be weak
}

// Points earned by reaching `level`, counted from level 1
static int pointsEarned(int who, int level) {
	int total = 0, l;
	for (l = 2; l <= level; ++l) {
		total += pointsForLevel(who, l);
	}
	return total;
}

// Nothing extra is stored in the save file: points already spent are worked out
// from how far the stats are above the character's starting stats.
static int pointsUnspent(int who) {
	const struct LG2Char* c = lg2Char(who);
	const struct LG2Char* start = (const struct LG2Char*) modRom(LG2_ROM_CHARS) + who;
	int spent = 0, room = 0, i;
	for (i = 0; i < LG2_STATS; ++i) {
		spent += (c->stat[i] >> 8) - (start->stat[i] >> 8);
		room += STAT_MAX - (c->stat[i] >> 8);
	}
	int left = pointsEarned(who, levelOf(c)) - spent;
	if (left > room) {
		left = room;
	}
	return left > 0 ? left : 0;
}

// Spend points the way the original game would have grown the character
static void autoSpend(int who, int points, uint8_t gained[LG2_STATS]) {
	struct LG2Char* c = lg2Char(who);
	const struct LG2Growth* g = (const struct LG2Growth*) modRom(LG2_ROM_GROWTH) + who;
	int weight[LG2_STATS], total = 0, i;
	for (i = 0; i < LG2_STATS; ++i) {
		weight[i] = g->stat[i].min + g->stat[i].max;
		total += weight[i];
	}
	int given[LG2_STATS] = { 0 }, n;
	for (n = 1; n <= points; ++n) {
		int best = -1;
		long bestLag = 0;
		for (i = 0; i < LG2_STATS; ++i) {
			long lag = (long) weight[i] * n - (long) given[i] * total;
			if ((c->stat[i] >> 8) < STAT_MAX && (best < 0 || lag > bestLag)) {
				best = i;
				bestLag = lag;
			}
		}
		if (best < 0) {
			break;
		}
		c->stat[best] += 0x100;
		++given[best];
	}
	for (i = 0; i < LG2_STATS; ++i) {
		gained[i] = given[i];
	}
}

static void levelUp(int who, int oldLevel, int newLevel) {
	struct LG2Char* c = lg2Char(who);
	uint8_t gained[LG2_STATS] = { 0 };
	// One level at a time is the player levelling up: they choose. A jump of
	// several levels is the story handing over a character, so spend for them.
	if (newLevel - oldLevel > 1) {
		autoSpend(who, pointsEarned(who, newLevel) - pointsEarned(who, oldLevel), gained);
	}
	int level;
	for (level = oldLevel; level < newLevel; ++level) {
		// Health: +5.5% to +7.5% a level as in the original, half that past 50
		uint32_t rate = modRandom(0x51F) + 0xE14;
		if (level >= 50) {
			rate /= 2;
		}
		uint32_t hp = c->maxHp + ((c->maxHp * rate) >> 16);
		c->maxHp = c->hp = hp > 30000 ? 30000 : hp;
		// Energy: +2 to +4
		uint32_t ep = c->maxEp + modRandom(3) + 2;
		c->maxEp = c->ep = ep > 30000 ? 30000 : ep;
	}
	memcpy(modRam(LG2_RAM_LEVELUP_GAINS), gained, LG2_STATS);
}

static int hookLevelUp(uint32_t* r) {
	if (r[0] != LG2_RAM_CHARDATA || r[1] >= LG2_CHARACTERS) {
		return MOD_CONTINUE; // not what we expect: leave it to the game
	}
	levelUp(r[1], r[2], r[3]);
	return MOD_RETURN;
}

// ---- stat menu ------------------------------------------------------------------
static int menuOpen, menuWho, menuCursor;
static uint32_t lastKeys;

static int available(int who) {
	return who == lg2CurrentChar() || (lg2Char(who)->flags & 1);
}

static int menuFresh;

int lg2StatPointsWaiting(void);

void lg2OpenStatMenu(void) {
	menuOpen = menuFresh = 1;
	menuWho = lg2CurrentChar();
	menuCursor = 0;
	if (!lg2HubOpen) {
		modSetPaused(1); modLog("progression: paused (%s)", __func__);
	}
}

int lg2StatMenuIsOpen(void) {
	return menuOpen;
}

static void drawMenu(void) {
	static const char* const statName[LG2_STATS] = { "STRENGTH", "POWER", "ENDURANCE" };
	const struct LG2Char* c = lg2Char(menuWho);
	int left = pointsUnspent(menuWho), i;
	modRect(28, 22, 184, 116, 0x402010);
	modRect(30, 24, 180, 112, 0x000000);
	modTextScale(2);
	modText(40, 32, 0x40C0FF, "%s  LV %d", lg2Name(menuWho), levelOf(c));
	modText(40, 50, left ? 0x40FF40 : 0x909090, "POINTS: %d", left);
	for (i = 0; i < LG2_STATS; ++i) {
		modText(40, 68 + i * 14, i == menuCursor ? 0xFFFFFF : 0x909090, "%c%-9s %3d",
		        i == menuCursor ? '>' : ' ', statName[i], c->stat[i] >> 8);
	}
	modTextScale(1);
	modText(40, 114, 0xC0C0C0, "A: ADD POINT   B: CLOSE");
	modText(40, 122, 0xC0C0C0, "LEFT/RIGHT: CHARACTER");
}

// ---- the three-year time skip -------------------------------------------------------
// The game has no "time skip" flag we know of, but Gohan changes from the outfit
// he wears before it (form 1) to the one he wears after (form 0) at that moment
// and at no other: transformations go to forms 4 and 5 and back.
#define GOHAN_FORM_BEFORE_SKIP 1
#define GOHAN_FORM_AFTER_SKIP 0
static unsigned trainingMessageUntil;

static void grantTraining(void) {
	int who, n;
	for (who = 0; who < LG2_CHARACTERS; ++who) {
		struct LG2Char* c = lg2Char(who);
		if (!(c->flags & 1)) {
			continue; // not with you yet
		}
		for (n = 0; n < TIMESKIP_LEVELS; ++n) {
			int level = levelOf(c);
			if (level >= LEVEL_CAP) {
				break;
			}
			c->xp += xpTable[level] - xpTable[level - 1]; // one level's worth, keeping progress within the level
			if (levelOf(c) > level) {
				levelUp(who, level, level + 1);
			}
		}
	}
	lg2SyncPlayerStats();
	modSetSetting("timeskip_training_given", 1);
	trainingMessageUntil = modFrame() + 420;
}

// A one-off boost for a character, asked for by the player: raised to the
// level, a level at a time as the training above, then never again
static void raiseTo(int who, int target, const char* setting) {
	struct LG2Char* c = lg2Char(who);
	if (modGetSetting(setting, 0) || !(c->flags & 1) || (modRead32(LG2_RAM_PLAYER_PTR) >> 24) != 2) {
		return;
	}
	int from = levelOf(c);
	while (levelOf(c) < target && levelOf(c) < LEVEL_CAP) {
		int level = levelOf(c);
		c->xp += xpTable[level] - xpTable[level - 1];
		if (levelOf(c) > level) {
			levelUp(who, level, level + 1);
		}
	}
	lg2SyncPlayerStats();
	modSetSetting(setting, 1);
	modLog("progression: %s raised from level %d to %d", lg2Name(who), from, levelOf(c));
}

static void watchTimeSkip(void) {
	raiseTo(LG2_PICCOLO, 30, "piccolo_level_30_given");
	raiseTo(LG2_TRUNKS, 50, "trunks_level_50_given");
	raiseTo(LG2_VEGETA, 50, "vegeta_level_50_given");
	raiseTo(LG2_PICCOLO, 50, "piccolo_level_50_given");
	// Everyone to the cap (asked for 2026-10-15); whoever joins later gets it on joining
	raiseTo(LG2_GOHAN, LEVEL_CAP, "gohan_level_100_given");
	raiseTo(LG2_PICCOLO, LEVEL_CAP, "piccolo_level_100_given");
	raiseTo(LG2_VEGETA, LEVEL_CAP, "vegeta_level_100_given");
	raiseTo(LG2_TRUNKS, LEVEL_CAP, "trunks_level_100_given");
	raiseTo(LG2_GOKU, LEVEL_CAP, "goku_level_100_given");
	raiseTo(LG2_HERCULE, LEVEL_CAP, "hercule_level_100_given");
	static int lastForm = -1;
	if ((modRead32(LG2_RAM_PLAYER_PTR) >> 24) != 2) {
		return; // only during play, so half-loaded data is never looked at
	}
	int form = lg2Char(LG2_GOHAN)->sprite;
	if (lastForm == GOHAN_FORM_BEFORE_SKIP && form == GOHAN_FORM_AFTER_SKIP) {
		grantTraining(); // it just happened
	} else if (lastForm < 0 && form == GOHAN_FORM_AFTER_SKIP && !modGetSetting("timeskip_training_given", 0)) {
		grantTraining(); // a save from after the skip, made before this feature existed
	}
	lastForm = form;
}

int lg2StatPointsWaiting(void) {
	int who, total = 0;
	for (who = 0; who < LG2_CHARACTERS; ++who) {
		if (available(who)) {
			total += pointsUnspent(who);
		}
	}
	return total;
}

static void frame(void) {
	watchTimeSkip();
	if (modFrame() < trainingMessageUntil && !modPaused()) {
		modTextScale(2);
		modText(8, 30, 0x40FF40, "3 YEARS OF TRAINING");
		modText(8, 46, 0x40FF40, "EVERYONE: +%d LEVELS", TIMESKIP_LEVELS);
		modTextScale(1);
	}
	uint32_t keys = modKeys();
	uint32_t pressed = keys & ~lastKeys;
	// L and R together open it during play; otherwise it is on the Start menu's Extras page
	int toggle = !lg2HubOpen && (keys & (BTN_L | BTN_R)) == (BTN_L | BTN_R) && (pressed & (BTN_L | BTN_R));
	lastKeys = keys;

	if (getenv("LG2_DEBUG") && modKeyPressed(KEY_F9)) { // test aid: one level for the current character
		int who = lg2CurrentChar();
		struct LG2Char* c = lg2Char(who);
		int level = levelOf(c);
		if (level < LEVEL_CAP) {
			c->xp = xpTable[level];
			levelUp(who, level, level + 1);
			lg2SyncPlayerStats();
			fprintf(stderr, "debug: %s to level %d at frame %u, %d points unspent\n", lg2Name(who), level + 1, modFrame(), pointsUnspent(who));
		}
	}

	if (!menuOpen) {
		if (toggle) {
			menuOpen = 1;
			menuWho = lg2CurrentChar();
			menuCursor = 0;
			modSetPaused(1); modLog("progression: paused (%s)", __func__);
		} else {
			int who;
			for (who = 0; who < LG2_CHARACTERS; ++who) {
				if (available(who) && pointsUnspent(who) && (modFrame() & 32) && lg2IsPlayer(modRead32(LG2_RAM_PLAYER_PTR))) {
					modText(146, 2, 0x40FF40, "STAT POINTS: START MENU");
					break;
				}
			}
		}
		return;
	}

	if (menuFresh) { // the press that opened it from the Extras page is not a press in it
		menuFresh = 0;
		pressed = 0;
	}
	if (toggle || (pressed & BTN_B)) {
		menuOpen = 0;
		if (!lg2HubOpen) {
			modSetPaused(0); modLog("progression: unpaused (%s)", __func__);
		}
		return;
	}
	if (pressed & BTN_UP) {
		menuCursor = (menuCursor + LG2_STATS - 1) % LG2_STATS;
	}
	if (pressed & BTN_DOWN) {
		menuCursor = (menuCursor + 1) % LG2_STATS;
	}
	if (pressed & (BTN_LEFT | BTN_RIGHT)) {
		int step = (pressed & BTN_RIGHT) ? 1 : LG2_CHARACTERS - 1, tries;
		for (tries = 0; tries < LG2_CHARACTERS; ++tries) {
			menuWho = (menuWho + step) % LG2_CHARACTERS;
			if (available(menuWho)) {
				break;
			}
		}
	}
	if (pressed & BTN_A) {
		struct LG2Char* c = lg2Char(menuWho);
		if (pointsUnspent(menuWho) && (c->stat[menuCursor] >> 8) < STAT_MAX) {
			c->stat[menuCursor] += 0x100;
			lg2SyncPlayerStats();
			if (getenv("LG2_DEBUG")) {
				fprintf(stderr, "debug: frame %u, %s stats now %d/%d/%d\n", modFrame(), lg2Name(menuWho), c->stat[0] >> 8, c->stat[1] >> 8, c->stat[2] >> 8);
			}
		}
	}
	drawMenu();
}

// The status screen's stat bars: the game draws each as a box 62..62+stat*3/4
// wide, full at 100. Now that stats go to 200, the bar is drawn at stat*3/8,
// so 200 fills it and 100 is halfway. The code cannot be changed (it is
// compiled), but the page formats the number just before it draws the box,
// so the value is caught there and the box's right edge put where it belongs.
#define LG2_FN_FORMAT 0x0801FB88        // (dest, format, value...): the game's sprintf
#define LG2_FN_FILL_RECT 0x08021638     // (dest, gfx, rect {x1, y1, x2, y2}, colour)
#define STAT_BAR_LEFT 0x3E
#define STAT_BAR_WIDTH 75
#define STAT_BAR_COLOUR 0x4B
static int lastStatShown = -1;

static int onFormat(uint32_t* r) {
	if (r[1] == 0x08000000 + LG2_ROM_STATUS_STAT_TEXT) {
		lastStatShown = (int) r[2];
	}
	if ((r[14] & ~1U) == LG2_STATUS_LEVEL_FORMAT) {
		if (statusToldLess && r[2] == 49) {
			r[2] = 50; // (see hookGetLevel)
		}
		statusToldLess = 0;
	}
	return MOD_CONTINUE;
}

static int onFillRect(uint32_t* r) {
	uint32_t rect = r[2];
	int x1 = (int) modRead32(rect), x2 = (int) modRead32(rect + 8), shown = lastStatShown;
	// the game's own width is (stat >> 2) * 3, 75 past 100: the box just formatted
	if (r[3] == STAT_BAR_COLOUR && x1 == STAT_BAR_LEFT && shown >= 0 && x2 == STAT_BAR_LEFT + (shown > 100 ? STAT_BAR_WIDTH : (shown >> 2) * 3)) {
		int width = (shown > LG2_STAT_CAP ? LG2_STAT_CAP : shown) * STAT_BAR_WIDTH / LG2_STAT_CAP;
		modWrite32(rect + 8, (uint32_t) (STAT_BAR_LEFT + width));
		lastStatShown = -1;
	}
	return MOD_CONTINUE;
}

MOD_INIT(progression) {
	// The level-up message used to list the stat gains, which are always +0 now
	// that levels give points to spend instead. (English text only.)
	strcpy((char*) modRom(LG2_ROM_LEVELUP_BONUS_TEXT), "@#Good Job!");
	// The status screen writes each stat as "47/100": the limit is 200 now
	if (!memcmp(modRom(LG2_ROM_STATUS_STAT_TEXT), "%d/100", 7)) {
		memcpy(modRom(LG2_ROM_STATUS_STAT_TEXT), "%d/200", 7);
		modHook(LG2_FN_FORMAT, onFormat);
		modHook(LG2_FN_FILL_RECT, onFillRect);
	}
	buildXpTable();
	modHook(LG2_FN_GET_LEVEL, hookGetLevel);
	modHook(LG2_FN_XP_TO_NEXT, hookXpToNext);
	modHook(LG2_FN_XP_INTO_LEVEL, hookXpIntoLevel);
	modHook(LG2_FN_XP_FOR_LEVEL, hookXpForLevel);
	modHook(LG2_FN_SET_LEVEL_XP, hookSetLevelXp);
	modHook(LG2_FN_LEVEL_UP, hookLevelUp);
	modOnFrame(frame);
}
