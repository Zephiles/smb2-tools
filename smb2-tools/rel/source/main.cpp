#include "mod.h"
#include "patch.h"
#include "assembly.h"
#include "heap.h"

#include <gc/OSModule.h>
#include <gc/OSAlloc.h>
#include <gc/OSError.h>

#include <cinttypes>

namespace mod {

bool Mod::performRelPatches(gc::OSModule::OSModuleInfo *newModule, void *bss)
{
	// Call the original function immediately, as the REL file should be linked before applying patches
	const bool Result = mPFN_OSLink_trampoline(newModule, bss);
	
	// Make sure a REL file is currently loaded
	if (!Result)
	{
		return Result;
	}
	
	// uint32_t NewModuleRaw = reinterpret_cast<uint32_t>(newModule);
	switch (newModule->id)
	{
		default:
		{
			return Result;
		}
	}
}

void Mod::performAssemblyPatches()
{
#ifdef SMB2_US
	uint32_t Offset = 0x600;
#elif defined SMB2_JP
	uint32_t Offset = 0x604;
#elif defined SMB2_EU
	uint32_t Offset = 0x604;
#endif
	// Inject the run function at the start of the main game loop
	patch::writeBranchBL(reinterpret_cast<void *>(reinterpret_cast<uint32_t>(
		heap::HeapData.RelLoaderAddresses.MainLoopRelLocation) + Offset), 
		reinterpret_cast<void *>(StartMainLoopAssembly));
	
	/* Remove OSReport call ``PERF : event is still open for CPU!`` 
	since it reports every frame, and thus clutters the console */
#ifdef SMB2_US
	// Only needs to be applied to the US version
	uint32_t *Address = reinterpret_cast<uint32_t *>(0x80033E9C);
	*Address = 0x60000000; // nop
	
	// Clear the cache for the address
	patch::clear_DC_IC_Cache(Address, sizeof(uint32_t));
#endif
}

void checkHeaps()
{
	heap::CustomHeapStruct *tempCustomHeap = heap::HeapData.CustomHeap;
	gc::OSAlloc::HeapInfo *HeapArray = tempCustomHeap->HeapArray;
	int32_t TotalHeaps = tempCustomHeap->MaxHeaps;
	
	for (int32_t i = 0; i < TotalHeaps; i++)
	{
		const gc::OSAlloc::HeapInfo &tempHeap = HeapArray[i];
		bool valid = true;
		
		gc::OSAlloc::ChunkInfo *currentChunk = nullptr;
		gc::OSAlloc::ChunkInfo *prevChunk = nullptr;
		for (currentChunk = tempHeap.firstUsed; currentChunk; currentChunk = currentChunk->next)
		{
			// Check pointer sanity
			auto checkIfPointerIsValid = [](void *ptr)
			{
				uint32_t ptrRaw = reinterpret_cast<uint32_t>(ptr);
				return (ptrRaw >= 0x80000000) && (ptrRaw < 0x81800000);
			};
			
			if (!checkIfPointerIsValid(currentChunk))
			{
				valid = false;
				break;
			}
			
			// Sanity check size
			if (currentChunk->size >= 0x1800000)
			{
				valid = false;
				break;
			}

			// Check linked list integrity
			if (prevChunk != currentChunk->prev)
			{
				valid = false;
				break;
			}

			prevChunk = currentChunk;
		}
		
		if (!valid)
		{
			// Print the error message to the console
			gc::OSError::OSReport(
			"Heap %" PRId32 " corrupt at 0x%08" PRIX32 "\n", 
			i, 
			reinterpret_cast<uint32_t>(currentChunk));
		}
	}
}

void enableDebugMode()
{
#ifdef SMB2_US
	uint32_t Offset = 0x6FB90;
#elif defined SMB2_JP
	uint32_t Offset = 0x29898;
#elif defined SMB2_EU
	uint32_t Offset = 0x29938;
#endif
	
	/* Should check to see if this value ever gets cleared. 
		If not, then the value should only be set once */
	*reinterpret_cast<uint32_t *>(reinterpret_cast<uint32_t>(
		heap::HeapData.MainLoopBSSLocation) + Offset) |= 
		((1 << 0) | (1 << 1)); // Turn on the 0 and 1 bits
}

void run()
{
	// Make sure there are no issues with the heap(s)
	checkHeaps();
	
	// Make sure debug mode is enabled
	enableDebugMode();
}

}