#ifndef TC_DRIVER_TEST_RUNNER_H
#define TC_DRIVER_TEST_RUNNER_H

#include <stddef.h>
#include <stdint.h>

int tc_test_runner_run(const char *const *test_names, size_t ntest_names,
                       uint64_t timeout_seconds, const char *timeout_text);

#endif
