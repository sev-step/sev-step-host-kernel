#ifndef USERSPACE_PAGE_TRACK_API
#define USERSPACE_PAGE_TRACK_API

#include <linux/types.h>

/* SEV-STEP API TYPES */

/**
 * @brief struct for storing tracking parameters 
 * which are sent from userspace
 */
typedef struct {
    //guest physical address of page
	uint64_t gpa;
    //one of the track modes defined in enum kvm_page_track_mode
	int track_mode;
} track_page_param_t;

/**
 * @brief struct for storing tracking parameters 
 * for all pages which are sent from userspace
 */
typedef struct {
    //one of the track modes defined in enum kvm_page_track_mode
	int track_mode;
} track_all_pages_t;

/**
 * @brief enum for supported event types
 */
typedef enum {
    //used for page fault events
    PAGE_FAULT_EVENT,
    //used for single stepping events
    SEV_STEP_EVENT,
} usp_event_type_t;

/**
 * @brief Stores the structure of the shared memory 
 * region between kernelspace and userspace
 */
typedef struct {
    //lock for all of the other values in this struct
    int spinlock;
    //if true, we have a valid event stored 
    int have_event;
    //if true, the receiver has acked the event
    int event_acked;
    //type of the stored event. Required to do the correct raw mem cast
    usp_event_type_t event_type;
    // buffer for the event
    uint8_t event_buffer[2048];
} shared_mem_region_t;

/**
 * @brief struct for storing parameters which are
 * needed for the initialization of the api
 */
typedef struct {
    //process id
    int pid;
    //the user defined shared memory address
    uint64_t user_vaddr_shared_mem;
} usp_init_poll_api_t;

/**
 * @brief struct for storing the api context
 */
typedef struct {
    //process id
    int pid;
    //memory region which is shared by kernelspace and userspace
    shared_mem_region_t* shared_mem_region;
    //next id
    uint64_t next_id;
    //if true, the api will be forced to reset
    int force_reset;
    
    //just for internal use. Used to remember get_user_pages_unlocked
    //page to be able to unpinn it on ctx destruction
    struct page* _page_for_shared_mem;
} usp_poll_api_ctx_t;

/**
 * @brief struct for storing page fault parameters 
 * which are sent to userspace
 */
typedef struct {
    // gpa of the page fault
    uint64_t faulted_gpa;
} usp_page_fault_event_t;

/* SEV-STEP API FUNCTIONS */

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

/**
 * @brief Determine the size of a supported event
 * 
 * @param event_type type of event
 * @param size pointer to save the size
 * @return int 0 on success
 */
int get_size_for_event(usp_event_type_t event_type, uint64_t *size);

/**
 * @brief Check if the usp_poll_api_ctx_t is initialized
 * 
 * @return int 1 if initialized
 */
int ctx_initialized(void);

extern usp_poll_api_ctx_t* ctx;
#endif