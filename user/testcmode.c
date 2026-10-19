
#include "kernel/types.h"
#include "user/user.h"

void
test_exit(void)
{
  int r = consolemode(1);
  printf("Acquire console: %d\n", r);

  // Intentionally exit without consolemode(0).
  exit(r == 0 ? 0 : 1);
}

void
test_pending(void)
{
  char c;
  int r;

  printf("Type abc and press Enter: ");

  if (read(0, &c, 1) != 1) {
    printf("Read failed\n");
    exit(1);
  }

  printf("First character: %c\n", c);

  // Remaining bytes should prevent mode switching.
  r = consolemode(1);
  printf("Enable with pending input: %d\n", r);

  // Drain the remaining characters of the line.
  while (c != '\n') {
    if (read(0, &c, 1) != 1)
      break;
  }

  // Buffer should now be empty.
  r = consolemode(1);
  printf("Enable after draining: %d\n", r);

  if (r == 0) {
    printf("Disable mode: %d\n", consolemode(0));
  }

  exit(0);
}

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    printf("Usage: testcmode exit|pending\n");
    exit(1);
  }

  if (strcmp(argv[1], "exit") == 0) {
    test_exit();
  } else if (strcmp(argv[1], "pending") == 0) {
    test_pending();
  } else {
    printf("Unknown test\n");
    exit(1);
  }

  exit(0);
}
