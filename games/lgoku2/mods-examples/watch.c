// Development aid: find the code that changes a value.
// Copy into ../mods/, run make, then start the game with e.g.
//   LG2_WATCH=0300110C:2 ./lgoku2      (address:bytes, here Trunks' current HP)
// Every write is printed with the address of the instruction that made it.
#include "mod.h"
#include <stdio.h>
#include <stdlib.h>

static void hit(uint32_t addr, uint32_t value, int bytes, uint32_t pc, const uint32_t* r) {
	static uint32_t lastPc, repeats;
	if (pc == lastPc && ++repeats > 3) {
		return; // same instruction again and again: seen enough
	}
	if (pc != lastPc) {
		repeats = 0;
	}
	lastPc = pc;
	fprintf(stderr, "frame %u: [%08X] = %X (%d bytes) by %08X, return address %08X, old value %X\n",
	        modFrame(), addr, value, bytes, pc, r[14] & ~1U, bytes == 1 ? modRead8(addr) : bytes == 2 ? modRead16(addr) : modRead32(addr));
}

MOD_INIT(watch) {
	const char* spec = getenv("LG2_WATCH");
	if (spec) {
		char* end;
		uint32_t addr = strtoul(spec, &end, 16);
		modWatchWrites(addr, *end == ':' ? strtoul(end + 1, NULL, 10) : 1, hit);
	}
}
