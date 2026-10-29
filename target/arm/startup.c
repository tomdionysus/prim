// Bare-metal start for the suite and the soak under QEMU's MPS2 machines: a
// vector table, a reset that zeroes .bss and runs the static constructors
// (each TEST registers itself in one), then main with the command line QEMU
// was given by -append. Output and exit go to the host by semihosting,
// through newlib's librdimon. Only for QEMU: it loads .data where it runs,
// so nothing is copied from flash.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t __bss_start__, __bss_end__, __stack_top;
extern void __libc_init_array(void);
extern void initialise_monitor_handles(void);
extern int main(int argc, char** argv);

// What crti, crtn and crtbegin give a hosted program, which -nostartfiles
// leaves out: newlib calls _init and _fini, and libstdc++ registers its
// destructors against __dso_handle.
void _init(void) {}
void _fini(void) {}
void* __dso_handle = 0;

static uint32_t semihost(uint32_t op, void* arg) {
  register uint32_t r0 __asm__("r0") = op;
  register void* r1 __asm__("r1") = arg;
  __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

// The command line, split at spaces: QEMU gives the image's path, then what
// -append says.
static char line[256];
static char* args[9];
static int command_line(void) {
  struct {
    char* buf;
    uint32_t len;
  } block = {line, sizeof line - 1};
  if (semihost(0x15, &block) != 0) return 0;  // SYS_GET_CMDLINE
  line[block.len] = 0;
  int n = 0;
  for (char* p = line; *p && n < 8;) {
    while (*p == ' ') *p++ = 0;
    if (!*p) break;
    args[n++] = p;
    while (*p && *p != ' ') ++p;
  }
  args[n] = 0;
  return n;
}

void Reset_Handler(void) {
  memset(&__bss_start__, 0, (size_t)((char*)&__bss_end__ - (char*)&__bss_start__));
  initialise_monitor_handles();
  __libc_init_array();
  const int argc = command_line();
  exit(main(argc, args));
}

// Any exception ends the run at once, by semihosting, as a failure: QEMU
// exits 1 for any stop reason but application exit. No newlib from here.
__attribute__((noreturn)) void Fault_Handler(void) {
  semihost(0x18, (void*)0x20023);  // SYS_EXIT, ADP_Stopped_InternalError
  for (;;) {
  }
}

__attribute__((section(".isr_vector"), used)) static void (*const vectors[16])(void) = {
    (void (*)(void))&__stack_top,
    Reset_Handler,
    Fault_Handler,  // NMI
    Fault_Handler,  // HardFault
    Fault_Handler,  // MemManage
    Fault_Handler,  // BusFault
    Fault_Handler,  // UsageFault
    Fault_Handler,  // SecureFault
    Fault_Handler,  // reserved
    Fault_Handler,  // reserved
    Fault_Handler,  // reserved
    Fault_Handler,  // SVCall
    Fault_Handler,  // DebugMonitor
    Fault_Handler,  // reserved
    Fault_Handler,  // PendSV
    Fault_Handler,  // SysTick
};
