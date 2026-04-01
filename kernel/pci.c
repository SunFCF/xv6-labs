//
// simple PCI-Express initialization, only
// works for qemu and its e1000 card.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
// 这个函数会扫描PCI总线，找到e1000网卡，并初始化它。
// 具体来说，这个函数会将e1000的寄存器映射到物理地址0x40000000，并调用e1000_init()函数来初始化e1000。
void
pci_init()
{
  // we'll place the e1000 registers at this address.
  // vm.c maps this range.
  uint64 e1000_regs = 0x40000000L;

  // qemu -machine virt puts PCIe config space here.
  // vm.c maps this range.
  uint32  *ecam = (uint32 *) 0x30000000L;
  
  // look at each possible PCI device on bus 0.
  // 在PCI总线0上扫描每个可能的设备，找到e1000网卡，并初始化它。
  for(int dev = 0; dev < 32; dev++){
    int bus = 0;
    int func = 0;
    int offset = 0;
    uint32 off = (bus << 16) | (dev << 11) | (func << 8) | (offset);  // 这个偏移量用于访问PCI配置空间中的寄存器。它由总线号、设备号、函数号和寄存器偏移量组成。  
    volatile uint32 *base = ecam + off; // 这个指针用于访问PCI配置空间中的寄存器。它指向ecam加上偏移量的位置。
    uint32 id = base[0];    // 这个寄存器包含了设备的厂商ID和设备ID。我们可以通过检查这个寄存器的值来判断是否找到了e1000网卡。
    
    // 100e:8086 is an e1000
    // 这个值是e1000网卡的厂商ID和设备ID的组合。厂商ID是8086，设备ID是100e。
    if(id == 0x100e8086){
      // command and status register.
      // bit 0 : I/O access enable
      // bit 1 : memory access enable
      // bit 2 : enable mastering
      base[1] = 7;  // 这个寄存器用于控制设备的访问权限。我们需要将它设置为7，来启用I/O访问、内存访问和总线主控。
      __sync_synchronize();
      // 这个循环用于确定e1000网卡的寄存器映射地址。我们通过向BAR寄存器写入0xffffffff来获取它的大小，然后再写回原来的值。
      // 最后，我们将BAR寄存器设置为0x40000000，告诉e1000网卡它的寄存器映射在这个地址。
      for(int i = 0; i < 6; i++){
        uint32 old = base[4+i];

        // writing all 1's to the BAR causes it to be
        // replaced with its size.
        // 这个寄存器用于指定设备的基地址寄存器（BAR）。我们通过写入0xffffffff来获取它的大小，然后再写回原来的值。
        base[4+i] = 0xffffffff;
        __sync_synchronize();

        base[4+i] = old;
      }

      // tell the e1000 to reveal its registers at
      // physical address 0x40000000.
      // 这个寄存器用于告诉e1000网卡它的寄存器映射在物理地址0x40000000。
      base[4+0] = e1000_regs;

      e1000_init((uint32*)e1000_regs);
    }
  }
}
