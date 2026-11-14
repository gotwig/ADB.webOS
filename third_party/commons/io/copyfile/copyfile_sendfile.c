#include "copyfile.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sendfile.h>

/**
 * @param source Source file path
 * @param dest Destination file path
 * @return 0 if file has been copied. -1 otherwise
 * @see https://medium.com/swlh/linux-zero-copy-using-sendfile-75d2eb56b39b
 */
int copyfile(const char *source, const char *dest) {
    // Copy file with sendfile api
    struct stat src_stat;
    int src_fd = open(source, O_RDONLY);
    if (src_fd == -1) {
        return -1;
    }
    if (fstat(src_fd, &src_stat) != 0) {
        close(src_fd);
        return -1;
    }
    int dest_fd = open(dest, O_WRONLY | O_CREAT, src_stat.st_mode);
    if (dest_fd == -1) {
        close(src_fd);
        return -1;
    }
    ssize_t n = 1;
    while (n > 0) {
        n = sendfile(dest_fd, src_fd, 0, 8192);
    }
    return 0;
}