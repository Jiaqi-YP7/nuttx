/****************************************************************************
 * arch/arm/src/lc823450/lc823450_idle.c
 *
 * SPDX-License-Identifier: Apache-2.0
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

#include <nuttx/config.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/board.h>
#include <arch/board/board.h>

#include "nvic.h"
#include "arm_internal.h"

#ifdef CONFIG_DVFS
#  include "lc823450_dvfs2.h"
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint32_t g_idle_counter[2];

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_idle
 *
 * Description:
 *   up_idle() is the logic that will be executed when there is no other
 *   ready-to-run task.  This is processor idle time and will continue until
 *   some interrupt occurs to cause a context switch from the idle task.
 *
 *   Processing in this state may be processor-specific. e.g., this is where
 *   power management operations might be performed.
 *
 ****************************************************************************/

void up_idle(void)
{
#if defined(CONFIG_SUPPRESS_INTERRUPTS) || defined(CONFIG_SUPPRESS_TIMER_INTS)
  /* If the system is idle and there are no timer interrupts, then process
   * "fake" timer interrupts. Hopefully, something will wake up.
   */

  nxsched_process_timer();
#else

  /* DVFS and LED control must be done with local interrupts disabled */

  irqstate_t flags;
  flags = up_irq_save();

#ifdef CONFIG_LC823450_SLEEP_MODE
  /* Clear SLEEPDEEP flag */

  uint32_t regval  = getreg32(NVIC_SYSCON);
  regval &= ~NVIC_SYSCON_SLEEPDEEP;
  putreg32(regval, NVIC_SYSCON);
#endif

#ifdef CONFIG_DVFS
  lc823450_dvfs_enter_idle();
#endif

  board_autoled_off(LED_CPU0 + this_cpu());

  up_irq_restore(flags);

  /* Sleep until an interrupt occurs to save power */

  asm("WFI");

  g_idle_counter[this_cpu()]++;

#endif
}
