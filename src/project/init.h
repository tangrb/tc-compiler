#ifndef TC_PROJECT_INIT_H
#define TC_PROJECT_INIT_H

#include <stdio.h>

#include "diagnostic/diagnostic.h"

/* Initialize the canonical current directory.  The caller maps the returned
 * implementation error with tc_impl_error_exit(). */
TcImplError tc_project_init(const char *project_name, FILE *stdout, FILE *stderr);

#endif
