#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

void
find(char *path, char *target)
{
  char buf[512], *p;
  char name[DIRSIZ + 1];
  char *start, *end;
  int len;
  int fd;
  struct dirent de;
  struct stat st;

  // Open the current path.
  if ((fd = open(path, O_RDONLY)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  // Get information about the current object.
  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  
    // Find the last component without modifying path.
    end = path + strlen(path);

    // Ignore trailing slashes, except for the root "/".
    while (end > path + 1 && *(end - 1) == '/')
    end--;

    start = end;

    while (start > path && *(start - 1) != '/')
    start--;

    len = end - start;

    // Compare the final component with the target.
    if (len <= DIRSIZ) {
    memmove(name, start, len);
    name[len] = '\0';

    if (strcmp(name, target) == 0)
        printf("%s\n", path);
    }

  // Only directories can contain child entries.
  if (st.type != T_DIR) {
    close(fd);
    return;
  }

  // Ensure enough space for the pathname and an entry name.
  if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
    fprintf(2, "find: path too long\n");
    close(fd);
    return;
  }

  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/';

  // Read all directory entries.
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)
      continue;

    // Copy the entry name and terminate the string.
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = '\0';

    // Avoid infinite recursion.
    if (strcmp(p, ".") == 0 ||
        strcmp(p, "..") == 0)
      continue;

    // Recursively process this entry.
    find(buf, target);
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  if (argc != 3) {
    fprintf(2, "Usage: find <path> <filename>\n");
    exit(1);
  }

  find(argv[1], argv[2]);

  exit(0);
}