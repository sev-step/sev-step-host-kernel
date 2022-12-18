#ifndef MY_IDT_H
#define MY_IDT_H
#include <linux/sev-step.h>
#include "svm/svm.h"

void setup_apic_timer(uint32_t tmict_value);
void install_handler_on_cpu(int cpu);
void apic_restore(void);
void apic_restart_timer(uint32_t tmict_value);
void apic_backup(void);
void my_idt_start_apic_timer(struct vcpu_svm *svm);
void my_idt_install_handler(void);
#endif
