#pragma once

#include <gc/OSAlloc.h>

#include <cstdint>

namespace heap {

struct RelLoaderGlobalAddresses
{
    void *RelocationDataArena;
    void *RelocationDataStart; // Also the custom REL module start
    void *CustomRelBSSAreaStart;
    void *MainLoopRelLocation;
    void *MainLoopBSSLocation;
    
    RelLoaderGlobalAddresses()
    {
        RelocationDataArena = *reinterpret_cast<uint32_t **>(0x8000452C);
        RelocationDataStart = *reinterpret_cast<uint32_t **>(0x80004534);
        CustomRelBSSAreaStart = *reinterpret_cast<uint32_t **>(0x80004530);
        MainLoopRelLocation = *reinterpret_cast<uint32_t **>(0x80004524);
        MainLoopBSSLocation = *reinterpret_cast<uint32_t **>(0x80004528);
    }
};

struct IndividualHeapVars
{
    int32_t HeapHandle;
    void *EndAddress;
};

struct CustomHeapStruct
{
    gc::OSAlloc::HeapInfo *HeapArray;
    IndividualHeapVars *HeapVars;
    int32_t MaxHeaps;
    void *ArenaStart;
    void *ArenaEnd;
    void *HeapArrayStart;
};

struct HeapDataStruct
{
    CustomHeapStruct *CustomHeap;
    RelLoaderGlobalAddresses RelLoaderAddresses;
};

gc::OSAlloc::ChunkInfo *extractChunk(gc::OSAlloc::ChunkInfo *list, gc::OSAlloc::ChunkInfo *chunk);
gc::OSAlloc::ChunkInfo *addChunkToFront(gc::OSAlloc::ChunkInfo *list, gc::OSAlloc::ChunkInfo *chunk);
gc::OSAlloc::ChunkInfo *findChunkInList(gc::OSAlloc::ChunkInfo *list, gc::OSAlloc::ChunkInfo *chunk);
void *clearAndFlushMemory(void *start, uint32_t size);
void *initMemAllocServices(uint32_t size, int32_t maxHeaps);
void *initAlloc(void *arenaStart, void *arenaEnd, int32_t maxHeaps);
int32_t addHeap(uint32_t size, bool removeHeapInfoSize);
int32_t createHeap(void *start, void *end);
bool destroyHeap(int32_t heapHandle);
void *allocFromMainLoopRelocMemory(uint32_t size);
void *memAlloc(int32_t heap, uint32_t size);
void *allocFromHeap(int32_t heapHandle, uint32_t size);
bool memFree(int32_t heap, void *ptr);
bool freeToHeap(int32_t heapHandle, void *ptr);

extern HeapDataStruct HeapData;

}