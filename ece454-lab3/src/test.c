#include <assert.h>
#include <stdlib.h>

#include "mm.h"

int main() {
    mem_init();

    mm_init();

    void *a = mm_malloc(10);
    void *b = mm_malloc(10);
    void *c = mm_malloc(10);
    void *d = mm_malloc(10);
    void *e = mm_malloc(10);

    mm_free(b);
    mm_free(a);

    assert(mm_check());

    mm_free(d);

    mm_free(c);

    assert(mm_check());
}

