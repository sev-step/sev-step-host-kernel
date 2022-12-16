#include <asm/cacheflush.h>
#include <linux/types.h>
#include  <asm/pgtable_types.h>
#include <linux/vmalloc.h>
#include <asm/io.h> //virt_to_phys
#include <asm/cacheflush.h>

#include <linux/kvm_host.h> //struct kvm
#include <asm/svm.h> //struct vmcb_save_area
#include <asm-generic/set_memory.h>
#include <linux/pid.h>
#include <linux/psp-sev.h>

#include <linux/my_idt.h>

#include <asm/tlbflush.h>

#include "desc.h"
#include "svm/svm.h"

typedef struct {
    gate_desc_t *base;
    size_t     entries;
} idt_t;

idt_t idt;
static bool idt_init = false;

struct vcpu_svm* main_vcpu_svm = NULL;
EXPORT_SYMBOL(main_vcpu_svm);

//set this in intr handler (as we cannot sleep and wait for sev api there)
//we check if this is true in intr_interception and call the print code from there
bool decrypt_rip = false;
EXPORT_SYMBOL(decrypt_rip);

bool waitingForTimer = false;
EXPORT_SYMBOL(waitingForTimer);

#define IRQ_NUMBER 45

//used to store old config values
uint32_t apic_lvtt = 0;
uint32_t apic_tdcr = 0;
uint32_t apic_tmict = 0;
gate_desc_t old_idt_gate;
bool old_saved = false;





extern void isr_wrapper(void);

static int __my_sev_issue_dbg_cmd(struct kvm *kvm, unsigned long src,
			       unsigned long dst, int size,
			       int *error);

static int __my_sev_issue_dbg_cmd(struct kvm *kvm, unsigned long src,
			       unsigned long dst, int size,
			       int *error)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_dbg *data;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL_ACCOUNT);
	if (!data)
		return -ENOMEM;

	data->handle = sev->handle;
	data->dst_addr = dst;
	data->src_addr = src;
	data->len = size;

	/*ret = sev_issue_cmd(kvm,
			     SEV_CMD_DBG_DECRYPT,
			    data, error);*/
	ret = sev_do_cmd(SEV_CMD_DBG_DECRYPT, data, error);
	kfree(data);
	return ret;
}

int my_sev_decrypt(struct kvm* kvm, void* dst_vaddr, void* src_vaddr, uint64_t dst_paddr, uint64_t src_paddr, uint64_t len, int* api_res) {

	int call_res;
	call_res  = 0x1337;
	*api_res = 0x1337;


	if( dst_paddr % PAGE_SIZE != 0 || src_paddr % PAGE_SIZE != 0) {
		printk("decrypt: for now, src_paddr, and dst_paddr must be page aligned");
		return -1;
	}

	if( len > PAGE_SIZE ) {
		printk("decrypt: for now, can be at most 4096 byte");
		return -1;
	}

	memset(dst_vaddr,0,PAGE_SIZE);

	//clflush_cache_range(src_vaddr, PAGE_SIZE);
	//clflush_cache_range(dst_vaddr, PAGE_SIZE);
	wbinvd_on_all_cpus();

	call_res = __my_sev_issue_dbg_cmd(kvm, __sme_set(src_paddr), 
		__sme_set(dst_paddr), len, api_res);

	return call_res;

}
EXPORT_SYMBOL(my_sev_decrypt);

int decrypt_vmsa(struct vcpu_svm* svm, struct vmcb_save_area* save_area) {

	uint64_t src_paddr, dst_paddr;
	void * dst_vaddr;
	void * src_vaddr;
	struct page * dst_page;
	int call_res,api_res;
	call_res = 1337;
	api_res = 1337;
	
	src_vaddr = svm->vmsa;
	src_paddr = svm->vmcb->control.vmsa_pa;

	if( src_paddr % 16 != 0) {
		printk("decrypt_vmsa: src_paddr was not 16b aligned");
	}

	if( sizeof( struct vmcb_save_area) % 16 != 0 ) {
		printk("decrypt_vmsa: size of vmcb_save_area is not 16 b aligned\n");
	}

	dst_page = alloc_page(GFP_KERNEL);
	dst_vaddr =  vmap(&dst_page, 1, 0, PAGE_KERNEL);
	dst_paddr = page_to_pfn(dst_page) << PAGE_SHIFT;
	memset(dst_vaddr,0,PAGE_SIZE);

	

	if( dst_paddr % 16 != 0 ) {
		printk("decrypt_vmsa: dst_paddr was not 16 byte aligned");
	}

	//printk("src_paddr = 0x%llx dst_paddr = 0x%llx\n", __sme_clr(src_paddr), __sme_clr(dst_paddr));
	//printk("Sizeof vmcb_save_area is: 0x%lx\n", sizeof( struct vmcb_save_area) );


	call_res = __my_sev_issue_dbg_cmd(svm->vcpu.kvm, __sme_set(src_paddr), __sme_set(dst_paddr), sizeof(struct vmcb_save_area), &api_res);


	//printk("decrypt_vmsa: result of call was %d, result of api command was %d\n",call_res, api_res);

	//todo error handling
	if( api_res != 0 ) {
		printk("api returned error code\n");
		__free_page(dst_page);
		return -1;
	}

	memcpy(save_area, dst_vaddr, sizeof( struct vmcb_save_area) );


	__free_page(dst_page);

	return 0;


}

/*
 * Contains a switch to work  SEV and SEV-ES
 */
uint64_t sev_step_get_rip(struct vcpu_svm* svm) {
	struct vmcb_save_area* save_area;
	struct kvm * kvm;
	struct kvm_sev_info *sev;
	uint64_t rip;


	kvm = svm->vcpu.kvm;
	sev = &to_kvm_svm(kvm)->sev_info;

	//for sev-es we need to use the debug api, to decrypt the vmsa
	if( sev->active && sev->es_active) {
		int res;
		save_area = vmalloc(sizeof(struct vmcb_save_area) );
		memset(save_area,0, sizeof(struct vmcb_save_area));

		res = decrypt_vmsa(svm, save_area);
		if( res != 0) {
			printk("sev_step_get_rip failed to decrypt\n");
			return 0;
		}

		rip =  save_area->rip;

		vfree(save_area);
	} else { //otherwise we can just access as plaintexts
		rip = svm->vmcb->save.rip;
	}
	return rip;

}
EXPORT_SYMBOL(sev_step_get_rip);

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

	 if( !idt_init ) {
		 get_idt(&idt);
		 idt_init = true;
	 }

    gate = gate_ptr(idt.base, vector);
	printk("old gate:\n");
	dump_gate(gate,vector);
	//store old entry
	memcpy(&old_idt_gate, gate, sizeof(gate_desc_t));


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

	 if( !idt_init ) {
		 get_idt(&idt);
		 idt_init = true;
	 }

    gate = gate_ptr(idt.base, vector);
	memcpy(gate,&old_idt_gate, sizeof(gate_desc_t));
}

 
void my_handler(void) {
	get_cpu();
	apic_write(APIC_EOI, 0x0); //aknowledge interrupt
	printk("my handler is running on: %d\n", smp_processor_id());

	waitingForTimer = false;
	decrypt_rip = true; //requset rip printing in svm.c handler
	put_cpu();
}

void apic_restore() {
	get_cpu();
	printk("restoring old irq handler\n");
	restore_kernel_irq_handler(IRQ_NUMBER);
	apic_write(APIC_LVTT, apic_lvtt); //0x320
	apic_write(APIC_TDCR, apic_tdcr); //0x3e0
	//without this write, the timer does not seem to be actually startet
	apic_write(APIC_TMICT, apic_tmict);
	put_cpu();
	waitingForTimer = false;

}
EXPORT_SYMBOL(apic_restore);

void setup_apic_timer(uint32_t tmict_value) {
	 get_cpu(); //disable preemption => cannot be moved to antoher cpu
	 printk("setup_apic is running on: %d\n", smp_processor_id());
	 //start apic_timer_oneshot
	apic_lvtt = apic_read(APIC_LVTT);
    apic_tdcr = apic_read(APIC_TDCR);
	apic_tmict = apic_read(APIC_TMICT);
	printk("in setup: apic_lvtt = 0x%x, apic_tdcr = 0x%x, apic_tmict = 0x%x",
		apic_lvtt, apic_tdcr, apic_tmict);
	 
    apic_write(APIC_LVTT, IRQ_NUMBER | APIC_LVT_TIMER_ONESHOT);
    apic_write(APIC_TDCR, APIC_TDR_DIV_2);
	// printk("APIC timer one-shot mode with division 2 (lvtt=%x/tdcr=%x)\n",
   //     apic_read(APIC_LVTT), apic_read(APIC_TDCR));

    // NOTE: APIC seems not to handle divide by 1 properly (?)
    // see also: http://wiki.osdev.org/APIC_timer)
	 
	 //start apic_timer_irq
	 waitingForTimer = true;
	 apic_write(APIC_TMICT, tmict_value); 
	//printk("setup_apic done at %llu\n", ktime_get_ns());
	 put_cpu(); //enable preemption

}
EXPORT_SYMBOL(setup_apic_timer);

void apic_backup() {
 	get_cpu(); //disable preemption => cannot be moved to antoher cpu
	printk("apic_backup is running on: %d\n", smp_processor_id());
	//start apic_timer_oneshot
	apic_lvtt = apic_read(APIC_LVTT);
    apic_tdcr = apic_read(APIC_TDCR);
	apic_tmict = apic_read(APIC_TMICT);
	put_cpu();
}
EXPORT_SYMBOL(apic_backup);

void apic_restart_timer(uint32_t tmict_value) {
	get_cpu();	
	printk("apic_restart_timer is running on: %d\n", smp_processor_id());
    apic_write(APIC_LVTT, IRQ_NUMBER | APIC_LVT_TIMER_ONESHOT);
    apic_write(APIC_TDCR, APIC_TDR_DIV_2);
	waitingForTimer = true;
	apic_write(APIC_TMICT, tmict_value); 
	//printk("apic_restart_timer done at %llu\n", ktime_get_ns());
	put_cpu();
}
EXPORT_SYMBOL(apic_restart_timer);


void my_idt_install_handler(void) {
	install_kernel_irq_handler(&isr_wrapper,IRQ_NUMBER);
}
EXPORT_SYMBOL(my_idt_install_handler);

void start_apic_timer(struct vcpu_svm *svm) {
	get_cpu();
	/*waitingFOrTimer == true means that the interrupt from the previous
	timmer programming has not yet been processed
	*/

	if( !waitingForTimer && sev_step_config.active) {
		//it's assumed that the old timer config has been backed up
		 apic_write(APIC_LVTT, IRQ_NUMBER | APIC_LVT_TIMER_ONESHOT);
   		 apic_write(APIC_TDCR, APIC_TDR_DIV_2);

		//start apic_timer_irq
		waitingForTimer = true;

		__asm__("mfence");
		process_perfs(0);
		apic_write(APIC_TMICT, sev_step_config.tmict_value); 
	}
	
	put_cpu();

}
EXPORT_SYMBOL(start_apic_timer);
