/* See COPYRIGHT for copyright information. */

#include <inc/stdio.h>
#include <inc/string.h>

#include <kern/acpi.h>
#include <kern/pmap.h>

#define ACPI_NR_MAX	32

struct acpi_tables {
	uint32_t nr;
	struct acpi_table_header *entries[ACPI_NR_MAX];
};

static struct acpi_tables acpi_tables;

static void
print_table_rsdp(struct acpi_table_rsdp *rsdp)
{
	cprintf("ACPI: RSDP %08p %06x v%02d %.6s\n",
		PADDR(rsdp), (rsdp->revision) ? rsdp->length : 20,
		rsdp->revision, rsdp->oem_id);
}

static void
print_table_header(struct acpi_table_header *hdr)
{
	cprintf("ACPI: %.4s %08p %06x v%02d %.6s %.8s %02d %.4s %02d\n",
		hdr->signature, PADDR(hdr), hdr->length, hdr->revision,
		hdr->oem_id, hdr->oem_table_id, hdr->oem_revision,
		hdr->asl_compiler_id, hdr->asl_compiler_revision);
}

static uint8_t
sum(void *addr, int len)
{
	int i, sum;

	sum = 0;
	for (i = 0; i < len; i++)
		sum += ((uint8_t *)addr)[i];
	return sum;
}

// Look for the RSDP in the len bytes at physical address addr.
static struct acpi_table_rsdp *
rsdp_search1(physaddr_t a, int len)
{
	void *p = KADDR(a), *e = KADDR(a + len);

	// The signature is on a 16-byte boundary.
	for (; p < e; p += 16) {
		struct acpi_table_rsdp *rsdp = p;

		if (memcmp(rsdp->signature, ACPI_SIG_RSDP, 8) ||
		    sum(rsdp, 20))
			continue;
		// ACPI 2.0+
		if (rsdp->revision && sum(rsdp, rsdp->length))
			continue;
		return rsdp;
	}
	return NULL;
}

// Search for the RSDP at the following locations:
// * the first KB of the EBDA;
// * the BIOS ROM between 0xE0000 and 0xFFFFF.
static struct acpi_table_rsdp *
rsdp_search(void)
{
	physaddr_t ebda;
	struct acpi_table_rsdp *rsdp;

	// The 16-bit segment of the EBDA is in the two byte at 0x40:0x0E.
	ebda = *(uint16_t *) KADDR(0x40E);
	ebda <<= 4;
	if ((rsdp = rsdp_search1(ebda, 1024)))
		return rsdp;
	return rsdp_search1(0xE0000, 0x20000);
}

void
acpi_init(void)
{
	struct acpi_table_rsdp *rsdp;
	struct acpi_table_header *hdr;
	const char *sig;
	size_t entry_size;
	void *p, *e;
	uint32_t i;

	rsdp = rsdp_search();
	if (!rsdp)
		panic("ACPI: No RSDP found");
	print_table_rsdp(rsdp);

	if (rsdp->revision) {
		hdr = KADDR(rsdp->xsdt_physical_address);
		sig = ACPI_SIG_XSDT;
		entry_size = 8;
	} else {
		hdr = KADDR(rsdp->rsdt_physical_address);
		sig = ACPI_SIG_RSDT;
		entry_size = 4;
	}

	if (memcmp(hdr->signature, sig, 4))
		panic("ACPI: Incorrect %s signature", sig);
	if (sum(hdr, hdr->length))
		panic("ACPI: Bad %s checksum", sig);
	print_table_header(hdr);

	p = hdr + 1;
	e = (void *)hdr + hdr->length;
	for (i = 0; p < e; p += entry_size) {
		hdr = KADDR(*(uint32_t *)p);
		if (sum(hdr, hdr->length))
			continue;
		print_table_header(hdr);
		assert(i < ACPI_NR_MAX);
		acpi_tables.entries[i++] = hdr;
	}
	acpi_tables.nr = i;
}

void *
acpi_get_table(const char *signature)
{
	uint32_t i;
	struct acpi_table_header **phdr = acpi_tables.entries;

	for (i = 0; i < acpi_tables.nr; ++i, ++phdr) {
		if (!memcmp((*phdr)->signature, signature, 4))
			return *phdr;
	}
	return NULL;
}
