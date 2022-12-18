#ifndef MY_IDT_H
#define MY_IDT_H
#include <linux/sev-step/sev-step.h>
#include "svm/svm.h"

void setup_apic_timer(sev_step_config_t *config, uint32_t tmict_value);
void install_handler_on_cpu(int cpu);
void apic_restore(sev_step_config_t *config);
void apic_restart_timer(sev_step_config_t *config, uint32_t tmict_value);
void apic_backup(sev_step_config_t *config);
void my_idt_start_apic_timer(sev_step_config_t *config, struct vcpu_svm *svm);
void my_idt_install_handler(sev_step_config_t *config);
#endif
