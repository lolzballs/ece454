#include <stdlib.h>

#include "mm.h"

int main() {
    mem_init();

    mm_init();

    void *a = mm_malloc(10);
    void *b = mm_malloc(10);
    mm_free(b);
    void *c = mm_malloc(10);
    void *d = mm_malloc(10);
    mm_free(d);
    void *e = mm_malloc(10);
    mm_free(a);
    void *f = mm_malloc(10);
    mm_free(c);
}

