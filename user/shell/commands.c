#include "shell.h"
#include <file.h>
#include <stdio.h>
#include <proc.h>
#include <string.h>

// Allocate space for MAX_DIR_DEPTH directories plus '/'s
#define MAX_DIR_DEPTH 10
#define CWD_SIZE      (MAX_DIR_DEPTH * (DIRSIZ + 1))
static char cwd[CWD_SIZE] = "/";

static void update_cwd(char *path)
{
    if (*path == '/') {
        ASSERT(strlen(path) < CWD_SIZE);
        strcpy(cwd, path);
    }
    else {
        ASSERT(strlen(cwd) + strlen(path) + 1 < CWD_SIZE);
        join_path(cwd, cwd, path);
    }

    normalize_path(cwd);
    ASSERT(*cwd == '/');
}

static err_t recurse_dir(char *name, err_t (*cmd)(int argc, char **argv, int optc, char **optv),
                         int optc, char **optv, int dir, char *src, char *dst)
{
    err_t ret = E_OK;
    struct dirent de;
    int argc = 3;
    char *sub_argv[3];
    sub_argv[0] = name;
    sub_argv[1] = alloca(CWD_SIZE);
    if (dst != NULL) {
        sub_argv[2] = alloca(CWD_SIZE);
        argc = 2;
    }

    while (read(dir, (char *) &de, sizeof(struct dirent)) == sizeof(struct dirent)) {
        if (de.inum != 0 && strcmp(de.name, ".") != 0 && strcmp(de.name, "..") != 0) {
            join_path(sub_argv[1], src, de.name);
            if (dst != NULL) {
                join_path(sub_argv[2], dst, de.name);
            }
            if ((ret = (*cmd)(argc, sub_argv, optc, optv)) != E_OK) {
                return ret;
            }
        }
    }

    return ret;
}

err_t cmd_ls(int argc, char **argv, int optc, char **optv)
{
    char *path;
    struct file_stat stat;
    int fd;
    struct dirent de;
    bool all = FALSE;

    for (optc--; optc >= 0; optc--) {
        switch (optv[optc][1]) {
        case 'a':
            all = TRUE;
            break;
        default:
            return E_INVAL_OPT;
        }
    }

    argv++;  // Skip command
    argc--;
    do {
        // Default to current directory
        if (argc == 0) {
            path = ".";
        }
        else {
            path = *argv;
        }

        if ((fd = open(path, O_RDONLY)) == -1) {
            return E_CANT_OPEN;
        }
        ASSERT(fstat(fd, &stat) != -1);

        switch (stat.type) {
        case T_FILE:
            // Print file name
            printf("%s\n", path);
            break;
        case T_DIR:
            // Print directory contents
            while (read(fd, (char *) &de, sizeof(struct dirent)) == sizeof(struct dirent)) {
                if (de.inum != 0 && (all || de.name[0] != '.')) {
                    printf("%s\n", de.name);
                }
            }
            break;
        default:
            ASSERT(close(fd) != -1);
            return E_INVAL_TYPE;
        }

        ASSERT(close(fd) != -1);
        argc--;
        argv++;
    } while (argc > 0);

    return E_OK;
}

err_t cmd_pwd(int argc, char **argv, int optc, char **optv)
{
    if (optc > 0) {
        return E_INVAL_OPT;
    }

    printf("%s\n", cwd);

    return E_OK;
}

err_t cmd_cd(int argc, char **argv, int optc, char **optv)
{
    char *path;

    if (optc > 0) {
        return E_INVAL_OPT;
    }

    // Default to root directory
    if (argc == 1) {
        path = "/";
    }
    else {
        path = argv[1];
    }

    if (chdir(path) == -1) {
        return E_INVAL_TYPE;
    }

    update_cwd(path);

    return E_OK;
}

err_t cmd_cp(int argc, char **argv, int optc, char **optv)
{
    char *src, *dst;
    int srcfd, dstfd = -1;
    struct file_stat stat;
    int len;
    char buf[1024];
    err_t ret = E_OK;
    bool recurse = FALSE;

    for (optc--; optc >= 0; optc--) {
        switch (optv[optc][1]) {
        case 'r':
            recurse = TRUE;
            break;
        default:
            return E_INVAL_OPT;
        }
    }

    src = argv[1];
    dst = argv[2];

    if ((srcfd = open(src, O_RDONLY)) == -1) {
        return E_CANT_OPEN;
    }
    ASSERT(fstat(srcfd, &stat) != -1);

    switch (stat.type) {
    case T_FILE:
        ASSERT((dstfd = open(dst, O_CREATE | O_RDWR)) != -1);
        while ((len = read(srcfd, buf, 1024)) > 0) {
            ASSERT(write(dstfd, buf, len) == len);
        }
        break;
    case T_DIR:
        if (!recurse) {
            ret = E_INVAL_TYPE;
            goto end;
        }
        ASSERT((dstfd = mkdir(dst)) != -1);
        if ((ret = recurse_dir("cp", &cmd_cp, optc, optv, srcfd, src, dst)) != E_OK) {
            goto end;
        }
        break;
    default:
        ret = E_INVAL_TYPE;
        goto end;
    }

end:
    ASSERT(close(srcfd) != -1);
    if (dstfd != -1) {
        ASSERT(close(dstfd) != -1);
    }
    return ret;
}

err_t cmd_mv(int argc, char **argv, int optc, char **optv)
{
    if (optc > 0) return E_INVAL_OPT;
    if (argc < 3) return E_FEW_ARGS;
    char *src = argv[1];
    char *dst = argv[2];
    if (link(src, dst) != 0) return E_CANT_OPEN;
    if (unlink(src) != 0) return E_CANT_OPEN;
    return E_OK;
}

err_t cmd_rm(int argc, char **argv, int optc, char **optv)
{
    if (optc > 0) return E_INVAL_OPT;
    if (argc < 2) return E_FEW_ARGS;
    if (unlink(argv[1]) != 0) return E_FNF;
    return E_OK;
}

err_t cmd_mkdir(int argc, char **argv, int optc, char **optv)
{
    if (optc > 0) return E_INVAL_OPT;
    if (argc < 2) return E_FEW_ARGS;
    if (mkdir(argv[1]) != 0) return E_CREATE;
    return E_OK;
}

err_t cmd_cat(int argc, char **argv, int optc, char **optv)
{
    int fd;
    char buf[1024];
    int n;
    if (optc > 0) return E_INVAL_OPT;
    if (argc < 2) return E_FEW_ARGS;
    if ((fd = open(argv[1], O_RDONLY)) == -1) return E_CANT_OPEN;
    while ((n = read(fd, buf, 1023)) > 0) {
        buf[n] = 0;
        printf("%s", buf);
    }
    close(fd);
    return E_OK;
}

err_t cmd_touch(int argc, char **argv, int optc, char **optv)
{
    if (optc > 0) return E_INVAL_OPT;
    if (argc < 2) return E_FEW_ARGS;
    int fd = open(argv[1], O_CREATE);
    if (fd == -1) return E_CREATE;
    close(fd);
    return E_OK;
}

err_t cmd_write(int argc, char **argv, int optc, char **optv)
{
    // Stub implementation
    printf("Redirection not supported in this shell version.\n");
    return E_OK;
}

err_t cmd_append(int argc, char **argv, int optc, char **optv)
{
    // Stub implementation
    printf("Redirection not supported in this shell version.\n");
    return E_OK;
}

err_t cmd_pathtest(int argc, char **argv, int optc, char **optv)
{
    printf("Path test not supported in this shell version.\n");
    return E_OK;
}

err_t cmd_waitdemo(int argc, char **argv, int optc, char **optv)
{
    pid_t pid;
    if ((pid = spawn(6, 1000)) != -1)
        printf("wait_demo running in process %d.\n", pid);
    else
        printf("Failed to launch wait_demo.\n");
    return E_OK;
}
