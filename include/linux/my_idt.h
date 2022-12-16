#ifndef MY_IDT_H
#define MY_IDT_H
#include <linux/sev-step.h>


extern bool decrypt_rip;
extern bool waitingForTimer;

extern struct vcpu_svm* main_vcpu_svm; //in my_idt.c
void setup_apic_timer(uint32_t tmict_value);
void install_handler_on_cpu(int cpu);
void apic_restore(void);
void apic_restart_timer(uint32_t tmict_value);
void apic_backup(void);
void start_apic_timer(struct vcpu_svm *svm);
void my_idt_install_handler(void);
uint64_t sev_step_get_rip(struct vcpu_svm* svm);
int my_sev_decrypt(struct kvm* kvm, void* dst_vaddr, void* src_vaddr, uint64_t dst_paddr, uint64_t src_paddr, uint64_t len, int* api_res);
#endif
