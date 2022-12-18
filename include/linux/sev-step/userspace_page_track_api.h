#ifndef USERSPACE_PAGE_TRACK_API
#define USERSPACE_PAGE_TRACK_API

#include <linux/types.h>
//
// SEV-STEP Types
//

typedef struct {
	uint64_t gpa;
	int track_mode;
} track_page_param_t;

typedef struct {
	int track_mode;
} track_all_pages_t;

typedef enum {
    PAGE_FAULT_EVENT,
    SEV_STEP_EVENT,
} usp_event_type_t;

typedef struct {
    //lock for all of the other values in this struct
    int spinlock;
    //if true, we have a valid event stored 
    int have_event;
    //if true, the receiver has acked the event
    int event_acked;
    //type of the stored event. Required to do the correct raw mem cast
    usp_event_type_t event_type;
    uint8_t event_buffer[2048];
} shared_mem_region_t;

typedef struct {
    int pid;
    uint64_t user_vaddr_shared_mem;
} usp_init_poll_api_t;

typedef struct {
    int pid;
    shared_mem_region_t* shared_mem_region;
    uint64_t next_id;
    int force_reset;
    
    //just for internal use. Used to remember get_user_pages_unlocked
    //page to be able to unpinn it on ctx destruction
    struct page* _page_for_shared_mem;
} usp_poll_api_ctx_t;

typedef struct {
    //uint64_t id; //filled automatically
    uint64_t faulted_gpa;
} usp_page_fault_event_t;

/**
 * @brief Initializes a usp_poll_api_ctx_t which is required for all API calls. Assumes that the shared_mem_region
 * is still a user space pointer/vaddr.
 * 
 * @param pid pid of calling user space process
 * @param shared_mem_region user space pointer pointer to shared memory region
 * @param ctx caller allocated result param that is initialized in this function
 * @return int 0 on success
 */
int usp_poll_init_user_vaddr(int pid,uint64_t user_vaddr_shared_mem,usp_poll_api_ctx_t* ctx);

/**
 * @brief Initializes a usp_poll_api_ctx_t which is required for all API calls. Assumes
 * kernel readable pointer to shared mem. See usp_poll_init_user_vaddr if you only have a user space
 * vaddr.
 * @param pid pid of calling user space process
 * @param shared_mem_region pointer to shared memory region. Pointer must be accessible from kernel space
 * @param ctx caller allocated result param that is initialized in this function
 * @return int 0 on success
 */
int usp_poll_init_kern_vaddr(int pid, shared_mem_region_t* shared_mem_region ,usp_poll_api_ctx_t* ctx);

/**
 * @brief Frees resources hold by the ctx
 * 
 * @param ctx ctx to clean up
 * @return int 0 on success
 */
int usp_poll_close_api(usp_poll_api_ctx_t* ctx);

/**
 * @brief Signal availability of the supplied event and block untill receiver has send ack for it
 * 
 * @param ctx ctx to operate on
 * @param event_type type of event
 * @param event event struct matching the supplied type
 * @return int 0 if event was ack'ed by receiver
 */
int usp_send_and_block(usp_poll_api_ctx_t* ctx, usp_event_type_t event_type, void* event);

int get_size_for_event(usp_event_type_t event_type, uint64_t* size);

int ctx_initialized(void);

extern usp_poll_api_ctx_t* ctx;
#endif