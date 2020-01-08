#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

int main(int argc, char *argv[]) {
  if (argc != 3) {
    printf(2, "Usage: rename old new\n");
    exit();
  }

  char *oldpath = argv[1];
  char *newpath = argv[2];

  int res = rename(oldpath, newpath);
  if (res == -1) {
    printf(2, "File '%s' does not exist.\n", oldpath);
  } else if (res == -2) {
    printf(2, "Cannot rename '%s' to '%s'.\n", oldpath, newpath);
  } else if (res >= 0) {
    printf(1, "Renamed '%s' to '%s'.\n", oldpath, newpath);
  } else {
    printf(2, "Cannot rename '%s' to '%s'.\n", oldpath, newpath);
  }

  exit();
}
