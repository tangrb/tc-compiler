/* TC 0.0.46 freestanding runtime platform seams (design §7.2/§8).
 *
 * The target runtime core delegates the environment-defined parts of the
 * platform instance to these functions:
 *   - program stdout (the language's write/writeln sink);
 *   - program stdin (a short-read byte source; EOF is reported separately);
 *   - the exit-record channel, kept separate from program stdout;
 *   - image termination with the profile's exit status.
 *
 * A platform implementation is freestanding: no libc, no host loader and no
 * device-side import resolution.  The build selects the word size from the
 * target pointer width; the two supported profiles are W=64 (AArch64) and
 * W=32 (ARM32).
 */
#ifndef TC_BACKEND_COMMON_RT_PLATFORM_H
#define TC_BACKEND_COMMON_RT_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#if UINTPTR_MAX == UINT64_MAX
#define TC_RT_W 64u
#elif UINTPTR_MAX == UINT32_MAX
#define TC_RT_W 32u
#else
#error "TC runtime requires a 32-bit or 64-bit target"
#endif

/* Program stdout: all bytes are committed or the operation fails. */
void tc_rt_platform_stdout(const char *bytes, size_t size);

/* Program stdin.  Reads at most capacity bytes, may return fewer, and sets
 * *n = 0 with *eof = 1 at end of input.  A nonzero return is an I/O error.
 * A platform may return a short read even when more input follows. */
int tc_rt_platform_read(char *bytes, size_t capacity, size_t *n, int *eof);

/* Exit-record channel.  It must not mix with program stdout. */
void tc_rt_platform_record(const char *bytes, size_t size);

/* Terminate the image with the environment-defined status.  Never returns. */
__attribute__((noreturn)) void tc_rt_platform_exit(uint32_t status);

#endif /* TC_BACKEND_COMMON_RT_PLATFORM_H */
