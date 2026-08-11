#include <types.h>

s64 __attribute__((used)) __lshrdi3(s64 a, s32 b) {
	Halves h = {.full = a};
	if(b >= 32) {
		return h.high >> (b - 32);
	} else if(b) {
		h.low >>= b;
		h.low |= h.high << (32 - b);
		h.high >>= b;
	}
	return h.full;
}

s64 __attribute__((used)) __ashldi3(s64 a, s32 b) {
	Halves h = {.full = a};
	if(b >= 32) {
		h.high = h.low << (b - 32);
		h.low = 0;
	} else if(b) {
		h.high <<= b;
		h.high |= h.low >> (32 - b);
		h.low <<= b;
	}
	return h.full;
}

int __clzdi2(u64 x);

// https://github.com/glitchub/arith64/blob/master/arith64.c
// TODO: optimize this
u64 __attribute__((used)) __udivdi3(u64 a, u64 b) {
	Halves ah = {.full = a};
	Halves bh = {.full = b};
	if(!(bh.high | ah.high)) {
		return ah.low / bh.low;
	}
	// let's do long division
	char bits = __clzdi2(b) - __clzdi2(a) + 1;      // number of bits to iterate (a and b are non-zero)
	u64 rem = a >> bits;                	        // init remainder
	a <<= 64 - bits;                                // shift numerator to the high bit
	u64 wrap = 0;                                   // start with wrap = 0
	while(bits-- > 0) {                             // for each bit
		rem = (rem << 1) | (a >> 63);               // shift numerator MSB to remainder LSB
		a = (a << 1) | (wrap & 1);                  // shift out the numerator, shift in wrap
		wrap = (s64) (b - rem - 1) >> 63;           // wrap = (b > rem) ? 0 : 0xffffffffffffffff (via sign extension)
		rem -= b & wrap;                            // if (wrap) rem -= b
	}
	//if(c) *c = rem;                                 // maybe set remainder
	return (a << 1) | (wrap & 1);                   // return the quotient
}

s64 __attribute__((used)) __divdi3(s64 a, s64 b) {
	Halves ah = {.full = a};
	Halves bh = {.full = b};
	u32 sign = ah.high >> 31;
	if(sign) {
		ah.full = -ah.full;
	}
	if(bh.high >> 31) {
		bh.full = -bh.full;
		sign ^= 1;
	}
	ah.ufull /= bh.ufull;
	if(sign) {
		ah.full = -ah.full;
	}
	return ah.full;
}
