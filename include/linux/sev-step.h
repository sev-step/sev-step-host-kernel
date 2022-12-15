#ifndef SEV_STEP_H
#define SEV_STEP_H

#include <linux/types.h>
#include <linux/spinlock_types.h>
#include <asm/atomic.h>
#include <linux/kvm_types.h>
#include <asm/kvm_page_track.h>

//
//	SEV STEP
//
bool __untrack_single_page(struct kvm_vcpu *vcpu, gfn_t gfn,
			   enum kvm_page_track_mode mode);
bool __track_single_page(struct kvm_vcpu *vcpu, gfn_t gfn,
			 enum kvm_page_track_mode mode);
long kvm_start_tracking(struct kvm_vcpu *vcpu, enum kvm_page_track_mode mode);
long kvm_stop_tracking(struct kvm_vcpu *vcpu, enum kvm_page_track_mode mode);
extern struct kvm* main_vm;

#endif