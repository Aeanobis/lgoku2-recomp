// Example: change each character's starting stats.
// To use it, copy this file into ../mods/ and run make.
//
// The game keeps one 28-byte record per playable character at ROM offset
// 0x2A868. Editing it does change the game: setting Trunks to hp 123 / maxHp 250
// turned his status screen from 149/199 into 331/331 at level 6. So the game
// derives what it shows from these numbers rather than using them 1:1, and the
// field names below are guesses from the values and from what the "Enhanced"
// hack changes. Working out the exact formula is the next step for a stats mod.
#include "mod.h"

struct StartStats {
	uint16_t hp, maxHp;
	uint16_t energy, maxEnergy;
	uint8_t unknown8;      // differs per character: 3, 6, 8, 18, 19, 21
	uint8_t unknown9;      // 3 for everyone
	uint8_t pad10;
	uint8_t strength;      // (?) three core stats, order not confirmed; vanilla Gohan is 3 / 5 / 3
	uint8_t pad12;
	uint8_t power;         // (?)
	uint8_t pad14;
	uint8_t endurance;     // (?)
	uint8_t rest[12];
};

// Order inferred from the HP values (85, 95, 105, 110, 100, 50)
enum { GOHAN, PICCOLO, VEGETA, TRUNKS, GOKU, HERCULE, CHARACTERS };

MOD_INIT(starting_stats) {
	struct StartStats* c = (struct StartStats*) modRom(0x2A868);
	c[GOHAN].hp = c[GOHAN].maxHp = 150;
	c[GOHAN].energy = c[GOHAN].maxEnergy = 60;
	c[GOHAN].strength = 6;
}
