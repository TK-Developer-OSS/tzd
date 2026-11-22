struct anon { int k; union { int ui; float uf; }; int last; };
int main(void){ struct anon an = { .ui = 5, 6 }; return an.last; }
