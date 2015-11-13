#ifndef JOS_INC_AHCI_H
#define JOS_INC_AHCI_H

#include <inc/types.h>

// Command register
enum {
	ATA_CMD_READ_DMA_EXT	= 0x25,
	ATA_CMD_WRITE_DMA_EXT	= 0x35,
	ATA_CMD_FLUSH_CACHE	= 0xe7,
	ATA_CMD_IDENTIFY	= 0xec,
	ATA_CMD_SET_FEATURES	= 0xef,
};

// Device register
enum {
	ATA_DEV_LBA		= BIT(6),
	ATA_DEV_FUA		= BIT(7),
};

// Status register
enum {
	ATA_STAT_ERR		= BIT(1),
	ATA_STAT_DRQ		= BIT(3),
	ATA_STAT_DF		= BIT(5),
	ATA_STAT_DRDY		= BIT(6),
	ATA_STAT_BSY		= BIT(7),
};

// Device Control register
enum {
	ATA_CTL_NIEN		= BIT(1),
	ATA_CTL_SRST		= BIT(2),
	ATA_CTL_HOB		= BIT(7),
};

enum {
	SATA_FIS_TYPE_REG_H2D	= 0x27,
};

// Register - Host to Device
struct sata_fis_reg_h2d {
	// DW0
	uint8_t fis_type;	// FIS Type: SATA_FIS_TYPE_REG_H2D
	uint8_t pmport : 4;
	uint8_t reserved0 : 3;
	uint8_t c : 1;		// 1: Command; 0:  Control
	uint8_t command;	// Command
	uint8_t features0;	// Features (7:0)
	// DW1
	uint8_t lba0;		// LBA (7:0)
	uint8_t lba1;		// LBA (15:8)
	uint8_t lba2;		// LBA (23:16)
	uint8_t device;
	// DW2
	uint8_t lba3;		// LBA (31:24)
	uint8_t lba4;		// LBA (39:32)
	uint8_t lba5;		// LBA (47:40)
	uint8_t features1;	// Features (15:8)
	// DW3
	uint8_t count0;		// Sector Count (7:0)
	uint8_t count1;		// Sector Count (15:8)
	uint8_t icc;		// Isochronous Command Completion
	uint8_t control;	// Device Control
	// DW4
	uint8_t reserved1[4];
} __attribute__((packed));

// IDENTIFY DEVICE
// Most fields are little-endian; there are four exceptions:
// in serial/firmware/model/wwn, each pair of bytes is swapped.
struct ata_identify_device {
	uint16_t reserved0[10];
	uint16_t serial[20 - 10];	// Serial number (20 ASCII characters)
	uint16_t reserved1[23 - 20];
	uint16_t firmware[27 - 23];	// Firmware revision (8 ASCII characters)
	uint16_t model[47 - 27];	// Model number (40 ASCII characters)
	uint16_t reserved2[60 - 47];
	uint16_t lba_sectors[62 - 60];
	uint16_t reserved3[100 - 62];
	uint16_t lba48_sectors[104 - 100];
	uint16_t reserved4[108 - 104];
	uint16_t wwn[112 - 108];	// World wide name
	uint16_t reserved5[256 - 112];
} __attribute__((packed));

// 3.1.2 Offset 04h: GHC – Global HBA Control
enum {
	AHCI_GHC_HR		= BIT(0),	// HBA Reset
	AHCI_GHC_IE		= BIT(1),	// Interrupt Enable
	AHCI_GHC_AE		= BIT(31),	// AHCI Enable
};

// 3.3.7 Offset 18h: PxCMD – Port x Command and Status
enum {
	AHCI_PORT_CMD_ST	= BIT(0),	// Start
	AHCI_PORT_CMD_SUD	= BIT(1),	// Spin-Up Device
	AHCI_PORT_CMD_POD	= BIT(2),	// Power On Device
	AHCI_PORT_CMD_FRE	= BIT(4),	// FIS Receive Enable
	AHCI_PORT_CMD_FR	= BIT(14),	// FIS Receive Running
	AHCI_PORT_CMD_CR	= BIT(15),	// Command List Running
};

// 3.3.8 Offset 20h: PxTFD – Port x Task File Data
struct ahci_port_tfd {
	uint8_t sts;			// Status
	uint8_t err;			// Error
	uint16_t reserved;
} __attribute__((packed));

// 3.3 Port Registers (one set per port)
struct ahci_port {
	uint64_t clb;			// Command List Base Address
	uint64_t fb;			// FIS Base Address
	uint32_t is;			// Interrupt Status
	uint32_t ie;			// Interrupt Enable
	uint32_t cmd;			// Command and Status
	uint32_t reserved0;
	struct ahci_port_tfd tfd;	// Task File Data
	uint32_t sig;			// Signature
	uint32_t ssts;			// SATA Status (SCR0: SStatus)
	uint32_t sctl;			// SATA Control (SCR2: SControl)
	uint32_t serr;			// SATA Error (SCR1: SError)
	uint32_t sact;			// SATA Active (SCR3: SActive)
	uint32_t ci;			// Command Issue
	uint32_t sntf;			// SATA Notification (SCR4: SNotification)
	uint32_t fbs;			// FIS-based Switching Control
	uint32_t devslp;		// Device Sleep
	uint8_t reserved1[0x80 - 0x48];
} __attribute__((packed));

// 3 HBA Memory Registers
struct ahci_memory {
	// 3.1 Generic Host Control
	uint32_t cap;			// Host Capabilities
	uint32_t ghc;			// Global Host Control
	uint32_t is;			// Interrupt Status
	uint32_t pi;			// Ports Implemented
	uint32_t vs;			// Version
	uint32_t ccc_ctl;		// Command Completion Coalescing Control
	uint32_t ccc_ports;		// Command Completion Coalescing Ports
	uint32_t em_loc;		// Enclosure Management Location
	uint32_t em_ctl;		// Enclosure Management Control
	uint32_t cap2;			// Host Capabilities Extended
	uint32_t bohc;			// BIOS/OS Handoff Control and Status
	uint8_t reserved[0x100 - 0x2c];
	volatile struct ahci_port ports[32];
} __attribute__((packed));

// 4.2.1 Received FIS Structure
struct ahci_recv_fis {
	uint8_t dsfis[0x1c];		// DMA Setup FIS
	uint8_t reserved0[0x20 - 0x1c];
	uint8_t psfis[0x34 - 0x20];	// PIO Setup FIS
	uint8_t reserved1[0x40 - 0x34];
	uint8_t rfis[0x54 - 0x40];	// D2H Register FIS
	uint8_t reserved2[0x58 - 0x54];
	uint8_t sdbfis[0x60 - 0x58];	// Set Device Bits FIS
	uint8_t ufis[0xa0 - 0x60];	// Unknown FIS
	uint8_t reserved3[0x100 - 0xa0];
} __attribute__((packed));

// 4.2.2 Command List Structure
struct ahci_cmd_header {
	// DW0
	uint8_t cfl : 5;		// Command FIS Length
	uint8_t a : 1;			// ATAPI
	uint8_t w : 1;			// Write
	uint8_t p : 1;			// Prefetchable
	uint8_t b : 1;			// BIST
	uint8_t c : 1;			// Clear Busy upon R_OK
	uint8_t reserved0 : 1;
	uint8_t pmp : 4;		// Port Multiplier Port
	uint16_t prdtl;			// Physical Region Descriptor Table Length
	// DW1
	uint32_t prdbc;			// PRD Byte Count
	// DW2-3
	uint64_t ctba;			// Command Table Descriptor Base Address
	// DW4-7
	uint32_t reserved1[4];
} __attribute__((packed));

// 4.2.3.3 Physical Region Descriptor Table (PRDT)
struct ahci_prd {
	uint64_t dba;			// Data Base Address
	uint32_t reserved0;
	uint32_t dbc : 22;		// Data Byte Count
	uint32_t reserved1 : 9;
	uint8_t i : 1;			// Interrupt on Completion
} __attribute__((packed));

// 4.2.3 Command Table
struct ahci_cmd_table {
	uint8_t cfis[0x40];		// Command FIS
	uint8_t acmd[0x10];		// ATAPI Command
	uint8_t reserved[0x30];
	struct ahci_prd prdt[1];	// Physical Region Descriptor Table
} __attribute__((packed));
#endif	// !JOS_INC_AHCI_H
