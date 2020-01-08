#include "types.h"
#include "fcntl.h"
#include "fs.h"
#include "stat.h"
#include "user.h"

#define NULL ((void*) 0)
#define PATH_SEPARATOR '/'

int getcwd(char *res);
char* up(int ino, char *parent_path, char *res);
int dirlookup(int fd, int ino, char *p);

int main(int argc, char *argv[]) {
  char res[512];
  if (getcwd(res)) {
    printf(1, "%s\n", res);
  } else {
    printf(2, "pwd failed\n");
  }

  exit();
}

int getcwd(char *res) {
  res[0] = 0;

  char parent_path[512];
  strcpy(parent_path, ".");

  struct stat st;
  if (stat(parent_path, &st) < 0) {
    return 0;
  }

  char *parent = up(st.ino, parent_path, res);
  if (!parent) {
    return 0;
  }

  if (!res[0]) {
    strcpy(res, parent_path);
  }
  return 1;
}

char* up(int ino, char *parent_path, char *res) {
  strcat(parent_path, PATH_SEPARATOR);
  strcat(parent_path, "..");

  struct stat st;
  if (stat(parent_path, &st) < 0) {
    return 0;
  }

  if (st.ino == ino) {
    return parent_path;
  }

  char *found = 0;
  int fd = open(parent_path, O_RDONLY);
  if (fd >= 0) {
    char *p = up(st.ino, parent_path, res);
    if (p) {
      strcpy(p, PATH_SEPARATOR);
      p += sizeof(PATH_SEPARATOR) - 1;

      if (dirlookup(fd, ino, p)) {
        found = p + strlen(p);
      }
    }
    close(fd);
  }
  return found;
}

int dirlookup(int fd, int ino, char *p) {
  struct dirent de;
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (!de.inum) {
      continue;
    }
    if (de.inum == ino) {
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      return 1;
    }
  }
  return 0;
}
