#ifndef SEV_STEP_H
#define SEV_STEP_H

#include <linux/types.h>
#include <linux/spinlock_types.h>
#include <asm/atomic.h>
#include <linux/kvm_types.h>
#include <asm/kvm_page_track.h>
#include <asm/svm.h>

#include "idt-gate-desc.h"

#define CTL_MSR_0  0xc0010200ULL
#define CTL_MSR_1  0xc0010202ULL
#define CTL_MSR_2  0xc0010204ULL
#define CTL_MSR_3  0xc0010206ULL
#define CTL_MSR_4  0xc0010208ULL
#define CTL_MSR_5  0xc001020aULL

#define CTR_MSR_0  0xc0010201ULL
#define CTR_MSR_1  0xc0010203ULL
#define CTR_MSR_2  0xc0010205ULL
#define CTR_MSR_3  0xc0010207ULL
#define CTR_MSR_4  0xc0010209ULL
#define CTR_MSR_5  0xc001020bULL

/* SEV-STEP TYPES */

/**
 * @brief struct for storing idt
 */
typedef struct {
    gate_desc_t *base;
    size_t     entries;
} idt_t;

/**
 * @brief Describes the current state of the signle stepping "engine"
 * 
 */
typedef enum  {
	///@brief Not running
	SEV_STEP_STEPPING_STATUS_DISABLED,
	///@brief User requested to enable, still pending until next vmenter
	SEV_STEP_STEPPING_STATUS_DISABLED_WANT_INIT,
	///@brief Running
	SEV_STEP_STEPPING_STATUS_ENABLED,
	/// @brief User requested to disable, still pending until next vmenter
	SEV_STEP_STEPPING_STATUS_ENABLED_WANT_DISABLE,
} sev_step_stepping_status_t;

/**
 * @brief global struct holding all parameters needed for single
 * stepping with SEV-STEP
 */
typedef struct {
	// value for the apic timer
    uint32_t tmict_value;
	/// @brief Status of single stepping engine. See comments for enum type
	sev_step_stepping_status_t single_stepping_status;
	// stores the number of steps executed in the vm
	uint32_t counted_instructions;
	// stores the decrypted rip address
	uint64_t rip;
	// stores the running vm
	struct kvm* main_vm;
	// if true, the rip address will be decrypted
	bool decrypt_rip;
	// if true, interrupt from the previous timer programming has not yet been processed
	bool waitingForTimer;
	// if true, the performance counter is initialized
	bool perf_init;
	//TODO: testing to periodically send apic interrupts
	uint64_t entry_counter;
	/// @brief May be null. If set, we reset the ACCESS bits of these pages before vmentry
	/// which improves single stepping accuracy
	uint64_t* gpas_target_pages;
	uint64_t gpas_target_pages_len;

	/// @brief core on which idt was retrieved or -1 if idt has never been fetched
	int got_idt_on_cpu;
	/// @brief if got_idt_on_cpu != -1, this holds the idt for that core
	idt_t idt;

	/* All values for storing old apic config */
	uint32_t old_apic_lvtt;
	uint32_t old_apic_tdcr;
	uint32_t old_apic_tmict;
	gate_desc_t old_idt_gate;
} sev_step_config_t;

/**
 * @brief struct for storing sev-step event parameters
 * to send them to userspace
 */
typedef struct {
	uint32_t counted_instructions;
	uint64_t sev_rip;
} sev_step_event_t;

/**
 * @brief struct for storing the performance counter config values
 */
typedef struct {
	uint64_t HostGuestOnly;
	uint64_t CntMask;
	uint64_t Inv;
	uint64_t En;
	uint64_t Int;
	uint64_t Edge;
	uint64_t OsUserMode;
	uint64_t UintMask;
	uint64_t EventSelect; //12 bits in total split in [11:8] and [7:0]
} perf_ctl_config_t;

/**
 * @brief struct for storing sev-step config parameters
 * which are sent from userspace
 */
typedef struct {
	// apic timer value
    uint32_t tmict_value;
	/// @brief May be null. If set, we reset the ACCESS bits of these pages before vmentry
	/// which improves single stepping accuracy
	uint64_t* gpas_target_pages;
	uint64_t gpas_target_pages_len;
} sev_step_param_t;

extern sev_step_config_t global_sev_step_config;
extern struct mutex sev_step_config_mutex;

/* SEV-STEP FUNCTIONS */

/**
 * @brief Remove a single page from page track pool
 * 
 * @param vcpu vcpu of kvm
 * @param gfn guest page number
 * @param mode tracking mode
 * @return true on success
 */
bool __untrack_single_page(struct kvm_vcpu *vcpu, gfn_t gfn,
			   enum kvm_page_track_mode mode);

/**
 * @brief Add a single page to page track pool
 * 
 * @param vcpu vcpu of kvm
 * @param gfn guest page number
 * @param mode page tracking mode
 * @return true on success
 */
bool __track_single_page(struct kvm_vcpu *vcpu, gfn_t gfn,
			 enum kvm_page_track_mode mode);

/**
 * @brief Start the tracking of all pages
 * 
 * @param vcpu vcpu of kvm
 * @param mode page tracking mode
 * @return long the number of tracked pages
 */
long kvm_start_tracking(struct kvm_vcpu *vcpu, enum kvm_page_track_mode mode);

/**
 * @brief Stop the tracking of all pages
 * 
 * @param vcpu vcpu of kvm
 * @param mode page tracking mode
 * @return long the number of untracked pages
 */
long kvm_stop_tracking(struct kvm_vcpu *vcpu, enum kvm_page_track_mode mode);

/**
 * @brief Convert the performance counter config to u64 int
 * 
 * @param config performance counter config
 * @return uint64_t config as u64 int
 */
uint64_t perf_ctl_to_u64(perf_ctl_config_t * config);

/**
 * @brief Set ctl config on cpu
 * 
 * @param config performance counter config
 * @param cpu the cpu id on which the performance counting runs
 * @param ctl_msr predefined ctl value
 */
void write_ctl(perf_ctl_config_t * config, int cpu, uint64_t ctl_msr);

/**
 * @brief Read the counter values
 * 
 * @param ctr_msr predefined ctr value
 * @param cpu the cpu id on which the performance counting runs
 * @param result for storing the performance values
 */
void read_ctr(uint64_t ctr_msr, int cpu, uint64_t* result);

/**
 * @brief Setup the performance counter config
 */
void setup_perfs(void);

/**
 * @brief Determine the amount of steps executed in the vm
 * 
 * @param config global sev step config
 */
void calculate_steps(sev_step_config_t *config);

/**
 * @brief Clear the no-execute bit
 * 
 * @param vcpu vcpu of kvm
 * @param gfn guest page number
 * @return true on success
 */
bool __clear_nx_on_page(struct kvm_vcpu *vcpu, gfn_t gfn);

/**
 * @brief Reset the access bit (dont' confuse with present bit, for access tracking)
 * for the given gfn. Mainly used to get more stable single
 * stepping as the page table walker has to reset this bit, enlarging the window where a timer interrupt
 * leads to a single step
 * 
 * @param vcpu 
 * @param gfn guest frame number
 * @return true on success
 * @return false on error
 */

bool sev_step_reset_access_bit(struct kvm_vcpu *vcpu, gfn_t gfn);
/**
 * @brief Helper function that return true for all sev_step_stepping_status_t states in which
 * signle stepping is enabled
 * 
 * @param cfg global sev_step_config
 * @return true if single stepping is enabled
 * @return false if single stepping is disabled
 */
bool sev_step_is_single_stepping_active(sev_step_config_t* cfg);

/**
 * @brief Copies the vmcb of the given vcpu into the result struct. Automatically decrypts
 * the data if sev is enabled
 * 
 * code adapted from `void dump_vmcb(struct kvm_vcpu *vcpu)` (also in svm.c)

 * 
 * @param struct vcpu whose vm control block field should be decrypted
 * @param result caller allocated. Will be filled with decrypted data
 * @return int 0 on success
 */
int sev_step_get_vmcb_save_area(struct kvm_vcpu *vcpu, struct vmcb_save_area* result);
#endif