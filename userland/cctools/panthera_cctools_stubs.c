#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <mach/mach.h>

void
make_obj_file_with_linker_options(uint32_t cputype, uint32_t cpusubtype,
    unsigned int nreflibs, char **reflibs, unsigned int nreffw, char **reffw,
    char *output_path)
{
	(void)cputype;
	(void)cpusubtype;
	(void)nreflibs;
	(void)reflibs;
	(void)nreffw;
	(void)reffw;
	(void)output_path;

	fprintf(stderr,
	    "libtool: -l/-framework reference object generation is not available in Panthera cctools yet\n");
	exit(1);
}

kern_return_t
vm_flush_cache(mach_port_t target_task, vm_address_t address, vm_size_t size)
{
	(void)target_task;
	(void)address;
	(void)size;
	return KERN_SUCCESS;
}

kern_return_t
vm_msync(vm_map_t target_task, vm_address_t address, vm_size_t size,
    vm_sync_t sync_flags)
{
	(void)target_task;
	(void)address;
	(void)size;
	(void)sync_flags;
	return KERN_SUCCESS;
}
