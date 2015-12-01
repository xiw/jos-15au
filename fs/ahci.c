#include <inc/ahci.h>
#include <inc/assert.h>
#include <inc/mmu.h>
#include <inc/string.h>
#include <fs/fs.h>

struct ahci_port_page {
	// 3.3.3 Offset 08h: PxFB – Port x FIS Base Address
	// 256-byte alignment
	volatile struct ahci_recv_fis rfis;
	uint8_t reserved[1024 - sizeof(struct ahci_recv_fis)];
	// 3.3.1 Offset 00h: PxCLB – Port x Command List Base Address
	// 1K-byte alignment
	volatile struct ahci_cmd_header cmdh[32];
	// 4.2.2 Command List Structure
	// Figure 10: DW 2 – Command Table Base Address
	// 128-byte alignment
	volatile struct ahci_cmd_table cmdt;
} __attribute__((packed, aligned(PGSIZE)));

static volatile struct ahci_memory *regs;
static struct ahci_port_page port_pages[32];
static int fs_portno;

static void
ahci_port_wait(volatile struct ahci_port *port, uint32_t ci)
{
	while ((port->tfd.sts & ATA_STAT_BSY) || (port->ci & ci))
		;
}

static int
ahci_port_reset(volatile struct ahci_port *port)
{
	int portno = port - regs->ports;
	struct ahci_port_page *page = &port_pages[port - regs->ports];

	if (!port->ssts)
		return -1;

	// Clear ST and then FRE
	port->cmd &= ~AHCI_PORT_CMD_ST;
	port->cmd &= ~AHCI_PORT_CMD_FRE;
	while (port->cmd & (AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR))
		;

	page->cmdh[0].ctba = physaddr((void *)&page->cmdt);
	port->clb = physaddr((void *)&page->cmdh);
	port->fb = physaddr((void *)&page->rfis);

	port->serr = ~0U;
	port->serr = 0;

	// Set FRE and then ST
	port->cmd |= AHCI_PORT_CMD_FRE;
	port->cmd |= AHCI_PORT_CMD_ST;

	return 0;
}

static void
fill_prd(volatile struct ahci_port *port, void *buf, size_t len)
{
	int portno = port - regs->ports;
	struct ahci_port_page *page = &port_pages[portno];

	page->cmdt.prdt[0].dba = physaddr(buf);
	page->cmdt.prdt[0].dbc = len - 1;
	page->cmdh[0].prdtl = 1;
}

static void
fill_fis(volatile struct ahci_port *port, void *fis, size_t len)
{
	int portno = port - regs->ports;
	struct ahci_port_page *page = &port_pages[portno];

	memcpy((void *)page->cmdt.cfis, fis, len);
	page->cmdh[0].cfl = len / sizeof(uint32_t);
}

static void
ahci_port_identify(volatile struct ahci_port *port, void *buf)
{
	struct sata_fis_reg_h2d fis = {
		.fis_type = SATA_FIS_TYPE_REG_H2D,
		.c = 1,
		.command = ATA_CMD_IDENTIFY,
		.count0 = 1,
	};

	fill_prd(port, buf, 512);
	fill_fis(port, &fis, sizeof(fis));
	port->ci |= 1;
	ahci_port_wait(port, 1);
}

static int
ahci_port_rw(volatile struct ahci_port *port, uint64_t secno, void *buf, uint16_t nsecs, bool iswrite)
{
	int portno = port - regs->ports;
	struct ahci_port_page *page = &port_pages[portno];
	volatile struct ahci_cmd_header *cmdh = &page->cmdh[0];
	struct sata_fis_reg_h2d fis = {
		.fis_type = SATA_FIS_TYPE_REG_H2D,
		.c = 1,
		.command = iswrite ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT,
		.count0 = (nsecs >> 0) & 0xff,
		.count1 = (nsecs >> 8) & 0xff,
		.lba0 = (secno >>  0) & 0xff,
		.lba1 = (secno >>  8) & 0xff,
		.lba2 = (secno >> 16) & 0xff,
		.lba3 = (secno >> 24) & 0xff,
		.lba4 = (secno >> 32) & 0xff,
		.lba5 = (secno >> 40) & 0xff,
		.device = ATA_DEV_LBA,
		.control = ATA_CTL_HOB,
	};

	assert(nsecs <= BLKSECTS);
	if (iswrite) {
		cmdh->prdbc = nsecs * SECTSIZE;
		cmdh->w = 1;
	} else {
		cmdh->prdbc = 0;
	}

	fill_prd(port, buf, nsecs * SECTSIZE);
	fill_fis(port, &fis, sizeof(fis));
	port->ci |= 1;
	ahci_port_wait(port, 1);
	return 0;
}

void
ahci_init(void)
{
	void *addr = (void *)UMMIOAHCI;
	int i;

	static_assert(sizeof(struct ata_identify_device) == 0x200);
	static_assert(sizeof(struct ahci_port) == 0x80);
	static_assert(sizeof(struct ahci_memory) == 0x1100);
	static_assert(sizeof(struct ahci_recv_fis) == 0x100);
	static_assert(sizeof(struct ahci_cmd_header) == 0x20);
	static_assert(sizeof(struct ahci_port_page) <= PGSIZE);

	// AHCI registers
	if (!va_is_mapped(addr))
		panic("AHCI not mapped");
	regs = addr;

	// Enable AHCI
	regs->ghc |= AHCI_GHC_AE;

	// Initialize each port
	for (i = 0; i < 32; i++) {
		volatile struct ahci_port *port;
		struct ata_identify_device dev;

		if (!(regs->pi & BIT(i)))
			continue;
		port = &regs->ports[i];
		if (ahci_port_reset(port))
			continue;
		ahci_port_identify(port, &dev);
		cprintf("AHCI.%d: %llu bytes\n",
			i, *(uint64_t *)dev.lba48_sectors * SECTSIZE);
		break;
	}
	// Use the first port found for FS
	if (i == 32)
		panic("Disk not found!");
	fs_portno = i;
}

int
ahci_read(uint64_t secno, void *buf, uint16_t nsecs)
{
	return ahci_port_rw(&regs->ports[fs_portno], secno, buf, nsecs, false);
}

int
ahci_write(uint64_t secno, void *buf, uint16_t nsecs)
{
	return ahci_port_rw(&regs->ports[fs_portno], secno, buf, nsecs, true);
}
