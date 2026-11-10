#pragma once
#include <stddef.h>
#include <sys/types.h>
typedef int file_t;
#define FILEHND_INVALID ((file_t)-1)
file_t fs_open(const char* path, int mode);
int fs_close(file_t fd);
ssize_t fs_read(file_t fd, void* buf, size_t n);
off_t fs_seek(file_t fd, off_t off, int whence);
size_t fs_total(file_t fd);
