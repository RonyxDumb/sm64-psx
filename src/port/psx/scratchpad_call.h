#pragma once
#include <types.h>

#if !defined(IDE) && defined(TARGET_PSX) && !defined(NO_SCRATCHPAD)

#define _OVERLOAD0TO4(_0, _1, _2, _3, _4, x, ...) x
#define _IF_ARG0(x, y, ...) _OVERLOAD0TO4(_0, ##__VA_ARGS__, x, x, x, x, y)
#define _IF_ARG1(x, y, ...) _OVERLOAD0TO4(_0, ##__VA_ARGS__, x, x, x, y, y)
#define _IF_ARG2(x, y, ...) _OVERLOAD0TO4(_0, ##__VA_ARGS__, x, x, y, y, y)
#define _IF_ARG3(x, y, ...) _OVERLOAD0TO4(_0, ##__VA_ARGS__, x, y, y, y, y)
#define _GET_ARG0(x, ...) x
#define _GET_ARG1(_0, x, ...) x
#define _GET_ARG2(_0, _1, x, ...) x
#define _GET_ARG3(_0, _1, _2, x, ...) x
#define _REMOVE_PARENS_INNER(...) __VA_ARGS__
#define _REMOVE_PARENS(x) _REMOVE_PARENS_INNER x
#define _ZERO(...) 0
#define scratchpad_call(fn, ...) ({\
	UNUSED __auto_type a0 = (_IF_ARG0(_GET_ARG0, _ZERO, ##__VA_ARGS__)(__VA_ARGS__));\
	UNUSED __auto_type a1 = (_IF_ARG1(_GET_ARG1, _ZERO, ##__VA_ARGS__)(__VA_ARGS__));\
	UNUSED __auto_type a2 = (_IF_ARG2(_GET_ARG2, _ZERO, ##__VA_ARGS__)(__VA_ARGS__));\
	UNUSED __auto_type a3 = (_IF_ARG3(_GET_ARG3, _ZERO, ##__VA_ARGS__)(__VA_ARGS__));\
	__builtin_choose_expr(__builtin_types_compatible_p(typeof((fn)(__VA_ARGS__)), void), ({\
		__asm__ volatile(\
			"move $s0, $sp\n" /* backup and change $sp to the end of scratchpad */ \
			"la $sp, 0x1F800400\n"\
			"move $s1, $ra\n" /* backup $ra */\
			_IF_ARG0("move $a0, %[a0]\n", , ##__VA_ARGS__)\
			_IF_ARG1("move $a1, %[a1]\n", , ##__VA_ARGS__)\
			_IF_ARG2("move $a2, %[a2]\n", , ##__VA_ARGS__)\
			_IF_ARG3("move $a3, %[a3]\n", , ##__VA_ARGS__)\
			"jalr %[fn_ptr]\n"\
			"move $sp, $s0\n" /* restore $sp and $ra */ \
			"move $ra, $s1\n"\
			:\
			: [fn_ptr]"c"(fn)\
				_REMOVE_PARENS(_IF_ARG0((, [a0]"r"(a0)), (), ##__VA_ARGS__))\
				_REMOVE_PARENS(_IF_ARG1((, [a1]"r"(a1)), (), ##__VA_ARGS__))\
				_REMOVE_PARENS(_IF_ARG2((, [a2]"r"(a2)), (), ##__VA_ARGS__))\
				_REMOVE_PARENS(_IF_ARG3((, [a3]"r"(a3)), (), ##__VA_ARGS__))\
			/* clobber all volatile registers due to the call, but also clobber $s0 and $s1 as backup locations, and clobber $ra to prevent it from being assigned to fn_ptr */\
			: "$ra", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3", "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "$t8", "$t9", "$s0", "$s1", "memory"\
		);\
	}), ({\
		typeof(__builtin_choose_expr(__builtin_types_compatible_p(typeof((fn)(__VA_ARGS__)), void), 0, (fn)(__VA_ARGS__))) retval;\
		__asm__ volatile(\
			"move $s0, $sp\n" /* backup and change $sp to the end of scratchpad */ \
			"la $sp, 0x1F800400\n"\
			"move $s1, $ra\n" /* backup $ra */\
			_IF_ARG0("move $a0, %[a0]\n", , ##__VA_ARGS__)\
			_IF_ARG1("move $a1, %[a1]\n", , ##__VA_ARGS__)\
			_IF_ARG2("move $a2, %[a2]\n", , ##__VA_ARGS__)\
			_IF_ARG3("move $a3, %[a3]\n", , ##__VA_ARGS__)\
			"jalr %[fn_ptr]\n"\
			"move $sp, $s0\n" /* restore $sp and $ra */ \
			"move $ra, $s1\n"\
			"move %[retval], $v0\n"\
			: [retval]"=r"(retval)\
			: [fn_ptr]"c"(fn)\
				_REMOVE_PARENS(_IF_ARG0((, [a0]"r"(a0)), (), ##__VA_ARGS__))\
				_REMOVE_PARENS(_IF_ARG1((, [a1]"r"(a1)), (), ##__VA_ARGS__))\
				_REMOVE_PARENS(_IF_ARG2((, [a2]"r"(a2)), (), ##__VA_ARGS__))\
				_REMOVE_PARENS(_IF_ARG3((, [a3]"r"(a3)), (), ##__VA_ARGS__))\
			/* clobber all volatile registers due to the call, but also clobber $s0 and $s1 as backup locations, and clobber $ra to prevent it from being assigned to fn_ptr */\
			: "$ra", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3", "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "$t8", "$t9", "$s0", "$s1", "memory"\
		);\
		retval;\
	}));\
})

#else

#warning scratchpad_call is off!!
#define scratchpad_call(fn, ...) (fn)(__VA_ARGS__)

#endif
