#include <engine/math_util.h>

typedef union {
	u32 absolute: 31;
	struct {
		u32 significand: 23;
		u32 exponent: 8;
		u32 sign: 1;
	};
	f32 f;
	u32 u;
} Decomposed;

#define HIDDEN_ONE ((u32) (1 << 23))
#define SIGN_BIT ((u32) (1 << 31))
#define EXPONENT_MASK ((u32) (0xFF << 23))
#define SIGNIFICAND_MASK ((u32) ((1 << 23) - 1))
#define ABSOLUTE_MASK (EXPONENT_MASK | SIGNIFICAND_MASK)
#define BITS(x) ((union {float f; unsigned u;}) {.f = (x)}.u)
#define FROM_BITS(x) ((union {float f; unsigned u;}) {.u = (x)}.f)
#define IS_ZERO(fd) (!fd.exponent)
#define IS_NAN(fd) (fd.exponent == 0xFF)

#ifndef TARGET_PSX
float qtof(q32 x) {
#ifdef USE_FLOATS
	return (float) x / (float) QONE;
#else
	if(x == 0) {
		return 0;
	}
	s32 absx = x < 0? -x: x;
	int leading_zeros = __builtin_clz(absx) + 1;
	u32 sig = (s32) ((u32) absx << leading_zeros);
	s32 exp = 127 + 32 - 12 - leading_zeros;
	return (Decomposed) {
		.sign = x >> 31,
		.exponent = exp,
		.significand = sig >> 9
	}.f;
#endif
}

q32 ftoq(float x) {
#ifdef USE_FLOATS
	return (q32) (x * QONE);
#else
	Decomposed f = {.f = x};
	int exp = (int) f.exponent - (127 - 12); // leave 12 fractional bits
	if(exp < 0) {
		return 0;
	} else if(exp > 30) {
		return 0x80000000;
	}
	u32 sig = f.significand | HIDDEN_ONE;
	sig <<= 8;
	sig >>= (31 - exp);
	return f.sign? -(s32) sig: (s32) sig;
#endif
}
#endif

// float __attribute__((used)) __addsf3(float x, float y) {
// 	u32 xb = BITS(x), yb = BITS(y);
// 	u32 absx = xb & ~SIGN_BIT;
// 	u32 absy = yb & ~SIGN_BIT;
// 	u32 sign = xb & SIGN_BIT;
// 	if(absx < absy) {
// 		absx = yb & ~SIGN_BIT;
// 		absy = xb & ~SIGN_BIT;
// 		sign = yb & SIGN_BIT;
// 	}
// 	s32 expy = absy >> 23;
// 	s32 expx = absx >> 23;
// 	if(expy == 0) {
// 		return (Decomposed) {.u = absx | sign}.f;
// 	}
// 	u32 sigx = (absx & SIGNIFICAND_MASK) | HIDDEN_ONE;
// 	u32 sigy = (absy & SIGNIFICAND_MASK) | HIDDEN_ONE;
// 	sigy >>= expx - expy;
// 	u32 sig = sigx + sigy;
// 	if((s32) (xb ^ yb) < 0) {
// 		sig = sigx - sigy;
// 		if(sigy == sigx) {
// 			return 0;
// 		}
// 	}
// 	u32 leading_zeros = __builtin_clz(sig);
// 	expx += 8;
// 	expx -= leading_zeros;
// 	return FROM_BITS(sign | (expx << 23) | (sig << 1 << leading_zeros >> 9));
// }

// float __attribute__((used)) __addsf3x(float x, float y) {
// 	u32 xb = BITS(x), yb = BITS(y);
// 	u32 absx = xb << 1;
// 	u32 expx = absx >> 24;
// 	if(expx == 0) return y;
// 	u32 absy = yb << 1;
// 	u32 expy = absy >> 24;
// 	if(expy == 0) return x;
// 	u32 sigx = (u32) (xb << 8 | SIGN_BIT) >> 8;
// 	u32 sigy = (u32) (yb << 8 | SIGN_BIT) >> 8;
// 	u32 sign;
// 	s32 inputsxor = xb ^ yb;
// 	u32 sig;
// 	int cond = absx < absy;
// 	if(cond != 0) goto addrhslarger;
// 	{
// 		sign = xb & SIGN_BIT;
// 		sigy >>= expx - expy;
// 		if(inputsxor >= 0) goto addaddition;
// 		sig = sigx - sigy;
// 		goto addmerge;
// 	}
// 	addrhslarger: {
// 		sign = yb & SIGN_BIT;
// 		sigx >>= expy - expx;
// 		expx = expy;
// 		if(inputsxor >= 0) goto addaddition;
// 		sigx = -sigx;
// 	}
// 	addaddition:
// 	sig = sigx + sigy;
// 	addmerge:
// 	u32 leading_zeros = __builtin_clz(sig);
// 	if(sig <= 0) return 0;
// 	sig <<= 1;
// 	expx += 8;
// 	expx -= leading_zeros;
// 	expx <<= 23;
// 	sig <<= leading_zeros;
// 	sig >>= 9;
// 	if((s32) expx <= 0) return 0;
// 	if((s32) sig <= 0) return 0;
// 	return FROM_BITS(sign | expx | sig);
// }

// float __attribute__((used)) __subsf3(float x, float y) {
// 	return __addsf3(x, -y);
// }

// float __attribute__((used)) __mulsf3(float x, float y) {
// 	// multiplication: check special cases, sum exponents, multiply significands, normalize
// 	u32 xb = BITS(x), yb = BITS(y);
// 	// extract significands
// 	u64 sigfull = (u64) ((xb & SIGNIFICAND_MASK) | HIDDEN_ONE) * ((yb & SIGNIFICAND_MASK) | HIDDEN_ONE);

// 	// extract the exponents, early return if one of them is 0
// 	u32 exp0 = (xb & EXPONENT_MASK);
// 	u32 exp1 = (yb & EXPONENT_MASK);
// 	if(exp0 == 0 || exp1 == 0) return 0;
// 	// sum the exponents
// 	u32 new_exp = exp0 + exp1;
// 	new_exp >>= 23;
// 	new_exp -= 119;

// 	u32 sighi = sigfull >> 32;
// 	u32 sig = sigfull;
// 	sig >>= 23;
// 	sighi <<= 9;
// 	sig |= sighi;
// 	//u32 sig = sigfull << 9;

// 	u32 leading_zeros = __builtin_clz(sig); // to normalize the result
// 	sig <<= 1;
// 	sig <<= leading_zeros;
// 	sig >>= 9;

// 	new_exp -= leading_zeros;
// 	new_exp <<= 23;
// 	return FROM_BITS(((xb ^ yb) & SIGN_BIT) | new_exp | sig);
// }

// float __attribute__((used)) __divsf3(float x, float y) {
// 	// division: check special cases, subtract exponents, divide significands, normalize
// 	u32 xb = BITS(x), yb = BITS(y);
// 	u32 sig0 = (xb & SIGNIFICAND_MASK) | HIDDEN_ONE;
// 	u32 sig1 = (yb & SIGNIFICAND_MASK) | HIDDEN_ONE;
// 	u32 quo = (u32) (sig0 << 8u) / (u32) (sig1 >> 8u);
// 	u32 sig = quo << 8u;
// 	s32 leading_zeros = __builtin_clz(sig); // to normalize the result
// 	u32 exp0 = (xb & EXPONENT_MASK) >> 23;
// 	u32 exp1 = (yb & EXPONENT_MASK) >> 23;
// 	s32 new_exp = (127 + 7) + exp0 - exp1 - leading_zeros;
// 	if(new_exp <= 0) return 0;
// 	return (Decomposed) {
// 		.sign = (xb ^ yb) >> 31,
// 		.exponent = new_exp,
// 		.significand = sig << 1 << leading_zeros >> 9
// 	}.f;
// }

// int __attribute__((used)) __cmpsf2(float x, float y) {
// 	u32 xb = BITS(x), yb = BITS(y);
// 	if((xb & EXPONENT_MASK) == 0) {
// 		xb = 0;
// 	}
// 	if((yb & EXPONENT_MASK) == 0) {
// 		yb = 0;
// 	}
// 	if(xb == yb) {
// 		return 0;
// 	}
// 	if((s32) xb < 0) {
// 		if((s32) yb < 0) {
// 			if((xb & ABSOLUTE_MASK) < (yb & ABSOLUTE_MASK)) {
// 				return -1;
// 			} else {
// 				return 1;
// 			}
// 			return yb - xb;
// 		} else {
// 			return -1;
// 		}
// 	} else {
// 		if((s32) yb < 0) {
// 			return 1;
// 		} else {
// 			if(xb < yb) {
// 				return -1;
// 			} else {
// 				return 1;
// 			}
// 			return xb - yb;
// 		}
// 	}
// }

// int __attribute__((used)) __eqsf2(float x, float y) {return __cmpsf2(x, y);}
// int __attribute__((used)) __nesf2(float x, float y) {return __cmpsf2(x, y);}
// int __attribute__((used)) __ltsf2(float x, float y) {return __cmpsf2(x, y);}
// int __attribute__((used)) __lesf2(float x, float y) {return __cmpsf2(x, y);}
// int __attribute__((used)) __gtsf2(float x, float y) {return __cmpsf2(x, y);}
// int __attribute__((used)) __gesf2(float x, float y) {return __cmpsf2(x, y);}

int __attribute__((used)) __fixsfsi(float x) {
	Decomposed f = {.f = x};
	int exp = (int) f.exponent - 127;
	if(exp < 0) {
		return 0;
	} else if(exp > 30) {
		return 0x80000000;
	}
	int res = 1 << exp;
	if(exp < 23) {
		res |= f.significand >> (23 - exp);
	} else {
		res |= f.significand << (exp - 23);
	}
	if(f.sign) {
		res = -res;
	}
	return res;
}

unsigned __attribute__((used)) __fixunssfsi(float x) {
	return __fixsfsi(x);
}

float __attribute__((used)) __floatsisf(int x) {
	if(x == 0) {
		return 0;
	}
	s32 absx = x < 0? -x: x;
	int leading_zeros = __builtin_clz(absx) + 1;
	u32 sig = absx << leading_zeros;
	return (Decomposed) {
		.sign = x >> 31,
		.exponent = 127 + 32 - leading_zeros,
		.significand = sig >> 9
	}.f;
}

float __attribute__((used)) __floatunsisf(unsigned x) {
	if(x == 0) {
		return 0;
	}
	int leading_zeros = __builtin_clz(x) + 1;
	u32 sig = x << leading_zeros;
	return (Decomposed) {
		.sign = 0,
		.exponent = 127 + 32 - leading_zeros,
		.significand = sig >> 9
	}.f;
}

float __attribute__((used)) __powisf2(float x, int y) {
	if(y == 0) return 1;
	float res = x;
	for(int i = 1; i < y; i++) {
		res = res * x;
	}
	return res;
}

//float sinf(float x) {
//	return sins(x * (32768 / M_PI));
//}

//float cosf(float x) {
//	return coss(x * (32768 / M_PI));
//}

float sqrtf(float x) {
	union {float f; uint32_t i;} val = {.f = x};
	val.i = (val.i >> 1) + ((1 << 29) - (1 << 22) - 0x4B0D2);
	return val.f;
}

[[gnu::flatten]] float rsqrtf(float number) {
#if 1
	return recipf(sqrtf(number));
#else
	union {
		float f;
		uint32_t i;
	} conv = {.f = number};
#if 1
	conv.i = 0x5F1FFFF9 - (conv.i >> 1);
	conv.f *= 0.703952253f * (2.38924456f - number * conv.f * conv.f);
#else
	conv.i = 0x5F37642F - (conv.i >> 1);
#endif
	return conv.f;
#endif
}

[[gnu::always_inline]] inline float recipf(float x) {
	// it may seem weird to do this instead of an approximation,
	// but the reality is, the approximation requires 2 multiplications
	// which, with my soft float implementation, is slower than a single division
	return 1.f / x;
	//u32 bits = BITS(x);
	//bits &= ~SIGN_BIT;
	//float guess = FROM_BITS(0x7EF127EA - bits);
	//return val.f * 2 - val.f * absx;
	//return val.f;
}

#if 0 && !defined(TARGET_PSX)
float absf(float x);

#include <stdlib.h>
#define TESTCOUNT 200000
#define RANGE 1000
#define EPSILON 0.01f
#ifdef TARGET_PSX
//#define TESTONEONE(f, op, x, y) printf("%d.%04u " #op " %d.%04u = %d.%04u\n", (int) x, (int) ((x - (int) x) * 10000) % 10000, (int) y, (int) ((y - (int) y) * 10000) % 10000, (int) f(x, y), (int) ((f(x, y) - (int) f(x, y)) * 10000) % 10000)
#define TESTONEONE(f, op, x, y) if(absf(f(x, y) - (float) (x op y)) > EPSILON) {printf("FAILED %d " #op " %d = %d (expected %d)\n", (int) x, (int) y, (int) f(x, y), (int) (float) (x op y)); failed++;} else {passed++;}
#else
//#define TESTONEONE(f, op, x, y) if(absf(f(x, y) - (float) (x op y)) > EPSILON) {printf("FAILED %f " #op " %f = %f (expected %f)\n", x, y, f(x, y), (float) (x op y)); failed++;} else {printf("PASSED %f " #op " %f = %f (expected %f)\n", x, y, f(x, y), (float) (x op y)); passed++;}
#define TESTONEONE(f, op, x, y) if(absf(f(x, y) - (float) (x op y)) > EPSILON) {printf("FAILED %f " #op " %f = %f (expected %f)\n", x, y, f(x, y), (float) (x op y)); failed++;} else {passed++;}
#endif
#define TESTONE(f, op, x, y) TESTONEONE(f, op, x, y); TESTONEONE(f, op, -x, y); TESTONEONE(f, op, x, -y); TESTONEONE(f, op, -x, -y)
#ifdef TARGET_PSX
//#define TESTONEONEB(f, op, x, y) printf("%d.%04u " #op " %d.%04u = %s\n", (int) x, (int) ((x - (int) x) * 10000) % 10000, (int) y, (int) ((y - (int) y) * 10000) % 10000, f(x, y)? "true": "false")
#define TESTONEONEB(f, op, x, y) if(f(x, y) != (int) (x op y)) {printf("FAILED %d " #op " %d = %d (expected %d)\n", (int) x, (int) y, f(x, y), (int) (x op y)); failed++;} else {passed++;}
#else
//#define TESTONEONEB(f, op, x, y) if(f(x, y) != (int) (x op y)) {printf("FAILED %f " #op " %f = %d (expected %d)\n", x, y, f(x, y), (int) (x op y)); failed++;} else {printf("PASSED %f " #op " %f = %d (expected %d)\n", x, y, f(x, y), (int) (x op y)); passed++;}
#define TESTONEONEB(f, op, x, y) if(f(x, y) != (int) (x op y)) {printf("FAILED %f " #op " %f = %d (expected %d)\n", x, y, f(x, y), (int) (x op y)); failed++;} else {passed++;}
#endif
#define TESTONEB(f, op, x, y) TESTONEONEB(f, op, x, y); TESTONEONEB(f, op, -x, y); TESTONEONEB(f, op, x, -y); TESTONEONEB(f, op, -x, -y)
#define TEST(f, op) TESTONE(f, op, 0.f, 0.f); TESTONE(f, op, 1.f, 2.f); TESTONE(f, op, 1000.f, 0.1f); TESTONE(f, op, 0.1f, 0.1f); TESTONE(f, op, 1.f, 1.f); TESTONE(f, op, 100.f, 1002.3f); TESTONE(f, op, 24.f, 132.0f)
#define TESTR(f, op) {for(int i = 0; i < TESTCOUNT; i++) {float x = (float) rand() / ((float) RAND_MAX / RANGE) - RANGE / 2; float y = (float) rand() / ((float) RAND_MAX / RANGE) - RANGE / 2; TESTONEONE(f, op, x, y);}}
#define TESTB(f, op) TESTONEB(f, op, 0.f, 0.f); TESTONEB(f, op, 1.f, 2.f); TESTONEB(f, op, 1000.f, 0.1f); TESTONEB(f, op, 0.1f, 0.1f); TESTONEB(f, op, 1.f, 1.f); TESTONEB(f, op, 100.f, 1002.3f); TESTONEB(f, op, 24.f, 132.0f)
#define TESTBR(f, op) {for(int i = 0; i < TESTCOUNT; i++) {float x = (float) rand() / ((float) RAND_MAX / RANGE) - RANGE / 2; float y = (float) rand() / ((float) RAND_MAX / RANGE) - RANGE / 2; TESTONEONEB(f, op, x, y);}}
#ifdef TARGET_PSX
//#define TESTONEUN(f, op, x) printf(#op "%d.%04u = %d.%04u\n", (int) x, (int) ((x - (int) x) * 10000) % 10000, (int) f(x), (int) ((f(x) - (int) f(x)) * 10000) % 10000)
#define TESTONEUN(f, op, x) if(absf(f(x) - (op x)) > EPSILON) {printf("FAILED " #op "%d = %d (expected %d)\n", (int) x, (int) f(x), (int) op x); failed++;} else {passed++;}
#else
//#define TESTONEUN(f, op, x) if(absf(f(x) - (op x)) > EPSILON) {printf("FAILED " #op "%f = %f (expected %f)\n", x, f(x), op x); failed++;} else {printf("PASSED " #op "%f = %f (expected %f)\n", x, f(x), op x); passed++;}
#define TESTONEUN(f, op, x) if(absf(f(x) - (op x)) > EPSILON) {printf("FAILED " #op "%f = %f (expected %f)\n", x, f(x), op x); failed++;} else {passed++;}
#endif
//#define TESTONEUN(f, op, x) printf(#op "%d = %d\n", q(x), q(f(x)))
#define TESTUNPOS(f, op) TESTONEUN(f, op, 0.f); TESTONEUN(f, op, 1.f); TESTONEUN(f, op, 1000.f); TESTONEUN(f, op, 0.1f); TESTONEUN(f, op, 0.3f); TESTONEUN(f, op, 10.2f)
#define TESTUNNEG(f, op) TESTONEUN(f, op, -0.f); TESTONEUN(f, op, -1.f); TESTONEUN(f, op, -1000.f); TESTONEUN(f, op, -0.1f); TESTONEUN(f, op, -0.3f); TESTONEUN(f, op, -10.2f)
#define TESTUN(f, op) TESTUNPOS(f, op); TESTUNNEG(f, op)
#define TESTUNR(f, op) {for(int i = 0; i < TESTCOUNT; i++) {float x = (float) rand() / ((float) RAND_MAX / RANGE) - RANGE / 2; TESTONEUN(f, op, x);}}
#define UTESTUNR(f, op) {for(int i = 0; i < TESTCOUNT; i++) {float x = (float) rand() / ((float) RAND_MAX / RANGE); TESTONEUN(f, op, x);}}
#ifdef TARGET_PSX
#include <ps1/cop0.h>
#endif
int main() {
#ifdef TARGET_PSX
	initSerialIO(115200);
	cop0_setReg(COP0_SR, COP0_SR_CU2);
#endif
	//printf("%u\n", __builtin_clz(24));
	//printf("%u\n", __builtin_clz(128));
	//printf("%u\n", __builtin_clz(0));
	//Decomposed d = {.f = 0.56f};
	//printf("%u %u %u\n", d.sign, d.exponent, d.significand);
	int passed = 0, failed = 0;
	//TESTR(__addsf3, +);
	//TESTR(__subsf3, -);
	//TESTR(__mulsf3, *);
	//TESTR(__divsf3, /);
	//TESTBR(__eqsf2, ==);
	//TESTBR(__nesf2, !=);
	//TESTBR(__gtsf2, >);
	//TESTBR(__gesf2, >=);
	//TESTBR(__ltsf2, <);
	//TESTBR(__lesf2, <=);
	//TESTUNR(__floatunsisf, (float) (unsigned));
	//TESTUNR(__fixunssfsi, (unsigned));
	TESTUNR(__floatsisf, (float) (int));
	{
		for(int i = 0; i < TESTCOUNT; i++) {
			//float x = (float) rand() / ((float) RAND_MAX / RANGE) - RANGE / 2;
			int x = rand() % RANGE - RANGE / 2;
			if(absf(qtof(x) - (float) x / (float) QONE) > EPSILON) {
				printf("FAILED qtof %d = %f (expected %f)\n", x, qtof(x), (float) x / (float) QONE);
				failed++;
			} else {
				printf("PASSED qtof %d = %f (expected %f)\n", x, qtof(x), (float) x / (float) QONE);
				passed++;
			}
		}
	}
	//TESTUNR(__fixsfsi, (int));
	//TESTONEONE(__divsf3, /, 1.f, 0.f);
	printf("passed %d/%d tests\n", passed, passed + failed);
	return 0;
}
#endif
