#ifndef CGROUP_H
#define CGROUP_H

#include <stdbool.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

int cg_create_cgroup(const char *root,
                     const char *controller,
                     const char *path);

int cg_remove_cgroup(const char *root,
                     const char *controller,
                     const char *path);

int cg_write_intlist(const char *root,
                     const char *controller,
                     const char *path,
                     const char *param,
                     int *values, int length);

__attribute__((format (printf, 5, 6)))
int cg_write_string(const char *root, 
                    const char *controller, 
                    const char *path,
                    const char *param, const char *fmt, ...);

int cg_write_bool(const char *root,
                  const char *controller,
                  const char *path,
                  const char *param, bool value);

int cg_read_int(const char *root,
                const char *controller,
                const char *path,
                const char *param, int *value_in);

int cg_read_string(const char *root,
                   const char *controller,
                   const char *path,
                   const char *param, char **value_in);

int cg_read_intlist(const char *root,
                    const char *controller,
                    const char *path,
                    const char *param, int **value_in, size_t *length_in);

#if defined(__cplusplus)
};
#endif

#endif
