/****************************************************************************
 * arch/arm64/src/common/arm64_backtrace.c
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

#include "sched/sched.h"
#include "arm64_internal.h"
#include "arm64_arch.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_FRAME_POINTER)

/****************************************************************************
 * Name: backtrace
 *
 * Description:
 *  backtrace() parsing the return address through frame pointer
 *
 ****************************************************************************/

nosanitize_address
static int backtrace(uintptr_t *base, uintptr_t *limit,
                     uintptr_t *fp, uintptr_t *pc,
                     void **buffer, int size, int *skip)
{
  int i = 0;

  if (pc)
    {
      if ((*skip)-- <= 0)
        {
          buffer[i++] = pc;
        }
    }

  for (; i < size; fp = (uintptr_t *)*fp)
    {
      if (fp > limit || fp < base || *fp == 0)
        {
          break;
        }

      if ((*skip)-- <= 0)
        {
          buffer[i++] = (void *)*(fp + 1);
        }
    }

  return i;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_backtrace
 *
 * Description:
 *  up_backtrace()  returns  a backtrace for the TCB, in the array
 *  pointed to by buffer.  A backtrace is the series of currently active
 *  function calls for the program.  Each item in the array pointed to by
 *  buffer is of type void *, and is the return address from the
 *  corresponding stack frame.  The size argument specifies the maximum
 *  number of addresses that can be stored in buffer.   If  the backtrace is
 *  larger than size, then the addresses corresponding to the size most
 *  recent function calls are returned; to obtain the complete backtrace,
 *  make sure that buffer and size are large enough.
 *
 * Input Parameters:
 *   tcb    - Address of the task's TCB
 *   buffer - Return address from the corresponding stack frame
 *   size   - Maximum number of addresses that can be stored in buffer
 *   skip   - number of addresses to be skipped
 *
 * Returned Value:
 *   up_backtrace() returns the number of addresses returned in buffer
 *
 * Assumptions:
 *   Have to make sure tcb keep safe during function executing, it means
 *   1. Tcb have to be self or not-running.  In SMP case, the running task
 *      PC & SP cannot be backtrace, as whose get from tcb is not the newest.
 *   2. Tcb have to keep not be freed.  In task exiting case, have to
 *      make sure the tcb get from pid and up_backtrace in one critical
 *      section procedure.
 *
 ****************************************************************************/

int up_backtrace(struct tcb_s *tcb,
                 void **buffer, int size, int skip)
{
  struct tcb_s *rtcb = running_task();
  int ret;

  if (size <= 0 || !buffer)
    {
      return 0;
    }

  if (tcb == NULL || tcb == rtcb)
    {
      if (up_interrupt_context())
        {
#if CONFIG_ARCH_INTERRUPTSTACK > 7
          void *istackbase = (void *)up_get_intstackbase(this_cpu());
          ret = backtrace(istackbase,
                          istackbase + INTSTACK_SIZE,
                          (void *)__builtin_frame_address(0),
                          NULL, buffer, size, &skip);
#else
          ret = backtrace(rtcb->stack_base_ptr,
                          rtcb->stack_base_ptr + rtcb->adj_stack_size,
                          (void *)__builtin_frame_address(0),
                          NULL, buffer, size, &skip);
#endif /* CONFIG_ARCH_INTERRUPTSTACK > 7 */
          if (ret < size)
            {
              ret += backtrace(rtcb->stack_base_ptr,
                               rtcb->stack_base_ptr + rtcb->adj_stack_size,
                               running_regs()[REG_X29],
                               running_regs()[REG_ELR],
                               &buffer[ret], size - ret, &skip);
            }
        }
      else
        {
          ret = backtrace(rtcb->stack_base_ptr,
                          rtcb->stack_base_ptr + rtcb->adj_stack_size,
                          (void *)__builtin_frame_address(0),
                          NULL, buffer, size, &skip);
        }
    }
  else
    {
      ret = backtrace(tcb->stack_base_ptr,
                      tcb->stack_base_ptr + tcb->adj_stack_size,
                      running_regs()[REG_X29],
                      running_regs()[REG_ELR],
                      buffer, size, &skip);
    }

  return ret;
}
#endif /* CONFIG_FRAME_POINTER */
