#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "stat.h"

// Whether the file is a directory
int isdir(int fd) {
    struct stat st;
    fstat(fd, &st);
    return st.type == T_DIR;
}

// e.g.
// Args:
//     srcname: 1/2.txt
//     dstname: 3/
// Result:
//     dstname: 3/2.txt
void concat_srcname(char *srcname, char *dstname) {
    char *tmp;
    char *src = srcname;
    while ((tmp = strchr(srcname, '/'))) {
        src = tmp + 1;
    }
    strcat(src, dstname);
}

void copy_file(int srcfd, int dstfd) {
    int nbytes;
    char buf[2048] = {};
    while ((nbytes = read(srcfd, buf, 2048) > 0)) {
        write(dstfd, buf, nbytes);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf(2, "Usage: cp source dest");
        exit();
    }

    char *srcname = argv[1];
    char *dstname = argv[2];

    int srcfd = open(srcname, O_RDONLY);
    if (srcfd == -1) {
        printf(1, "Open source file %s failed\n", srcname);
        exit();
    }

    if (isdir(srcfd)) {
        printf(1, "Recursive copy of directory is not supported!\n");
        exit();
    }

    int dstlen = strlen(dstname);
    if (dstname[dstlen - 1] == '/') {
        // The dest file name contains a dir prefix
        concat_srcname(srcname, dstname);
    }

    int dstfd = open(dstname, O_CREATE | O_WRONLY);
    if (dstfd == -1) {
        printf(1, "Open destination file %s failed\n", dstname);
        exit();
    }

    copy_file(srcfd, dstfd);
    close(srcfd);
    close(dstfd);
    exit();
}
