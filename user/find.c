#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char* fmtname(char* path) {
  static char buf[DIRSIZ + 1];
  char* p;

  // Find first character after last slash
  for (p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return name without leading spaces
  if (strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  buf[strlen(p)] = '\0';
  return buf;
}

void find(char* path, char* filename) {
  char buf[512], name[DIRSIZ + 1], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  char* path_name = fmtname(path);
  if (strcmp(path_name, filename) == 0) {
    printf("%s\n", path);
  }

  if (st.type == T_DIR) {
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
      printf("find: path too long\n");
      close(fd);
      return;
    }

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0)
        continue;

      memmove(name, de.name, DIRSIZ);
      name[DIRSIZ] = '\0';

      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        continue;

      // Construct the new path
      strcpy(buf, path);
      p = buf + strlen(buf);
      *p++ = '/';
      strcpy(p, name);

      if (stat(buf, &st) < 0) {
        printf("find: cannot stat %s\n", buf);
        continue;
      }

      find(buf, filename);
    }
  }
  close(fd);
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    fprintf(2, "Usage: find <path> <name>\n");
    exit(1);
  }
  find(argv[1], argv[2]);
  exit(0);
}
