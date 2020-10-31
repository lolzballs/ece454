#include <stdlib.h>

#include "mm.h"

int main() {
    mem_init();

    mm_init();

    mm_malloc(10);
    mm_malloc(20);
}

