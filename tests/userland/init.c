/* init for the TzD guest (task #42): PID 1, compiled with TzD.
 *
 * Mounts /proc, /sys and /dev, runs /etc/rc (if any) with the shell, then
 * runs /bin/sh on the console (no getty for now, TK 2026-11-13).  When the shell exits, init syncs and powers
 * the machine off.  Built by tests/userland/build.sh (bin/tzd --host
 * -static).  */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>

static void say (const char *s)
{
  write (1, s, strlen (s));
}

static void try_mount (const char *src, const char *dir, const char *type)
{
  mkdir (dir, 0755);
  if (mount (src, dir, type, 0, NULL) != 0 && errno != EBUSY)
    {
      say ("init: cannot mount ");
      say (dir);
      say ("\n");
    }
}

int main (void)
{
  /* The console as stdin/stdout/stderr (the kernel opens it for PID 1
     when /dev/console exists; do it again in case it did not).  */
  int fd = open ("/dev/console", O_RDWR);
  if (fd >= 0)
    {
      dup2 (fd, 0);
      dup2 (fd, 1);
      dup2 (fd, 2);
      if (fd > 2)
        close (fd);
    }

  try_mount ("proc", "/proc", "proc");
  try_mount ("sys", "/sys", "sysfs");
  try_mount ("dev", "/dev", "devtmpfs");
  /* Pseudo-terminals for telnetd's logins.  */
  try_mount ("devpts", "/dev/pts", "devpts");
  setenv ("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
  setenv ("HOME", "/", 1);
  say ("TZD-INIT: init (TzD) started\n");

  /* /etc/rc, if there is one: start-up commands read by the shell
     (network, servers).  */
  if (access ("/etc/rc", R_OK) == 0)
    {
      pid_t rc = fork ();
      if (rc == 0)
        {
          int in = open ("/etc/rc", O_RDONLY);
          if (in >= 0)
            {
              dup2 (in, 0);
              close (in);
            }
          char *argv[] = { "sh", NULL };
          execv ("/bin/sh", argv);
          _exit (127);
        }
      if (rc > 0)
        {
          int status;
          while (waitpid (rc, &status, 0) < 0 && errno == EINTR)
            ;
        }
      /* The console again for the interactive shell.  */
      int fd = open ("/dev/console", O_RDWR);
      if (fd >= 0)
        {
          dup2 (fd, 0);
          if (fd > 2)
            close (fd);
        }
    }

  pid_t pid = fork ();
  if (pid < 0)
    say ("init: fork failed\n");
  else if (pid == 0)
    {
      char *argv[] = { "sh", NULL };
      execv ("/bin/sh", argv);
      say ("init: cannot run /bin/sh\n");
      _exit (127);
    }
  else
    {
      /* Reap every child; stop when the shell is gone.  */
      for (;;)
        {
          int status;
          pid_t w = wait (&status);
          if (w == pid || (w < 0 && errno == ECHILD))
            break;
        }
    }

  say ("TZD-POWEROFF\n");
  sync ();
  reboot (RB_POWER_OFF);
  for (;;)
    pause ();
}
