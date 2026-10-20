// Shell.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/fs.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

// Command history storage.
#define HISTORY_SIZE 10
#define HISTORY_CMD_LEN 100

static char history[HISTORY_SIZE][HISTORY_CMD_LEN];

// Number of commands currently stored.
static int history_count = 0;

// Index where the next command will be stored.
static int history_next = 0;

// Total number of commands saved by this shell.
static int history_total = 0;

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void); // Fork but panics on failure.
void panic(char *);
struct cmd *parsecmd(char *);
void runcmd(struct cmd *) __attribute__((noreturn));

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    exit(1);

  switch (cmd->type) {
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd *)cmd;
    if (ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    close(rcmd->fd);
    if (open(rcmd->file, rcmd->mode) < 0) {
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    if (fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    if (pipe(p) < 0)
      panic("pipe");
    if (fork1() == 0) {
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if (fork1() == 0) {
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    if (fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}


// Find the beginning of the word being completed.
// Also determine whether this is the first word in the command line.
int
find_completion_start(char *buf, int len, int *is_first_word)
{
  int start = len;
  int i;

  // Move backward to find the beginning of the current word.
  while (start > 0 &&
         buf[start - 1] != ' ' &&
         buf[start - 1] != '\t') {
    start--;
  }

  // Check whether any previous word exists.
  *is_first_word = 1;

  for (i = 0; i < start; i++) {
    if (buf[i] != ' ' && buf[i] != '\t') {
      *is_first_word = 0;
      break;
    }
  }

  return start;
}

// Save a command in the circular history buffer.
void
history_add(char *cmd)
{
  int len;

  if (cmd == 0)
    return;

  len = strlen(cmd);

  // Do not save empty commands.
  if (len == 0)
    return;

  // Reject commands that exceed the storage capacity.
  if (len >= HISTORY_CMD_LEN)
    return;

  // Copy the command into the next available slot.
  strcpy(history[history_next], cmd);

  // Move to the next slot, wrapping around if necessary.
  history_next = (history_next + 1) % HISTORY_SIZE;

  // Increase stored count until the buffer becomes full.
  if (history_count < HISTORY_SIZE)
    history_count++;

  // Keep a monotonically increasing command number.
  history_total++;
}


// Retrieve a command by its absolute history number.
// Return 0 if the requested command is unavailable.
char *
history_get(int number)
{
  int oldest_number;
  int oldest_index;
  int offset;
  int index;

  if (history_count == 0)
    return 0;

  // Determine the number of the oldest stored command.
  oldest_number = history_total - history_count + 1;

  // Reject invalid or overwritten history numbers.
  if (number < oldest_number || number > history_total)
    return 0;

  // Find the physical index of the oldest command.
  oldest_index =
    (history_next - history_count + HISTORY_SIZE) % HISTORY_SIZE;

  // Calculate the requested command's offset.
  offset = number - oldest_number;

  index = (oldest_index + offset) % HISTORY_SIZE;

  return history[index];
}

// Print stored commands from oldest to newest.
void
history_print(void)
{
  int oldest_number;
  int number;
  char *cmd;

  if (history_count == 0) {
    printf("history: empty\n");
    return;
  }

  oldest_number = history_total - history_count + 1;

  for (number = oldest_number; number <= history_total; number++) {
    cmd = history_get(number);

    if (cmd != 0)
      printf("%d %s\n", number, cmd);
  }
}

// Check whether name starts with prefix.
int
match_prefix(char *name, char *prefix)
{
  int i = 0;

  while (prefix[i] != 0) {
    if (name[i] == 0 || name[i] != prefix[i])
      return 0;

    i++;
  }

  return 1;
}

// Scan a directory for names beginning with prefix.
// Return the number of matches.
// If exactly one match exists, its name is available in result.
int
scan_completions(char *dir, char *prefix, char *result)
{
  int fd;
  int count = 0;
  struct dirent de;
  struct stat st;
  char name[DIRSIZ + 1];

  result[0] = 0;

  fd = open(dir, O_RDONLY);
  if (fd < 0)
    return 0;

  if (fstat(fd, &st) < 0 || st.type != T_DIR) {
    close(fd);
    return 0;
  }

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)
      continue;

    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = 0;

    // Ignore "." and "..".
    if (strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0)
      continue;

    if (match_prefix(name, prefix)) {
      count++;

      // Save the first matching name.
      if (count == 1)
        strcpy(result, name);
    }
  }

  close(fd);
  return count;
}

// Display all directory entries matching the prefix.
void
print_completions(char *dir, char *prefix)
{
  int fd;
  struct dirent de;
  struct stat st;
  char name[DIRSIZ + 1];

  fd = open(dir, O_RDONLY);
  if (fd < 0)
    return;

  if (fstat(fd, &st) < 0 || st.type != T_DIR) {
    close(fd);
    return;
  }

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)
      continue;

    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = 0;

    if (strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0)
      continue;

    if (match_prefix(name, prefix))
      printf("%s\n", name);
  }

  close(fd);
}

// Handle Tab completion for the current input line.
void
complete_input(char *buf, int *len, int nbuf)
{
  int start;
  int first_word;
  int count;
  int prefix_len;
  int match_len;
  int i;

  char result[DIRSIZ + 1];
  char prefix[DIRSIZ + 1];
  char *dir;

  start = find_completion_start(buf, *len, &first_word);
  prefix_len = *len - start;

  // File names in xv6 cannot exceed DIRSIZ.
  if (prefix_len > DIRSIZ)
    return;

  memmove(prefix, buf + start, prefix_len);
  prefix[prefix_len] = 0;

  dir = first_word ? "/" : ".";

  count = scan_completions(dir, prefix, result);

  if (count == 0)
    return;

  if (count == 1) {
    match_len = strlen(result);

    // Reserve space for newline and null terminator.
    if (start + match_len > nbuf - 2)
      return;

    // Append only the missing characters.
    for (i = prefix_len; i < match_len; i++) {
      buf[*len] = result[i];
      write(1, &result[i], 1);
      (*len)++;
    }

    buf[*len] = 0;
    return;
  }

  // Multiple matches: display candidates and restore input.
  write(1, "\n", 1);

  print_completions(dir, prefix);

  // Redisplay prompt and current input.
  write(2, "$ ", 2);
  write(1, buf, *len);
}

// Replace the current interactive input line.
void
replace_input_line(char *buf, int *len, int nbuf, char *new_line)
{
  int new_len;
  int i;

  new_len = strlen(new_line);

  // Reserve space for newline and null terminator.
  if (new_len > nbuf - 2)
    return;

  // Erase the current line from the terminal.
  for (i = 0; i < *len; i++)
    write(1, "\b \b", 3);

  // Copy the new command into the input buffer.
  memmove(buf, new_line, new_len);
  buf[new_len] = 0;

  *len = new_len;

  // Display the replacement command.
  write(1, buf, *len);
}

// Read and edit one interactive command line.
int
read_interactive(char *buf, int nbuf)
{
  int len = 0;
  int n;
  char c;
  int esc_state = 0;
  int history_pos = history_total + 1;
  char draft[100];

  memset(buf, 0, nbuf);
  
  draft[0] = 0; 


  while (1) {
    n = read(0, &c, 1);

    if (n != 1)
      return -1;

    // Handle escape sequences for arrow keys.
  if (esc_state == 1) {
    if (c == '[') {
      esc_state = 2;
      continue;
    }

    esc_state = 0;
    continue;
  }

  if (esc_state == 2) {
    char *saved;
    int oldest_number;

    esc_state = 0;

    oldest_number = history_total - history_count + 1;

    // Arrow Up: move to an older command.
    if (c == 'A') {
      if (history_count == 0)
        continue;

      // Save the current draft before entering History.
      if (history_pos == history_total + 1) {
        strcpy(draft, buf);
      }

      if (history_pos > oldest_number)
        history_pos--;

      saved = history_get(history_pos);

      if (saved != 0)
        replace_input_line(buf, &len, nbuf, saved);

      continue;
    }

    // Arrow Down: move to a newer command.
    if (c == 'B') {
      if (history_pos == history_total + 1)
        continue;

      if (history_pos < history_total)
        history_pos++;

      else {
        history_pos = history_total + 1;
        replace_input_line(buf, &len, nbuf, draft);
        continue;
      }

      saved = history_get(history_pos);

      if (saved != 0)
        replace_input_line(buf, &len, nbuf, saved);

      continue;
    }

    // Ignore unsupported escape sequences.
    continue;
  }

    if (c == '\x1b') {
      esc_state = 1;
      continue;
    }

    // Enter: finish the command line.
    if (c == '\n' || c == '\r') {
      write(1, "\n", 1);

      buf[len++] = '\n';
      buf[len] = 0;

      return 0;
    }

    // Backspace: remove the last character.
    if (c == '\x7f' || c == '\b') {
      if (len > 0) {
        len--;
        buf[len] = 0;
        write(1, "\b \b", 3);
      }
      continue;
    }

    // Ctrl+D: EOF when the current line is empty.
    if (c == 4) {
      if (len == 0)
        return -1;

      continue;
    }

    // Ctrl+U: clear the current input line.
    if (c == 21) {
      while (len > 0) {
        len--;
        buf[len] = 0;
        write(1, "\b \b", 3);
      }
      continue;
    }

    // Handle Tab completion.
    if (c == '\t') {
      complete_input(buf, &len, nbuf);
      continue;
    }

    // Ignore other control characters.
    if ((unsigned char)c < 32)
      continue;

    // Reserve space for newline and null terminator.
    if (len >= nbuf - 2)
      continue;

    buf[len++] = c;
    buf[len] = 0;

    // Kernel echo is disabled in interactive mode.
    write(1, &c, 1);
  }
}

int
getcmd(char *buf, int nbuf)
{
  struct stat st;
  int interactive = 0;
  int result;

  if (fstat(0, &st) >= 0 && st.type == T_DEVICE) {
    write(2, "$ ", 2);
    interactive = 1;
  }

  if (interactive && consolemode(1) == 0) {
    result = read_interactive(buf, nbuf);

    // Restore normal console input before executing commands.
    if (consolemode(0) < 0) {
      fprintf(2, "sh: cannot restore console mode\n");
      return -1;
    }

    return result;
  }

  // Keep the original line-input behavior for scripts.
  memset(buf, 0, nbuf);
  gets(buf, nbuf);

  if (buf[0] == 0)
    return -1;

  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while ((fd = open("console", O_RDWR)) >= 0) {
    if (fd >= 3) {
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while (getcmd(buf, sizeof(buf)) >= 0) {
    char *cmd = buf;
    char *saved;
    int len;
    int number;
    int i;
    int digit;
    int valid;

    // Remove leading spaces and tabs.
    while (*cmd == ' ' || *cmd == '\t')
      cmd++;

    // Remove trailing whitespace and newline.
    len = strlen(cmd);

    while (len > 0 &&
           (cmd[len - 1] == '\n' ||
            cmd[len - 1] == '\r' ||
            cmd[len - 1] == ' ' ||
            cmd[len - 1] == '\t')) {
      cmd[--len] = 0;
    }

    // Ignore empty commands.
    if (len == 0)
      continue;

    // History expansion: !! or !n.
    if (cmd[0] == '!') {

      if (strcmp(cmd, "!!") == 0) {
        // Recall the most recent command.
        number = history_total;
      } else {
        // Parse the number after '!'.
        number = 0;
        valid = (cmd[1] != 0);

        for (i = 1; cmd[i] != 0; i++) {
          if (cmd[i] < '0' || cmd[i] > '9') {
            valid = 0;
            break;
          }

          digit = cmd[i] - '0';

          // Reject a number larger than history_total.
          // This also avoids integer overflow.
          if (number > history_total / 10 ||
              (number == history_total / 10 &&
               digit > history_total % 10)) {
            valid = 0;
            break;
          }

          number = number * 10 + digit;
        }

        if (!valid) {
          fprintf(2, "history: invalid reference\n");
          continue;
        }
      }

      saved = history_get(number);

      if (saved == 0) {
        fprintf(2, "history: command not found\n");
        continue;
      }

      // Copy before saving again: history_add()
      // may overwrite an entry in the circular buffer.
      memmove(buf, saved, strlen(saved) + 1);
      cmd = buf;

      // Show the expanded command.
      printf("%s\n", cmd);
    }

  // Save the actual command, including "history".
  history_add(cmd);

  // Built-in: history.
  if (strcmp(cmd, "history") == 0) {
    history_print();
    continue;
  }

  // Built-in: cd must run in the parent shell.
    if (cmd[0] == 'c' &&
        cmd[1] == 'd' &&
        cmd[2] == ' ') {
      if (chdir(cmd + 3) < 0)
        fprintf(2, "cannot cd %s\n", cmd + 3);
    } else {
      if (fork1() == 0)
        runcmd(parsecmd(cmd));

      wait(0);
    }
  }

  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if (pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd *
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd *)cmd;
}

struct cmd *
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd *)cmd;
}

struct cmd *
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd *)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  if (q)
    *q = s;
  ret = *s;
  switch (*s) {
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if (*s == '>') {
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if (eq)
    *eq = s;

  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char **, char *);
struct cmd *parsepipe(char **, char *);
struct cmd *parseexec(char **, char *);
struct cmd *nulterminate(struct cmd *);

struct cmd *
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if (s != es) {
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd *
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while (peek(ps, es, "&")) {
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if (peek(ps, es, ";")) {
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd *
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if (peek(ps, es, "|")) {
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd *
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while (peek(ps, es, "<>")) {
    tok = gettoken(ps, es, 0, 0);
    if (gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch (tok) {
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
      break;
    case '+': // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd *
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if (!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if (!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd *
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if (peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd *)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while (!peek(ps, es, "|)&;")) {
    if ((tok = gettoken(ps, es, &q, &eq)) == 0)
      break;
    if (tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if (argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd *
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    return 0;

  switch (cmd->type) {
  case EXEC:
    ecmd = (struct execcmd *)cmd;
    for (i = 0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
