#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data, int len);

int
main(int argc, char *argv[])
{
  if (argc == 1) {
    printf("Example 1:\n");
    int a[2] = {61810, 2026};
    memdump("ii", (char *)a, sizeof(a));

    printf("Example 2:\n");
    memdump("S", "a string", sizeof("a string"));

    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *)&s, sizeof(s));

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;

    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");

    printf("Example 4:\n");
    memdump("pihcS", (char *)&example, sizeof(example));

    printf("Example 5:\n");
    memdump("sccccc", (char *)&example, sizeof(example));
  } else if (argc == 2) {
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while (n < sizeof(data)) {
      int nn = read(0, data + n, sizeof(data) - n);
      if (nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data, n);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void
memdump(char *fmt, char *data, int len)
{
  int offset = 0;

  for (int j = 0; fmt[j] != '\0'; j++) {

    if (fmt[j] == 'i') {

      // Kiểm tra còn đủ 4 byte không
      if (len - offset < 4) {
        printf("memdump: not enough data for 'i'\n");
        return;
      }

      // Đọc 4 byte thành số nguyên
      int value = *(int *)(data + offset);

      // In giá trị
      printf("%d\n", value);

      // Di chuyển đến vị trí tiếp theo
      offset += 4;
    }
  
    else if (fmt[j] == 'h') {

      if (len - offset < 2) {
        printf("memdump: not enough data for 'h'\n");
        return;
      }

      short value = *(short *)(data + offset);

      printf("%d\n", value);

      offset += 2;
    }

    else if (fmt[j] == 'c') {

      if (len - offset < 1) {
        printf("memdump: not enough data for 'c'\n");
        return;
      }

      char value = *(data + offset);

      printf("%c\n", value);

      offset += 1;
    }

    else if (fmt[j] == 'p') {

      if (len - offset < 8) {
        printf("memdump: not enough data for 'p'\n");
        return;
      }

      uint64 value = *(uint64 *)(data + offset);

      printf("%lx\n", value);

      offset += 8;
    }

    else if (fmt[j] == 's') {

      if (len - offset < 8) {
        printf("memdump: not enough data for 's'\n");
        return;
      }

      char *value = *(char **)(data + offset);

      printf("%s\n", value);

      offset += 8;
    }
    else if (fmt[j] == 'S') {

      while (offset < len && data[offset] != '\0') {
        printf("%c", data[offset]);
        offset++;
      }

      printf("\n");

      if (offset < len && data[offset] == '\0') {
        offset++;
      }
    }
  }


}
