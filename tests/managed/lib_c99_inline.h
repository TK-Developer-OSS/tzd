static inline int twice_static (int x) { return 2 * x; }
inline int twice (int x) { return 2 * x; }
int twice (int x);   /* a declaration without inline: emitted here */
