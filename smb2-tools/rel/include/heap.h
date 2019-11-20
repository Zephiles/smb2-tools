#pragma once

#include <gc/OSAlloc.h>

#include <cstdint>

namespace heap {

struct IndividualHeapVars
{
	int32_t HeapHandle;
	void *EndAddress;
};

struct CustomHeap
{
	gc::OSAlloc::HeapInfo *HeapArray;
	IndividualHeapVars *HeapVars;
	int32_t MaxHeaps;
	void *ArenaStart;
	void *ArenaEnd;
	void *HeapArrayStart;
};

gc::OSAlloc::ChunkInfo *extractChunk(gc::OSAlloc::ChunkInfo *list, gc::OSAlloc::ChunkInfo *chunk);
gc::OSAlloc::ChunkInfo *addChunkToFront(gc::OSAlloc::ChunkInfo *list, gc::OSAlloc::ChunkInfo *chunk);
gc::OSAlloc::ChunkInfo *findChunkInList(gc::OSAlloc::ChunkInfo *list, gc::OSAlloc::ChunkInfo *chunk);
void *clearAndFlushMemory(void *start, uint32_t size);
void *initMemAllocServices(uint32_t size, int32_t maxHeaps);
void *initAlloc(void *arenaStart, void *arenaEnd, int32_t maxHeaps);
int32_t addHeap(uint32_t size, bool removeHeaderSize);
int32_t createHeap(void *start, void *end);
bool destroyHeap(int32_t heapHandle);
void *allocFromArenaLow(uint32_t size);
void *memAlloc(int32_t heap, uint32_t size);
void *allocFromHeap(int32_t heapHandle, uint32_t size);
bool memFree(int32_t heap, void *ptr);
bool freeToHeap(int32_t heapHandle, void *ptr);

extern CustomHeap *Heap;

}