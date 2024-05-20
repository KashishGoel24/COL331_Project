#include "types.h"
#include "stat.h"
#include "param.h"
#include "mmu.h"
#include "memlayout.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"

pte_t editPTEentry(pte_t* pte, int diskblock){

    uint flags = PTE_FLAGS(pte); // Get flags from existing PTE
    uint new_entry = ((uint)diskblock << 12) | (flags & ~PTE_P) | PTE_SWAP;   // Use bitwise OR operator to set PTE_P and unset PTE_SWAP
    return ((pte_t)new_entry);
}

void swapOut(void){
    // cprintf("called here in swap out \n");
    struct proc *p;
    p = findVictimProcess();
    pte_t *pte;
    int virtualPagenumber = findVictimPage(p);
    pte = walkpgdir(p->pgdir, (void *)virtualPagenumber, 0);

    char *addPage = (char*)(P2V(PTE_ADDR(*pte)));
    int vacantSlot = findVacantSwapSlot();
    int diskblock = diskBlockNumber(vacantSlot);
    writeToDisk(ROOTDEV, addPage, diskblock);

    int* proclist2 = getprocreflist(PTE_ADDR(*pte) >> PTXSHIFT);
    int refcount = get_ref_count(PTE_ADDR(*pte));
    static int proclist[NPROC];
    for (int i = 0; i < NPROC; i++) {
        proclist[i] = proclist2[i];
    }

    updateSwapSlot(vacantSlot, 0, (int)PTE_FLAGS(*pte), refcount, proclist, PTE_ADDR(*pte) >> PTXSHIFT) ;
    reInitialisermap2(PTE_ADDR(*pte) >> PTXSHIFT); 
    reInitialisermap(PTE_ADDR(*pte) >> PTXSHIFT);            // check if this is reqd cause shayad kfree se kaam ho jaayega
    kfree(addPage,-1);                  // not updating the pid list here cause we can prob work without it rn as later in loop we are anyways re-iniitlasing the pid list
    pte_t newentry = editPTEentry(pte, diskblock);
    for (int i = 0 ; i < NPROC ; i++){
        if (proclist[i] != -1){
            decreaserssforswapout(mapPIDtoindex(proclist[i]));
            editPTEentries(proclist[i], (void*)virtualPagenumber, newentry, 1);
        }
    }
    // panic("stop");
    // cprintf("value of pte after chanfe in swap out %d \n",*pte);
    // cprintf("permsiions %d \n", (newentry & PTE_P));
    // cprintf("permsiions %d \n", (*pte & PTE_P));
    // cprintf("permsiions %d \n", (*pte & PTE_A));
    // cprintf("permsiions %d \n", (*pte & PTE_W));
    // cprintf("permsiions %d \n", (*pte & PTE_U));
    // cprintf("permsiions %d \n", (*pte & PTE_SWAP));
    // lcr3(V2P(ptable.proc[index].pgdir));
    // cprintf("exiting swap out\n");
}


void pgfault_handler(void){
    uint va = (rcr2());
    // cprintf("value of the rcr2 register %d \n ",va);
    // cprintf("here 2 \n");
    pte_t *pte = walkpgdir(myproc()->pgdir, (void*)va, 0);
    // cprintf("value of pte is %d \n",*pte);
    // if ((int)va < 0 || va > (KERNBASE)){
    //     panic("stop");
    // }

    if ( !(*pte & PTE_P)){
        // cprintf("in the swap in page fault \n");
        uint diskBlock = (uint)(*pte >> 12);
        char *pg;
        pg = kalloc(myproc()->pid);
        // updateproclist4(V2P(pg) >> PTXSHIFT);
        // updateproclist2(V2P(pg) >> PTXSHIFT, myproc()->pid);
        readFromDiskWriteToMem(ROOTDEV, pg, diskBlock);

        int* proclist = getProclist(((diskBlock-2)/8));
        
        for (int i=0; i < NPROC ; i++){
            if (proclist[i] != -1){
                increaserssforswapout(mapPIDtoindex(proclist[i]));
                updateproclist5(V2P(pg) >> PTXSHIFT);
                updateproclist2(V2P(pg) >> PTXSHIFT, proclist[i]);
                pte_t newentry = V2P(pg) | getPerm(((diskBlock-2)/8)) | PTE_P;
                editPTEentries(proclist[i], (void *)va, newentry, 0);
            }
        }

        updateSwapSlot(((diskBlock-2)/8), 1, PTE_FLAGS(*pte), 0, 0, -1);
        // cprintf("extiting \n");
        return;
    }
    
    // if (!(*pte & PTE_W) ){
    // if (!(*pte & PTE_W) && !(*pte & PTE_SWAP)){
    else if (!(*pte & PTE_W)){
        // cprintf("came in pgfault hamndler of write bit \n");
        // cprintf("stuck in this page faukt \n");
        // cprintf("proces creating the bt is %d \n", myproc()->pid);
        // cprintf("call ti get ref coount form here 2 \n");
        uint refCount = get_ref_count(PTE_ADDR(*pte));
        // cprintf("ref count is %d \n", get_ref_count(PTE_ADDR(*pte)));
        if (refCount == 1){
            // cprintf("got the ref ciunt as 1 \n");
            *pte = *pte | PTE_W ;
            lcr3(V2P(myproc()->pgdir));
            return;
        }
        char *pg;
        pg = kalloc(myproc()->pid);
        updateproclist4(V2P(pg) >> PTXSHIFT);
        updateproclist2(V2P(pg) >> PTXSHIFT, myproc()->pid);

        // myproc()->rss += PGSIZE;
        char *addPage = (char*)(P2V(PTE_ADDR(*pte)));
        copycontents(ROOTDEV, addPage, pg);
        // cprintf("ref count before is %d \n", get_ref_count(PTE_ADDR(*pte)));
        decrement_ref_count(PTE_ADDR(*pte));
        // cprintf("ref count after is %d \n", get_ref_count(PTE_ADDR(*pte)));
        updateproclist3((PTE_ADDR(*pte) >> PTXSHIFT),myproc()->pid) ;
        *pte =  V2P(pg) | PTE_W | PTE_FLAGS(*pte) ;
        // kmem.rmap_list[PTE_ADDR(*pte) >> PTXSHIFT].procRefList = -1;
        lcr3(V2P(myproc()->pgdir));
        return;
    }
    
}

void swapIn(pte_t *pte){
    uint diskBlock = (uint)(*pte >> 12);
    char *pg;
    pg = kalloc(myproc()->pid);
    // updateproclist4(V2P(pg) >> PTXSHIFT);
    // updateproclist2(V2P(pg) >> PTXSHIFT, myproc()->pid);
    readFromDiskWriteToMem(ROOTDEV, pg, diskBlock);

    int* proclist = getProclist(((diskBlock-2)/8));
    for (int i=0; i < NPROC ; i++){
        if (proclist[i] != -1){
            increaserssforswapout(mapPIDtoindex(proclist[i]));
            updateproclist5(V2P(pg) >> PTXSHIFT);
            updateproclist2(V2P(pg) >> PTXSHIFT, proclist[i]);
            pte_t newentry = V2P(pg) | getPerm(((diskBlock-2)/8)) | PTE_P;
            editPTEentries(proclist[i], ((void *)(char*)(P2V(PTE_ADDR(*pte)))), newentry, 0);
        }
    }
    updateSwapSlot(((diskBlock-2)/8), 1, PTE_FLAGS(*pte), 0, 0, 0);
}