/* Reading heap bytes that were never written traps (task #8, CWE-908):
   only the first 16 bytes are written, byte 32 is read.  */
#include <stdlib.h>
#include <string.h>
int main(void)
{
    char *reply = malloc(64);
    memcpy(reply, "OK-header-16byte", 16);
    return reply[32];
}
