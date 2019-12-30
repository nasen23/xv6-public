#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "stat.h"

int main(int argc, char *argv[]) {
  char buf[200];
  printf(1, "%s\n", getcwd(buf, sizeof(buf)));
  exit();
}
