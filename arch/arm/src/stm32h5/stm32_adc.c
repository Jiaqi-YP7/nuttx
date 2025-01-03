/****************************************************************************
 * arch/arm/src/stm32h5/stm32_adc.c
 *
 * SPDX-License-Identifier: Apache-2.0
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

#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <assert.h>
#include <debug.h>
#include <string.h>

#include <arch/board/board.h>
#include <nuttx/nuttx.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/power/pm.h>

#include "chip.h"
#include "stm32_adc.h"
#include "stm32_rcc.h"

#ifdef CONFIG_ADC

#if defined(CONFIG_STM32H5_ADC1) || defined(CONFIG_STM32H5_ADC2)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_PM
#pragma message "Power Management not implemented in H5 ADC driver. "
#endif

/* ADC Channels/DMA *********************************************************/

#define ADC_MAX_CHANNELS_DMA   20
#define ADC_MAX_CHANNELS_NODMA 20

#ifdef ADC_HAVE_DMA
#  error "STM32H5 ADC does not have DMA support."
#  undef ADC_HAVE_DMA
#endif

#ifdef ADC_HAVE_DMA
#  define ADC_MAX_SAMPLES ADC_MAX_CHANNELS_DMA
#else
#  define ADC_MAX_SAMPLES ADC_MAX_CHANNELS_NODMA
#endif

#define ADC_SMPR_DEFAULT    ADC_SMPR_640p5
#define ADC_SMPR1_DEFAULT   ((ADC_SMPR_DEFAULT << ADC_SMPR1_SMP0_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR1_SMP1_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR1_SMP2_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR1_SMP3_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR1_SMP4_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR1_SMP5_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR1_SMP6_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR1_SMP7_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR1_SMP8_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR1_SMP9_SHIFT))
#define ADC_SMPR2_DEFAULT   ((ADC_SMPR_DEFAULT << ADC_SMPR2_SMP10_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR2_SMP11_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR2_SMP12_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR2_SMP13_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR2_SMP14_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR2_SMP15_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR2_SMP16_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR2_SMP17_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR2_SMP18_SHIFT) | \
                             (ADC_SMPR_DEFAULT << ADC_SMPR2_SMP19_SHIFT))

#define ADC_EXTERNAL_CHAN_MAX  18

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure describes the state of one ADC block */

struct stm32_dev_s
{
  const struct adc_callback_s *cb;
  uint8_t irq;          /* Interrupt generated by this ADC block */
  uint8_t nchannels;    /* Number of channels */
  uint8_t cchannels;    /* Number of configured channels */
  uint8_t intf;         /* ADC interface number */
  uint8_t current;      /* Current ADC channel being converted */
#ifdef ADC_HAVE_DMA
  uint8_t dmachan;      /* DMA channel needed by this ADC */
  bool    hasdma;       /* True: This ADC supports DMA */
#endif
#ifdef ADC_HAVE_TIMER
  uint8_t trigger;      /* Timer trigger channel: 0=CC1, 1=CC2, 2=CC3,
                         * 3=CC4, 4=TRGO, 5=TRGO2 */
#endif
  xcpt_t   isr;         /* Interrupt handler for this ADC block */
  uint32_t base;        /* Base address of registers unique to this ADC
                         * block */
  uint32_t mbase;       /* Base address of master ADC (allows for access to
                         * shared common registers) */
  bool     initialized; /* Keeps track of the initialization status of the ADC */
#ifdef ADC_HAVE_TIMER
  uint32_t tbase;       /* Base address of timer used by this ADC block */
  uint32_t trcc_enr;    /* RCC ENR Register */
  uint32_t trcc_en;     /* RCC EN Bit in ENR Register */
  uint32_t extsel;      /* EXTSEL value used by this ADC block */
  uint32_t pclck;       /* The PCLK frequency that drives this timer */
  uint32_t freq;        /* The desired frequency of conversions */
#endif

#ifdef CONFIG_PM
  struct pm_callback_s pm_callback;
#endif

#ifdef ADC_HAVE_DMA
  DMA_HANDLE dma;       /* Allocated DMA channel */

  /* DMA transfer buffer */

  uint16_t dmabuffer[ADC_MAX_SAMPLES];
#endif

  /* List of selected ADC channels to sample */

  uint8_t  chanlist[ADC_MAX_SAMPLES];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* ADC Register access */

static uint32_t adc_getreg(struct stm32_dev_s *priv, int offset);
static void adc_putreg(struct stm32_dev_s *priv, int offset, uint32_t value);
static void adc_modifyreg(struct stm32_dev_s *priv, int offset,
                              uint32_t clrbits, uint32_t setbits);

/* ADC Miscellaneous Helpers */

static void adc_rccreset(struct stm32_dev_s *priv, bool reset);
static void adc_setupclock(struct stm32_dev_s *priv);
static void adc_enable(struct stm32_dev_s *priv);
static uint32_t adc_sqrbits(struct stm32_dev_s *priv, int first,
                            int last, int offset);
static int  adc_set_ch(struct adc_dev_s *dev, uint8_t ch);
static bool adc_internal(struct stm32_dev_s * priv, uint32_t *adc_ccr);
static void adc_startconv(struct stm32_dev_s *priv, bool enable);
static void adc_wdog_enable(struct stm32_dev_s *priv);

/* ADC Interrupt Handler */

static int adc_interrupt(struct adc_dev_s *dev, uint32_t regval);
static int adc12_interrupt(int irq, void *context, void *arg);

/* ADC Driver Methods */

static int  adc_bind(struct adc_dev_s *dev,
                     const struct adc_callback_s *callback);
static void adc_reset(struct adc_dev_s *dev);
static int  adc_setup(struct adc_dev_s *dev);
static void adc_shutdown(struct adc_dev_s *dev);
static void adc_rxint(struct adc_dev_s *dev, bool enable);
static int  adc_ioctl(struct adc_dev_s *dev, int cmd, unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* ADC interface operations */

static const struct adc_ops_s g_adcops =
{
  .ao_bind      = adc_bind,
  .ao_reset     = adc_reset,
  .ao_setup     = adc_setup,
  .ao_shutdown  = adc_shutdown,
  .ao_rxint     = adc_rxint,
  .ao_ioctl     = adc_ioctl,
};

/* ADC1 state */

#ifdef CONFIG_STM32H5_ADC1
static struct stm32_dev_s g_adcpriv1 =
{
  .irq         = STM32_IRQ_ADC1,
  .isr         = adc12_interrupt,
  .intf        = 1,
  .base        = STM32_ADC1_BASE,
  .mbase       = STM32_ADC1_BASE,
  .initialized = false,
};

static struct adc_dev_s g_adcdev1 =
{
  .ad_ops       = &g_adcops,
  .ad_priv      = &g_adcpriv1,
};
#endif

/* ADC2 state */

#ifdef CONFIG_STM32H5_ADC2
static struct stm32_dev_s g_adcpriv2 =
{
  .irq         = STM32_IRQ_ADC2,
  .isr         = adc12_interrupt,
  .intf        = 2,
  .base        = STM32_ADC2_BASE,
  .mbase       = STM32_ADC2_BASE,
  .initialized = false,
};

static struct adc_dev_s g_adcdev2 =
{
  .ad_ops       = &g_adcops,
  .ad_priv      = &g_adcpriv2,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: adc_getreg
 *
 * Description:
 *   Read the value of an ADC register.
 *
 * Input Parameters:
 *   priv   - A reference to the ADC block status
 *   offset - The offset to the register to read
 *
 * Returned Value:
 *   The current contents of the specified register
 *
 ****************************************************************************/

static uint32_t adc_getreg(struct stm32_dev_s *priv, int offset)
{
  return getreg32(priv->base + offset);
}

/****************************************************************************
 * Name: adc_putreg
 *
 * Description:
 *   Write a value to an ADC register.
 *
 * Input Parameters:
 *   priv   - A reference to the ADC block status
 *   offset - The offset to the register to write to
 *   value  - The value to write to the register
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void adc_putreg(struct stm32_dev_s *priv, int offset,
                       uint32_t value)
{
  putreg32(value, priv->base + offset);
}

/****************************************************************************
 * Name: adc_modifyreg
 *
 * Description:
 *   Modify the value of an ADC register (not atomic).
 *
 * Input Parameters:
 *   priv    - A reference to the ADC block status
 *   offset  - The offset to the register to modify
 *   clrbits - The bits to clear
 *   setbits - The bits to set
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void adc_modifyreg(struct stm32_dev_s *priv, int offset,
                          uint32_t clrbits, uint32_t setbits)
{
  adc_putreg(priv, offset, (adc_getreg(priv, offset) & ~clrbits) | setbits);
}

/****************************************************************************
 * Name: adc_getregm
 *
 * Description:
 *   Read the value of an ADC register from the associated ADC master.
 *
 * Input Parameters:
 *   priv   - A reference to the ADC block status
 *   offset - The offset to the register to read
 *
 * Returned Value:
 *   The current contents of the specified register in the ADC master.
 *
 ****************************************************************************/

static uint32_t adc_getregm(struct stm32_dev_s *priv, int offset)
{
  return getreg32(priv->mbase + offset);
}

/****************************************************************************
 * Name: adc_putregm
 *
 * Description:
 *   Write a value to an ADC register in the associated ADC master.
 *
 * Input Parameters:
 *   priv   - A reference to the ADC block status
 *   offset - The offset to the register to write to
 *   value  - The value to write to the register
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void adc_putregm(struct stm32_dev_s *priv, int offset,
                        uint32_t value)
{
  putreg32(value, priv->mbase + offset);
}

/****************************************************************************
 * Name: adc_modifyregm
 *
 * Description:
 *   Modify the value of an ADC register in the associated ADC master
 *  (not atomic).
 *
 * Input Parameters:
 *   priv    - A reference to the ADC block status
 *   offset  - The offset to the register to modify
 *   clrbits - The bits to clear
 *   setbits - The bits to set
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void adc_modifyregm(struct stm32_dev_s *priv, int offset,
                           uint32_t clrbits, uint32_t setbits)
{
  adc_putregm(priv, offset,
              (adc_getregm(priv, offset) & ~clrbits) | setbits);
}

/****************************************************************************
 * Name: adc_enable
 *
 * Description:
 *   Enables the specified ADC peripheral.
 *
 * Input Parameters:
 *   priv - A reference to the ADC block status
 *
 * Returned Value:
 *
 ****************************************************************************/

static void adc_enable(struct stm32_dev_s *priv)
{
  uint32_t regval;

  regval = adc_getreg(priv, STM32_ADC_CR_OFFSET);

  /* Exit deep power down mode and enable voltage regulator */

  regval &= ~ADC_CR_DEEPPWD;
  adc_putreg(priv, STM32_ADC_CR_OFFSET, regval);

  regval = adc_getreg(priv, STM32_ADC_CR_OFFSET);
  regval |= ADC_CR_ADVREGEN;
  adc_putreg(priv, STM32_ADC_CR_OFFSET, regval);

  /* Wait for voltage regulator to power up */

  up_udelay(20);

  /* Enable ADC calibration. ADCALDIF == 0 so this is only for
   * single-ended conversions, not for differential ones.
   */

  regval |= ADC_CR_ADCAL;
  adc_putreg(priv, STM32_ADC_CR_OFFSET, regval);

  /* Wait for calibration to complete */

  while (adc_getreg(priv, STM32_ADC_CR_OFFSET) & ADC_CR_ADCAL);

  /* Enable ADC
   * Note: ADEN bit cannot be set during ADCAL=1 and 4 ADC clock cycle
   * after the ADCAL bit is cleared by hardware. If we are using SYSCLK
   * as ADC clock source, this is the same as time taken to execute 4
   * ARM instructions.
   */

  regval  = adc_getreg(priv, STM32_ADC_CR_OFFSET);
  regval |= ADC_CR_ADEN;
  adc_putreg(priv, STM32_ADC_CR_OFFSET, regval);

  /* Wait for hardware to be ready for conversions */

  while (!(adc_getreg(priv, STM32_ADC_ISR_OFFSET) & ADC_INT_ADRDY));

  adc_modifyreg(priv, STM32_ADC_ISR_OFFSET, 0, ADC_INT_ADRDY);
}

/****************************************************************************
 * Name: adc_bind
 *
 * Description:
 *   Bind the upper-half driver callbacks to the lower-half implementation.
 *   This must be called early in order to receive ADC event notifications.
 *
 ****************************************************************************/

static int adc_bind(struct adc_dev_s *dev,
                    const struct adc_callback_s *callback)
{
  struct stm32_dev_s *priv = (struct stm32_dev_s *)dev->ad_priv;

  DEBUGASSERT(priv != NULL);
  priv->cb = callback;
  return OK;
}

/****************************************************************************
 * Name: adc_wdog_enable
 *
 * Description:
 *   Enable analog watchdog 1. Sets continuous and overrun mode. Turns on
 *   AWD1 interrupt and disables end of conversion interrupt.
 ****************************************************************************/

static void adc_wdog_enable(struct stm32_dev_s *priv)
{
  uint32_t regval;

  /* Initialize analog watchdog */

  regval = adc_getreg(priv, STM32_ADC_CFGR_OFFSET);
  regval |= ADC_CFGR_AWD1EN | ADC_CFGR_CONT | ADC_CFGR_OVRMOD;
  adc_putreg(priv, STM32_ADC_CFGR_OFFSET, regval);

  /* Switch to analog watchdog interrupt */

  regval = adc_getreg(priv, STM32_ADC_IER_OFFSET);
  regval |= ADC_INT_AWD1;
  regval &= ~ADC_INT_EOC;
  adc_putreg(priv, STM32_ADC_IER_OFFSET, regval);
}

/****************************************************************************
 * Name: adc_startconv
 *
 * Description:
 *   Start (or stop) the ADC conversion process
 *
 * Input Parameters:
 *   priv - A reference to the ADC block status
 *   enable - True: Start conversion
 *
 * Returned Value:
 *
 ****************************************************************************/

static void adc_startconv(struct stm32_dev_s *priv, bool enable)
{
  uint32_t regval;

  ainfo("enable: %d\n", enable ? 1 : 0);

  regval = adc_getreg(priv, STM32_ADC_CR_OFFSET);
  if (enable)
    {
      /* Start conversion of regular channels */

      regval |= ADC_CR_ADSTART;
    }
  else
    {
      /* Disable the conversion of regular channels */

      regval |= ADC_CR_ADSTP;
    }

  adc_putreg(priv, STM32_ADC_CR_OFFSET, regval);
}

/****************************************************************************
 * Name: adc_rccreset
 *
 * Description:
 *   Deinitializes the ADCx peripheral registers to their default
 *   reset values. It could set all the ADCs configured.
 *
 * Input Parameters:
 *   priv - A reference to the ADC block status
 *   reset - Condition, set or reset
 *
 * Returned Value:
 *
 ****************************************************************************/

static void adc_rccreset(struct stm32_dev_s *priv, bool reset)
{
  irqstate_t flags;
  uint32_t regval;

  /* First must disable interrupts because the AHB2RSTR register is used by
   * several different drivers.
   */

  flags = enter_critical_section();

  /* Set or clear the adc reset bit in the AHB2 reset register */

  regval = getreg32(STM32_RCC_AHB2RSTR);

  if (reset)
    {
      regval |= RCC_AHB2RSTR_ADCRST;
    }
  else
    {
      regval &= ~RCC_AHB2RSTR_ADCRST;
    }

  putreg32(regval, STM32_RCC_AHB2RSTR);
  leave_critical_section(flags);
}

/****************************************************************************
 * Name: adc_shutdown
 *
 * Description:
 *   Disable the ADC.  This method is called when the ADC device is closed.
 *   This method reverses the operation the setup method.
 *
 * Input Parameters:
 *
 * Returned Value:
 *
 ****************************************************************************/

static void adc_shutdown(struct adc_dev_s *dev)
{
  struct stm32_dev_s *priv = (struct stm32_dev_s *)dev->ad_priv;

  /* Stop the ADC */

  adc_startconv(priv, false);

  /* Disable ADC interrupts and detach the ADC interrupt handler */

  up_disable_irq(priv->irq);
  irq_detach(priv->irq);

  /* Disable and reset the ADC module */

  adc_reset(dev);

  priv->initialized = false;
}

/****************************************************************************
 * Name: adc_rxint
 *
 * Description:
 *   Call to enable or disable RX interrupts.
 *
 * Input Parameters:
 *
 * Returned Value:
 *
 ****************************************************************************/

static void adc_rxint(struct adc_dev_s *dev, bool enable)
{
  struct stm32_dev_s *priv = (struct stm32_dev_s *)dev->ad_priv;
  uint32_t regval;

  ainfo("intf: %d enable: %d\n", priv->intf, enable ? 1 : 0);

  regval = adc_getreg(priv, STM32_ADC_IER_OFFSET);
  if (enable)
    {
      /* Enable end of conversion and overrun interrupts */

      regval |= ADC_INT_EOC | ADC_INT_OVR;
    }
  else
    {
      /* Disable all interrupts */

      regval &= ~ADC_INT_MASK;
    }

  adc_putreg(priv, STM32_ADC_IER_OFFSET, regval);
}

/****************************************************************************
 * Name: adc_setupclock
 *
 ****************************************************************************/

static void adc_setupclock(struct stm32_dev_s *priv)
{
  uint32_t max_clock = 75000000;
  uint32_t setbits = 0;

#ifndef STM32_ADC_CLK_FREQUENCY
#error "board.h must define STM32_ADC_CLK_FREQUENCY"
#endif

  if (STM32_ADC_CLK_FREQUENCY <= max_clock)
    {
      setbits = ADC_CCR_PRESC_NOT_DIV;
    }
  else if (STM32_ADC_CLK_FREQUENCY / 2 <= max_clock)
    {
      setbits = ADC_CCR_PRESC_DIV2;
    }
  else if (STM32_ADC_CLK_FREQUENCY / 4 <= max_clock)
    {
      setbits = ADC_CCR_PRESC_DIV4;
    }
  else if (STM32_ADC_CLK_FREQUENCY / 6 <= max_clock)
    {
      setbits = ADC_CCR_PRESC_DIV6;
    }
  else if (STM32_ADC_CLK_FREQUENCY / 8 <= max_clock)
    {
      setbits = ADC_CCR_PRESC_DIV8;
    }
  else if (STM32_ADC_CLK_FREQUENCY / 10 <= max_clock)
    {
      setbits = ADC_CCR_PRESC_DIV10;
    }
  else if (STM32_ADC_CLK_FREQUENCY / 12 <= max_clock)
    {
      setbits = ADC_CCR_PRESC_DIV12;
    }
  else if (STM32_ADC_CLK_FREQUENCY / 16 <= max_clock)
    {
      setbits = ADC_CCR_PRESC_DIV16;
    }
  else if (STM32_ADC_CLK_FREQUENCY / 32 <= max_clock)
    {
      setbits = ADC_CCR_PRESC_DIV32;
    }
  else if (STM32_ADC_CLK_FREQUENCY / 64 <= max_clock)
    {
      setbits = ADC_CCR_PRESC_DIV64;
    }
  else if (STM32_ADC_CLK_FREQUENCY / 128 <= max_clock)
    {
      setbits = ADC_CCR_PRESC_DIV128;
    }
  else if (STM32_ADC_CLK_FREQUENCY / 256 <= max_clock)
    {
      setbits = ADC_CCR_PRESC_DIV256;
    }
  else
    {
      aerr("ERROR: source clock too high\n");
    }

  adc_modifyreg(priv, STM32_ADC_CCR_OFFSET, ADC_CCR_PRESC_MASK, setbits);
}

/****************************************************************************
 * Name: adc_reset
 *
 * Description:
 *   Reset the ADC device.  Called early to initialize the hardware. This
 *   is called, before adc_setup() and on error conditions.
 *
 * Input Parameters:
 *
 * Returned Value:
 *
 ****************************************************************************/

static void adc_reset(struct adc_dev_s *dev)
{
  struct stm32_dev_s *priv = (struct stm32_dev_s *)dev->ad_priv;

  ainfo("intf: ADC%d\n", priv->intf);

  /* Enable ADC reset state */

  adc_rccreset(priv, true);

  /* Release ADC from reset state */

  adc_rccreset(priv, false);
}

/****************************************************************************
 * Name: adc_setup
 *
 * Description:
 *   Configure the ADC. This method is called the first time that the ADC
 *   device is opened.  This will occur when the port is first opened.
 *   This setup includes configuring and attaching ADC interrupts.
 *   Interrupts are all disabled upon return.
 *
 * Input Parameters:
 *
 * Returned Value:
 *
 ****************************************************************************/

static int adc_setup(struct adc_dev_s *dev)
{
  struct stm32_dev_s *priv = (struct stm32_dev_s *)dev->ad_priv;
  int ret;
  irqstate_t flags;
  uint32_t clrbits;
  uint32_t setbits;

  /* Attach the ADC interrupt */

  ret = irq_attach(priv->irq, priv->isr, NULL);
  if (ret < 0)
    {
      ainfo("irq_attach failed: %d\n", ret);
      return ret;
    }

  flags = enter_critical_section();

  /* Make sure that the ADC device is in the powered up, reset state.
   * Since reset is shared between ADC1 and ADC2, don't reset one if the
   * other has already been reset. (We only need to worry about this if both
   * ADC1 and ADC2 are enabled.)
   */

#if defined(CONFIG_STM32H5_ADC1) && defined(CONFIG_STM32H5_ADC2)
  if ((dev == &g_adcdev1 &&
      !((struct stm32_dev_s *)g_adcdev2.ad_priv)->initialized) ||
     (dev == &g_adcdev2 &&
      !((struct stm32_dev_s *)g_adcdev1.ad_priv)->initialized))
#endif
    {
      adc_reset(dev);
    }

  /* Initialize the same sample time for each ADC.
   * During sample cycles channel selection bits must remain unchanged.
   */

  adc_putreg(priv, STM32_ADC_SMPR1_OFFSET, ADC_SMPR1_DEFAULT);
  adc_putreg(priv, STM32_ADC_SMPR2_OFFSET, ADC_SMPR2_DEFAULT);

  /* Set the resolution of the conversion. */

  clrbits = ADC_CFGR_RES_MASK | ADC_CFGR_DMACFG | ADC_CFGR_DMAEN;
  setbits = ADC_CFGR_RES_12BIT;

#ifdef ADC_HAVE_DMA
  if (priv->hasdma)
    {
      /* Enable One shot DMA */

      setbits |= ADC_CFGR_DMAEN;
    }
#endif

  /* Disable continuous mode */

  clrbits |= ADC_CFGR_CONT;

  /* Disable external trigger for regular channels */

  clrbits |= ADC_CFGR_EXTEN_MASK;
  setbits |= ADC_CFGR_EXTEN_NONE;

  /* Set overrun mode to preserve the data register */

  clrbits |= ADC_CFGR_OVRMOD;

  /* Set CFGR configuration */

  adc_modifyreg(priv, STM32_ADC_CFGR_OFFSET, clrbits, setbits);

  /* Set CFGR2 configuration to align right no oversample */

  clrbits = ADC_CFGR2_ROVSE | ADC_CFGR2_JOVSE | ADC_CFGR2_OVSS_MASK \
          | ADC_CFGR2_OVSR_MASK;
  setbits = 0;

  adc_modifyreg(priv, STM32_ADC_CFGR2_OFFSET, clrbits, setbits);

  /* Configuration of the channel conversions */

  adc_set_ch(dev, 0);

  /* ADC CCR configuration */

  clrbits = ADC_CCR_PRESC_MASK | ADC_CCR_VREFEN |
            ADC_CCR_TSEN | ADC_CCR_VBATEN;
  setbits = ADC_CCR_CKMODE_ASYCH;

  adc_internal(priv, &setbits);

  adc_modifyregm(priv, STM32_ADC_CCR_OFFSET, clrbits, setbits);

  adc_setupclock(priv);

#ifdef ADC_HAVE_DMA

  /* Enable DMA */

  if (priv->hasdma)
    {
      /* Stop and free DMA if it was started before */

      if (priv->dma != NULL)
        {
          stm32_dmastop(priv->dma);
          stm32_dmafree(priv->dma);
        }

      priv->dma = stm32_dmachannel(priv->dmachan);

      stm32_dmasetup(priv->dma,
                       priv->base + STM32_ADC_DR_OFFSET,
                       (uint32_t)priv->dmabuffer,
                       priv->nchannels,
                       ADC_DMA_CONTROL_WORD);

      stm32_dmastart(priv->dma, adc_dmaconvcallback, dev, false);
    }

#endif

  /* Set ADEN to wake up the ADC from Power Down. */

  adc_enable(priv);

#ifdef ADC_HAVE_TIMER
  if (priv->tbase != 0)
    {
      ret = adc_timinit(priv);
      if (ret < 0)
        {
          aerr("ERROR: adc_timinit failed: %d\n", ret);
        }

      adc_startconv(priv, ret < 0 ? false : true);
    }
#endif

  leave_critical_section(flags);

  ainfo("ISR:   0x%08" PRIx32 " CR:    0x%08" PRIx32 " "
        "CFGR:  0x%08" PRIx32 " CFGR2: 0x%08" PRIx32 "\n",
        adc_getreg(priv, STM32_ADC_ISR_OFFSET),
        adc_getreg(priv, STM32_ADC_CR_OFFSET),
        adc_getreg(priv, STM32_ADC_CFGR_OFFSET),
        adc_getreg(priv, STM32_ADC_CFGR2_OFFSET));
  ainfo("SQR1:  0x%08" PRIx32 " SQR2:  0x%08" PRIx32 " "
        "SQR3:  0x%08" PRIx32 " SQR4:  0x%08" PRIx32 "\n",
        adc_getreg(priv, STM32_ADC_SQR1_OFFSET),
        adc_getreg(priv, STM32_ADC_SQR2_OFFSET),
        adc_getreg(priv, STM32_ADC_SQR3_OFFSET),
        adc_getreg(priv, STM32_ADC_SQR4_OFFSET));
  ainfo("CCR:   0x%08" PRIx32 "\n", adc_getregm(priv, STM32_ADC_CCR_OFFSET));

  /* Enable the ADC interrupt */

  ainfo("Enable the ADC interrupt: irq=%d\n", priv->irq);
  up_enable_irq(priv->irq);

  priv->initialized = true;

  return ret;
}

/****************************************************************************
 * Name: adc_sqrbits
 ****************************************************************************/

static uint32_t adc_sqrbits(struct stm32_dev_s *priv, int first,
                            int last, int offset)
{
  uint32_t bits = 0;
  int i;

  for (i = first - 1;
       i < priv->nchannels && i < last;
       i++, offset += ADC_SQ_OFFSET)
    {
      bits |= (uint32_t)priv->chanlist[i] << offset;
    }

  return bits;
}

/****************************************************************************
 * Name: adc_internal
 ****************************************************************************/

static bool adc_internal(struct stm32_dev_s * priv, uint32_t *adc_ccr)
{
  int i;
  bool internal = false;

  if (priv->intf == 3)
    {
      for (i = 0; i < priv->nchannels; i++)
        {
          if (priv->chanlist[i] > ADC_EXTERNAL_CHAN_MAX)
            {
              internal = true;
              switch (priv->chanlist[i])
                {
                  case 17:
                    *adc_ccr |= ADC_CCR_VBATEN;
                    break;

                  case 18:
                    *adc_ccr |= ADC_CCR_TSEN;
                    break;

                  case 19:
                     *adc_ccr |= ADC_CCR_VREFEN;
                    break;
                }
            }
        }
    }

  return internal;
}

/****************************************************************************
 * Name: adc_set_ch
 *
 * Description:
 *   Sets the ADC channel.
 *
 * Input Parameters:
 *   dev - pointer to device structure used by the driver
 *   ch  - ADC channel number + 1. 0 reserved for all configured channels
 *
 * Returned Value:
 *   int - errno
 *
 ****************************************************************************/

static int adc_set_ch(struct adc_dev_s *dev, uint8_t ch)
{
  struct stm32_dev_s *priv = (struct stm32_dev_s *)dev->ad_priv;
  uint32_t bits;
  int i;

  if (ch == 0)
    {
      priv->current   = 0;
      priv->nchannels = priv->cchannels;
    }
  else
    {
      for (i = 0; i < priv->cchannels && priv->chanlist[i] != ch - 1; i++);

      if (i >= priv->cchannels)
        {
          return -ENODEV;
        }

      priv->current   = i;
      priv->nchannels = 1;
    }

  DEBUGASSERT(priv->nchannels <= ADC_MAX_SAMPLES);

  bits = adc_sqrbits(priv, ADC_SQR4_FIRST, ADC_SQR4_LAST,
                     ADC_SQR4_SQ_OFFSET);
  adc_modifyreg(priv, STM32_ADC_SQR4_OFFSET, ~ADC_SQR4_RESERVED, bits);

  bits = adc_sqrbits(priv, ADC_SQR3_FIRST, ADC_SQR3_LAST,
                     ADC_SQR3_SQ_OFFSET);
  adc_modifyreg(priv, STM32_ADC_SQR3_OFFSET, ~ADC_SQR3_RESERVED, bits);

  bits = adc_sqrbits(priv, ADC_SQR2_FIRST, ADC_SQR2_LAST,
                     ADC_SQR2_SQ_OFFSET);
  adc_modifyreg(priv, STM32_ADC_SQR2_OFFSET, ~ADC_SQR2_RESERVED, bits);

  bits = ((uint32_t)priv->nchannels - 1) << ADC_SQR1_L_SHIFT |
         adc_sqrbits(priv, ADC_SQR1_FIRST, ADC_SQR1_LAST,
                     ADC_SQR1_SQ_OFFSET);
  adc_modifyreg(priv, STM32_ADC_SQR1_OFFSET, ~ADC_SQR1_RESERVED, bits);

  return OK;
}

/****************************************************************************
 * Name: adc_ioctl
 *
 * Description:
 *   All ioctl calls will be routed through this method.
 *
 * Input Parameters:
 *   dev - pointer to device structure used by the driver
 *   cmd - command
 *   arg - arguments passed with command
 *
 * Returned Value:
 *
 ****************************************************************************/

static int adc_ioctl(struct adc_dev_s *dev, int cmd, unsigned long arg)
{
  struct stm32_dev_s *priv = (struct stm32_dev_s *)dev->ad_priv;
  uint32_t regval;
  uint32_t tmp;
  int ret = OK;

  switch (cmd)
    {
      case ANIOC_TRIGGER:
        {
          adc_startconv(priv, true);
        }
        break;

      case ANIOC_GET_NCHANNELS:
        {
          /* Return the number of configured channels */

          ret = priv->cchannels;
        }
        break;

      case ANIOC_WDOG_UPPER: /* Set watchdog upper threshold */
        {
          regval = adc_getreg(priv, STM32_ADC_TR1_OFFSET);

          /* Verify new upper threshold greater than lower threshold */

          tmp = (regval & ADC_TR1_LT1_MASK) >> ADC_TR1_LT1_SHIFT;
          if (arg < tmp)
            {
              ret = -EINVAL;
              break;
            }

          /* Set the watchdog threshold register */

          regval = ((arg << ADC_TR1_HT1_SHIFT) & ADC_TR1_HT1_MASK);
          adc_putreg(priv, STM32_ADC_TR1_OFFSET, regval);

          /* Ensure analog watchdog is enabled */

          adc_wdog_enable(priv);
        }
        break;

      case ANIOC_WDOG_LOWER: /* Set watchdog lower threshold */
        {
          regval = adc_getreg(priv, STM32_ADC_TR1_OFFSET);

          /* Verify new lower threshold less than upper threshold */

          tmp = (regval & ADC_TR1_HT1_MASK) >> ADC_TR1_HT1_SHIFT;
          if (arg > tmp)
            {
              ret = -EINVAL;
              break;
            }

          /* Set the watchdog threshold register */

          regval = ((arg << ADC_TR1_LT1_SHIFT) & ADC_TR1_LT1_MASK);
          adc_putreg(priv, STM32_ADC_TR1_OFFSET, regval);

          /* Ensure analog watchdog is enabled */

          adc_wdog_enable(priv);
        }
        break;

      default:
        aerr("ERROR: Unknown cmd: %d\n", cmd);
        ret = -ENOTTY;
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: adc_interrupt
 *
 * Description:
 *   Common ADC interrupt handler.
 *
 * Input Parameters:
 *
 * Returned Value:
 *
 ****************************************************************************/

static int adc_interrupt(struct adc_dev_s *dev, uint32_t adcisr)
{
  struct stm32_dev_s *priv = (struct stm32_dev_s *)dev->ad_priv;
  int32_t value;

  /* Identifies the AWD interrupt */

  if ((adcisr & ADC_INT_AWD1) != 0)
    {
      value  = adc_getreg(priv, STM32_ADC_DR_OFFSET);
      value &= ADC_DR_MASK;

      awarn("WARNING: Analog Watchdog, Value (0x%03" PRIx32 ") "
            "out of range!\n", value);

      /* Stop ADC conversions to avoid continuous interrupts */

      adc_startconv(priv, false);

      /* Clear the interrupt. This register only accepts write 1's so its
       * safe to only set the 1 bit without regard for the rest of the
       * register
       */

      adc_putreg(priv, STM32_ADC_ISR_OFFSET, ADC_INT_AWD1);
    }

  /* OVR: Overrun */

  if ((adcisr & ADC_INT_OVR) != 0)
    {
      /* In case of a missed ISR - due to interrupt saturation -
       * the upper half needs to be informed to terminate properly.
       */

      awarn("WARNING: Overrun has occurred!\n");

      /* To make use of already sampled data the conversion needs to be
       * stopped first before reading out the data register.
       */

      adc_startconv(priv, false);

      while ((adc_getreg(priv, STM32_ADC_CR_OFFSET) & ADC_CR_ADSTART) != 0);

      /* Verify that the upper-half driver has bound its callback functions */

      if ((priv->cb != NULL) && (priv->cb->au_reset != NULL))
        {
          /* Notify upper-half driver about the overrun */

          priv->cb->au_reset(dev);
        }

      /* Clear the interrupt. This register only accepts write 1's so its
       * safe to only set the 1 bit without regard for the rest of the
       * register
       */

      adc_putreg(priv, STM32_ADC_ISR_OFFSET, ADC_INT_OVR);
    }

  /* EOC: End of conversion */

  if ((adcisr & ADC_INT_EOC) != 0)
    {
      /* Read from the ADC_DR register until 8 stage FIFO is empty.
       * The FIFO is first mentioned in STM32H7 Reference Manual
       * rev. 7, though, not yet indicated in the block diagram!
       */

      do
        {
          /* Read the converted value and clear EOC bit
           * (It is cleared by reading the ADC_DR)
           */

          value  = adc_getreg(priv, STM32_ADC_DR_OFFSET);
          value &= ADC_DR_MASK;

          /* Verify that the upper-half driver has bound its
           * callback functions
           */

          if (priv->cb != NULL)
            {
              /* Hand the ADC data to the ADC driver.  The ADC receive()
               * method accepts 3 parameters:
               *
               * 1) The first is the ADC device instance for this ADC block.
               * 2) The second is the channel number for the data, and
               * 3) The third is the converted data for the channel.
               */

              DEBUGASSERT(priv->cb->au_receive != NULL);
              priv->cb->au_receive(dev, priv->chanlist[priv->current],
                                   value);
            }

          /* Set the channel number of the next channel that will
           * complete conversion
           */

          priv->current++;

          if (priv->current >= priv->nchannels)
            {
              /* Restart the conversion sequence from the beginning */

              priv->current = 0;
            }
        }
      while ((adc_getreg(priv, STM32_ADC_ISR_OFFSET) & ADC_INT_EOC) != 0);

      /* We dont't add EOC to the bits to clear. It will cause a race
       * condition.  EOC should only be cleared by reading the ADC_DR
       */
    }

  return OK;
}

/****************************************************************************
 * Name: adc12_interrupt
 *
 * Description:
 *   ADC1/2 interrupt handler
 *
 * Input Parameters:
 *
 * Returned Value:
 *
 ****************************************************************************/

#if defined(CONFIG_STM32H5_ADC1) || defined(CONFIG_STM32H5_ADC2)
static int adc12_interrupt(int irq, void *context, void *arg)
{
  uint32_t regval;
  uint32_t pending;

#ifdef CONFIG_STM32H5_ADC1
  regval  = getreg32(STM32_ADC1_ISR);
  pending = regval & ADC_INT_MASK;
  if (pending != 0)
    {
      adc_interrupt(&g_adcdev1, regval);
    }
#endif

#ifdef CONFIG_STM32H5_ADC2
  regval  = getreg32(STM32_ADC2_ISR);
  pending = regval & ADC_INT_MASK;
  if (pending != 0)
    {
      adc_interrupt(&g_adcdev2, regval);
    }
#endif

  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stm32h5_adc_initialize
 */

struct adc_dev_s *stm32h5_adc_initialize(int intf,
                                         const uint8_t *chanlist,
                                         int cchannels)
{
  struct adc_dev_s *dev;
  struct stm32_dev_s *priv;

  ainfo("intf: %d cchannels: %d\n", intf, cchannels);

  switch (intf)
    {
#ifdef CONFIG_STM32H5_ADC1
      case 1:
        ainfo("ADC1 selected\n");
        dev = &g_adcdev1;
        break;
#endif
#ifdef CONFIG_STM32H5_ADC2
      case 2:
        ainfo("ADC2 selected\n");
        dev = &g_adcdev2;
        break;
#endif
      default:
        aerr("ERROR: No ADC interface defined\n");
        return NULL;
    }

  /* Configure the selected ADC */

  priv = (struct stm32_dev_s *)dev->ad_priv;
  priv->cb = NULL;

  DEBUGASSERT(cchannels <= ADC_MAX_SAMPLES);
  if (cchannels > ADC_MAX_SAMPLES)
    {
      cchannels = ADC_MAX_SAMPLES;
    }

  priv->cchannels = cchannels;
  memcpy(priv->chanlist, chanlist, cchannels);

#ifdef CONFIG_PM
  if (pm_register(&priv->pm_callback) != OK)
    {
      aerr("ADC Power management registration failed\n");
      return NULL;
    }
#endif

  return dev;
}
#endif /* CONFIG_STM32H5_ADC1 || CONFIG_STM32H5_ADC2 */
#endif /* CONFIG_ADC */