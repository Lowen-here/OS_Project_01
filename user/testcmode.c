
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

// Test console ownership between two processes.
void
test_owner(void)
{
  int to_child[2];
  int to_parent[2];
  int pid;
  int r;
  char signal = 'x';

  if (pipe(to_child) < 0 || pipe(to_parent) < 0) {
    printf("Pipe creation failed\n");
    exit(1);
  }

  // Parent acquires Character Mode first.
  r = consolemode(1);
  printf("Parent acquire: %d\n", r);

  if (r != 0) {
    printf("Cannot start ownership test\n");
    exit(1);
  }

  pid = fork();

  if (pid < 0) {
    consolemode(0);
    printf("Fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    close(to_child[1]);
    close(to_parent[0]);

    // First attempt while parent owns Character Mode.
    r = consolemode(1);
    printf("Child acquire while owned: %d\n", r);

    // Tell parent the first attempt is complete.
    write(to_parent[1], &signal, 1);

    // Wait until parent releases Character Mode.
    if (read(to_child[0], &signal, 1) != 1)
      exit(1);

    // Second attempt after release.
    r = consolemode(1);
    printf("Child acquire after release: %d\n", r);

    if (r == 0)
      consolemode(0);

    close(to_child[0]);
    close(to_parent[1]);
    exit(0);
  }

  close(to_child[0]);
  close(to_parent[1]);

  // Wait for child's first attempt.
  if (read(to_parent[0], &signal, 1) != 1) {
    consolemode(0);
    exit(1);
  }

  // Release ownership.
  r = consolemode(0);
  printf("Parent release: %d\n", r);

  // Allow child to attempt acquisition again.
  write(to_child[1], &signal, 1);

  wait(0);

  close(to_child[1]);
  close(to_parent[0]);

  exit(0);
}

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    printf("Usage: testcmode exit|pending|owner\n");
    exit(1);
  }

  if (strcmp(argv[1], "exit") == 0) {
    test_exit();
  } else if (strcmp(argv[1], "pending") == 0) {
    test_pending();
  } else if (strcmp(argv[1], "owner") == 0) {
    test_owner();
  } else {
    printf("Unknown test\n");
    exit(1);
  }
  

  exit(0);
}
