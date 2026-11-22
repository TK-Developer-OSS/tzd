/* A small shell for the TzD guest (task #42), compiled with TzD.
 *
 *   commands separated by ';' or '&' (run in the background: not waited
 *   for, stdin from /dev/null), pipelines with '|', redirections
 *   '<' '>' '>>', words quoted with '...' or "...", builtins cd, pwd,
 *   echo, exit; other commands are looked up in PATH (execvp).
 *
 * sh -c COMMANDS runs COMMANDS (line by line) and exits with the last
 * status (ssh remote commands, scp, the sftp subsystem).  Otherwise it
 * reads one line at a time from stdin, prompt "tzd# " when stdin is a
 * terminal or TZD_PROMPT is set.  No variables, globbing, job control or
 * line editing (the console's line discipline does the editing).  */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define LINE_MAX_LEN 4096
#define MAX_WORDS 128
#define MAX_CMDS 16

static int last_status;
/* The words of the line being run (unquoted copies).  */
static char wbuf[2 * 4096 + 2];
static size_t wpos;

static void put (int fd, const char *s)
{
  write (fd, s, strlen (s));
}

static void err2 (const char *a, const char *b)
{
  put (2, "sh: ");
  put (2, a);
  if (b)
    {
      put (2, ": ");
      put (2, b);
    }
  put (2, "\n");
}

/* One command of a pipeline: its words and redirections.  */
struct cmd
{
  char *argv[MAX_WORDS + 1];
  int argc;
  char *in;       /* < file */
  char *out;      /* > or >> file */
  int append;
};

/* Splits LINE (modified in place) into the words of one pipeline,
   stopping at ';', '&' or the end.  Returns the rest of the line after
   it, or NULL at the end.  *NCMDS gets the number of commands; -1 on a
   syntax error.  *BG is set when the pipeline ended with '&'.  */
static char *parse (char *line, struct cmd *cmds, int *ncmds, int *bg)
{
  *bg = 0;
  int n = 0;
  memset (&cmds[0], 0, sizeof cmds[0]);
  char *p = line;
  char **pending = NULL;   /* the next word goes to a redirection */
  for (;;)
    {
      while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
      if (*p == '\0' || *p == ';' || *p == '&' || *p == '#')
        {
          char *rest = (*p == ';' || *p == '&') ? p + 1 : NULL;
          *bg = (*p == '&');
          *p = '\0';
          if (pending)
            {
              err2 ("syntax error", "missing file name");
              *ncmds = -1;
              return rest;
            }
          if (cmds[n].argc == 0 && n > 0)
            {
              err2 ("syntax error", "empty command after '|'");
              *ncmds = -1;
              return rest;
            }
          *ncmds = cmds[n].argc ? n + 1 : n;
          return rest;
        }
      if (*p == '|')
        {
          *p++ = '\0';
          if (cmds[n].argc == 0 || pending || n + 1 >= MAX_CMDS)
            {
              err2 ("syntax error", "near '|'");
              *ncmds = -1;
              return NULL;
            }
          n++;
          memset (&cmds[n], 0, sizeof cmds[n]);
          continue;
        }
      if (*p == '<' || *p == '>')
        {
          if (pending)
            {
              err2 ("syntax error", "near redirection");
              *ncmds = -1;
              return NULL;
            }
          if (*p == '<')
            pending = &cmds[n].in;
          else
            {
              pending = &cmds[n].out;
              cmds[n].append = (p[1] == '>');
              if (cmds[n].append)
                *p++ = '\0';
            }
          *p++ = '\0';
          continue;
        }
      /* A word, unquoted into wbuf (the line itself keeps its
         separators).  */
      if (wpos + strlen (p) + 1 > sizeof wbuf)
        {
          err2 ("line too long", NULL);
          *ncmds = -1;
          return NULL;
        }
      char *w = &wbuf[wpos];
      while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r'
             && *p != ';' && *p != '&' && *p != '|' && *p != '<'
             && *p != '>')
        {
          if (*p == '\'' || *p == '"')
            {
              char q = *p++;
              while (*p && *p != q)
                wbuf[wpos++] = *p++;
              if (*p != q)
                {
                  err2 ("syntax error", "unterminated quote");
                  *ncmds = -1;
                  return NULL;
                }
              p++;
            }
          else
            wbuf[wpos++] = *p++;
        }
      wbuf[wpos++] = '\0';
      if (pending)
        {
          *pending = w;
          pending = NULL;
        }
      else if (cmds[n].argc < MAX_WORDS)
        cmds[n].argv[cmds[n].argc++] = w;
      else
        {
          err2 ("too many words", NULL);
          *ncmds = -1;
          return NULL;
        }
    }
}

/* Builtins that change the shell itself; run in the shell process when
   alone (not in a pipeline).  Returns 1 if CMD was one.  */
static int builtin (struct cmd *c, int *done)
{
  char *name = c->argv[0];
  if (strcmp (name, "exit") == 0)
    {
      *done = 1;
      if (c->argc > 1)
        last_status = atoi (c->argv[1]);
      return 1;
    }
  if (strcmp (name, "cd") == 0)
    {
      const char *dir = c->argc > 1 ? c->argv[1] : getenv ("HOME");
      if (!dir)
        dir = "/";
      if (chdir (dir) != 0)
        {
          err2 (dir, strerror (errno));
          last_status = 1;
        }
      else
        last_status = 0;
      return 1;
    }
  return 0;
}

/* Builtins that only print: also usable in a pipeline (run in the child).
   Returns 1 if CMD was one; the status is in *STATUS.  */
static int print_builtin (struct cmd *c, int *status)
{
  char *name = c->argv[0];
  if (strcmp (name, "pwd") == 0)
    {
      char buf[4096];
      if (getcwd (buf, sizeof buf))
        {
          put (1, buf);
          put (1, "\n");
          *status = 0;
        }
      else
        {
          err2 ("pwd", strerror (errno));
          *status = 1;
        }
      return 1;
    }
  if (strcmp (name, "echo") == 0)
    {
      int i = 1, nl = 1;
      if (c->argc > 1 && strcmp (c->argv[1], "-n") == 0)
        {
          nl = 0;
          i = 2;
        }
      for (; i < c->argc; i++)
        {
          put (1, c->argv[i]);
          if (i + 1 < c->argc)
            put (1, " ");
        }
      if (nl)
        put (1, "\n");
      *status = 0;
      return 1;
    }
  return 0;
}

/* Applies C's redirections in the current process.  0 on success.  */
static int redirect (struct cmd *c)
{
  if (c->in)
    {
      int fd = open (c->in, O_RDONLY);
      if (fd < 0)
        {
          err2 (c->in, strerror (errno));
          return -1;
        }
      dup2 (fd, 0);
      close (fd);
    }
  if (c->out)
    {
      int fl = O_WRONLY | O_CREAT | (c->append ? O_APPEND : O_TRUNC);
      int fd = open (c->out, fl, 0644);
      if (fd < 0)
        {
          err2 (c->out, strerror (errno));
          return -1;
        }
      dup2 (fd, 1);
      close (fd);
    }
  return 0;
}

static void run_pipeline (struct cmd *cmds, int n, int *done, int bg)
{
  if (!bg && n == 1 && !cmds[0].in && !cmds[0].out
      && builtin (&cmds[0], done))
    return;
  pid_t pids[MAX_CMDS];
  int prev = -1;
  for (int i = 0; i < n; i++)
    {
      int fds[2] = { -1, -1 };
      if (i + 1 < n && pipe (fds) != 0)
        {
          err2 ("pipe", strerror (errno));
          n = i;
          break;
        }
      pid_t pid = fork ();
      if (pid < 0)
        {
          err2 ("fork", strerror (errno));
          n = i;
          break;
        }
      if (pid == 0)
        {
          if (prev >= 0)
            {
              dup2 (prev, 0);
              close (prev);
            }
          else if (bg)
            {
              /* A background job does not read the terminal.  */
              int nul = open ("/dev/null", O_RDONLY);
              if (nul >= 0)
                {
                  dup2 (nul, 0);
                  close (nul);
                }
            }
          if (fds[1] >= 0)
            {
              dup2 (fds[1], 1);
              close (fds[1]);
              close (fds[0]);
            }
          if (redirect (&cmds[i]) != 0)
            _exit (1);
          int st;
          if (print_builtin (&cmds[i], &st))
            _exit (st);
          int dummy = 0;
          if (builtin (&cmds[i], &dummy))
            _exit (last_status);
          execvp (cmds[i].argv[0], cmds[i].argv);
          err2 (cmds[i].argv[0], errno == ENOENT ? "not found"
                                                 : strerror (errno));
          _exit (127);
        }
      pids[i] = pid;
      if (prev >= 0)
        close (prev);
      if (fds[1] >= 0)
        close (fds[1]);
      prev = fds[0];
    }
  if (prev >= 0)
    close (prev);
  if (bg)
    {
      if (n > 0)
        {
          char b[32];
          snprintf (b, sizeof b, "[%d]\n", (int) pids[n - 1]);
          put (2, b);
        }
      last_status = 0;
      return;
    }
  for (int i = 0; i < n; i++)
    {
      int st;
      if (waitpid (pids[i], &st, 0) == pids[i] && i == n - 1)
        last_status = WIFEXITED (st) ? WEXITSTATUS (st) : 128 + WTERMSIG (st);
    }
}

/* Reads one line from stdin into BUF (at most N-1 bytes).  Returns the
   length, or -1 at the end of input.  */
static int read_line (char *buf, int n)
{
  int len = 0;
  for (;;)
    {
      char c;
      ssize_t r = read (0, &c, 1);
      if (r < 0 && errno == EINTR)
        continue;
      if (r <= 0)
        return len ? len : -1;
      if (c == '\n')
        break;
      if (len < n - 1)
        buf[len++] = c;
    }
  buf[len] = '\0';
  return len;
}

/* Run one line (commands separated by ';' and '&').  */
static void run_line (char *line, struct cmd *cmds, int *done)
{
  char *rest = line;
  while (rest && !*done)
    {
      int n, bg;
      wpos = 0;
      rest = parse (rest, cmds, &n, &bg);
      if (n > 0)
        run_pipeline (cmds, n, done, bg);
      else if (n < 0)
        {
          last_status = 2;
          break;
        }
    }
}

int main (int argc, char **argv)
{
  int prompt = isatty (0) || getenv ("TZD_PROMPT");
  static char line[LINE_MAX_LEN];
  static struct cmd cmds[MAX_CMDS];
  int done = 0;
  if (argc == 3 && strcmp (argv[1], "-c") == 0)
    {
      /* sh -c STRING (sshd runs remote commands, scp and the sftp
         subsystem this way): each line of STRING in turn.  */
      const char *p = argv[2];
      while (*p && !done)
        {
          size_t k = strcspn (p, "\n");
          if (k >= sizeof line)
            {
              err2 ("-c", "line too long");
              return 2;
            }
          memcpy (line, p, k);
          line[k] = '\0';
          run_line (line, cmds, &done);
          p += k;
          if (*p == '\n')
            p++;
        }
      return last_status;
    }
  if (argc > 1)
    {
      err2 ("usage", "sh [-c COMMANDS] (otherwise commands are read from stdin)");
      return 2;
    }
  while (!done)
    {
      /* Background jobs that have finished.  */
      while (waitpid (-1, NULL, WNOHANG) > 0)
        ;
      if (prompt)
        put (1, "tzd# ");
      if (read_line (line, sizeof line) < 0)
        break;
      run_line (line, cmds, &done);
    }
  if (prompt)
    put (1, "\n");
  return last_status;
}
