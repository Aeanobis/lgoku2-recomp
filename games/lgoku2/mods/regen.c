// Slow healing: the character you are playing recovers health over time.
//
// 1% of maximum health (at least 1 point) every 3 seconds, so a full heal from
// nothing takes about five minutes. It stops for 5 seconds after taking a hit,
// does nothing in menus, and never revives a fallen character.
//
// Standing on a Capsule Corp save pad restores health completely.
#include "lg2.h"

#define HEAL_EVERY_FRAMES 180   // 3 seconds
#define HEAL_PERCENT 1
#define PAUSE_AFTER_HIT_FRAMES 300

// The game has no "standing on a save pad" flag that we have found, so the pad
// is recognised by sight: its three shades of deep blue, which nothing else in
// the game's floors uses, filling much of the ground around the character's feet.
static int onSavePad(uint32_t player) {
	uint32_t frameRecord = modRead32(player + LG2_ACTOR_FRAME_RECORD);
	int w = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 2) : 16;
	int h = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 3) : 32;
	int cx = (int) modRead32(player + LG2_ACTOR_SCREEN_X) + w / 2;
	int feet = (int) modRead32(player + LG2_ACTOR_SCREEN_Y) + h;
	const uint32_t* screen = modScreen();
	int x, y, blue = 0;
	for (y = feet - 24; y < feet + 12; ++y) {
		for (x = cx - 24; x < cx + 24; ++x) {
			if (x < 0 || x >= 240 || y < 0 || y >= 160) {
				continue;
			}
			uint32_t c = screen[y * 240 + x] & 0xF8F8F8; // 0xBBGGRR
			blue += c == 0xC86020 || c == 0xA86020 || c == 0xB03800;
		}
	}
	return blue >= 150;
}

static void frame(void) {
	static int lastHp = -1, lastWho = -1;
	static unsigned timer, hitPause;
	if (modPaused() || (modRead32(LG2_RAM_PLAYER_PTR) >> 24) != 2) {
		return; // a mod menu, the Start menu, or no player on screen
	}
	int who = lg2CurrentChar();
	if (who < 0 || who >= LG2_CHARACTERS) {
		return;
	}
	struct LG2Char* c = lg2Char(who);
	if (c->hp > 0 && modFrame() % 8 == 0 && (c->hp < c->maxHp || c->ep < c->maxEp || lg2FusionWaiting()) &&
	    onSavePad(modRead32(LG2_RAM_PLAYER_PTR))) {
		c->hp = c->maxHp; // the pad restores health and energy both,
		c->ep = c->maxEp;
		lg2FusionReady(); // and ends the wait before the next fusion
	}
	if (who != lastWho) { // switched character: start counting afresh
		lastWho = who;
		lastHp = c->hp;
		timer = hitPause = 0;
	}
	if (c->hp < lastHp) {
		hitPause = PAUSE_AFTER_HIT_FRAMES;
		timer = 0;
	}
	if (hitPause) {
		--hitPause;
	} else if (c->hp > 0 && c->hp < c->maxHp && ++timer >= HEAL_EVERY_FRAMES) {
		int amount = c->maxHp * HEAL_PERCENT / 100;
		timer = 0;
		c->hp += amount < 1 ? 1 : amount;
		if (c->hp > c->maxHp) {
			c->hp = c->maxHp;
		}
	}
	lastHp = c->hp;
}

MOD_INIT(regen) {
	modOnFrame(frame);
}
