/* (buffer, length) host calls within bounds work as before.  */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

int main(void)
{
    char a[32], b[64], c[8];
    FILE *f = fopen("/dev/null", "r");
    time_t t = 0;
    if (snprintf(a, sizeof a, "%d-%s", 42, "x") != 4 || strcmp(a, "42-x"))
        return 1;
    if (strftime(b, sizeof b, "%Y", gmtime(&t)) != 4 || strcmp(b, "1970"))
        return 2;
    if (fgets(c, sizeof c, f) != NULL)
        return 3;
    if (getcwd(b, sizeof b) == NULL && getcwd(NULL, 0) == NULL)
        return 4;
    fclose(f);
    return write(1, "", 0);
}
