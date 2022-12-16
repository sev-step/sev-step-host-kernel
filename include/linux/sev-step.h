#ifndef SEV_STEP_H
#define SEV_STEP_H

#include <linux/types.h>
#include <linux/spinlock_types.h>
#include <asm/atomic.h>
#include <linux/kvm_types.h>
#include <asm/kvm_page_track.h>


//
// SEV STEP Types
//
typedef struct {
    uint32_t tmict_value;
	bool need_disable;
	bool need_init;
	bool active;
	uint32_t counted_instructions;
	uint64_t rip;
} sev_step_config_t;

typedef struct {
	uint32_t counted_instructions;
	uint64_t sev_rip;
} sev_step_event_t;

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

typedef struct {
    uint32_t tmict_value;
} sev_step_param_t;

extern struct kvm* main_vm;
extern sev_step_config_t sev_step_config;
extern struct mutex sev_step_config_mutex;

//
//	SEV STEP Functions
//
bool __untrack_single_page(struct kvm_vcpu *vcpu, gfn_t gfn,
			   enum kvm_page_track_mode mode);
bool __track_single_page(struct kvm_vcpu *vcpu, gfn_t gfn,
			 enum kvm_page_track_mode mode);
long kvm_start_tracking(struct kvm_vcpu *vcpu, enum kvm_page_track_mode mode);
long kvm_stop_tracking(struct kvm_vcpu *vcpu, enum kvm_page_track_mode mode);
uint64_t perf_ctl_to_u64(perf_ctl_config_t * config);
void write_ctl(perf_ctl_config_t * config, int cpu, uint64_t ctl_msr);
void read_ctr(uint64_t ctr_msr, int cpu, uint64_t* result);
void setup_perfs(void);
void process_perfs(int mode);

#endif