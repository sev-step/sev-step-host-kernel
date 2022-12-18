#include <asm/cacheflush.h>
#include <linux/types.h>
#include  <asm/pgtable_types.h>
#include <linux/vmalloc.h>
#include <asm/io.h> //virt_to_phys
#include <asm/cacheflush.h>

//#include <linux/kvm_host.h> //struct kvm
#include <asm/svm.h> //struct vmcb_save_area
#include <asm-generic/set_memory.h>

#include <linux/sev-step/my_idt.h>

#include <asm/tlbflush.h>



idt_t idt;

struct vcpu_svm* main_vcpu_svm = NULL;
EXPORT_SYMBOL(main_vcpu_svm);

//set this in intr handler (as we cannot sleep and wait for sev api there)
//we check if this is true in intr_interception and call the print code from there
bool decrypt_rip = false;
EXPORT_SYMBOL(decrypt_rip);

bool waitingForTimer = false;
EXPORT_SYMBOL(waitingForTimer);

#define IRQ_NUMBER 45






extern void isr_wrapper(void);

static void dump_gate(gate_desc_t *gate, int idx)
{
    printk("IDT[%3d] @%016llx = %016lx (seg sel 0x%x); p=%d; dpl=%d; type=%02d; ist=%d",
        idx, (unsigned long long) gate,  gate_offset(gate), gate->segment, gate->p, gate->dpl, gate->type, gate->ist);
}

static void get_idt( idt_t *idt ) {
	dtr_t idtr = {0};
	int entries;

	asm volatile ("sidt %0\n\t" :"=m"(idtr) :: );

	printk("idtr.size=%04x idtr.base = %016llx\n",idtr.size,idtr.base);

	entries = (idtr.size+1)/sizeof(gate_desc_t);

	set_memory_rw(idtr.base,1);

	idt->base = (gate_desc_t*) idtr.base;
	idt->entries = entries;
}

void install_kernel_irq_handler(void *asm_handler, int vector) {
	gate_desc_t *gate;

	 if( !sev_step_config.idt_init ) {
		 get_idt(&sev_step_config.idt);
		 sev_step_config.idt_init = true;
	 }

    gate = gate_ptr(sev_step_config.idt.base, vector);
	printk("old gate:\n");
	dump_gate(gate,vector);
	//store old entry
	memcpy(&sev_step_config.old_idt_gate, gate, sizeof(gate_desc_t));


    gate->offset_low    = PTR_LOW(asm_handler);
    gate->offset_middle = PTR_MIDDLE(asm_handler);
    gate->offset_high   = PTR_HIGH(asm_handler);
    
    gate->p = 1; //segment present flag
    gate->segment = KERNEL_CS; //segment selector
    gate->dpl = KERNEL_DPL; //descriptor privilege lvel
    gate->type = GATE_INTERRUPT; //type
    gate->ist = 0; // new stack for interrupt handling?
}

static void restore_kernel_irq_handler(int vector) {
	gate_desc_t *gate;

	 if( !sev_step_config.idt_init ) {
		 get_idt(&sev_step_config.idt);
		 sev_step_config.idt_init = true;
	 }

    gate = gate_ptr(sev_step_config.idt.base, vector);
	memcpy(gate,&sev_step_config.old_idt_gate, sizeof(gate_desc_t));
}

 
void my_handler(void) {
	get_cpu();
	apic_write(APIC_EOI, 0x0); //aknowledge interrupt
	printk("my handler is running on: %d\n", smp_processor_id());

	sev_step_config.waitingForTimer = false;
	sev_step_config.decrypt_rip = true; //requset rip printing in svm.c handler
	put_cpu();
}

void apic_restore() {
	get_cpu();
	printk("restoring old irq handler\n");
	restore_kernel_irq_handler(IRQ_NUMBER);
	apic_write(APIC_LVTT, sev_step_config.old_apic_lvtt); //0x320
	apic_write(APIC_TDCR, sev_step_config.old_apic_tdcr); //0x3e0
	//without this write, the timer does not seem to be actually startet
	apic_write(APIC_TMICT, sev_step_config.old_apic_tmict);
	put_cpu();
	sev_step_config.waitingForTimer = false;

}
EXPORT_SYMBOL(apic_restore);

void setup_apic_timer(uint32_t tmict_value) {
	 get_cpu(); //disable preemption => cannot be moved to antoher cpu
	 printk("setup_apic is running on: %d\n", smp_processor_id());
	 //start apic_timer_oneshot
	sev_step_config.old_apic_lvtt = apic_read(APIC_LVTT);
    sev_step_config.old_apic_tdcr = apic_read(APIC_TDCR);
	sev_step_config.old_apic_tmict = apic_read(APIC_TMICT);
	printk("in setup: old_apic_lvtt = 0x%x, old_apic_tdcr = 0x%x, old_apic_tmict = 0x%x",
		sev_step_config.old_apic_lvtt, sev_step_config.old_apic_tdcr, sev_step_config.old_apic_tmict);
	 
    apic_write(APIC_LVTT, IRQ_NUMBER | APIC_LVT_TIMER_ONESHOT);
    apic_write(APIC_TDCR, APIC_TDR_DIV_2);
	// printk("APIC timer one-shot mode with division 2 (lvtt=%x/tdcr=%x)\n",
   //     apic_read(APIC_LVTT), apic_read(APIC_TDCR));

    // NOTE: APIC seems not to handle divide by 1 properly (?)
    // see also: http://wiki.osdev.org/APIC_timer)
	 
	 //start apic_timer_irq
	 sev_step_config.waitingForTimer = true;
	 apic_write(APIC_TMICT, tmict_value); 
	//printk("setup_apic done at %llu\n", ktime_get_ns());
	 put_cpu(); //enable preemption

}
EXPORT_SYMBOL(setup_apic_timer);

void apic_backup() {
 	get_cpu(); //disable preemption => cannot be moved to antoher cpu
	printk("apic_backup is running on: %d\n", smp_processor_id());
	//start apic_timer_oneshot
	sev_step_config.old_apic_lvtt = apic_read(APIC_LVTT);
    sev_step_config.old_apic_tdcr = apic_read(APIC_TDCR);
	sev_step_config.old_apic_tmict = apic_read(APIC_TMICT);
	put_cpu();
}
EXPORT_SYMBOL(apic_backup);

void apic_restart_timer(uint32_t tmict_value) {
	get_cpu();	
	printk("apic_restart_timer is running on: %d\n", smp_processor_id());
    apic_write(APIC_LVTT, IRQ_NUMBER | APIC_LVT_TIMER_ONESHOT);
    apic_write(APIC_TDCR, APIC_TDR_DIV_2);
	sev_step_config.waitingForTimer = true;
	apic_write(APIC_TMICT, tmict_value); 
	//printk("apic_restart_timer done at %llu\n", ktime_get_ns());
	put_cpu();
}
EXPORT_SYMBOL(apic_restart_timer);


void my_idt_install_handler(void) {
	install_kernel_irq_handler(&isr_wrapper,IRQ_NUMBER);
}
EXPORT_SYMBOL(my_idt_install_handler);

void my_idt_start_apic_timer(struct vcpu_svm *svm) {
	get_cpu();
	/*waitingFOrTimer == true means that the interrupt from the previous
	timmer programming has not yet been processed
	*/

	if( !sev_step_config.waitingForTimer && sev_step_config.active) {
		//it's assumed that the old timer config has been backed up
		 apic_write(APIC_LVTT, IRQ_NUMBER | APIC_LVT_TIMER_ONESHOT);
   		 apic_write(APIC_TDCR, APIC_TDR_DIV_2);

		//start apic_timer_irq
		sev_step_config.waitingForTimer = true;

		__asm__("mfence");
		process_perfs(0);
		apic_write(APIC_TMICT, sev_step_config.tmict_value); 
	}
	
	put_cpu();

}
EXPORT_SYMBOL(my_idt_start_apic_timer);
