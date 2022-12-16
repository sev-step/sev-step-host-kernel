#include <linux/smp.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sev-step.h>

DEFINE_MUTEX(sev_step_config_mutex);
EXPORT_SYMBOL(sev_step_config_mutex);

sev_step_config_t sev_step_config = {
    .need_disable = false,
    .need_init = false,
    .tmict_value = 0,
    .active = false,
    .counted_instructions = 404,
    .rip = 0,
};
EXPORT_SYMBOL(sev_step_config);

//used to store performance counter values; 6 counters, 2 readings per counter
uint64_t perf_reads[6][2];
perf_ctl_config_t perf_configs[6];
int perf_cpu;

uint64_t perf_ctl_to_u64(perf_ctl_config_t * config) {

	uint64_t result = 0;
	result |= (  config->EventSelect & 0xffULL); //[7:0] in result and  [7:0] in EventSelect
	result |= ( (config->UintMask & 0xffULL) << 8 ); //[15:8]
	result |= ( (config->OsUserMode & 0x3ULL) << 16); //[17:16]
	result |= ( (config->Edge & 0x1ULL ) << 18 ); // 18
	result |= ( (config->Int & 0x1ULL ) << 20 ); // 20
	result |= ( (config->En & 0x1ULL ) << 22 ); //22
	result |= ( (config->Inv & 0x1ULL ) << 23); //23
	result |= ( (config->CntMask & 0xffULL) << 24); //[31:24]
	result |= ( ( (config->EventSelect & 0xf00ULL) >> 8 ) << 32); //[35:32] in result and [11:8] in EventSelect
	result |= ( (config->HostGuestOnly & 0x3ULL) << 40); // [41:40]

	return result;

}

void write_ctl(perf_ctl_config_t * config, int cpu, uint64_t ctl_msr){
	wrmsrl_on_cpu(cpu, ctl_msr, perf_ctl_to_u64(config)); //always returns zero
}

void read_ctr(uint64_t ctr_msr, int cpu, uint64_t* result) {
    uint64_t tmp;
	rdmsrl_on_cpu(cpu, ctr_msr, &tmp); //always returns zero
	*result = tmp & ( (0x1ULL << 48) - 1);
}

void setup_perfs() {
    int i;
    
    perf_cpu = smp_processor_id();
    
    for( i = 0; i < 6; i++) {
        perf_configs[i].HostGuestOnly = 0x1; //0x1 means: count only guest
        perf_configs[i].CntMask = 0x0;
        perf_configs[i].Inv = 0x0;
        perf_configs[i].En = 0x0;
        perf_configs[i].Int = 0x0;
        perf_configs[i].Edge = 0x0;
        perf_configs[i].OsUserMode = 0x3; //0x3 means: count userland and kernel events
    }
    
    //remember to set .En to enable the individual counter

    perf_configs[0].EventSelect = 0x0c0;
	perf_configs[0].UintMask = 0x0;
    perf_configs[0].En = 0x1;
	write_ctl(&perf_configs[0],perf_cpu, CTL_MSR_0);

    /*programm l2d hit from data cache miss perf for 
    cpu_probe_pointer_chasing_inplace without counting thread.
    N.B. that this time we count host events
    */
    perf_configs[1].EventSelect = 0x064;
    perf_configs[1].UintMask = 0x70;
    perf_configs[1].En = 0x1;
    perf_configs[1].HostGuestOnly = 0x2; //0x2 means: count only host events, as we do the chase here
    write_ctl(&perf_configs[1],perf_cpu,CTL_MSR_1);
}
EXPORT_SYMBOL(setup_perfs);

//process_perfs reads perfs.if called with "0", data is gathered. if called
//with "1" data is gathered on compared to previous data.
void process_perfs(int mode) {
    static int lastMode = 0;
    if( perf_cpu != smp_processor_id() ) {
        printk("read_perfs: called on cpu %d but setup_perfs was called on cpu %d\n", smp_processor_id(), perf_cpu);
    }

    if( mode == 0) {
        read_ctr(CTR_MSR_0, perf_cpu, &perf_reads[0][0]);        
        lastMode = 0;
    } else if ( mode == 1) {
        if( lastMode == 1 ) {
            printk("process_perfs(1) called with out previous call to procces_perfs(0)\n");
        }
        read_ctr(CTR_MSR_0, perf_cpu, &perf_reads[0][1] );
        sev_step_config.counted_instructions = perf_reads[0][1] - perf_reads[0][0] -1;
        lastMode = 1;
    } else {
        printk("read_perfs called with invalid mode arg\n");
    }
}
EXPORT_SYMBOL(process_perfs);