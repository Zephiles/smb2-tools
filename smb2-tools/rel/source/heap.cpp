#include "heap.h"

#include <gc/OSAlloc.h>
#include <gc/OSCache.h>
#include <gc/OSArena.h>

#include <cstring>

namespace heap {

struct CustomHeap *Heap;

gc::OSAlloc::ChunkInfo *extractChunk(gc::OSAlloc::ChunkInfo *list, gc::OSAlloc::ChunkInfo *chunk)
{
	if (chunk->next)
	{
		chunk->next->prev = chunk->prev;
	}
	
	if (!chunk->prev)
	{
		return chunk->next;
	}
	else
	{
		chunk->prev->next = chunk->next;
		return list;
	}
}

gc::OSAlloc::ChunkInfo *addChunkToFront(gc::OSAlloc::ChunkInfo *list, gc::OSAlloc::ChunkInfo *chunk)
{
	chunk->next = list;
	chunk->prev = nullptr;
	
	if (list)
	{
		list->prev = chunk;
	}
	
	return chunk;
}

gc::OSAlloc::ChunkInfo *findChunkInList(gc::OSAlloc::ChunkInfo *list, gc::OSAlloc::ChunkInfo *chunk)
{
	for (; list; list = list->next)
	{
		if (list == chunk)
		{
			return list;
		}
	}
	return nullptr;
}

void *clearAndFlushMemory(void *start, uint32_t size)
{
	// Clear the memory
	memset(start, 0, size);
	
	// Flush the memory
	gc::OSCache::DCFlushRange(start, size);
	
	// Return the address
	return start;
}

void *initMemAllocServices(uint32_t maxSize, int32_t maxHeaps)
{
	// Make sure the number of heaps is valid
	if (maxHeaps < 1)
	{
		maxHeaps = 1;
	}
	
	// Allocate memory for the heap array
	Heap = reinterpret_cast<CustomHeap *>(allocFromArenaLow(sizeof(CustomHeap)));
	
	// Allocate memory for the individual heap vars
	Heap->HeapVars = reinterpret_cast<IndividualHeapVars *>(allocFromArenaLow(sizeof(IndividualHeapVars) * maxHeaps));
	
	// Initialize all of the heap handles to -1
	for (int32_t i = 0; i < maxHeaps; i++)
	{
		Heap->HeapVars[i].HeapHandle = -1;
	}
	
	// Round the size up to the nearest 32 bytes
	const uint32_t Alignment = 32;
	maxSize = (maxSize + Alignment - 1) & ~(Alignment - 1);
	
	void *LeftoverRelLoaderMem = reinterpret_cast<void *>(*reinterpret_cast<uint32_t *>(0x8000452C));
	void *ArenaLow;
	
	// Allocate the desired memory
	if (LeftoverRelLoaderMem)
	{
		const uint32_t LeftoverRelLoaderMemSize = 0xA220;
		if (maxSize <= LeftoverRelLoaderMemSize)
		{
			// Use the leftover memory from the rel loader
			ArenaLow = clearAndFlushMemory(LeftoverRelLoaderMem, LeftoverRelLoaderMemSize);
		}
		else
		{
			ArenaLow = allocFromArenaLow(maxSize);
		}
	}
	else
	{
		ArenaLow = allocFromArenaLow(maxSize);
	}
	
	// Set up the arena end
	void *ArenaEnd = reinterpret_cast<void *>(reinterpret_cast<uint32_t>(ArenaLow) + maxSize);
	
	// Init the memory allocation services
	void *HeapArrayStart = initAlloc(ArenaLow, ArenaEnd, maxHeaps);
	
	// Store the start of the heap array
	Heap->HeapArrayStart = HeapArrayStart;
	return HeapArrayStart;
}

void *initAlloc(void *arenaStart, void *arenaEnd, int32_t maxHeaps)
{
	// Make sure the number of heaps is valid
	if (maxHeaps < 1)
	{
		maxHeaps = 1;
	}
	
	uint32_t ArenaStartRaw = reinterpret_cast<uint32_t>(arenaStart);
	uint32_t ArenaEndRaw = reinterpret_cast<uint32_t>(arenaEnd);
	
	// Make sure arenaStart is before arenaEnd
	if (ArenaStartRaw >= ArenaEndRaw)
	{
		return nullptr;
	}
	
	// Make sure the maximum number of heaps is valid
	if (static_cast<uint32_t>(maxHeaps) > ((ArenaEndRaw - ArenaStartRaw) / sizeof(gc::OSAlloc::HeapInfo)))
	{
		return nullptr;
	}
	
	// Put the heap array at the start of the arena
	Heap->HeapArray = reinterpret_cast<gc::OSAlloc::HeapInfo *>(arenaStart);
	Heap->MaxHeaps = maxHeaps;
	
	// Initialize all members of the heap array
	gc::OSAlloc::HeapInfo *HeapArray = Heap->HeapArray;
	for (int32_t i = 0; i < maxHeaps; i++)
	{
		gc::OSAlloc::HeapInfo *Info = &HeapArray[i];
		Info->capacity = -1;
		Info->firstFree = nullptr;
		Info->firstUsed = nullptr;
	}
	
	const uint32_t Alignment = 32;
	uint32_t ArraySize = sizeof(gc::OSAlloc::HeapInfo) * maxHeaps;
	
	// Adjust arenaStart to be at the nearest reasonable location; gets rounded up to the nearest 32 bytes
	ArenaStartRaw = ((ArenaStartRaw + ArraySize) + Alignment - 1) & ~(Alignment - 1);
	
	// Round the end down to the nearest 32 bytes
	ArenaEndRaw &= ~(Alignment - 1);
	
	arenaStart = reinterpret_cast<void *>(ArenaStartRaw);
	arenaEnd = reinterpret_cast<void *>(ArenaEndRaw);
	Heap->ArenaStart = arenaStart;
	Heap->ArenaEnd = arenaEnd;
	
	// Make sure at least one entry can fit in the heap array
	uint32_t MinSize = ((sizeof(gc::OSAlloc::ChunkInfo) + Alignment - 1) & ~(Alignment - 1)) + Alignment;
	if (MinSize > (ArenaEndRaw - ArenaStartRaw))
	{
		return nullptr;
	}
	
	return arenaStart;
}

int32_t addHeap(uint32_t size, bool removeHeaderSize)
{
	const uint32_t Alignment = 32;
	if (removeHeaderSize)
	{
		// Remove the header size and then round up to the nearest 32 bytes
		size -= (sizeof(gc::OSAlloc::ChunkInfo) + Alignment - 1) & ~(Alignment - 1);
	}
	
	// Round the size up to the nearest 32 bytes
	size = (size + Alignment - 1) & ~(Alignment - 1);
	
	void *Start;
	void *End;
	int32_t CurrentHeap = -1;
	
	// Get the starting address
	if (!Heap->HeapVars[0].EndAddress)
	{
		// Use the start of the heap array as the address
		Start = Heap->HeapArrayStart;
		CurrentHeap = 0;
	}
	else
	{
		// Use the ending address of the previous heap
		int32_t Size = Heap->MaxHeaps;
		for (int32_t i = (Size - 1); i >= 0; i--)
		{
			Start = Heap->HeapVars[i].EndAddress;
			if (Start)
			{
				CurrentHeap = i + 1;
				break;
			}
		}
	}
	
	// Make sure the current heap index is valid
	if (CurrentHeap < 0)
	{
		return -1;
	}
	
	// Set the new end address
	End = reinterpret_cast<void *>(reinterpret_cast<uint32_t>(Start) + size);
	
	// Set the end address for the current heap
	Heap->HeapVars[CurrentHeap].EndAddress = End;
	
	// Create the heap
	int32_t Handle = createHeap(Start, End);
	
	// Make sure the heap was created correctly
	if (Handle < 0)
	{
		return -1;
	}
	
	// Add the new handle to the heap handle array
	Heap->HeapVars[CurrentHeap].HeapHandle = Handle;
	return Handle;
}

int32_t createHeap(void *start, void *end)
{
	// Make sure the heap array has been created
	gc::OSAlloc::HeapInfo *HeapArray = Heap->HeapArray;
	if (!HeapArray)
	{
		return -1;
	}
	
	uint32_t StartRaw = reinterpret_cast<uint32_t>(start);
	uint32_t EndRaw = reinterpret_cast<uint32_t>(end);
	
	// Make sure the start and end are valid
	if (StartRaw >= EndRaw)
	{
		return -1;
	}
	
	// Round the start up to the nearest 32 bytes, and the end down to the nearest 32 bytes
	const uint32_t Alignment = 32;
	StartRaw = (StartRaw + Alignment - 1) & ~(Alignment - 1);
	EndRaw &= ~(Alignment - 1);
	
	// Make sure the new start and end are valid
	if (StartRaw >= EndRaw)
	{
		return -1;
	}
	
	// Make sure the start and end are a subset of the arena start and arena end
	if ((reinterpret_cast<uint32_t>(Heap->ArenaStart) > StartRaw) || 
		EndRaw > reinterpret_cast<uint32_t>(Heap->ArenaEnd))
	{
		return -1;
	}
	
	// Make sure at least one entry can fit in the heap
	uint32_t MinSize = ((sizeof(gc::OSAlloc::ChunkInfo) + Alignment - 1) & ~(Alignment - 1)) + Alignment;
	if (MinSize > (EndRaw - StartRaw))
	{
		return -1;
	}
	
	// Get the next free spot
	int32_t MaxHeaps = Heap->MaxHeaps;
	for (int32_t i = 0; i < MaxHeaps; i++)
	{
		gc::OSAlloc::HeapInfo *Info = &HeapArray[i];
		if (Info->capacity < 0)
		{
			int32_t Size = EndRaw - StartRaw;
			Info->capacity = Size;
			
			gc::OSAlloc::ChunkInfo *tempChunk = reinterpret_cast<gc::OSAlloc::ChunkInfo *>(StartRaw);
			tempChunk->prev = nullptr;
			tempChunk->next = nullptr;
			tempChunk->size = Size;
			
			Info->firstFree = tempChunk;
			Info->firstUsed = nullptr;
			
			return i;
		}
	}
	
	return -1;
}

bool destroyHeap(int32_t heapHandle)
{
	// Make sure the heap array has been created
	gc::OSAlloc::HeapInfo *HeapArray = Heap->HeapArray;
	if (!HeapArray)
	{
		return false;
	}
	
	// Make sure the heap handle is valid
	if (heapHandle < 0)
	{
		return false;
	}
	
	// Make sure the heap handle does not exceed the total number of heaps
	if (heapHandle > Heap->MaxHeaps)
	{
		return false;
	}
	
	// Make sure the total size available in the current heap is valid
	if (HeapArray[heapHandle].capacity < 0)
	{
		return false;
	}
	
	gc::OSAlloc::HeapInfo *Info = &HeapArray[heapHandle];
	Info->capacity = -1;
	Info->firstFree = nullptr;
	Info->firstUsed = nullptr;
	
	return true;
}

void *allocFromArenaLow(uint32_t size)
{
	// Round the size up to the nearest 32 bytes
	const uint32_t Alignment = 32;
	size = (size + Alignment - 1) & ~(Alignment - 1);
	
	return clearAndFlushMemory(gc::OSArena::OSAllocFromArenaLo(size, Alignment), size);
}

void *memAlloc(int32_t heap, uint32_t size)
{
	// Make sure the heap does not exceed the total number of heaps
	if (heap >= Heap->MaxHeaps)
	{
		return nullptr;
	}
	
	// Allocate the desired memory
	void *AllocatedMemory = allocFromHeap(Heap->HeapVars[heap].HeapHandle, size);
	
	if (AllocatedMemory)
	{
		return clearAndFlushMemory(AllocatedMemory, size);
	}
	else
	{
		return nullptr;
	}
}

void *allocFromHeap(int32_t heapHandle, uint32_t size)
{
	// Make sure the heap array has been created
	gc::OSAlloc::HeapInfo *HeapArray = Heap->HeapArray;
	if (!HeapArray)
	{
		return nullptr;
	}
	
	// Make sure the size is valid
	if (static_cast<int32_t>(size) < 0)
	{
		return nullptr;
	}
	
	// Make sure the heap handle is valid
	if (heapHandle < 0)
	{
		return nullptr;
	}
	
	// Make sure the heap handle does not exceed the total number of heaps
	if (heapHandle > Heap->MaxHeaps)
	{
		return nullptr;
	}
	
	// Make sure the total size available in the current heap is valid
	if (HeapArray[heapHandle].capacity < 0)
	{
		return nullptr;
	}
	
	// Enlarge size to the smallest possible chunk size
	const uint32_t Alignment = 32;
	size += (sizeof(gc::OSAlloc::ChunkInfo) + Alignment - 1) & ~(Alignment - 1);
	size = (size + Alignment - 1) & ~(Alignment - 1);
	
	gc::OSAlloc::HeapInfo *Info = &HeapArray[heapHandle];
	gc::OSAlloc::ChunkInfo *tempChunk = nullptr;
	
	// Find a memory area large enough
	for (tempChunk = Info->firstFree; tempChunk; tempChunk = tempChunk->next)
	{
		if (static_cast<int32_t>(size) <= tempChunk->size)
		{
			break;
		}
	}
	
	// Make sure the found region is valid
	if (!tempChunk)
	{
		return nullptr;
	}
	
	// Make sure the memory region is properly aligned
	if ((reinterpret_cast<uint32_t>(tempChunk) & (Alignment - 1)) != 0)
	{
		return nullptr;
	}
	
	int32_t LeftoverSize = tempChunk->size - static_cast<int32_t>(size);
	int32_t MinSize = ((sizeof(gc::OSAlloc::ChunkInfo) + Alignment - 1) & ~(Alignment - 1)) + Alignment;
	
	// Check if the current chunk can be split into two pieces
	if (LeftoverSize < MinSize)
	{
		// Too small to split, so just extract it
		Info->firstFree = extractChunk(Info->firstFree, tempChunk);
	}
	else
	{
		// Large enough to split
		tempChunk->size = static_cast<int32_t>(size);
		
		// Create a new chunk
		gc::OSAlloc::ChunkInfo *NewChunk = reinterpret_cast<gc::OSAlloc::ChunkInfo *>(
			reinterpret_cast<uint32_t>(tempChunk) + size);
		
		NewChunk->size = LeftoverSize;
		
		NewChunk->prev = tempChunk->prev;
		NewChunk->next = tempChunk->next;
		
		if (NewChunk->next)
		{
			NewChunk->next->prev = NewChunk;
		}
		
		if (NewChunk->prev)
		{
			NewChunk->prev->next = NewChunk;
		}
		else
		{
			// Make sure the free memory region is the temp chunk
			if (Info->firstFree != tempChunk)
			{
				return nullptr;
			}
			
			Info->firstFree = NewChunk;
		}
	}
	
	// Add the chunk to the allocated list
	Info->firstUsed = addChunkToFront(Info->firstUsed, tempChunk);
	
	// Add the header size to the chunk and then return it
	return reinterpret_cast<void *>(reinterpret_cast<uint32_t>(tempChunk) + 
		((sizeof(gc::OSAlloc::ChunkInfo) + Alignment - 1) & ~(Alignment - 1)));
}

bool memFree(int32_t heap, void *ptr)
{
	// Make sure the heap does not exceed the total number of heaps
	if (heap >= Heap->MaxHeaps)
	{
		return false;
	}
	
	return freeToHeap(Heap->HeapVars[heap].HeapHandle, ptr);
}

bool freeToHeap(int32_t heapHandle, void *ptr)
{
	// Make sure the heap array has been created
	gc::OSAlloc::HeapInfo *HeapArray = Heap->HeapArray;
	if (!HeapArray)
	{
		return false;
	}
	
	const uint32_t Alignment = 32;
	uint32_t PtrRaw = reinterpret_cast<uint32_t>(ptr);
	uint32_t HeaderSize = (sizeof(gc::OSAlloc::ChunkInfo) + Alignment - 1) & ~(Alignment - 1);
	
	// Make sure ptr is in the range of the arenas
	if (((reinterpret_cast<uint32_t>(Heap->ArenaStart) + HeaderSize) > PtrRaw) || 
		(PtrRaw >= reinterpret_cast<uint32_t>(Heap->ArenaEnd)))
	{
		return false;
	}
	
	// Make sure ptr is properly aligned
	if ((PtrRaw & (Alignment - 1)) != 0)
	{
		return false;
	}
	
	// Make sure the total size available in the current heap is valid
	if (HeapArray[heapHandle].capacity < 0)
	{
		return false;
	}
	
	// Remove the header size from ptr, as the value stored in the list does not include it
	gc::OSAlloc::ChunkInfo *tempChunk = reinterpret_cast<gc::OSAlloc::ChunkInfo *>(PtrRaw - HeaderSize);
	gc::OSAlloc::HeapInfo *Info = &HeapArray[heapHandle];
	
	// Make sure ptr is actually allocated
	if (!findChunkInList(Info->firstUsed, tempChunk))
	{
		return false;
	}
	
	// Extract the chunk from the allocated list
	Info->firstUsed = extractChunk(Info->firstUsed, tempChunk);
	
	// Add in sorted order to the free list
	Info->firstFree = gc::OSAlloc::DLInsert(Info->firstFree, tempChunk);
	return true;
}

}