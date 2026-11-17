/* HMAC-SHA256 via OpenSSL's libcrypto, for tests/openssl/run_hmac.sh: the
   output is compared against the reference (GCC) OpenSSL to show the
   TzD-compiled libcrypto computes correct crypto (task #59/#61).  */
#include <stdio.h>
#include <string.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
int main (void)
{
  const char *key = "key";
  const char *data = "The quick brown fox jumps over the lazy dog";
  unsigned char out[32];
  unsigned int n = 0;
  if (!HMAC (EVP_sha256 (), key, 3, (const unsigned char *) data,
             strlen (data), out, &n)) {
    printf ("HMAC failed\n");
    return 1;
  }
  for (unsigned i = 0; i < n; i++)
    printf ("%02x", out[i]);
  printf ("\n");
  return 0;
}
