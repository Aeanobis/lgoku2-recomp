// Test aid (not part of the normal build: copy into ../mods/ to use).
//   WARP=zone,area,x,y   at frame 130 (or WARPAT) the player is sent there through the game's exit routine
//   WATCH=1              prints the current character's form, attacks and selected slot when they change
#include "lg2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void frame(void) {
	uint32_t p = modRead32(LG2_RAM_PLAYER_PTR);
	if ((int) modFrame() == (getenv("WARPAT") ? atoi(getenv("WARPAT")) : 130) && modCanCall() && getenv("WARP") && lg2IsPlayer(p)) {
		int z, a, x, y;
		sscanf(getenv("WARP"), "%d,%d,%d,%d", &z, &a, &x, &y);
		uint8_t* e = modRom(0x8F1000);
		memset(e, 0, 64);
		e[17] = 0xFF;
		*(uint32_t*) (e + 20) = 0x088F1020;
		e[32 + 17] = z;
		e[32 + 18] = a;
		e[32 + 19] = 1;
		*(int16_t*) (e + 52) = x;
		*(int16_t*) (e + 54) = y;
		uint32_t q = modRead32(LG2_RAM_INPUT_PTR) + 18;
		uint16_t was = modRead16(q);
		modWrite16(q, 0xFF);
		modCall(LG2_FN_TAKE_EXIT | 1, 0x088F1000, p, 0, 0);
		modWrite16(q, was);
	}
	if (getenv("WATCH")) {
		static struct LG2Char last;
		static int lastKind = -1;
		const struct LG2Char* c = lg2Char(lg2CurrentChar());
		if (memcmp(c->attack, last.attack, 4) || c->sprite != last.sprite || c->selectedAttack != last.selectedAttack || c->flags != last.flags || lastKind != lg2FusionKind) {
			fprintf(stderr, "frame %u: char %d form %d attacks %d %d %d %d selected %d flags %X pow %d fusion %d\n", modFrame(), lg2CurrentChar(), c->sprite,
			        c->attack[0], c->attack[1], c->attack[2], c->attack[3], c->selectedAttack, c->flags, c->stat[1] >> 8, lg2FusionKind);
			last = *c;
			lastKind = lg2FusionKind;
		}
	}
}

MOD_INIT(warp) { modOnFrame(frame); }
