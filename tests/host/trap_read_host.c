/* A length larger than the buffer is caught before the host's read()
   writes past it (buffer-length rules, task #14).  */
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
    char buf[16];
    int fd = open("/dev/zero", O_RDONLY);
    if (read(fd, buf, sizeof buf) != sizeof buf)   /* ok */
        return 1;
    read(fd, buf, 64);                             /* traps */
    return 0;
}
