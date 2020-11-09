#include "heap.h"

#include <cstddef>

void *operator new(std::size_t size)
{
    return heap::memAlloc(0, size);
}
void *operator new[](std::size_t size)
{
    return heap::memAlloc(0, size);
}
void operator delete(void *ptr)
{
    heap::memFree(0, ptr);
}
void operator delete[](void *ptr)
{
    heap::memFree(0, ptr);
}
void operator delete(void *ptr, std::size_t size)
{
    heap::memFree(0, ptr);
}
void operator delete[](void *ptr, std::size_t size)
{
    heap::memFree(0, ptr);
}
