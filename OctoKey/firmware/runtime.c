#include <stddef.h>
void *memcpy(void *d,const void *s,size_t n) { unsigned char *p=d;const unsigned char *q=s;while(n--) *p++=*q++;return d; }
void *memset(void *d,int c,size_t n) { unsigned char *p=d;while(n--) *p++=(unsigned char)c;return d; }
int memcmp(const void *a,const void *b,size_t n) { const unsigned char *p=a,*q=b;while(n--) { if(*p!=*q) return *p-*q; ++p;++q; }return 0; }
size_t strlen(const char *s) { size_t n=0;while(s[n]) ++n;return n; }
void __aeabi_memcpy(void *d,const void *s,size_t n) { memcpy(d,s,n); }
void __aeabi_memcpy4(void *d,const void *s,size_t n) { memcpy(d,s,n); }
void __aeabi_memclr(void *d,size_t n) { memset(d,0,n); }
void __aeabi_memclr4(void *d,size_t n) { memset(d,0,n); }
