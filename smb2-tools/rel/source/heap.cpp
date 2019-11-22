#include "heap.h"

#include <gc/OSAlloc.h>
#include <gc/OSCache.h>
#include <gc/OSArena.h>

#include <cstring>

namespace heap {

struct HeapDataStruct HeapData;

gc::OSAlloc::ChunkInfo *extractChunk(
	gc::OSAlloc::ChunkInfo *list, gc::OSAlloc::ChunkInfo *chunk)
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

gc::OSAlloc::ChunkInfo *addChunkToFront(
	gc::OSAlloc::ChunkInfo *list, gc::OSAlloc::ChunkInfo *chunk)
{
	chunk->next = list;
	chunk->prev = nullptr;
	
	if (list)
	{
		list->prev = chunk;
	}
	
	return chunk;
}

gc::OSAlloc::ChunkInfo *findChunkInList(
	gc::OSAlloc::ChunkInfo *list, gc::OSAlloc::ChunkInfo *chunk)
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
	CustomHeapStruct *tempCustomHeap = reinterpret_cast<CustomHeapStruct *>(
		allocFromMainLoopRelocMemory(sizeof(CustomHeapStruct)));
	
	HeapData.CustomHeap = tempCustomHeap;
	
	// Allocate memory for the individual heap vars
	IndividualHeapVars *HeapVars = reinterpret_cast<IndividualHeapVars *>(
		allocFromMainLoopRelocMemory(sizeof(IndividualHeapVars) * maxHeaps));
	
	tempCustomHeap->HeapVars = HeapVars;
	
	// Initialize all of the heap handles to -1
	for (int32_t i = 0; i < maxHeaps; i++)
	{
		HeapVars[i].HeapHandle = -1;
	}
	
	// Round the size up to the nearest multiple of 0x20 bytes
	const uint32_t Alignment = 0x20;
	maxSize = (maxSize + Alignment - 1) & ~(Alignment - 1);
	
	// Allocate the desired memory
	void *ArenaStart = allocFromMainLoopRelocMemory(maxSize);
	
	// Set up the arena end
	void *ArenaEnd = reinterpret_cast<void *>(
		reinterpret_cast<uint32_t>(ArenaStart) + maxSize);
	
	// Init the memory allocation services
	void *HeapArrayStart = initAlloc(ArenaStart, ArenaEnd, maxHeaps);
	
	// Store the start of the heap array
	tempCustomHeap->HeapArrayStart = HeapArrayStart;
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
	if (static_cast<uint32_t>(maxHeaps) > 
		((ArenaEndRaw - ArenaStartRaw) / sizeof(gc::OSAlloc::HeapInfo)))
	{
		return nullptr;
	}
	
	// Put the heap array at the start of the arena
	CustomHeapStruct *tempCustomHeap = HeapData.CustomHeap;
	tempCustomHeap->HeapArray = reinterpret_cast<gc::OSAlloc::HeapInfo *>(arenaStart);
	tempCustomHeap->MaxHeaps = maxHeaps;
	
	// Initialize all members of the heap array
	gc::OSAlloc::HeapInfo *HeapArray = tempCustomHeap->HeapArray;
	for (int32_t i = 0; i < maxHeaps; i++)
	{
		gc::OSAlloc::HeapInfo *Info = &HeapArray[i];
		Info->capacity = -1;
		Info->firstFree = nullptr;
		Info->firstUsed = nullptr;
	}
	
	const uint32_t Alignment = 0x20;
	uint32_t ArraySize = sizeof(gc::OSAlloc::HeapInfo) * maxHeaps;
	
	// Adjust arenaStart to be at the nearest reasonable location
	// Gets rounded up to the nearest multiple of 0x20 bytes
	ArenaStartRaw = ((ArenaStartRaw + ArraySize) + Alignment - 1) & ~(Alignment - 1);
	
	// Round the end down to the nearest multiple of 0x20 bytes
	ArenaEndRaw &= ~(Alignment - 1);
	
	arenaStart = reinterpret_cast<void *>(ArenaStartRaw);
	arenaEnd = reinterpret_cast<void *>(ArenaEndRaw);
	tempCustomHeap->ArenaStart = arenaStart;
	tempCustomHeap->ArenaEnd = arenaEnd;
	
	// Make sure at least one entry can fit in the heap array
	uint32_t MinSize = ((sizeof(gc::OSAlloc::ChunkInfo) + 
		Alignment - 1) & ~(Alignment - 1)) + Alignment;
	
	if (MinSize > (ArenaEndRaw - ArenaStartRaw))
	{
		return nullptr;
	}
	
	return arenaStart;
}

int32_t addHeap(uint32_t size, bool removeHeapInfoSize)
{
	CustomHeapStruct *tempCustomHeap = HeapData.CustomHeap;
	const uint32_t Alignment = 0x20;
	
	if (removeHeapInfoSize)
	{
		// Remove the total heap info size and then round down to the nearest multiple of 0x20 bytes
		uint32_t ArraySize = sizeof(gc::OSAlloc::HeapInfo) * tempCustomHeap->MaxHeaps;
		size = (size - ArraySize) & ~(Alignment - 1);
	}
	else
	{
		// Round the size up to the nearest multiple of 0x20 bytes
		size = (size + Alignment - 1) & ~(Alignment - 1);
	}
	
	void *Start;
	void *End;
	int32_t CurrentHeap = -1;
	IndividualHeapVars *HeapVars = tempCustomHeap->HeapVars;
	
	// Get the starting address
	if (!HeapVars[0].EndAddress)
	{
		// Use the start of the heap array as the address
		Start = tempCustomHeap->HeapArrayStart;
		CurrentHeap = 0;
	}
	else
	{
		// Find the next empty heap
		int32_t Size = tempCustomHeap->MaxHeaps;
		for (int32_t i = 1; i < Size; i++)
		{
			if (!HeapVars[i].EndAddress)
			{
				// Use the ending address of the previous heap
				Start = HeapVars[i - 1].EndAddress;
				CurrentHeap = i;
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
	HeapVars[CurrentHeap].EndAddress = End;
	
	// Create the heap
	int32_t Handle = createHeap(Start, End);
	
	// Make sure the heap was created correctly
	if (Handle < 0)
	{
		return -1;
	}
	
	// Add the new handle to the heap handle array
	HeapVars[CurrentHeap].HeapHandle = Handle;
	return Handle;
}

int32_t createHeap(void *start, void *end)
{
	CustomHeapStruct *tempCustomHeap = HeapData.CustomHeap;
	gc::OSAlloc::HeapInfo *HeapArray = tempCustomHeap->HeapArray;
	
	// Make sure the heap array has been created
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
	
	// Round the start up to the nearest multiple of 0x20 bytes,
	// Round the end down to the nearest multiple of 0x20 bytes
	const uint32_t Alignment = 0x20;
	StartRaw = (StartRaw + Alignment - 1) & ~(Alignment - 1);
	EndRaw &= ~(Alignment - 1);
	
	// Make sure the new start and end are valid
	if (StartRaw >= EndRaw)
	{
		return -1;
	}
	
	// Make sure the start and end are a subset of the arena start and arena end
	if ((reinterpret_cast<uint32_t>(tempCustomHeap->ArenaStart) > StartRaw) || 
		EndRaw > reinterpret_cast<uint32_t>(tempCustomHeap->ArenaEnd))
	{
		return -1;
	}
	
	// Make sure at least one entry can fit in the heap
	uint32_t MinSize = ((sizeof(gc::OSAlloc::ChunkInfo) + 
		Alignment - 1) & ~(Alignment - 1)) + Alignment;
	
	if (MinSize > (EndRaw - StartRaw))
	{
		return -1;
	}
	
	// Get the next free spot
	int32_t MaxHeaps = tempCustomHeap->MaxHeaps;
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
	CustomHeapStruct *tempCustomHeap = HeapData.CustomHeap;
	gc::OSAlloc::HeapInfo *HeapArray = tempCustomHeap->HeapArray;
	
	// Make sure the heap array has been created
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
	if (heapHandle > tempCustomHeap->MaxHeaps)
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

void *allocFromMainLoopRelocMemory(uint32_t size)
{
	// Round the size up to the nearest multiple of 0x20 bytes
	const uint32_t Alignment = 0x20;
	size = (size + Alignment - 1) & ~(Alignment - 1);
	
	// Take the memory from the main game loop's relocation data
	uint32_t AddressRaw = reinterpret_cast<uint32_t>(
		HeapData.RelLoaderAddresses.RelocationDataArena);
	
	// Increment the main game loop's relocation data by the size
	HeapData.RelLoaderAddresses.RelocationDataArena = reinterpret_cast<void *>(AddressRaw + size);
	
	return clearAndFlushMemory(reinterpret_cast<void *>(AddressRaw), size);
}

void *memAlloc(int32_t heap, uint32_t size)
{
	// Make sure the heap does not exceed the total number of heaps
	CustomHeapStruct *tempCustomHeap = HeapData.CustomHeap;
	if (heap >= tempCustomHeap->MaxHeaps)
	{
		return nullptr;
	}
	
	// Allocate the desired memory
	void *AllocatedMemory = allocFromHeap(tempCustomHeap->HeapVars[heap].HeapHandle, size);
	
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
	CustomHeapStruct *tempCustomHeap = HeapData.CustomHeap;
	gc::OSAlloc::HeapInfo *HeapArray = tempCustomHeap->HeapArray;
	
	// Make sure the heap array has been created
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
	if (heapHandle > tempCustomHeap->MaxHeaps)
	{
		return nullptr;
	}
	
	// Make sure the total size available in the current heap is valid
	if (HeapArray[heapHandle].capacity < 0)
	{
		return nullptr;
	}
	
	// Enlarge size to the smallest possible chunk size
	const uint32_t Alignment = 0x20;
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
	
	int32_t MinSize = ((sizeof(gc::OSAlloc::ChunkInfo) + 
		Alignment - 1) & ~(Alignment - 1)) + Alignment;
	
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
	CustomHeapStruct *tempCustomHeap = HeapData.CustomHeap;
	if (heap >= tempCustomHeap->MaxHeaps)
	{
		return false;
	}
	
	return freeToHeap(tempCustomHeap->HeapVars[heap].HeapHandle, ptr);
}

bool freeToHeap(int32_t heapHandle, void *ptr)
{
	CustomHeapStruct *tempCustomHeap = HeapData.CustomHeap;
	gc::OSAlloc::HeapInfo *HeapArray = tempCustomHeap->HeapArray;
	
	// Make sure the heap array has been created
	if (!HeapArray)
	{
		return false;
	}
	
	const uint32_t Alignment = 0x20;
	uint32_t PtrRaw = reinterpret_cast<uint32_t>(ptr);
	
	uint32_t HeaderSize = (sizeof(gc::OSAlloc::ChunkInfo) + 
		Alignment - 1) & ~(Alignment - 1);
	
	// Make sure ptr is in the range of the arenas
	if (((reinterpret_cast<uint32_t>(tempCustomHeap->ArenaStart) + HeaderSize) > PtrRaw) || 
		(PtrRaw >= reinterpret_cast<uint32_t>(tempCustomHeap->ArenaEnd)))
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