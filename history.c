#include "types.h"
#include "user.h"

int main(int argc, char **argv) {
  if (argc != 1) {
    printf(2, "Usage: history\n");
    exit();
  }

  history();
  exit();
}
