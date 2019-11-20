#include "mod.h"
#include "heap.h"
#include "patch.h"

#include <gc/OSModule.h>

namespace mod {

Mod *gMod = nullptr;

void main()
{
	/* Create one heap with a size of 0xA220 bytes, as the 
		rel loader allocated that much and didn't free it */
	const uint32_t LeftoverRelLoaderMemSize = 0xA220;
	heap::initMemAllocServices(LeftoverRelLoaderMemSize, 1);
	heap::addHeap(LeftoverRelLoaderMemSize, true);
	
	/* Remove OSReport call ``PERF : event is still open for CPU!`` 
		since it reports every frame, and thus clutters the console */
#ifdef SMB2_US
	// Only needs to be applied to the US version
	*reinterpret_cast<uint32_t *>(0x80033E9C) = 0x60000000; // nop
#endif
	
	Mod *mod = new Mod();
	mod->init();
}

Mod::Mod()
{
	
}

void Mod::init()
{
	gMod = this;
	
	mPFN_OSLink_trampoline = patch::hookFunction(
		gc::OSModule::OSLink, [](gc::OSModule::OSModuleInfo *newModule, void *bss)
	{
		return gMod->performRelPatches(newModule, bss);
	});
}

}