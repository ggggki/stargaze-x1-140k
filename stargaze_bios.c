//==============================================================================
// STARGASE X1 - RISC-V BIOS
// Compile: riscv-none-elf-gcc -march=rv64imafdc -mabi=lp64d -nostdlib -O2 -o bios.elf bios.c
//==============================================================================

#define UART_BASE      0x10000000
#define DDR3_BASE      0x00000000
#define SPI_FLASH_BASE 0x00000000
#define PCIE_BASE      0x20000000
#define SATA_BASE      0x30000000
#define LINUX_KERNEL_OFFSET 0x01000000  // 1MB into flash

typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

// UART registers
#define UART_TXDATA    (*(volatile uint32_t*)(UART_BASE + 0x00))
#define UART_TXFULL    (*(volatile uint32_t*)(UART_BASE + 0x04))
#define UART_RXDATA    (*(volatile uint32_t*)(UART_BASE + 0x08))
#define UART_RXEMPTY   (*(volatile uint32_t*)(UART_BASE + 0x0C))

// Simple UART print
void uart_putc(char c) {
    while (UART_TXFULL);
    UART_TXDATA = c;
}

void uart_print(const char *s) {
    while (*s) uart_putc(*s++);
}

void uart_print_hex(uint64_t n) {
    char hex[] = "0123456789ABCDEF";
    char buf[17] = {0};
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[n & 0xF];
        n >>= 4;
    }
    uart_print(buf);
}

// DDR3 initialization
void ddr3_init(void) {
    uart_print("[BIOS] Initializing DDR3 memory...\n");
    
    // 1. Wait for voltage stable (already handled by SMC)
    // 2. Reset DDR3 controller
    *(volatile uint32_t*)(DDR3_BASE + 0x100) = 0x00000001;  // Reset
    for (volatile int i = 0; i < 10000; i++);               // Wait
    *(volatile uint32_t*)(DDR3_BASE + 0x100) = 0x00000000;  // Release reset
    
    // 3. Configure DDR3 timing registers
    *(volatile uint32_t*)(DDR3_BASE + 0x104) = 0x0000000E;  // tRCD = 14
    *(volatile uint32_t*)(DDR3_BASE + 0x108) = 0x0000000B;  // tCL = 11
    *(volatile uint32_t*)(DDR3_BASE + 0x10C) = 0x0000000E;  // tRP = 14
    *(volatile uint32_t*)(DDR3_BASE + 0x110) = 0x00000023;  // tRAS = 35
    
    // 4. Enable DDR3 controller
    *(volatile uint32_t*)(DDR3_BASE + 0x114) = 0x00000001;  // CKE high
    
    // 5. Send ZQ calibration
    *(volatile uint32_t*)(DDR3_BASE + 0x118) = 0x00000003;  // ZQCL command
    for (volatile int i = 0; i < 500000; i++);              // Wait 500us
    
    uart_print("[BIOS] DDR3 ready\n");
}

// PCIe initialization
void pcie_init(void) {
    uart_print("[BIOS] Scanning PCIe bus...\n");
    
    // Scan all PCIe devices (bus 0, devices 0-31)
    for (int dev = 0; dev < 32; dev++) {
        uint32_t vendor = *(volatile uint32_t*)(PCIE_BASE + (dev << 12) + 0x00);
        if ((vendor & 0xFFFF) != 0xFFFF) {
            uart_print("  Found PCIe device: ");
            uart_print_hex(vendor);
            uart_print("\n");
        }
    }
    
    uart_print("[BIOS] PCIe scan complete\n");
}

// SATA disk detection
void sata_detect(void) {
    uart_print("[BIOS] Detecting SATA drives...\n");
    
    // Check each SATA port (0-3)
    for (int port = 0; port < 4; port++) {
        uint32_t status = *(volatile uint32_t*)(SATA_BASE + (port << 8) + 0x00);
        if (status & 0x00000001) {
            uart_print("  SATA port ");
            uart_putc('0' + port);
            uart_print(": Drive detected\n");
        }
    }
    
    uart_print("[BIOS] SATA detection complete\n");
}

// Load Linux kernel
void load_kernel(void) {
    uart_print("[BIOS] Loading Linux kernel...\n");
    
    // Read kernel from SPI flash at offset
    volatile uint32_t *kernel_dest = (uint32_t*)0x80000000;  // RISC-V boot address
    volatile uint32_t *kernel_src  = (uint32_t*)(SPI_FLASH_BASE + LINUX_KERNEL_OFFSET);
    
    for (int i = 0; i < 1024; i++) {  // Copy 4KB (placeholder)
        kernel_dest[i] = kernel_src[i];
    }
    
    uart_print("[BIOS] Kernel loaded at 0x80000000\n");
}

// Jump to Linux kernel
void jump_to_kernel(void) __attribute__((noreturn));
void jump_to_kernel(void) {
    uart_print("[BIOS] Jumping to Linux kernel...\n");
    uart_print("[BIOS] Boot complete!\n\n");
    
    // Jump to Linux kernel entry point
    void (*kernel_entry)(void) = (void (*)(void))0x80000000;
    kernel_entry();
    
    // Should never reach here
    while(1);
}

//==============================================================================
// MAIN - BIOS Entry Point
//==============================================================================
void _start(void) {
    // 1. Initialize UART for debug output
    uart_print("\n========================================\n");
    uart_print("  STARGASE X1 BIOS v1.0\n");
    uart_print("  RISC-V RV64IMAFDC\n");
    uart_print("========================================\n\n");
    
    // 2. Initialize DDR3 memory
    ddr3_init();
    
    // 3. Scan PCIe bus
    pcie_init();
    
    // 4. Detect storage drives
    sata_detect();
    
    // 5. Load Linux kernel
    load_kernel();
    
    // 6. Boot Linux!
    jump_to_kernel();
}