#include "cgroup.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>

#include "util.h"

int cg_create_cgroup(const char *root,
                     const char *controller,
                     const char *path) {
    char file_path[256];
    snprintf(file_path, sizeof file_path, "%s/%s/%s", root, controller, path);

    return mkdir(file_path, 01777);
}

int cg_remove_cgroup(const char *root,
                     const char *controller,
                     const char *path) {
    char file_path[256];
    snprintf(file_path, sizeof file_path, "%s/%s/%s", root, controller, path);

    return rmdir(file_path);
}

int cg_write_intlist(const char *root,
                     const char *controller,
                     const char *path,
                     const char *param,
                     int *values, int length) {
    char file_path[256];
    snprintf(file_path, sizeof file_path, "%s/%s/%s/%s", root, controller, path, param);

    FILE *fp = fopen(file_path, "w");
    int err = 0;

    if (!fp)
        return -1;

    char buf[8192];

    intlist_to_string(values, length, buf, sizeof buf, ",");

    if (fprintf(fp, "%s", buf) != (int) strlen(buf))
        err = errno;

    if (fclose(fp) < 0)
        err = errno;

    errno = err;
    return errno ? -1 : 0;
}

int cg_write_string(const char *root, 
                    const char *controller, 
                    const char *path,
                    const char *param, const char *fmt, ...) {
    char file_path[256];
    snprintf(file_path, sizeof file_path, "%s/%s/%s/%s", root, controller, path, param);
    va_list args;

    FILE *fp = fopen(file_path, "w");
    int err = 0;

    if (!fp)
        return -1;

    va_start(args, fmt);
    if (vfprintf(fp, fmt, args) < 0)
        err = errno;

    va_end(args);
    if (fclose(fp) < 0)
        err = errno;

    errno = err;
    return errno ? -1 : 0;
}

int cg_write_bool(const char *root,
                  const char *controller,
                  const char *path,
                  const char *param, bool value) {
    char file_path[256];
    snprintf(file_path, sizeof file_path, "%s/%s/%s/%s", root, controller, path, param);

    FILE *fp = fopen(file_path, "w");
    int err = 0;

    if (!fp)
        return -1;

    if (fprintf(fp, "%d", value) < 0)
        err = errno;

    if (fclose(fp) < 0)
        err = errno;

    errno = err;
    return errno ? -1 : 0;
}

int cg_read_int(const char *root,
                const char *controller,
                const char *path,
                const char *param, int *value_in) {
    char file_path[256];
    snprintf(file_path, sizeof file_path, "%s/%s/%s/%s", root, controller, path, param);

    FILE *fp = fopen(file_path, "r");
    int err = 0;

    if (!fp)
        return -1;

    if (fscanf(fp, "%d", value_in) < 1)
        err = errno;

    if (fclose(fp) < 0)
        err = errno;

    errno = err;
    return errno ? -1 : 0;
}

int cg_read_string(const char *root,
                   const char *controller,
                   const char *path,
                   const char *param, char **value_in) {
    char file_path[256];
    snprintf(file_path, sizeof file_path, "%s/%s/%s/%s", root, controller, path, param);

    FILE *fp = fopen(file_path, "r");
    int err = 0;

    if (!fp)
        return -1;

    if (fscanf(fp, "%ms", value_in) < 1)
        err = errno;

    if (fclose(fp) < 0)
        err = errno;

    errno = err;
    return errno ? -1 : 0;
}

int cg_read_intlist(const char *root,
                    const char *controller,
                    const char *path,
                    const char *param, int **value_in, size_t *length_in) {
    char file_path[256];
    snprintf(file_path, sizeof file_path, "%s/%s/%s/%s", root, controller, path, param);

    FILE *fp = fopen(file_path, "r");
    int err = 0;
    char *buf = NULL;

    if (!fp)
        return -1;

    fscanf(fp, "%ms", &buf);

    if (string_to_intlist(buf, value_in, length_in) != 0)
        err = errno;

    free(buf);
    if (fclose(fp) < 0)
        err = errno;
    errno = err;
    return errno ? -1 : 0;
}

