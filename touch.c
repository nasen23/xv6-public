#include "types.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        // 2: stderr file descriptor
        printf(2, "Usage: touch filename\n");
        exit();
    }

    char *filename = argv[1];

    int fd = open(filename, O_CREATE | O_WRONLY);
    if (fd == -1) {
        printf(1, "touch: error when creating file %s\n", filename);
        exit();
    }
    close(fd);
    exit();
}
