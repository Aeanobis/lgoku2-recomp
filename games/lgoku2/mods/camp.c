// Camps: spots of the player's choosing with a Capsule Corp circle (a save
// pad) where he stood, and in the first one, its enemies gone.
//
// The area record (56 bytes: zone, area, a flag, how many trigger pairs; +0x10
// the trigger list of {extra, record} pairs; +0x18 the enemy spawn group)
// gets a copy of its trigger list with one more pair in free ROM, a
// save pad record {class, class, x1, y1, x2, y2} as the game's own pads
// have, in free ROM. The game builds trigger objects when it loads the area,
// so the first time the player is found in the area after a start the area
// is entered again (a moment's fade). The circle itself is drawn here.
#include "lg2.h"
#include <string.h>

#define PAD_HALF 25                    // the game's pads are 50 x 50
#define SAVE_PAD_CLASS 0x0801ED13
#define SAVE_PAD_CLASS_2 0x0801F1ED

struct Camp {
	uint8_t zone, area;
	int16_t x, y;            // the circle's centre
	uint32_t areaRecordRom;  // the area record's place in the ROM
	uint32_t spawnCountRom;  // the area's enemy spawn count, to be zeroed (0: the enemies stay)
	uint32_t listRom;        // free: the new trigger list, then the pad record
};

static const struct Camp camps[] = {
	{ 13, 5, 252, 163, 0x530D8, 0x4DF8C + 0x40, 0x8F4000 }, // a snowy clearing: no enemies
	{ 3, 3, 92, 424, 0x4F6A8, 0, 0x8F4200 },                // the wasteland by the Cell Games arena
};
#define CAMPS ((int) (sizeof(camps) / sizeof(camps[0])))

static unsigned entered; // bit n: camp n entered again since the start

// Whatever enemy the game builds in the camp is sent far out of the way
// the moment it appears (and kept there), so none is ever seen or fought
#define FAR_AWAY (-640 * 256)
static void banishEnemies(uint32_t player) {
	uint32_t found[16];
	int n = lg2FindEnemies(player, found, 16), i;
	for (i = 0; i < n; ++i) {
		if ((int) modRead32(found[i] + LG2_ACTOR_WORLD_X) > FAR_AWAY + 256) {
			modWrite32(found[i] + LG2_ACTOR_WORLD_X, (uint32_t) FAR_AWAY);
			modWrite32(found[i] + LG2_ACTOR_WORLD_Y, (uint32_t) FAR_AWAY);
		}
	}
}

static void install(const struct Camp* camp) {
	uint8_t* rec = modRom(camp->areaRecordRom);
	uint32_t count = rec[3], list = *(uint32_t*) (rec + 0x10); // (byte 3 of the record: how many trigger pairs)
	uint8_t* copy = modRom(camp->listRom);
	uint8_t* pad;
	if (rec[0] != camp->zone || rec[1] != camp->area || (list >> 24) != 8 || count > 32 || list == 0x08000000 + camp->listRom) {
		return; // not as expected, or done already
	}
	memcpy(copy, modRom(list - 0x08000000), count * 8);
	pad = copy + (count + 1) * 8;
	pad += (uintptr_t) pad & 3 ? 4 - ((uintptr_t) pad & 3) : 0;
	*(uint32_t*) pad = SAVE_PAD_CLASS;
	*(uint32_t*) (pad + 4) = SAVE_PAD_CLASS_2;
	*(uint16_t*) (pad + 8) = (uint16_t) (camp->x - PAD_HALF);
	*(uint16_t*) (pad + 10) = (uint16_t) (camp->y - PAD_HALF);
	*(uint16_t*) (pad + 12) = (uint16_t) (camp->x + PAD_HALF);
	*(uint16_t*) (pad + 14) = (uint16_t) (camp->y + PAD_HALF);
	*(uint32_t*) (copy + count * 8) = 0;
	*(uint32_t*) (copy + count * 8 + 4) = 0x08000000 + camp->listRom + (uint32_t) (pad - copy);
	*(uint32_t*) (rec + 0x10) = 0x08000000 + camp->listRom;
	rec[3] = (uint8_t) (count + 1);
	if (camp->spawnCountRom) {
		*(uint32_t*) modRom(camp->spawnCountRom) = 0;
	}
}

// ---- drawing ----------------------------------------------------------------
// Which pixels of the screen a sprite covers this frame (the characters, the
// enemies, the beams: everything in the game's sprite table that has an
// opaque pixel there), so that the circle is laid over the floor only and
// whoever stands on it shows.
static const uint8_t objSizes[4][4][2] = {
	{ { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
	{ { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
	{ { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
	{ { 8, 8 }, { 8, 8 }, { 8, 8 }, { 8, 8 } },
};

static uint8_t covered[160][240];

static void findSprites(int left, int top, int right, int bottom) {
	int oneD = modRead16(0x04000000) & 0x40; // sprite tile mapping mode
	int i, y;
	for (y = top < 0 ? 0 : top; y < bottom && y < 160; ++y) {
		memset(covered[y] + (left < 0 ? 0 : left), 0, (size_t) ((right > 240 ? 240 : right) - (left < 0 ? 0 : left)));
	}
	for (i = 0; i < 128; ++i) {
		uint16_t a0 = modRead16(0x07000000 + i * 8), a1 = modRead16(0x07000002 + i * 8), a2 = modRead16(0x07000004 + i * 8);
		int w, h, x, bpp8, tile, step, tx, ty;
		if ((a0 & 0x300) == 0x200 || (a0 & 0x100)) {
			continue; // hidden, or rotated/scaled
		}
		w = objSizes[a0 >> 14][a1 >> 14][0];
		h = objSizes[a0 >> 14][a1 >> 14][1];
		y = a0 & 0xFF;
		x = a1 & 0x1FF;
		if (y >= 160) y -= 256;
		if (x >= 240) x -= 512;
		if (x >= right || x + w <= left || y >= bottom || y + h <= top) {
			continue;
		}
		bpp8 = (a0 >> 13) & 1;
		tile = a2 & 0x3FF;
		step = bpp8 ? 2 : 1;
		for (ty = 0; ty < h; ++ty) {
			for (tx = 0; tx < w; ++tx) {
				int sx = (a1 & 0x1000) ? w - 1 - tx : tx, sy = (a1 & 0x2000) ? h - 1 - ty : ty;
				int t = oneD ? tile + ((sy / 8) * (w / 8) + sx / 8) * step : tile + (sy / 8) * 32 + (sx / 8) * step;
				int c, px = x + tx, py = y + ty;
				if (px < left || px >= right || py < top || py >= bottom || px < 0 || px >= 240 || py < 0 || py >= 160) {
					continue;
				}
				if (bpp8) {
					c = modRead8(0x06010000 + t * 32 + (sy & 7) * 8 + (sx & 7));
				} else {
					c = modRead8(0x06010000 + t * 32 + (sy & 7) * 4 + (sx & 7) / 2);
					c = (sx & 1) ? c >> 4 : c & 15;
				}
				if (c) {
					covered[py][px] = 1;
				}
			}
		}
	}
}

// The circle, as the game draws its own (its picture taken from the one in
// Capsule Corp): laid over the floor, under whatever sprite is there
#include "generated/pad_image.inc"

static void drawPad(const struct Camp* camp, uint32_t player) {
	int camX = ((int) modRead32(player + LG2_ACTOR_WORLD_X) >> 8) - (int) modRead32(player + LG2_ACTOR_SCREEN_X);
	int camY = ((int) modRead32(player + LG2_ACTOR_WORLD_Y) >> 8) - (int) modRead32(player + LG2_ACTOR_SCREEN_Y);
	int left = camp->x - PAD_HALF + PAD_DX - camX, top = camp->y - PAD_HALF + PAD_DY - camY, x, y;
	uint32_t* screen = modScreen();
	if (left >= 240 || top >= 160 || left + PAD_W <= 0 || top + PAD_H <= 16) {
		return;
	}
	findSprites(left, top, left + PAD_W, top + PAD_H);
	for (y = 0; y < PAD_H; ++y) {
		for (x = 0; x < PAD_W; ++x) {
			int sx = left + x, sy = top + y;
			uint32_t c = padImage[y][x];
			if (!c || sx < 0 || sx >= 240 || sy < 16 || sy >= 160 || covered[sy][sx]) {
				continue;
			}
			screen[sy * 240 + sx] = c;
		}
	}
}

static void frame(void) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	int zone = modRead8(LG2_RAM_ZONE), area = modRead8(LG2_RAM_ZONE + 1), i;
	const struct Camp* camp = 0;
	for (i = 0; i < CAMPS; ++i) {
		if (camps[i].zone == zone && camps[i].area == area) {
			camp = camps + i;
		}
	}
	if (!camp || !lg2IsPlayer(player)) {
		return;
	}
	if (!(entered & (1U << (camp - camps))) && modCanCall() && !modPaused() && !lg2HubOpen) {
		entered |= 1U << (camp - camps);
		lg2GoTo(camp->zone, camp->area, (int) modRead32(player + LG2_ACTOR_WORLD_X) >> 8, (int) modRead32(player + LG2_ACTOR_WORLD_Y) >> 8);
		modLog("camp: area %d:%d entered again, for its new save pad", camp->zone, camp->area);
		return;
	}
	if (camp->spawnCountRom) {
		banishEnemies(player);
	}
	if (!modPaused()) {
		drawPad(camp, player);
	}
}

MOD_INIT(camp) {
	int i;
	for (i = 0; i < CAMPS; ++i) {
		install(camps + i);
	}
	modOnFrame(frame);
}
