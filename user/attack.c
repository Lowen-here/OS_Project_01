
#include "kernel/types.h"
#include "user/user.h"

#define PGSIZE 4096
#define MEMSIZE (64 * PGSIZE)

int
main(int argc, char *argv[])
{
  char *mem = sbrk(MEMSIZE);

  if(mem == (char *)-1) {
    exit(1);
  }

  char marker[] = "Here it is: ";
  int marker_len = sizeof(marker) - 1;

  for(int i = 0; i <= MEMSIZE - marker_len; i++) {
    int found = 1;

    for(int j = 0; j < marker_len; j++) {
      if(mem[i + j] != marker[j]) {
        found = 0;
        break;
      }
    }

    if(!found)
      continue;

    int start = i + marker_len;
    int end = start;

    while(end < MEMSIZE) {
      char c = mem[end];

      if(!((c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9'))) {
        break;
      }

      end++;
    }

    if(end > start) {
      for(int j = start; j < end; j++) {
        fprintf(1, "%c", mem[j]);
      }

      fprintf(1, "\n");
      exit(0);
    }
  }

  exit(0);
}
