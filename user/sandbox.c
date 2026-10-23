#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/syscall.h"
#include "user/user.h"

void
usage(char *s)
{
  fprintf(2, "Usage: %s <mask> <path> <command> [args...]\n", s);
  exit(1);
}

int
main(int argc, char *argv[])
{
  int i;
  char *nargv[MAXARG];

  if (argc < 4) {
    usage(argv[0]);
  }

  // Kiểm tra mask có phải là số không
  if (argv[1][0] < '0' || argv[1][0] > '9') {
    usage(argv[0]);
  }

  // Chuẩn bị mảng tham số cho lệnh cần chạy (bắt đầu từ argv[3])
  for (i = 3; i < argc && i - 3 < MAXARG - 1; i++) {
    nargv[i - 3] = argv[i];
  }
  nargv[argc - 3] = 0;

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "%s: fork failed\n", argv[0]);
    exit(1);
  }
  if (pid == 0) {
    // Tiến trình con gọi interpose và exec
    if (interpose(atoi(argv[1]), argv[2]) < 0) {
      fprintf(2, "sandbox: interpose failed\n");
      exit(1);
    }
    exec(nargv[0], nargv);
    fprintf(2, "sandbox: exec %s failed\n", nargv[0]);
    exit(1);
  } else {
    // Tiến trình cha chờ tiến trình con hoàn tất
    wait(0);
  }

  return 0;
}