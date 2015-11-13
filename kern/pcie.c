#include <inc/assert.h>

#include <kern/acpi.h>
#include <kern/pcie.h>
#include <kern/pci.h>
#include <kern/pcireg.h>
#include <kern/pmap.h>

#define ACPI_SIG_MCFG	"MCFG"

struct acpi_mcfg_allocation {
	uint64_t address;
	uint16_t pci_segment;
	uint8_t start_bus_number;
	uint8_t end_bus_number;
	uint32_t reserved;
} __attribute__((packed));

struct acpi_table_mcfg {
	struct acpi_table_header header;
	uint8_t reserved[8];
	struct acpi_mcfg_allocation entry[];
} __attribute__((packed));

// Forward declarations
static int pci_bridge_attach(struct pci_func *pcif);
//static int ahci_attach(struct pci_func *pcif);

// PCI driver table
struct pci_driver {
	uint32_t key1, key2;
	int (*attachfn) (struct pci_func *pcif);
};

// pci_attach_class matches the class and subclass of a PCI device
static struct pci_driver pci_attach_class[] = {
	{ PCI_CLASS_BRIDGE, PCI_SUBCLASS_BRIDGE_PCI, &pci_bridge_attach },
//	{ PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_MASS_STORAGE_SATA, &ahci_attach },
	{ 0, 0, 0 },
};

// pci_attach_vendor matches the vendor ID and device ID of a PCI device
static struct pci_driver pci_attach_vendor[] = {
	{ 0, 0, 0 },
};

static uint32_t
pci_conf_read(void *va, uint32_t off)
{
	return *(volatile uint32_t *)(va + off);
}
/*
static void
pci_conf_write(struct pci_func *f, uint32_t off, uint32_t v)
{
}
*/

static int __attribute__((warn_unused_result))
pci_attach_match(uint32_t key1, uint32_t key2,
		 struct pci_driver *list, struct pci_func *pcif)
{
	uint32_t i;

	for (i = 0; list[i].attachfn; i++) {
		if (list[i].key1 == key1 && list[i].key2 == key2) {
			int r = list[i].attachfn(pcif);
			if (r > 0)
				return r;
			if (r < 0)
				cprintf("pci_attach_match: attaching "
					"%x.%x (%p): e\n",
					key1, key2, list[i].attachfn, r);
		}
	}
	return 0;
}

static int
pci_attach(struct pci_func *f)
{
	return
		pci_attach_match(PCI_CLASS(f->dev_class),
				 PCI_SUBCLASS(f->dev_class),
				 &pci_attach_class[0], f) ||
		pci_attach_match(PCI_VENDOR(f->dev_id),
				 PCI_PRODUCT(f->dev_id),
				 &pci_attach_vendor[0], f);
}

static const char *pci_class[] =
{
	[0x0] = "Unknown",
	[0x1] = "Mass storage controller",
	[0x2] = "Network controller",
	[0x3] = "Display controller",
	[0x4] = "Multimedia device",
	[0x5] = "Memory controller",
	[0x6] = "Bridge device",
};

static void
pci_print_func(struct pci_func *f)
{
	const char *class = pci_class[0];
	if (PCI_CLASS(f->dev_class) < ARRAY_SIZE(pci_class))
		class = pci_class[PCI_CLASS(f->dev_class)];

	cprintf("PCI: %02x:%02x.%d %04x:%04x %02x.%02x v%x %s\n",
		f->bus->busno, f->dev, f->func,
		PCI_VENDOR(f->dev_id), PCI_PRODUCT(f->dev_id),
		PCI_CLASS(f->dev_class), PCI_SUBCLASS(f->dev_class),
		PCI_REVISION(f->dev_class), class);
}

static void
pci_scan_bus(struct pci_bus *bus)
{
	struct pci_func df = { .bus = bus };

        for (df.dev = 0; df.dev < 32; df.dev++) {
		uint32_t bhlc = pci_conf_read(&df, PCI_BHLC_REG);
		if (PCI_HDRTYPE_TYPE(bhlc) > 1)	    // Unsupported or no device
			continue;

		struct pci_func f = df;
		for (f.func = 0; f.func < (PCI_HDRTYPE_MULTIFN(bhlc) ? 8 : 1);
		     f.func++) {
			struct pci_func af = f;
			void *va;

			va = mmio_map_region(af.bus->address + (af.bus->busno << 20) + (af.dev << 15) + (af.func << 12), PGSIZE);
			af.dev_id = pci_conf_read(va, PCI_ID_REG);
			if (PCI_VENDOR(af.dev_id) == 0xffff)
				continue;

			uint32_t intr = pci_conf_read(va, PCI_INTERRUPT_REG);
			af.irq_line = PCI_INTERRUPT_LINE(intr);

			af.dev_class = pci_conf_read(va, PCI_CLASS_REG);
			mmio_unmap_region(va, PGSIZE);
			pci_print_func(&af);
			pci_attach(&af);
		}
	}
}

void
pcie_init()
{
	struct acpi_table_mcfg *mcfg;
	struct acpi_mcfg_allocation *alloc, *alloc_end;

	mcfg = acpi_get_table("MCFG");
	if (!mcfg)
		panic("PCIe: MCFG not found!\n");
	alloc = mcfg->entry;
	alloc_end = (void *)mcfg + mcfg->header.length;
	for (; alloc < alloc_end; ++alloc) {
		int busno, busno_end;

		busno = alloc->start_bus_number;
		busno_end = alloc->end_bus_number;
		cprintf("PCIe: %08p [bus %d-%d]\n", alloc->address, busno, busno_end);
//		for (; busno != busno_end + 1; ++busno) {
			struct pci_bus bus = {
				.address = alloc->address,
				.busno = busno,
			};
			pci_scan_bus(&bus);
//		}
	}
}

static int
pci_bridge_attach(struct pci_func *pcif)
{
	uint32_t ioreg  = pci_conf_read(pcif, PCI_BRIDGE_STATIO_REG);
	uint32_t busreg = pci_conf_read(pcif, PCI_BRIDGE_BUS_REG);

	if (PCI_BRIDGE_IO_32BITS(ioreg)) {
		cprintf("PCI: %02x:%02x.%d: 32-bit bridge IO not supported.\n",
			pcif->bus->busno, pcif->dev, pcif->func);
		return 0;
	}

	struct pci_bus nbus = {
		.parent_bridge = pcif,
		.address = pcif->bus->address,
		.busno = (busreg >> PCI_BRIDGE_BUS_SECONDARY_SHIFT) & 0xff,
	};

	cprintf("PCI: %02x:%02x.%d: bridge to PCI bus %d--%d\n",
		pcif->bus->busno, pcif->dev, pcif->func,
		nbus.busno,
		(busreg >> PCI_BRIDGE_BUS_SUBORDINATE_SHIFT) & 0xff);

	pci_scan_bus(&nbus);
	return 1;
}
