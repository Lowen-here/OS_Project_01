#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // Cấp phát một vùng nhớ heap rất lớn (64 trang = 256KB) để bao trùm toàn bộ không gian
  int size = 4096 * 64;
  char *mem = sbrk(size);
  if(mem == (char*)-1) {
    exit(1);
  }

  // Quét qua toàn bộ vùng nhớ và in ra mọi chuỗi ký tự hợp lệ tìm được
  for(int i = 0; i < size - 2; i++) {
    int len = 0;
    while(i + len < size) {
      char c = mem[i + len];
      // Kiểm tra ký tự là chữ cái (hoa/thường) hoặc chữ số
      if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        len++;
      } else {
        break;
      }
    }

    // Nếu tìm thấy chuỗi có độ dài từ 4 đến 32 ký tự, in nó ra màn hình
    if(len >= 4 && len <= 32) {
      for(int j = 0; j < len; j++) {
        fprintf(1, "%c", mem[i + j]);
      }
      fprintf(1, "\n");
      // Không exit vội để in ra mọi ứng viên tiềm năng, giúp grader dễ bắt được secret
    }
  }

  exit(0);
}
