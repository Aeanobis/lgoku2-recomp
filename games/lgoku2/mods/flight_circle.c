// Extra flight pads: glowing circles on the ground that carry you somewhere when
// you stand on one and press A. Each trip is made through the game's ordinary
// "walked into an exit" routine, so whatever you are carrying comes with you.
//
// To add one, add a line to the table: where it stands (zone, area, world
// position), where it goes (zone, area, position to arrive at). Pads that lead to the
// world map (zone 6 area 99) are blue, the others gold.
#include "lg2.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { RADIUS = 20, RADIUS_Y = 10, BEAM_HEIGHT = 34 };
enum { WORLD_MAP_ZONE = 6, WORLD_MAP_AREA = 99 };

static const struct Circle {
	uint8_t zone, area;
	int16_t x, y;
	uint8_t toZone, toArea;
	int16_t landX, landY;
} circles[] = {
	// The hatchery on top of the Northern Mountains (the egg quest's version of
	// it) down to where the path from above enters the foot of the mountain
	{ 8, 50, 288, 344, 8, 51, 448, 8 },
	// The wasteland canyon, out to the world map, just below this region's marker
	{ 7, 18, 240, 215, WORLD_MAP_ZONE, WORLD_MAP_AREA, 1776, 2440 },
	// The forest clearing, out to the world map (middle of the central continent)
	{ 10, 8, 200, 384, WORLD_MAP_ZONE, WORLD_MAP_AREA, 1500, 1500 },
};
#define CIRCLES (sizeof(circles) / sizeof(circles[0]))

// Made-up exits, one per circle, kept in the unused top of the ROM
#define EXIT_ROM 0x8F0000
#define EXIT_BYTES 64

// Mix a colour into a screen pixel; amount 0-256
static void blend(int x, int y, uint32_t color, int amount) {
	if (x < 0 || x >= 240 || y < 0 || y >= 160 || amount <= 0) {
		return;
	}
	if (amount > 256) {
		amount = 256;
	}
	uint32_t* p = &modScreen()[y * 240 + x];
	int r = *p & 0xFF, g = (*p >> 8) & 0xFF, b = (*p >> 16) & 0xFF;
	r += (((int) (color & 0xFF)) - r) * amount / 256;
	g += (((int) ((color >> 8) & 0xFF)) - g) * amount / 256;
	b += (((int) ((color >> 16) & 0xFF)) - b) * amount / 256;
	*p = r | (g << 8) | (b << 16);
}

// The pad lies on the ground, so whoever stands on it has to show on top of
// it; but a mod can only paint over the finished picture. So before painting,
// work out which pixels of the pad's box are covered by a sprite (from the
// sprite list and their graphics in video memory) and leave those alone.
enum { BOX_W = RADIUS * 2 + 3, BOX_H = RADIUS_Y * 2 + 3 };
static uint8_t covered[BOX_H][BOX_W];

static void findCovered(int left, int top) {
	static const uint8_t sizes[3][4][2] = {
		{ { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
		{ { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
		{ { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
	};
	int oneRow = modRead16(0x04000000) & 0x40; // tiles of a sprite follow one another, or sit in a 32-wide sheet
	int i, x, y;
	memset(covered, 0, sizeof(covered));
	for (i = 0; i < 128; ++i) {
		uint16_t a0 = modRead16(0x07000000 + i * 8), a1 = modRead16(0x07000002 + i * 8), a2 = modRead16(0x07000004 + i * 8);
		int shape = a0 >> 14;
		if ((a0 & 0x300) == 0x200 || shape == 3) {
			continue; // switched off
		}
		int w = sizes[shape][a1 >> 14][0], h = sizes[shape][a1 >> 14][1];
		int sx = a1 & 0x1FF, sy = a0 & 0xFF;
		if (sx >= 240) {
			sx -= 512;
		}
		if (sy >= 160) {
			sy -= 256;
		}
		if (sx >= left + BOX_W || sx + w <= left || sy >= top + BOX_H || sy + h <= top) {
			continue;
		}
		int deep = a0 & 0x2000; // 256 colours: a byte a pixel
		int rotated = a0 & 0x100, flipX = !rotated && (a1 & 0x1000), flipY = !rotated && (a1 & 0x2000);
		for (y = 0; y < h; ++y) {
			int by = sy + y - top;
			if (by < 0 || by >= BOX_H) {
				continue;
			}
			for (x = 0; x < w; ++x) {
				int bx = sx + x - left;
				if (bx < 0 || bx >= BOX_W || covered[by][bx]) {
					continue;
				}
				int px = flipX ? w - 1 - x : x, py = flipY ? h - 1 - y : y;
				int tile = (a2 & 0x3FF) + (oneRow ? (py / 8) * (w / 8) * (deep ? 2 : 1) + (px / 8) * (deep ? 2 : 1)
				                                  : (py / 8) * 32 + (px / 8) * (deep ? 2 : 1));
				uint32_t at = 0x06010000 + (tile & 0x3FF) * 32;
				int pixel = deep ? modRead8(at + (py & 7) * 8 + (px & 7))
				                 : (modRead8(at + (py & 7) * 4 + (px & 7) / 2) >> ((px & 1) * 4)) & 15;
				covered[by][bx] = pixel != 0;
			}
		}
	}
}

// A glowing pad lying on the ground: a soft disc of light, a bright rim, a
// ripple running outward, sparks circling the rim and a faint column of light
// rising from it. Blue-white leads to the world map, gold anywhere else.
static void draw(const struct Circle* c) {
	int sx = c->x - (int) modRead32(LG2_RAM_CAMERA_X);
	int sy = c->y - (int) modRead32(LG2_RAM_CAMERA_Y);
	int toMap = c->toZone == WORLD_MAP_ZONE && c->toArea == WORLD_MAP_AREA;
	uint32_t glow = toMap ? 0xFFC850 : 0x30C8FF;   // 0x00BBGGRR
	uint32_t bright = toMap ? 0xFFF8E0 : 0xD0FFFF;
	unsigned t = modFrame();
	float ripple = (t % 48) / 48.0f; // 0 at the middle, 1 at the rim
	int x, y, i;
	if (sx < -RADIUS - 2 || sx > 242 + RADIUS || sy < -RADIUS_Y - 2 || sy > 162 + RADIUS_Y + BEAM_HEIGHT) {
		return;
	}
	findCovered(sx - RADIUS - 1, sy - RADIUS_Y - 1);
	for (y = -RADIUS_Y - 1; y <= RADIUS_Y + 1; ++y) {
		for (x = -RADIUS - 1; x <= RADIUS + 1; ++x) {
			if (covered[y + RADIUS_Y + 1][x + RADIUS + 1]) {
				continue; // someone is standing here
			}
			// distance from the middle, 1.0 on the rim (the pad is a circle seen at a slant)
			float d = sqrtf((float) (x * x) / (RADIUS * RADIUS) + (float) (y * y) / (RADIUS_Y * RADIUS_Y));
			if (d > 1.12f) {
				continue;
			}
			if (d > 1.0f) {
				blend(sx + x, sy + y, 0x201000, 110);              // dark edge, so it sits on the ground
			} else if (d > 0.86f) {
				blend(sx + x, sy + y, bright, 230);                 // rim
			} else {
				blend(sx + x, sy + y, glow, 50 + (int) (70 * d));   // disc, brighter toward the rim
				float off = d - ripple * 0.86f;
				if (off > -0.07f && off < 0.07f) {
					blend(sx + x, sy + y, bright, (int) (170 * (1.0f - ripple)));
				}
			}
		}
	}
	// the column of light: strongest at the pad, gone a little above head height
	for (y = 1; y <= BEAM_HEIGHT; ++y) {
		int half = RADIUS - 3 - y / 6;
		int strength = 46 * (BEAM_HEIGHT - y) / BEAM_HEIGHT;
		for (x = -half; x <= half; ++x) {
			int flicker = ((x * 7 + (int) t / 3) & 7) == 0 ? 26 : 0; // thin brighter streaks drifting sideways
			blend(sx + x, sy - y - RADIUS_Y / 3, bright, strength * (half - abs(x) + 4) / (half + 4) + flicker * (BEAM_HEIGHT - y) / BEAM_HEIGHT);
		}
	}
	// sparks circling the rim
	for (i = 0; i < 6; ++i) {
		float angle = t * 0.045f + i * 1.0472f;
		int px = sx + (int) lroundf(cosf(angle) * RADIUS * 0.93f);
		int py = sy + (int) lroundf(sinf(angle) * RADIUS_Y * 0.93f);
		blend(px, py, 0xFFFFFF, 256);
		blend(px - 1, py, bright, 140);
		blend(px + 1, py, bright, 140);
		blend(px, py - 1, bright, 140);
		blend(px, py + 1, bright, 140);
	}
	// motes of light drifting up out of it
	for (i = 0; i < 5; ++i) {
		int life = (int) ((t + i * 13) % 64);
		int mx = sx + ((i * 37) % (RADIUS * 2 - 8)) - RADIUS + 4;
		blend(mx, sy - life / 2, 0xFFFFFF, 220 * (64 - life) / 64);
	}
}

// Standing on a pad and pressing A starts the trip. The game is first given a
// few frames with no buttons at all, so the character is standing still when
// it happens: leaving for the world map while he is mid-step crashes the game.
enum { BTN_A = 1, SETTLE_FRAMES = 4 };
static int onPad;    // standing on one: the game does not get A (it would throw a punch)
static int settling; // frames left before the trip starts, 0 = not leaving

static uint32_t filter(uint32_t keys) {
	return settling ? 0 : onPad ? keys & ~BTN_A : keys;
}

static void frame(void) {
	static uint32_t lastKeys;
	uint32_t pressed = modKeys() & ~lastKeys;
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	unsigned i;
	lastKeys = modKeys();
	onPad = 0;
	if (modPaused() || (player >> 24) != 2) {
		settling = 0;
		return;
	}
	for (i = 0; i < CIRCLES; ++i) {
		const struct Circle* c = &circles[i];
		if (modRead8(LG2_RAM_ZONE) != c->zone || modRead8(LG2_RAM_ZONE + 1) != c->area) {
			continue;
		}
		// where his feet are: the bottom middle of his sprite as drawn
		uint32_t frameRecord = modRead32(player + LG2_ACTOR_FRAME_RECORD);
		int w = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 2) : 16;
		int h = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 3) : 32;
		int px = (int) modRead32(player + LG2_ACTOR_SCREEN_X) + w / 2 + (int) modRead32(LG2_RAM_CAMERA_X);
		int py = (int) modRead32(player + LG2_ACTOR_SCREEN_Y) + h + (int) modRead32(LG2_RAM_CAMERA_Y);
		int standing = px > c->x - RADIUS + 3 && px < c->x + RADIUS - 3 && py > c->y - RADIUS_Y + 1 && py < c->y + RADIUS_Y + 6;
		draw(c);
		if (!standing) {
			continue;
		}
		onPad = 1;
		if (!settling && (pressed & BTN_A)) {
			settling = SETTLE_FRAMES;
		} else if (settling && --settling == 0 && modCanCall()) {
			// The routine only takes an exit while its direction is being held;
			// it reads that from the game's record of the buttons, so say "all"
			// for the length of the call.
			uint32_t held = modRead32(LG2_RAM_INPUT_PTR) + 18;
			uint16_t was = modRead16(held);
			modWrite16(held, 0xFF);
			modCall(LG2_FN_TAKE_EXIT | 1, 0x08000000 + EXIT_ROM + i * EXIT_BYTES, player, 0, 0);
			modWrite16(held, was);
			onPad = 0;
			return;
		} else if (settling == 0 && !modCanCall()) {
			settling = 1; // not a safe moment: try again next frame
		}
	}
	if (!onPad) {
		settling = 0;
	}
}

MOD_INIT(flight_circle) {
	unsigned i;
	for (i = 0; i < CIRCLES; ++i) {
		uint8_t* exit = modRom(EXIT_ROM + i * EXIT_BYTES);
		uint8_t* record = exit + 32;
		memset(exit, 0, EXIT_BYTES);
		exit[17] = 0xFF;
		*(uint32_t*) (exit + 20) = 0x08000000 + EXIT_ROM + i * EXIT_BYTES + 32;
		record[17] = circles[i].toZone;
		record[18] = circles[i].toArea;
		record[19] = 1;
		*(int16_t*) (record + 20) = circles[i].landX;
		*(int16_t*) (record + 22) = circles[i].landY;
	}
	modFilterKeys(filter);
	modOnFrame(frame);
}
