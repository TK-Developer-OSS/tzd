/* TzD directives for building OpenSSL 3.5.6 under TzD (task #61).  Every file
   in the TzD OpenSSL build is compiled with this header force-included
   (tests/openssl/build_tzd.sh: bin/tzd -include this), so the exceptions
   OpenSSL needs are stated in one auditable place; the default elsewhere
   stays strict.

   - atomics: refcount.h's C11 _Atomic reference counting and the __atomic
     builtins are accepted.  _Atomic scalars keep their representation and
     the atomic access is still bounds-checked (an out-of-bounds atomic
     traps), so no safety is given up.
   - returns_twice: the __sigsetjmp_cancel declaration reached via pthread.h
     is accepted on the declaration only.  OpenSSL never calls it; this does
     NOT make an actual setjmp/longjmp safe (a longjmp skips the two-phase
     wipe -- a separate task).  */
#pragma tzd atomics
#pragma tzd returns_twice
