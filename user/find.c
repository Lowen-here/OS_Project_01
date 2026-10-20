#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

// Execute a command with the matched pathname appended.
void
run_command(char **cmd_argv, char *path)
{
  char *args[MAXARG];
  int i = 0;
  int pid;

  // Copy the original command arguments.
  while (cmd_argv[i] != 0) {
    if (i >= MAXARG - 2) {
      fprintf(2, "find: too many arguments\n");
      return;
    }

    args[i] = cmd_argv[i];
    i++;
  }

  // Append the matched pathname.
  args[i] = path;
  args[i + 1] = 0;

  pid = fork();

  if (pid < 0) {
    fprintf(2, "find: fork failed\n");
    return;
  }

  if (pid == 0) {
    exec(args[0], args);
    fprintf(2, "find: exec %s failed\n", args[0]);
    exit(1);
  }

  wait(0);
}

// Recursively find files/directories by name.
void
find(char *path, char *target, char **cmd_argv)
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

    if (strcmp(name, target) == 0) {
      if (cmd_argv == 0) {
        printf("%s\n", path);
      } else {
        run_command(cmd_argv, path);
      }
    }
  }

  // Only directories can contain child entries.
  if (st.type != T_DIR) {
    close(fd);
    return;
  }

  // Ensure enough space for the pathname and an entry name.
  int path_len = strlen(path);
  int need_slash = (path_len > 0 && path[path_len - 1] != '/');

  if (path_len + need_slash + DIRSIZ + 1 > sizeof(buf)) {
    fprintf(2, "find: path too long\n");
    close(fd);
    return;
  }

  strcpy(buf, path);
  p = buf + strlen(buf);

  // Add '/' only if the path does not already end with '/'.
  if (p > buf && *(p - 1) != '/')
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
    find(buf, target, cmd_argv);
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  char **cmd_argv = 0;

  if (argc < 3) {
    fprintf(2, "Usage: find <path> <filename> [-exec command args...]\n");
    exit(1);
  }

  if (argc > 3) {
    if (argc < 5 || strcmp(argv[3], "-exec") != 0) {
      fprintf(2, "find: invalid -exec syntax\n");
      exit(1);
    }

    cmd_argv = &argv[4];
  }

  find(argv[1], argv[2], cmd_argv);

  exit(0);
}