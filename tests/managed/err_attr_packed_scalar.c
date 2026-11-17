/* packed on a plain variable means nothing in GCC; here it is an error
   rather than a silent drop.  */
int x __attribute__((packed));
int main(void){ return x; }
