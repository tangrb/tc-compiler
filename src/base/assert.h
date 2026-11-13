/* TC 0.0.46 contract: compile-time assertions.
 *
 * Contract structures are frozen for S0.  The macro form stays portable so
 * public headers can still be consumed from C99 translation units; the
 * project itself builds as C11. */
#ifndef TC_BASE_ASSERT_H
#define TC_BASE_ASSERT_H

#define TC_STATIC_ASSERT_JOIN_(a, b) a##b
#define TC_STATIC_ASSERT_JOIN(a, b) TC_STATIC_ASSERT_JOIN_(a, b)

/* cond must be a compile-time integer constant expression. */
#define TC_STATIC_ASSERT(cond, line) \
    typedef char TC_STATIC_ASSERT_JOIN(tc_static_assert_, line)[(cond) ? 1 : -1]

#endif /* TC_BASE_ASSERT_H */
