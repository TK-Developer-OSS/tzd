/* task #61: _Atomic is rejected without an explicit #pragma tzd atomics. */
struct rc { _Atomic int val; };
int main (void) { struct rc r; r.val = 0; return 0; }
