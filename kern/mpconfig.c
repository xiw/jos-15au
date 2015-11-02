/* See COPYRIGHT for copyright information. */

#include <inc/assert.h>

#include <kern/acpi.h>
#include <kern/cpu.h>

struct CpuInfo cpus[NCPU];
int ncpu;

// Per-CPU kernel stacks
unsigned char percpu_kstacks[NCPU][KSTKSIZE]
__attribute__ ((aligned(PGSIZE)));

void
mp_init(void)
{
	struct acpi_table_madt *madt;
	struct acpi_subtable_header *hdr, *end;

	// 5.2.12.1 MADT Processor Local APIC / SAPIC Structure Entry Order
	// * initialize processors in the order that they appear in MADT;
	// * the boot processor is the first processor entry.
	bootcpu->cpu_status = CPU_STARTED;

	madt = acpi_get_table(ACPI_SIG_MADT);
	if (!madt)
		panic("ACPI: No MADT found");

	lapic_addr = madt->address;

	hdr = (void *)madt + sizeof(*madt);
	end = (void *)madt + madt->header.length;
	for (; hdr < end; hdr = (void *)hdr + hdr->length) {
		switch (hdr->type) {
		case ACPI_MADT_TYPE_LOCAL_APIC: {
			struct acpi_madt_local_apic *p = (void *)hdr;
			bool enabled = p->lapic_flags & BIT(0);

			if (ncpu < NCPU && enabled) {
				// Be careful: cpu_apicid may differ from cpus index
				cpus[ncpu].cpu_apicid = p->id;
				ncpu++;
			}
			break;
		}
		case ACPI_MADT_TYPE_IO_APIC: {
			struct acpi_madt_io_apic *p = (void *)hdr;

			// We use one IOAPIC.
			if (p->global_irq_base == 0)
				ioapic_addr = p->address;
			break;
		}
		default:
			break;
		}
	}

	cprintf("SMP: %d CPU(s)\n", ncpu);
}
