//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define BACKSPACE 0x100       // erase the last output character
#define C(x)      ((x) - '@') // Control-x

//
// send one character to the uart, but don't use
// interrupts or sleep(). safe to be called from
// interrupts, e.g. by printk and to echo input
// characters.
//
void
consputc(int c)
{
  if (c == BACKSPACE) {
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b');
    uartputc_sync(' ');
    uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

struct {
  struct spinlock lock;

  // input circular buffer
#define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE];
  uint r; // Read index
  uint w; // Write index
  uint e; // Edit index

  int interactive_owner_pid;
} cons;

//
// user write() system calls to the console go here.
// uses sleep() and UART interrupts.
//
int
consolewrite(int user_src, uint64 src, int n)
{
  char buf[32]; // move batches from user space to uart.
  int i = 0;

  while (i < n) {
    int nn = sizeof(buf);
    if (nn > n - i)
      nn = n - i;
    if (either_copyin(buf, user_src, src + i, nn) == -1)
      break;
    uartwrite(buf, nn);
    i += nn;
  }

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dst indicates whether dst is a user
// or kernel address.
//
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;
  int pid = myproc()->pid;

  target = n;
  acquire(&cons.lock);

  while (n > 0) {
    // Interactive input belongs to its owning process.
    if (cons.interactive_owner_pid != 0 &&
        cons.interactive_owner_pid != pid) {
      int copied = target - n;
      release(&cons.lock);
      return copied > 0 ? copied : -1;
    }

    // Wait until input is available.
    while (cons.r == cons.w) {
      if (killed(myproc())) {
        release(&cons.lock);
        return -1;
      }

      sleep_prepare(&cons.r);
      release(&cons.lock);
      sleep();
      acquire(&cons.lock);

      // Ownership may have changed while sleeping.
      if (cons.interactive_owner_pid != 0 &&
          cons.interactive_owner_pid != pid) {
        int copied = target - n;
        release(&cons.lock);
        return copied > 0 ? copied : -1;
      }
    }

    int interactive =
      (cons.interactive_owner_pid == pid);

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    // Keep the original Ctrl+D behavior in normal mode.
    if (!interactive && c == C('D')) {
      if (n < target) {
        cons.r--;
      }
      break;
    }

    cbuf = c;

    if (either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    // In interactive mode, return after one character.
    // In normal mode, retain the original newline behavior.
    if (interactive || c == '\n')
      break;
  }

  release(&cons.lock);

  return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for each input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
void
consoleintr(int c)
{
  acquire(&cons.lock);

  // Interactive mode: deliver input bytes without kernel editing/echo.
  if (cons.interactive_owner_pid != 0) {
    if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      // Make this byte immediately available to read().
      cons.w = cons.e;
      wakeup(&cons.r);
    }

    release(&cons.lock);
    return;
  }

  switch (c) {
  case C('P'): // Print process list.
    procdump();
    break;
  case C('U'): // Kill line.
    while (cons.e != cons.w &&
           cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    if (cons.e != cons.w) {
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
      c = (c == '\r') ? '\n' : c;

      // echo back to the user.
      consputc(c);

      // store for consumption by consoleread().
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      if (c == '\n' || c == C('D') || cons.e - cons.r == INPUT_BUF_SIZE) {
        // wake up consoleread() if a whole line (or end-of-file)
        // has arrived.
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }

  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");
  cons.interactive_owner_pid = 0;

  uartinit();

  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}


int
consolemode_set(int mode)
{
  struct proc *p = myproc();
  int pid = p->pid;

  if (mode != 0 && mode != 1)
    return -1;

  // Only a process reading from the console may use this mode.
  struct file *f = p->ofile[0];

  if (f == 0 ||
      f->type != FD_DEVICE ||
      f->major != CONSOLE ||
      !f->readable)
    return -1;

  acquire(&cons.lock);

  if (mode == 1) {
    if (cons.interactive_owner_pid != 0 &&
        cons.interactive_owner_pid != pid) {
      release(&cons.lock);
      return -1;
    }

    // Do not switch modes with unprocessed input.
    if (cons.interactive_owner_pid == 0 &&
        (cons.r != cons.w || cons.w != cons.e)) {
      release(&cons.lock);
      return -1;
    }

    cons.interactive_owner_pid = pid;
  } else {
    if (cons.interactive_owner_pid != pid) {
      release(&cons.lock);
      return -1;
    }

    // Discard unread interactive input before returning to line mode.
    cons.r = cons.e;
    cons.w = cons.e;
    cons.interactive_owner_pid = 0;
  }

  // Recheck the mode and ownership in any waiting console reads.
  wakeup(&cons.r);

  release(&cons.lock);
  return 0;
}

void
consolemode_release(int pid)
{
  acquire(&cons.lock);

  if (cons.interactive_owner_pid == pid) {
    // Discard unread input before restoring normal mode.
    cons.r = cons.e;
    cons.w = cons.e;

    cons.interactive_owner_pid = 0;
    wakeup(&cons.r);
  }

  release(&cons.lock);
}