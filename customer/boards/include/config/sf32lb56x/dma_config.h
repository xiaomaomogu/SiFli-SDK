/**
  ******************************************************************************
  * @file   dma_config.h
  * @author Sifli software development team
  ******************************************************************************
*/
/**
 * @attention
 * Copyright (c) 2019 - 2022,  Sifli Technology
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Sifli integrated circuit
 *    in a product or a software update for such product, must reproduce the above
 *    copyright notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Sifli nor the names of its contributors may be used to endorse
 *    or promote products derived from this software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Sifli integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY SIFLI TECHNOLOGY "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL SIFLI TECHNOLOGY OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef __DMA_CONFIG_H__
#define __DMA_CONFIG_H__

#include <rtconfig.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************************DMA1 ***************************************/
#define FLASH1_DMA_REQUEST                     DMA_REQUEST_0
#define FLASH2_DMA_REQUEST                     DMA_REQUEST_1
#define FLASH3_DMA_REQUEST                     DMA_REQUEST_2
#define I2C4_DMA_REQUEST                       DMA_REQUEST_3
#define UART1_TX_DMA_REQUEST                   DMA_REQUEST_4
#define UART1_RX_DMA_REQUEST                   DMA_REQUEST_5
#define UART2_TX_DMA_REQUEST                   DMA_REQUEST_6
#define UART2_RX_DMA_REQUEST                   DMA_REQUEST_7
#define GPTIM1_UPDATE_DMA_REQUEST              DMA_REQUEST_8
#define GPTIM1_TRIGGER_DMA_REQUEST             DMA_REQUEST_9
#define GPTIM1_CC1_DMA_REQUEST                 DMA_REQUEST_10
#define GPTIM1_CC2_DMA_REQUEST                 DMA_REQUEST_11
#define GPTIM1_CC3_DMA_REQUEST                 DMA_REQUEST_12
#define GPTIM1_CC4_DMA_REQUEST                 DMA_REQUEST_13
#define BTIM1_DMA_REQUEST                      DMA_REQUEST_14
#define BTIM2_DMA_REQUEST                      DMA_REQUEST_15
#define ATIM1_UPDATE_DMA_REQUEST               DMA_REQUEST_16
#define ATIM1_TRIGGER_DMA_REQUEST              DMA_REQUEST_17
#define PWMA1_CC1_DMA_REQUEST                  DMA_REQUEST_18//atime1_cc1
#define PWMA1_CC2_DMA_REQUEST                  DMA_REQUEST_19//atime1_cc2
#define PWMA1_CC3_DMA_REQUEST                  DMA_REQUEST_20//atime1_cc3
#define PWMA1_CC4_DMA_REQUEST                  DMA_REQUEST_21//atime1_cc4
#define I2C1_DMA_REQUEST                       DMA_REQUEST_22
#define I2C2_DMA_REQUEST                       DMA_REQUEST_23
#define I2C3_DMA_REQUEST                       DMA_REQUEST_24
#define ATIM1_COM_DMA_REQUEST                  DMA_REQUEST_25
#define UART3_TX_DMA_REQUEST                   DMA_REQUEST_26
#define UART3_RX_DMA_REQUEST                   DMA_REQUEST_27
#define SPI1_TX_DMA_REQUEST                    DMA_REQUEST_28
#define SPI1_RX_DMA_REQUEST                    DMA_REQUEST_29
#define SPI2_TX_DMA_REQUEST                    DMA_REQUEST_30
#define SPI2_RX_DMA_REQUEST                    DMA_REQUEST_31
#define I2S_TX_DMA_REQUEST                     DMA_REQUEST_32
#define I2S_RX_DMA_REQUEST                     DMA_REQUEST_33
#define SCI_TX_DMA_REQUEST                     DMA_REQUEST_34
#define SCI_RX_DMA_REQUEST                     DMA_REQUEST_35
#define PDM1_L_DMA_REQUEST                     DMA_REQUEST_36
#define PDM1_R_DMA_REQUEST                     DMA_REQUEST_37
#define PDM2_L_DMA_REQUEST                     DMA_REQUEST_38
#define PDM2_R_DMA_REQUEST                     DMA_REQUEST_39
#define AUDCODEC_DAC0_DMA_REQUEST              DMA_REQUEST_41
#define AUDCODEC_DAC1_DMA_REQUEST              DMA_REQUEST_42
#define GPTIM2_UPDATE_DMA_REQUEST              DMA_REQUEST_43
#define GPTIM2_TRIGGER_DMA_REQUEST             DMA_REQUEST_44
#define GPTIM2_CC1_DMA_REQUEST                 DMA_REQUEST_45
#define AUDPRC_TX_OUT1_DMA_REQUEST             DMA_REQUEST_46
#define AUDPRC_TX_OUT0_DMA_REQUEST             DMA_REQUEST_47
#define AUDPRC_TX3_DMA_REQUEST                 DMA_REQUEST_48
#define AUDPRC_TX2_DMA_REQUEST                 DMA_REQUEST_49
#define AUDPRC_TX1_DMA_REQUEST                 DMA_REQUEST_50
#define AUDPRC_TX0_DMA_REQUEST                 DMA_REQUEST_51
#define AUDPRC_RX1_DMA_REQUEST                 DMA_REQUEST_52
#define AUDPRC_RX0_DMA_REQUEST                 DMA_REQUEST_53
#define GPTIM2_CC2_DMA_REQUEST                 DMA_REQUEST_54
#define GPTIM2_CC3_DMA_REQUEST                 DMA_REQUEST_55
#define GPTIM2_CC4_DMA_REQUEST                 DMA_REQUEST_56
#define SDMMC2_DMA_REQUEST                     DMA_REQUEST_57




/* DMA1 channel1 */
#define FLASH1_IRQHandler              DMAC1_CH1_IRQHandler
#define FLASH1_DMA_IRQ_PRIO            0
#define FLASH1_DMA_INSTANCE            DMA1_Channel1
#define FLASH1_DMA_IRQ                 DMAC1_CH1_IRQn

#define AUDPRC_TX0_DMA_IRQHandler              DMAC1_CH1_IRQHandler
#define AUDPRC_TX0_DMA_IRQ_PRIO                0
#define AUDPRC_TX0_DMA_INSTANCE                DMA1_Channel1

#define AUDPRC_TX0_DMA_IRQ                     DMAC1_CH1_IRQn

#define AUDPRC_RX1_DMA_IRQHandler              DMAC1_CH1_IRQHandler
#define AUDPRC_RX1_DMA_IRQ_PRIO                0
#define AUDPRC_RX1_DMA_INSTANCE                DMA1_Channel1
#define AUDPRC_RX1_DMA_IRQ                     DMAC1_CH1_IRQn


/* DMA1 channel2 */
#define FLASH2_IRQHandler              DMAC1_CH2_IRQHandler
#define FLASH2_DMA_IRQ_PRIO            0
#define FLASH2_DMA_INSTANCE            DMA1_Channel2
#define FLASH2_DMA_IRQ                 DMAC1_CH2_IRQn

#define AUDPRC_TX1_DMA_IRQHandler              DMAC1_CH2_IRQHandler
#define AUDPRC_TX1_DMA_IRQ_PRIO                0
#define AUDPRC_TX1_DMA_INSTANCE                DMA1_Channel2
#define AUDPRC_TX1_DMA_IRQ                     DMAC1_CH2_IRQn



#define AUDPRC_TX_OUT0_DMA_IRQHandler              DMAC1_CH2_IRQHandler
#define AUDPRC_TX_OUT0_DMA_IRQ_PRIO                0
#define AUDPRC_TX_OUT0_DMA_INSTANCE                DMA1_Channel2
#define AUDPRC_TX_OUT0_DMA_IRQ                     DMAC1_CH2_IRQn

// AUDPRC RX CH0
#define AUDPRC_RX0_DMA_IRQHandler              DMAC1_CH2_IRQHandler
#define AUDPRC_RX0_DMA_IRQ_PRIO                0
#define AUDPRC_RX0_DMA_INSTANCE                DMA1_Channel2
#define AUDPRC_RX0_DMA_IRQ                     DMAC1_CH2_IRQn


/* DMA1 channel3 */
#define FLASH3_IRQHandler              DMAC1_CH3_IRQHandler
#define FLASH3_DMA_IRQ_PRIO            0
#define FLASH3_DMA_INSTANCE            DMA1_Channel3
#define FLASH3_DMA_IRQ                 DMAC1_CH3_IRQn


#define SDMMC2_DMA_IRQHandler          DMAC1_CH3_IRQHandler
#define SDMMC2_DMA_IRQ_PRIO            0
#define SDMMC2_DMA_INSTANCE            DMA1_Channel3
#define SDMMC2_DMA_IRQ                 DMAC1_CH3_IRQn

/* DMA1 channel4  */
//PDM1 L
#define PDM1_L_DMA_IRQHandler           DMAC1_CH4_IRQHandler
#define PDM1_L_DMA_IRQ_PRIO                  0
#define PDM1_L_DMA_INSTANCE             DMA1_Channel4
#define PDM1_L_DMA_IRQ                  DMAC1_CH4_IRQn

#define I2S_TX_DMA_IRQHandler              DMAC1_CH4_IRQHandler
#define I2S_TX_DMA_IRQ_PRIO                     0
#define I2S_TX_DMA_INSTANCE                DMA1_Channel4
#define I2S_TX_DMA_IRQ                     DMAC1_CH4_IRQn



/* DMA1 channel5 */
#define UART1_DMA_TX_IRQHandler          DMAC1_CH5_IRQHandler
#define UART1_TX_DMA_IRQ_PRIO            0
#define UART1_TX_DMA_INSTANCE            DMA1_Channel5
#define UART1_TX_DMA_IRQ                 DMAC1_CH5_IRQn


/*UART 2 RX DMA, shared with UART 1 TX DMA*/
#define UART2_DMA_RX_IRQHandler          DMAC1_CH5_IRQHandler
#define UART2_RX_DMA_IRQ_PRIO            0
#define UART2_RX_DMA_INSTANCE            DMA1_Channel5
#define UART2_RX_DMA_IRQ                 DMAC1_CH5_IRQn

#define AUDPRC_TX3_DMA_IRQHandler              DMAC1_CH5_IRQHandler
#define AUDPRC_TX3_DMA_IRQ_PRIO                0
#define AUDPRC_TX3_DMA_INSTANCE                DMA1_Channel5
#define AUDPRC_TX3_DMA_IRQ                     DMAC1_CH5_IRQn


/* DMA1 channel6 */
#define UART1_DMA_RX_IRQHandler          DMAC1_CH6_IRQHandler
#define UART1_RX_DMA_IRQ_PRIO            0
#define UART1_RX_DMA_INSTANCE            DMA1_Channel6
#define UART1_RX_DMA_IRQ                 DMAC1_CH6_IRQn


/* DMA1 channel7  */
#define UART3_DMA_TX_IRQHandler         DMAC1_CH7_IRQHandler
#define UART3_TX_DMA_IRQ_PRIO           0
#define UART3_TX_DMA_INSTANCE           DMA1_Channel7
#define UART3_TX_DMA_IRQ                DMAC1_CH7_IRQn


/* DMA1 channel7  */
//PDM1 R
#define PDM1_R_DMA_IRQHandler              DMAC1_CH7_IRQHandler
#define PDM1_R_DMA_IRQ_PRIO                     0
#define PDM1_R_DMA_INSTANCE                DMA1_Channel7
#define PDM1_R_DMA_IRQ                     DMAC1_CH7_IRQn


#define I2S_RX_DMA_IRQHandler              DMAC1_CH7_IRQHandler
#define I2S_RX_DMA_IRQ_PRIO                     0
#define I2S_RX_DMA_INSTANCE                DMA1_Channel7
#define I2S_RX_DMA_IRQ                     DMAC1_CH7_IRQn


/* DMA1 channel8  */
#define UART3_DMA_RX_IRQHandler         DMAC1_CH8_IRQHandler
#define UART3_RX_DMA_IRQ_PRIO           0
#define UART3_RX_DMA_INSTANCE           DMA1_Channel8
#define UART3_RX_DMA_IRQ                DMAC1_CH8_IRQn

#define AUDPRC_TX2_DMA_IRQHandler              DMAC1_CH8_IRQHandler
#define AUDPRC_TX2_DMA_IRQ_PRIO                0
#define AUDPRC_TX2_DMA_INSTANCE                DMA1_Channel8
#define AUDPRC_TX2_DMA_IRQ                     DMAC1_CH8_IRQn



/*************************************DMA2 ***************************************/
#define UART4_TX_DMA_REQUEST                   DMA_REQUEST_0
#define UART4_RX_DMA_REQUEST                   DMA_REQUEST_1
#define UART5_TX_DMA_REQUEST                   DMA_REQUEST_2
#define UART5_RX_DMA_REQUEST                   DMA_REQUEST_3
#define UART6_TX_DMA_REQUEST                   DMA_REQUEST_4
#define UART6_RX_DMA_REQUEST                   DMA_REQUEST_5
#define BTIM3_DMA_REQUEST                      DMA_REQUEST_6
#define BTIM4_DMA_REQUEST                      DMA_REQUEST_7
#define GPTIM3_UPDATE_DMA_REQUEST              DMA_REQUEST_8
#define GPTIM3_TRIGGER_DMA_REQUEST             DMA_REQUEST_9
#define GPTIM3_CC1_DMA_REQUEST                 DMA_REQUEST_10
#define GPTIM3_CC2_DMA_REQUEST                 DMA_REQUEST_11
#define GPTIM3_CC3_DMA_REQUEST                 DMA_REQUEST_12
#define GPTIM3_CC4_DMA_REQUEST                 DMA_REQUEST_13
#define PWM4_CC4_DMA_REQUEST                   DMA_REQUEST_13//GTIM3_CH4
#define GPTIM5_UPDATE_DMA_REQUEST              DMA_REQUEST_14
#define GPTIM5_TRIGGER_DMA_REQUEST             DMA_REQUEST_15
#define SPI3_TX_DMA_REQUEST                    DMA_REQUEST_16
#define SPI3_RX_DMA_REQUEST                    DMA_REQUEST_17
#define SPI4_TX_DMA_REQUEST                    DMA_REQUEST_18
#define SPI4_RX_DMA_REQUEST                    DMA_REQUEST_19
#define FLASH5_DMA_REQUEST                     DMA_REQUEST_20
#define I2C5_DMA_REQUEST                       DMA_REQUEST_21
#define I2C6_DMA_REQUEST                       DMA_REQUEST_22
#define I2C7_DMA_REQUEST                       DMA_REQUEST_23
#define GPTIM4_UPDATE_DMA_REQUEST              DMA_REQUEST_24
#define GPTIM4_TRIGGER_DMA_REQUEST             DMA_REQUEST_25
#define PWM5_CC1_DMA_REQUEST                   DMA_REQUEST_26
#define GPTIM4_CC1_DMA_REQUEST                 DMA_REQUEST_26
#define GPTIM4_CC2_DMA_REQUEST                 DMA_REQUEST_27
#define GPTIM4_CC3_DMA_REQUEST                 DMA_REQUEST_28
#define GPTIM4_CC4_DMA_REQUEST                 DMA_REQUEST_29
#define GPADC_DMA_REQUEST                      DMA_REQUEST_30




/* DMA2 channel1  */
#if defined(BSP_UART4_TX_USING_DMA) && !defined(UART4_TX_DMA_INSTANCE)
#define UART4_DMA_TX_IRQHandler         DMAC2_CH1_IRQHandler
#define UART4_TX_DMA_IRQ_PRIO           0
#define UART4_TX_DMA_INSTANCE           DMA2_Channel1
#define UART4_TX_DMA_IRQ                DMAC2_CH1_IRQn
#endif
/* DMA2 channel1  */
#if defined(BSP_PWM4_CC4_USING_DMA) && !defined(PWM4_CC4_DMA_INSTANCE)//GTIM3_CH4
#define PWM4_CC4_DMA_IRQHandler              DMAC2_CH1_IRQHandler
#define PWM4_CC4_DMA_IRQ_PRIO                1
#define PWM4_CC4_DMA_INSTANCE                DMA2_Channel1
#define PWM4_CC4_DMA_IRQ                     DMAC2_CH1_IRQn
#define PWM4_CC4_DMA_PDATAALIGN                       DMA_PDATAALIGN_HALFWORD
#define PWM4_CC4_DMA_MDATAALIGN                       DMA_MDATAALIGN_HALFWORD
#endif//lcd
#if defined(BSP_PWM5_CC1_USING_DMA) && !defined(PWM5_CC1_DMA_INSTANCE)//GTIM4_CH1
#define PWM5_CC1_DMA_IRQHandler              DMAC2_CH1_IRQHandler
#define PWM5_CC1_DMA_IRQ_PRIO                1
#define PWM5_CC1_DMA_INSTANCE                DMA2_Channel1
#define PWM5_CC1_DMA_IRQ                     DMAC2_CH1_IRQn
#define PWM5_CC1_DMA_PDATAALIGN                       DMA_PDATAALIGN_HALFWORD
#define PWM5_CC1_DMA_MDATAALIGN                       DMA_MDATAALIGN_HALFWORD
#endif//rgbled

/* DMA2 channel2  */
#if defined(BSP_UART4_RX_USING_DMA) && !defined(UART4_RX_DMA_INSTANCE)
#define UART4_DMA_RX_IRQHandler         DMAC2_CH2_IRQHandler
#define UART4_RX_DMA_IRQ_PRIO           0
#define UART4_RX_DMA_INSTANCE           DMA2_Channel2
#define UART4_RX_DMA_IRQ                DMAC2_CH2_IRQn
#endif


/* DMA2 channel3  */
#define SPI3_DMA_RX_IRQHandler         DMAC2_CH3_IRQHandler
#define SPI3_RX_DMA_IRQ_PRIO           0
#define SPI3_RX_DMA_INSTANCE           DMA2_Channel3
#define SPI3_RX_DMA_IRQ                DMAC2_CH3_IRQn

/* DMA2 channel4  */
#define SPI3_DMA_TX_IRQHandler         DMAC2_CH4_IRQHandler
#define SPI3_TX_DMA_IRQ_PRIO           0
#define SPI3_TX_DMA_INSTANCE           DMA2_Channel4
#define SPI3_TX_DMA_IRQ                DMAC2_CH4_IRQn


/* DMA2 channel3 */
#define I2C5_DMA_IRQHandler              DMAC2_CH3_IRQHandler
#define I2C5_DMA_IRQ_PRIO                1
#define I2C5_DMA_INSTANCE                DMA2_Channel3
#define I2C5_DMA_IRQ                     DMAC2_CH3_IRQn


/* DMA2 channel4 */
#define I2C6_DMA_IRQHandler              DMAC2_CH4_IRQHandler
#define I2C6_DMA_IRQ_PRIO                1
#define I2C6_DMA_INSTANCE                DMA2_Channel4
#define I2C6_DMA_IRQ                     DMAC2_CH4_IRQn


/* DMA2 channel5 */
#define UART5_DMA_TX_IRQHandler          DMAC2_CH5_IRQHandler
#define UART5_TX_DMA_IRQ_PRIO            0
#define UART5_TX_DMA_INSTANCE            DMA2_Channel5
#define UART5_TX_DMA_IRQ                 DMAC2_CH5_IRQn


/* DMA2 channel5 */
#define GPADC_DMA_IRQHandler              DMAC2_CH5_IRQHandler
#define GPADC_DMA_IRQ_PRIO                 0
#define GPADC_DMA_INSTANCE            DMA2_Channel5
#define GPADC_DMA_IRQ                 DMAC2_CH5_IRQn


/* DMA2 channel6 */
#define UART5_DMA_RX_IRQHandler          DMAC2_CH6_IRQHandler
#define UART5_RX_DMA_IRQ_PRIO            0
#define UART5_RX_DMA_INSTANCE            DMA2_Channel6
#define UART5_RX_DMA_IRQ                 DMAC2_CH6_IRQn


/* DMA2 channel7 */
#define SPI4_DMA_RX_IRQHandler         DMAC2_CH7_IRQHandler
#define SPI4_RX_DMA_IRQ_PRIO           0
#define SPI4_RX_DMA_INSTANCE           DMA2_Channel7
#define SPI4_RX_DMA_IRQ                DMAC2_CH7_IRQn


/* DMA2 channel8 */
#define I2C7_DMA_IRQHandler              DMAC2_CH8_IRQHandler
#define I2C7_DMA_IRQ_PRIO                1
#define I2C7_DMA_INSTANCE                DMA2_Channel8
#define I2C7_DMA_IRQ                     DMAC2_CH8_IRQn


/* DMA2 channel8 */
#define SPI4_DMA_TX_IRQHandler         DMAC2_CH8_IRQHandler
#define SPI4_TX_DMA_IRQ_PRIO           0
#define SPI4_TX_DMA_INSTANCE           DMA2_Channel8
#define SPI4_TX_DMA_IRQ                DMAC2_CH8_IRQn


/* DMA2 channel7 */
#define UART6_DMA_TX_IRQHandler          DMAC2_CH7_IRQHandler
#define UART6_TX_DMA_IRQ_PRIO            0
#define UART6_TX_DMA_INSTANCE            DMA2_Channel7
#define UART6_TX_DMA_IRQ                 DMAC2_CH7_IRQn


/* DMA2 channel8 */
#define UART6_DMA_RX_IRQHandler          DMAC2_CH8_IRQHandler
#define UART6_RX_DMA_IRQ_PRIO            0
#define UART6_RX_DMA_INSTANCE            DMA2_Channel8
#define UART6_RX_DMA_IRQ                 DMAC2_CH8_IRQn

/* DMA2 channel8 */
#define FLASH5_IRQHandler              DMAC2_CH8_IRQHandler
#define FLASH5_DMA_IRQ_PRIO            0
#define FLASH5_DMA_INSTANCE            DMA2_Channel8
#define FLASH5_DMA_IRQ                 DMAC2_CH8_IRQn

#ifdef __cplusplus
}
#endif

#endif /* __DMA_CONFIG_H__ */
/************************ (C) COPYRIGHT Sifli Technology *******END OF FILE****/
