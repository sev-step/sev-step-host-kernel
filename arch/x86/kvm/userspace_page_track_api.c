#include <linux/printk.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/sev-step/userspace_page_track_api.h>
#include <linux/raw_spinlock.h>
#include <asm/string.h>
#include <linux/sev-step/sev-step.h>

usp_poll_api_ctx_t* ctx = NULL;
EXPORT_SYMBOL(ctx);

int get_size_for_event(usp_event_type_t event_type, uint64_t* size) {
    switch (event_type)
    {
        case PAGE_FAULT_EVENT:
            *size = sizeof(usp_page_fault_event_t);
            return 0;
        case SEV_STEP_EVENT:
            *size = sizeof(sev_step_event_t);
            return 0;
        default:
            return 1;
    }
}

int usp_send_and_block(usp_poll_api_ctx_t* ctx, usp_event_type_t event_type, void* event) {
    uint64_t event_size = 0;

/*
Initial: have_event = 0, event_acked = 1

Sender: if event_acked true, we can send a new event by locking and setting
have_event = 1 and event_acked = 0

Receiver: if have_event is true, we can access the event buffer.
We must set event_acked = 1 to allow the sender to send the next event
we also reset have_event
*/
    if (!ctx_initialized()) {
        printk("usp_send_and_block: Ctx is not initialized\n");
        return 1;
    }
    //wait untill we are allowed to send the next event
    printk("usp_send_and_block: trying to get lock...\n");
    while(1) {
        raw_spinlock_lock(&ctx->shared_mem_region->spinlock);
        if( ctx->shared_mem_region->event_acked ) {
            //DO NOT UNLOCK HERE
            break;
        }
        if( ctx->force_reset ) {
            printk("usp_send_and_block: abort wait for send due to force_reset=1\n");
            raw_spinlock_unlock(&ctx->shared_mem_region->spinlock);
            return 1;

        }
        raw_spinlock_unlock(&ctx->shared_mem_region->spinlock);
    }

    //if we are here, we hold ctx->shared_mem_region.spinlock and last event was acked
    printk("usp_send_and_block: got lock\n");
    
    //copy event
    if( get_size_for_event(event_type,&event_size)) {
        printk("Failed to determine size for event %d.",event_type);
        raw_spinlock_unlock(&ctx->shared_mem_region->spinlock);
        return 1;
    }
    printk("resolved event size, copying...\n");
    memcpy((void*)ctx->shared_mem_region->event_buffer,event,event_size);
       
    ctx->shared_mem_region->event_type = event_type;
    printk("usp_send_and_block: done copying. Setting status flags...\n");


    //set status flags
    ctx->shared_mem_region->event_acked = 0;
    ctx->shared_mem_region->have_event = 1;

    raw_spinlock_unlock(&ctx->shared_mem_region->spinlock);
    printk("usp_send_and_block: unlocked and done\n");
    return 0;
}
EXPORT_SYMBOL(usp_send_and_block);

int usp_poll_init_user_vaddr(int pid,uint64_t user_vaddr_shared_mem,usp_poll_api_ctx_t* ctx) {
    struct page *shared_mem_page;
	shared_mem_region_t* shared_mem_kern_mapping;
    int res;

    //create persistent, kernel accessible mapping for user space memory
    if ( (user_vaddr_shared_mem & 0xfff) != 0 ) {
            printk("usp_poll_init_user_vaddr: the provided vaddr 0x%llx is not page aligend!\n",user_vaddr_shared_mem);
            return 1;
    }
    printk("usp_poll_init_user_vaddr: trying get_user_pages_unlocked");
    res = get_user_pages_unlocked(
        user_vaddr_shared_mem, //start vaddr
        1, //number of pages to pin from start vaddr
        &shared_mem_page,
        FOLL_WRITE
        );
    if( res <= 0 ) {
        printk("usp_poll_init_user_vaddr: get_user_pages failed with %d\n",res);
        return 1;
    }
    printk("usp_poll_init_user_vaddr: success, pinned %d pages\n",res);


    shared_mem_kern_mapping = (shared_mem_region_t*)vmap(&shared_mem_page,1,0,PAGE_SHARED);
    if( shared_mem_kern_mapping == NULL ) {
        printk("usp_poll_init_user_vaddr: failed to get virtual mapping for page struct\n");
        return 1;
    }
    printk("usp_poll_init_user_vaddr: vaddr of kernel mapping is 0x%llx\n",(uint64_t)shared_mem_kern_mapping);

    //call regular create ctx path

    usp_poll_init_kern_vaddr(pid,shared_mem_kern_mapping,ctx);

    //fix ctx values specific to this ctx creation path

    ctx->_page_for_shared_mem = shared_mem_page;
    return 0;
}


int usp_poll_init_kern_vaddr(int pid, shared_mem_region_t* shared_mem_region ,usp_poll_api_ctx_t* ctx ) {

    ctx->pid = pid;
    ctx->next_id = 1;
    ctx->force_reset = 0;
    ctx->shared_mem_region = shared_mem_region;
    ctx->_page_for_shared_mem = NULL;
    return 0;
}

int usp_poll_close_api(usp_poll_api_ctx_t* ctx) {
    raw_spinlock_lock(&ctx->shared_mem_region->spinlock);
    ctx->force_reset = 1;
    raw_spinlock_unlock(&ctx->shared_mem_region->spinlock);
    if( ctx->_page_for_shared_mem != NULL ) {
        unpin_user_pages(&ctx->_page_for_shared_mem,1);
    }
    return 0;
}

int ctx_initialized() {
    return ctx != NULL;
}