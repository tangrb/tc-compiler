/* TC 0.0.46 contract: compile-time assertions (C99 compatible).
 *
 * Contract structures are frozen for S0.  These macros are the portable
 * substitute for C11 _Static_assert because the project builds as C99. */
#ifndef TC_TC_ASSERT_H
#define TC_TC_ASSERT_H

#define TC_STATIC_ASSERT_JOIN_(a, b) a##b
#define TC_STATIC_ASSERT_JOIN(a, b) TC_STATIC_ASSERT_JOIN_(a, b)

/* cond must be a compile-time integer constant expression. */
#define TC_STATIC_ASSERT(cond, line) \
    typedef char TC_STATIC_ASSERT_JOIN(tc_static_assert_, line)[(cond) ? 1 : -1]

#endif /* TC_TC_ASSERT_H */
