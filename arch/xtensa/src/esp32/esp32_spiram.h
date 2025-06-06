/****************************************************************************
 * arch/xtensa/src/esp32/esp32_spiram.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#ifndef __ARCH_XTENSA_SRC_ESP32_ESP32_SPIRAM_H
#define __ARCH_XTENSA_SRC_ESP32_ESP32_SPIRAM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xtensa_attr.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ESP_SPIRAM_SIZE_16MBITS   0   /* SPI RAM size is 16 MBits */
#define ESP_SPIRAM_SIZE_32MBITS   1   /* SPI RAM size is 32 MBits */
#define ESP_SPIRAM_SIZE_64MBITS   2   /* SPI RAM size is 64 MBits */
#define ESP_SPIRAM_SIZE_INVALID   3   /* SPI RAM size is invalid  */

/* Errors that can be returned by cache_sram_* */

#define MMU_SET_ADDR_ALIGNED_ERROR 1
#define MMU_SET_PAGE_SIZE_ERROR    3
#define MMU_SET_VADDR_OUT_RANGE    5

#define PROCACHE_MMU_ADDR_BASE     0x3FF10000
#define APPCACHE_MMU_ADDR_BASE     0x3FF12000

/* sram address */

#define PRO_DRAM1_START_ADDR       0x3F800000
#define PRO_DRAM1_END_ADDR(psize)  (PRO_DRAM1_START_ADDR + ((psize) << 17))

/* cache mmu register file address */

#define CACHE_MMU_ADDRESS_BASE(cpu_no) ((cpu_no) ? (APPCACHE_MMU_ADDR_BASE) :\
                                        (PROCACHE_MMU_ADDR_BASE))

/* virtual address, physical address check */

#define ADDRESS_CHECK(addr,psize) (((addr) & (0xFFFF >>((64/(psize))-1))) != 0)

/* CPU number check */

#define CPU_NUMBER_CHECK(cpu_no)  (((cpu_no)<0) || ((cpu_no)>1))

/* PID check */

#define PID_CHECK(pid)  (((pid)<0) || ((pid)>7))

/* flash MMU edge check (flash size default : 16*1024 K) */

#define FLASH_MMU_EDGE_CHECK(mmu_val,num) (((mmu_val) + (num)) > 256)

/* sram MMU edge check (sram size default : 8*1024 K) */

#define SRAM_MMU_EDGE_CHECK(mmu_val,num,psize) (((mmu_val) + (num)) > ((8*1024)/(psize)))

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

/* Description: get SPI RAM size
 * return
 *   - ESP_SPIRAM_SIZE_INVALID if SPI RAM not enabled or not valid
 *   - SPI RAM size
 */

int esp_spiram_get_chip_size(void);

/* Description: Initialize spiram interface/hardware. Normally called from
 *              cpu_start.c.
 *
 * return:
 *   OK on success
 */

int esp_spiram_init(void);

/* Description: Configure Cache/MMU for access to external SPI RAM.
 *
 * Normally this function is called from cpu_start, if
 * CONFIG_ESP32_SPIRAM_BOOT_INIT option is enabled. Applications which need
 * to enable SPI RAM at run time can disable CONFIG_ESP32_SPIRAM_BOOT_INIT,
 * and call this function later.
 *
 * Attention this function must be called with flash cache disabled.
 */

void esp_spiram_init_cache(void);

/* Description: Memory test for SPI RAM. Should be called after SPI RAM is
 *              initialized and (in case of a dual-core system) the app CPU
 *              is online. This test overwrites the memory with crap, so do
 *              not call after e.g. the heap allocator has stored important
 *              stuff in SPI RAM.
 *
 * return:
 *   OK on success, ERROR on failed memory test
 */

int esp_spiram_test(void);

/* Description Add the initialized SPI RAM to the heap allocator. */

int esp_spiram_add_to_heapalloc(void);

/* Description: Get the size of the attached SPI RAM chip selected in
 *              menuconfig
 *
 * return:
 *   Size in bytes, or 0 if no external RAM chip support compiled in.
 */

size_t esp_spiram_get_size(void);

/* Description: Force a writeback of the data in the SPI RAM cache. This is
 * to be called whenever cache is disabled, because disabling cache on the
 * ESP32 discards the data in the SPI RAM cache.
 *
 * This is meant for use from within the SPI flash code.
 */

void esp_spiram_writeback_cache(void);

/****************************************************************************
 * Name: esp_spiram_writeback_range
 *
 * Description:
 *   Writeback the Cache items (also clean the dirty bit) in the region from
 *   DCache. If the region is not in DCache addr room, nothing will be done.
 *
 * Input Parameters:
 *   addr - writeback region start address
 *   size - writeback region size
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void esp_spiram_writeback_range(uint32_t addr, uint32_t size);

/* Description: Reserve a pool of internal memory for specific DMA/internal
 *              allocations.
 *
 * param:
 *   size   Size of reserved pool in bytes
 *
 * return:
 *          - ESP_OK on success
 *          - ESP_ERR_NO_MEM when no memory available for pool
 */

int esp_spiram_reserve_dma_pool(size_t size);

/* Description: If SPI RAM(PSRAM) has been initialized
 *
 * return:
 *   - true SPI RAM has been initialized successfully
 *   - false SPI RAM hasn't been initialized or initialized failed
 */

bool esp_spiram_is_initialized(void);

/* Description: Set Ext-SRAM-Cache mmu mapping.
 *
 * Note that this code lives in IRAM and has a bugfix in respect to the
 * ROM version of this function (which erroneously refused a vaddr > 2MiB
 *
 * param:
 *   int cpu_no : CPU number, 0 for PRO cpu, 1 for APP cpu.
 *   int pod : process identifier. Range 0~7.
 *   unsigned int vaddr : virtual address in CPU address space.
 *                        Can be IRam0, IRam1, IRom0 and DRom0 memory
 *                        address.
 *                        Should be aligned by psize.
 *
 *   unsigned int paddr : physical address in Ext-SRAM.
 *                        Should be aligned by psize.
 *   int psize : page size of flash, in kilobytes. Should be 32 here.
 *   int num : pages to be set.
 *
 *   unsigned int: error status
 *             0 : mmu set success
 *             1 : vaddr or paddr is not aligned
 *             2 : pid error
 *             3 : psize error
 *             4 : mmu table to be written is out of range
 *             5 : vaddr is out of range
 */

unsigned int IRAM_ATTR
  cache_sram_mmu_set(int cpu_no, int pid, unsigned int vaddr,
                     unsigned int paddr, int psize, int num);

#endif /* __ARCH_XTENSA_SRC_ESP32_ESP32_SPIRAM_H */
