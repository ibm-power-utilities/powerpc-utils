/**
 * @file lparstat.h
 * @brief lparstat command header
 *
 * Copyright (c) 2011 International Business Machines
 * Common Public License Version 1.0 (see COPYRIGHT)
 *
 * @author Nathan Fontenot <nfont@linux.vnet.ibm.com>
 */

#define SYSDATA_VALUE_SZ	64
#define SYSDATA_NAME_SZ		64
#define SYSDATA_DESCR_SZ	128

struct sysentry {
	char	value[SYSDATA_VALUE_SZ];	/* value from file */
	char	old_value[SYSDATA_VALUE_SZ];	/* previous value from file */
	char	name[SYSDATA_NAME_SZ];		/* internal name */
	char	descr[SYSDATA_DESCR_SZ];	/* description of data */
	void (*get)(struct sysentry *, char *);
};

extern void get_smt_state(struct sysentry *, char *);
extern void get_capped_mode(struct sysentry *, char *);
extern void get_memory_mode(struct sysentry *, char *);
extern void get_percent_entry(struct sysentry *, char *);
extern void get_phys_cpu_percentage(struct sysentry *, char *);
extern void get_active_cpus_in_pool(struct sysentry *, char *);
extern void get_partition_name(struct sysentry *, char *);
extern void get_node_name(struct sysentry *, char *);
extern void get_mem_total(struct sysentry *, char *);
extern void get_smt_mode(struct sysentry *, char *);
extern void get_cpu_stat(struct sysentry *, char *);
extern void get_cpu_physc(struct sysentry *, char *);
extern void get_per_entc(struct sysentry *, char *);

struct sysentry system_data[] = {
	/* System Names */
	{.name = "node_name",
	 .descr = "Node Name",
	 .get = &get_node_name},
	{.name = "partition_name",
	 .descr = "Partition Name",
	 .get = &get_partition_name},

	/* lparcfg data */
	{.name = "serial_number",
	 .descr = "Serial Number"},
	{.name = "system_type",
	 .descr = "System Model"},
	{.name = "partition_id",
	 .descr = "Partition Number"},
	{.name = "group",
	 .descr = "Partition Group-ID"},
	{.name = "BoundThrds",
	 .descr = "Bound Threads"},
	{.name = "CapInc",
	 .descr = "Capacity Increment",
	 .get = &get_percent_entry},
	{.name = "DisWheRotPer",
	 .descr = "Dispatch Wheel Rotation Period"},
	{.name = "MinEntCap",
	 .descr = "Minimum Capacity",
	 .get = &get_percent_entry},
	{.name = "MinEntCapPerVP",
	 .descr = "Minimum Entitled Capacity per Virtual Processor"},
	{.name = "MinProcs",
	 .descr = "Minimum Virtual CPUs"},
	{.name = "partition_max_entitled_capacity",
	 .descr = "Maximum Capacity",
	 .get = &get_percent_entry},
	{.name = "system_potential_processors",
	 .descr = "Maximum System Processors"},
	{.name = "DesEntCap",
	 .descr = "Entitled Capacity",
	 .get = &get_percent_entry},
	{.name = "DesProcs",
	 .descr = "Desired Processors"},
	{.name = "DesVarCapWt",
	 .descr = "Desired Variable Capacity Weight"},
	{.name = "DedDonMode",
	 .descr = "Dedicated Donation Mode"},
	{.name = "partition_entitled_capacity",
	 .descr = "Partition Entitled Capacity"},
	{.name = "system_active_processors",
	 .descr = "Active Physical CPUs in system"},
	{.name = "pool",
	 .descr = "Shared Pool ID"},
	{.name = "pool_capacity",
	 .descr = "Maximum Capacity of Pool",
	 .get = &get_percent_entry},
	{.name = "pool_idle_time",
	 .descr = "Shared Processor Pool Idle Time"},
	{.name = "pool_num_procs",
	 .descr = "Shared Processor Pool Processors"},
	{.name = "unallocated_capacity_weight",
	 .descr = "Unallocated Weight"},
	{.name = "capacity_weight",
	 .descr = "Entitled Capacity of Pool"},
	{.name = "capped",
	 .descr = "Mode",
	 .get = &get_capped_mode},
	{.name = "unallocated_capacity",
	 .descr = "Unallocated Processor Capacity"},
	{.name = "physical_procs_allocated_to_virtualization",
	 .descr = "Physical Processor Allocated to Virtualization"},
	{.name = "max_proc_entitled_capacity",
	 .descr = "Maximum Processor Capacity Available to Pool"},
	{.name = "entitled_proc_capacity_available",
	 .descr = "Entitled Capacity of Pool"},
	{.name = "dispatches",
	 .descr = "Virtual Processor Dispatch Counter"},
	{.name = "dispatch_dispersions",
	 .descr = "Virtual Processor Dispersions"},
	{.name = "purr",
	 .descr = "Processor Utilization Resource Register"},
	{.name = "partition_active_processors",
	 .descr = "Online Virtual CPUs"},
	{.name = "partition_potential_processors",
	 .descr = "Maximum Virtual CPUs"},
	{.name = "shared_processor_mode",
	 .descr = "Type",
	 .get = &get_smt_state},
	{.name = "slb_size",
	 .descr = "SLB Entries"},
	{.name = "MinMem",
	 .descr = "Minimum Memory"},
	{.name = "DesMem",
	 .descr = "Desired Memory"},
	{.name = "entitled_memory",
	 .descr = "Total I/O Memory Entitlement"},
	{.name = "mapped_entitled_memory",
	 .descr = "Total I/O Mapped Entitled Memory"},
	{.name = "entitled_memory_group_number",
	 .descr = "Memory Group ID of LPAR"},
	{.name = "entitled_memory_pool_number",
	 .descr = "Memory Pool ID"},
	{.name = "entitled_memory_pool_size",
	 .descr = "Physical Memory in the Pool"},
	{.name = "entitled_memory_weight",
	 .descr = "Variable Memory Capacity Weight"},
	{.name = "unallocated_entitled_memory_weight",
	 .descr = "Unallocated Variable Memory Capacity Weight"},
	{.name = "unallocated_io_mapping_entitlement",
	 .descr = "Unallocated I/O Memory Entitlement"},
	{.name = "entitled_memory_loan_request",
	 .descr = "Entitled Memory Loan Request"},
	{.name = "backing_memory",
	 .descr = "Backing Memory"},
	{.name = "cmo_enabled",
	 .descr = "Active Memory Sharing Enabled"},
	{.name = "cmo_faults",
	 .descr = "Active Memory Sharing Page Faults"},
	{.name = "cmo_fault_time_usec",
	 .descr = "Active Memory Sharing Fault Time"},
	{.name = "cmo_primary_psp",
	 .descr = "Primary VIOS Partition ID"},
	{.name = "cmo_secondary_psp",
	 .descr = "Secondary VIOS Partition ID"},
	{.name = "cmo_page_size",
	 .descr = "Physical Page Size"},

	/* /proc/meminfo */
	{.name = "MemTotal",
	 .descr = "Online Memory",
	 .get = &get_mem_total},

	/* ppc64_cpu --smt */
	{.name = "smt_state",
	 .descr = "SMT",
	 .get = &get_smt_state},

	/* /proc/stat */
	{.name = "cpu_total",
	 .descr = "CPU Total Time"},
	{.name = "cpu_user",
	 .descr = "CPU User Time",
	 .get = &get_cpu_stat},
	{.name = "cpu_nice",
	 .descr = "CPU Nice Time",
	 .get = &get_cpu_stat},
	{.name = "cpu_sys",
	 .descr = "CPU System Time",
	 .get = &get_cpu_stat},
	{.name = "cpu_idle",
	 .descr = "CPU Idle Time",
	 .get = &get_cpu_stat},
	{.name = "cpu_iowait",
	 .descr = "CPU I/O Wait Time",
	 .get = &get_cpu_stat},
	{.name = "cpu_lbusy",
	 .descr = "Logical CPU Utilization",
	 .get = &get_cpu_stat},

	/* placeholders for derived values */
	{.name = "active_cpus_in_pool",
	 .descr = "Active CPUs in Pool",
	 .get = &get_active_cpus_in_pool},
	{.name = "phys_cpu_percentage",
	 .descr = "Physical CPU Percentage",
	 .get = &get_phys_cpu_percentage},
	{.name = "memory_mode",
	 .descr = "Memory Mode",
	 .get = &get_memory_mode},
	{.name = "physc",
	 .descr = "Physical CPU Consumed",
	 .get = &get_cpu_physc},
	{.name = "per_entc",
	 .descr = "Entitled CPU Consumed",
	 .get = &get_per_entc},

	/* Time */
	{.name = "time",
	 .descr = "Time"},

	/* /proc/cpuinfo */
	{.name = "timebase",
	 .descr = "Timebase"},

	/* /proc/interrupts */
	{.name = "phint",
	 .descr = "Phantom Interrupts"},

	{.name[0] = '\0'},
};

char *iflag_entries[] = {
	"node_name",
	"partition_name",
	"partition_id",
	"shared_processor_mode",
	"capped",
	"DesEntCap",
	"group",
	"pool",
	"partition_active_processors",
	"partition_potential_processors",
	"MinProcs",
	"MemTotal",
	"MinMem",
	"MaxMem",
	"DesVarCapWt",
	"MinEntCap",
	"partition_max_entitled_capacity",
	"CapInc",
	"max_system_cpus",
	"system_active_processors",
	"active_cpus_in_pool",
	"shared_cpus_in_system",
	"pool_capacity",
	"entitled_proc_capacity_available",
	"unallocated_capacity",
	"phys_cpu_percentage",
	"unallocated_capacity_weight",
	"memory_mode",
	"entitled_memory",
	"entitled_memory_weight",
	"entitled_memory_pool_number",
	"entitled_memory_pool_size",
	"hypervisor_page_size",
	"unallocated_entitled_memory_weight",	
	"unallocated_io_mapping_entitlement",
	"entitled_memory_group_number",
	"desired_virt_cpus",
	"desired_memory",
	"DesVarCapWt",
	"desired_capacity",
	"target_mem_factor",
	"target_mem_size",
	NULL
};

