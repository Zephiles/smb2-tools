#include "mod.h"
#include "heap.h"
#include "patch.h"

#include <gc/OSModule.h>

namespace mod {

Mod *gMod = nullptr;

void main()
{
    // Create one heap with a size of 0x15000 bytes
    const uint32_t SizeToAllocate = 0x15000;
    heap::initMemAllocServices(SizeToAllocate, 1);
    heap::addHeap(SizeToAllocate, true);
    
    Mod *mod = new Mod();
    mod->init();
}

Mod::Mod()
{
    
}

void Mod::init()
{
    performAssemblyPatches();
    
    gMod = this;
    
    /*
    mPFN_OSLink_trampoline = patch::hookFunction(
        gc::OSModule::OSLink, [](gc::OSModule::OSModuleInfo *newModule, void *bss)
    {
        return gMod->performRelPatches(newModule, bss);
    });
    */
}

}