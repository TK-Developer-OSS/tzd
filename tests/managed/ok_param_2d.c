/* A parameter's outer array dimension is dropped (it is a pointer); the
   inner ones stay part of the element type (Apache's mod_imap:
   double coords[MAXVERTS][2]).  A size naming an earlier parameter is
   allowed there (glibc's regexec).  */
static double sum (int n, double c[n][2])
{
    double s = 0;
    for (int i = 0; i < n; i++)
        s += c[i][0] + c[i][1];
    return s;
}

int main (void)
{
    double c[3][2] = { { 1, 2 }, { 3, 4 }, { 5, 6 } };
    return sum (3, c) == 21 ? 0 : 1;
}
