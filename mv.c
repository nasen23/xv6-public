#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "stat.h"


int main(int argc, char *argv[]) {
  if (argc != 3) {
    printf(2, "Usage: mv source dest\n");
    exit();
  }

  if ((link(argv[1], argv[2]) < 0) || (unlink(argv[1]) < 0)) {
    printf(2, "mv %s to %s failed\n", argv[1], argv[2]);
  }

  exit();
}
