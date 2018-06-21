#ifndef Minisat_XAlloc_h
#define Minisat_XAlloc_h

#include <errno.h>
#include <stdlib.h>

namespace CDCL {

//=================================================================================================
// Simple layer on top of malloc/realloc to catch out-of-memory situtaions and provide some typing:

class OutOfMemoryException{};
static inline void* xrealloc(void *ptr, size_t size)
{
    void* mem = realloc(ptr, size);
    if (mem == NULL && errno == ENOMEM){
        throw OutOfMemoryException();
    }else
        return mem;
}

//=================================================================================================
}

#endif
