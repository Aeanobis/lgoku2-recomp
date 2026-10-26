// Guard: hold R to raise the energy orb from Buu's Fury. While it is up no
// attack hurts you, and it costs no energy. You cannot move or attack while
// guarding.
//
// "Untouchable" (switched on the Start menu's Extras page): enemies' attacks pass through you
// as if you were not there (no damage, no flinch), and you can still move and
// fight. It stays on until you press I again or quit.
#include "lg2.h"

#include <stdio.h>
#include <stdlib.h>
#include "orb_frames.h"

#include <stdlib.h>

#define ORB_FULL 24 // frames for the orb to reach full size

enum { BTN_START = 8, BTN_R = 256 };

int lg2Untouchable;
#define untouchable lg2Untouchable

static int guarding;
static int orb; // 0 = no orb, up to ORB_FULL = full size

static uint32_t filter(uint32_t keys) {
	// Only out in the world: in the Start menu (no player object) R has to
	// reach the game, where it turns to the next page.
	guarding = (keys & BTN_R) != 0 && (modRead32(LG2_RAM_PLAYER_PTR) >> 24) == 2;
	// While guarding the game never sees R (it would cycle the selected
	// attack); only Start gets through.
	return guarding ? keys & BTN_START : keys;
}

static void (*hitFilters[8])(int32_t* damage);
static int nHitFilters;

void lg2AddPlayerHitFilter(void (*fn)(int32_t* damage)) {
	if (nHitFilters < 8) {
		hitFilters[nHitFilters++] = fn;
	}
}

static int onPlayerHit(uint32_t* r) {
	int32_t* damage = modRam(r[1] + 8);
	if (getenv("LG2_DEBUG")) {
		fprintf(stderr, "frame %u: PLAYER takes damage: hit %08X damage %d attacker %08X descriptor %08X; from %08X, stack:", modFrame(), r[1], damage ? *damage : -1, modRead32(r[1] - 4), modRead32(r[1] - 8), r[14] & ~1U);
		for (uint32_t a = r[13] & ~3U; a < 0x03007F00 && a >= 0x03007000; a += 4) { uint32_t v = modRead32(a); if ((v >> 24) == 8 && (v & 1) && v < 0x08030000) fprintf(stderr, " %08X", v & ~1U); }
		fprintf(stderr, "\n");
	}
	int i;
	for (i = 0; damage && i < nHitFilters; ++i) {
		hitFilters[i](damage);
	}
	return MOD_CONTINUE;
}

// A hit that the guard stops must not happen at all as far as the game is
// concerned. Zeroing the damage is not enough: the game would still make the
// character flinch and tell the story scripts that the player was hit (which,
// for one thing, cracks an egg being carried). So the whole "player was struck"
// routine is skipped.
static int onStruck(uint32_t* r) {
	if (guarding || untouchable) {
		r[0] = 0; // "not knocked out"
		return MOD_RETURN;
	}
	return MOD_CONTINUE;
}

// The orb is centred on the character's sprite. Actors keep the screen position
// of their sprite's top-left corner; the current frame record has its size.
static void drawOrb(uint32_t player) {
	int i = orb > 22 ? 0 : orb > 14 ? 1 : orb > 6 ? 2 : 3;
	uint32_t frameRecord = modRead32(player + LG2_ACTOR_FRAME_RECORD);
	int w = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 2) : 16;
	int h = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 3) : 32;
	int size = orbFrames[i].size;
	int left = (int) modRead32(player + LG2_ACTOR_SCREEN_X) + w / 2 - size / 2;
	int top = (int) modRead32(player + LG2_ACTOR_SCREEN_Y) + h / 2 - size / 2;
	uint32_t* screen = modScreen();
	int x, y;
	for (y = 0; y < size; ++y) {
		for (x = 0; x < size; ++x) {
			uint32_t c = orbFrames[i].pixels[y * size + x];
			if ((c >> 24) && left + x >= 0 && left + x < 240 && top + y >= 0 && top + y < 160) {
				screen[(top + y) * 240 + left + x] = c & 0xFFFFFF;
			}
		}
	}
}

static void frame(void) {
	static unsigned noteUntil;
	if (modPaused()) {
		return;
	}
	static int was;
	if (untouchable != was) {
		was = untouchable;
		noteUntil = modFrame() + 120;
	}
	if (modFrame() < noteUntil) {
		modTextScale(2);
		modText(8, 30, 0x40FFFF, untouchable ? "UNTOUCHABLE: ON" : "UNTOUCHABLE: OFF");
		modTextScale(1);
	} else if (untouchable) {
		modText(196, 28, 0x40FFFF, "UNTOUCHABLE"); // small reminder while it is on
	}
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	if ((player >> 24) != 2) { // no player on screen (menus, scene changes)
		orb = 0;
		return;
	}
	if (guarding) {
		if (orb < ORB_FULL) {
			++orb;
		}
	} else if (orb > 0) {
		--orb;
	}
	if (orb > 0 && !getenv("LG2_NO_ORB")) {
		drawOrb(player);
	}
}

MOD_INIT(blocking) {
	modHook(LG2_FN_PLAYER_TAKE_DAMAGE, onPlayerHit);
	modHook(LG2_FN_PLAYER_ON_HIT, onStruck);
	modFilterKeys(filter);
	modOnFrame(frame);
}
