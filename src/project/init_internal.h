#ifndef TC_PROJECT_INIT_INTERNAL_H
#define TC_PROJECT_INIT_INTERNAL_H

#include <stddef.h>
#include <sys/types.h>

#include "project/init.h"

/* Private fault-injection seam.  create_file must use
 * O_CREAT|O_EXCL|O_NOFOLLOW; open_dir must not follow a final symlink. */
typedef struct {
    int (*mkdir_entry)(int parent_fd, const char *name, mode_t mode);
    int (*create_file)(int parent_fd, const char *name, mode_t mode);
    int (*open_dir)(int parent_fd, const char *name);
    ssize_t (*write_fd)(int fd, const void *data, size_t length);
    int (*close_fd)(int fd);
    int (*remove_file)(int parent_fd, const char *name);
    int (*remove_dir)(int parent_fd, const char *name);
} TcProjectInitOps;

TcImplError tc_project_init_with_ops(const char *project_name, FILE *stdout,
                                     FILE *stderr, const TcProjectInitOps *ops);

#endif
