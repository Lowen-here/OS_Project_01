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

// Sandbox a command by disallowing system calls in mask and
// system calls that are using path
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

  // Cấu hình interpose cho process hiện tại (hoặc process con)
  if (interpose(atoi(argv[1]), argv[2]) < 0) {
    fprintf(2, "sandbox: interpose failed\n");
    exit(1);
  }

  // Chuẩn bị mảng tham số cho lệnh cần chạy (bắt đầu từ argv[3])
  for (i = 3; i < argc && i - 3 < MAXARG - 1; i++) {
    nargv[i - 3] = argv[i];
  }
  nargv[argc - 3] = 0;

  // Thực thi lệnh
  exec(nargv[0], nargv);
  fprintf(2, "sandbox: exec %s failed\n", nargv[0]);
  exit(1);
}