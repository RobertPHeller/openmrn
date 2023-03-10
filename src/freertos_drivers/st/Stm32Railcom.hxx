/** \copyright
 * Copyright (c) 2014, Balazs Racz
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file Stm32Railcom.hxx
 *
 * Device driver for STM32 chips to read one or more UART inputs for railcom
 * data.
 *
 * @author Balazs Racz
 * @date 6 Mar 2023
 */

#ifndef _FREERTOS_DRIVERS_ST_STM32RAILCOM_HXX_
#define _FREERTOS_DRIVERS_ST_STM32RAILCOM_HXX_

#include "stm32f_hal_conf.hxx"

#include "freertos_drivers/common/RailcomImpl.hxx"


#if defined(STM32F072xB) || defined(STM32F091xC)
#include "stm32f0xx_ll_usart.h"
#elif defined(STM32F103xB)
#include "stm32f1xx_ll_usart.h"
#elif defined(STM32F303xC) || defined(STM32F303xE)
#include "stm32f3xx_ll_usart.h"
#elif defined(STM32L431xx) || defined(STM32L432xx)
#include "stm32l4xx_ll_usart.h"
#elif defined(STM32F767xx)
#include "stm32f7xx_ll_usart.h"
#else
#error Dont know what STM32 chip you have.
#endif

/// This struct helps casting the UART base addresses to the appropriate
/// type. It can be allocated in read-only memory (flash) as a static array. It
/// can be passed to the STM32 HAL macros as well.
struct RailcomUartHandle
{
    union
    {
        /// Initialized by the constants from the processor header like
        /// USART1_BASE.
        uint32_t baseAddress;
        /// Use this to access the registers.
        USART_TypeDef *Instance;
    };
};

/*
struct RailcomHw
{
    static const uint32_t CHANNEL_COUNT = 4;
    static const RailcomUartHandle UART_BASE[CHANNEL_COUNT];
    // Make sure there are enough entries here for all the channels times a few
    // DCC packets.
    static const uint32_t Q_SIZE = 16;

    static const auto OS_INTERRUPT = USART2_IRQn;

    GPIO_HWPIN(CH1, GpioHwPin, C, 4, U4RX);
    GPIO_HWPIN(CH2, GpioHwPin, C, 6, U3RX);
    GPIO_HWPIN(CH3, GpioHwPin, G, 4, U2RX);
    GPIO_HWPIN(CH4, GpioHwPin, E, 0, U7RX);

    static void hw_init() {
         CH1_Pin::hw_init();
         CH2_Pin::hw_init();
         CH3_Pin::hw_init();
         CH4_Pin::hw_init();
    }

    static void set_input() {
        CH1_Pin::set_input();
        CH2_Pin::set_input();
        CH3_Pin::set_input();
        CH4_Pin::set_input();
    }

    static void set_hw() {
        CH1_Pin::set_hw();
        CH2_Pin::set_hw();
        CH3_Pin::set_hw();
        CH4_Pin::set_hw();
    }

    /// @returns a bitmask telling which pins are active. Bit 0 will be set if
    /// channel 0 is active (drawing current).
    static uint8_t sample() {
        uint8_t ret = 0;
        if (!CH1_Pin::get()) ret |= 1;
        if (!CH2_Pin::get()) ret |= 2;
        if (!CH3_Pin::get()) ret |= 4;
        if (!CH4_Pin::get()) ret |= 8;
        return ret;
    }
}; 

// The weak attribute is needed if the definition is put into a header file.
const uint32_t RailcomHw::UART_BASE[] __attribute__((weak)) = {UART4_BASE, UART3_BASE, UART2_BASE, UART7_BASE};
const uint32_t RailcomHw::UART_PERIPH[]
__attribute__((weak)) = {SYSCTL_PERIPH_UART4, SYSCTL_PERIPH_UART3, SYSCTL_PERIPH_UART2, SYSCTL_PERIPH_UART7};

*/
/// Railcom driver for STM32-class microcontrollers using the HAL middleware
/// library.
///
/// This railcom driver supports parallel polling of multiple UART channels for
/// the railcom data. 
template <class HW> class Stm32RailcomDriver : public RailcomDriverBase<HW>
{
public:
    /// Constructor. @param path is the device node path (e.g. "/dev/railcom0").
    Stm32RailcomDriver(const char *path)
        : RailcomDriverBase<HW>(path)
    {
#ifdef GCC_CM3
        SetInterruptPriority(HW::OS_INTERRUPT, configKERNEL_INTERRUPT_PRIORITY);
#else
        SetInterruptPriority(HW::OS_INTERRUPT, 0xff);
#endif        
        HAL_NVIC_EnableIRQ(HW::OS_INTERRUPT);
    }

    ~Stm32RailcomDriver()
    {
        HAL_NVIC_DisableIRQ(HW::OS_INTERRUPT);
    }

private:
    /// True when we are currently within a cutout.
    bool inCutout_ = false;

    using RailcomDriverBase<HW>::returnedPackets_;

    /// Sets a given software interrupt pending.
    /// @param int_nr interrupt number (will be HW::OS_INTERRUPT)
    void int_set_pending(unsigned int_nr) override
    {
        HAL_NVIC_SetPendingIRQ((IRQn_Type)int_nr);
    }

    // File node interface
    void enable() OVERRIDE
    {
        for (unsigned i = 0; i < HW::CHANNEL_COUNT; ++i)
        {
            UART_HandleTypeDef uart_handle;
            memset(&uart_handle, 0, sizeof(uart_handle));
            uart_handle.Init.BaudRate   = 250000;
            uart_handle.Init.WordLength = UART_WORDLENGTH_8B;
            uart_handle.Init.StopBits   = UART_STOPBITS_1;
            uart_handle.Init.Parity     = UART_PARITY_NONE;
            uart_handle.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
            uart_handle.Init.Mode       = UART_MODE_RX;
            uart_handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
            uart_handle.Instance = HW::UART_BASE[i].Instance;
            HAL_UART_DeInit(&uart_handle); 
            volatile auto ret = HAL_UART_Init(&uart_handle);
            HASSERT(HAL_OK == ret);

            // Disables the receiver.
            LL_USART_SetTransferDirection(
                HW::UART_BASE[i].Instance, LL_USART_DIRECTION_NONE);
            LL_USART_Enable(HW::UART_BASE[i].Instance);
        }
    }
    void disable() OVERRIDE
    {
        for (unsigned i = 0; i < ARRAYSIZE(HW::UART_BASE); ++i)
        {
            LL_USART_Disable(HW::UART_BASE[i].Instance);
        }
    }

    // RailcomDriver interface
    void feedback_sample() OVERRIDE {
        HW::enable_measurement(true);
        this->add_sample(HW::sample());
        HW::disable_measurement();
    }

    void start_cutout() OVERRIDE
    {
        HW::enable_measurement(false);
        const bool need_ch1_cutout = HW::need_ch1_cutout() || (this->feedbackKey_ < 11000);
        Debug::RailcomRxActivate::set(true);
        for (unsigned i = 0; i < HW::CHANNEL_COUNT; ++i)
        {
            if (need_ch1_cutout)
            {
                LL_USART_SetTransferDirection(
                    HW::UART_BASE[i].Instance, LL_USART_DIRECTION_RX);
            }
            while (LL_USART_IsActiveFlag_RXNE(HW::UART_BASE[i].Instance)) {
                uint8_t data = HW::UART_BASE[i].Instance->RDR;
                (void) data;
            }
            returnedPackets_[i] = 0;
        }
        Debug::RailcomDriverCutout::set(true);
    }

    void middle_cutout() OVERRIDE
    {
        Debug::RailcomDriverCutout::set(false);
        for (unsigned i = 0; i < HW::CHANNEL_COUNT; ++i)
        {
            while (LL_USART_IsActiveFlag_RXNE(HW::UART_BASE[i].Instance))
            {
                Debug::RailcomDataReceived::toggle();
                Debug::RailcomAnyData::set(true);
                if (!returnedPackets_[i])
                {
                    returnedPackets_[i] = this->alloc_new_packet(i);
                }
                if (!returnedPackets_[i])
                {
                    break;
                }
                uint8_t data = HW::UART_BASE[i].Instance->RDR;
                if (LL_USART_IsActiveFlag_FE(HW::UART_BASE[i].Instance)) {
                    // We reset the receiver circuitry because we got an error
                    // in channel 1. Typical cause of this error is if there
                    // are multiple locomotives on the block (e.g. if this is
                    // the global detector) and they talk over each other
                    // during ch1 broadcast. There is a good likelihood that
                    // the asynchronous receiver is out of sync with the
                    // transmitter, but right now we should be in the
                    // between-byte space.
                    LL_USART_SetTransferDirection(
                        HW::UART_BASE[i].Instance, LL_USART_DIRECTION_NONE);
                    LL_USART_ClearFlag_FE(HW::UART_BASE[i].Instance);
                    Debug::RailcomError::toggle();
                    // Not a valid railcom byte.
                    //returnedPackets_[i]->add_ch1_data(0xF8);
                    continue;
                }
                returnedPackets_[i]->add_ch1_data(data);
            }
            LL_USART_Disable(HW::UART_BASE[i].Instance);
            LL_USART_SetTransferDirection(
                HW::UART_BASE[i].Instance, LL_USART_DIRECTION_RX);
            LL_USART_Enable(HW::UART_BASE[i].Instance);
        }
        HW::middle_cutout_hook();
        Debug::RailcomDriverCutout::set(true);
    }

    void end_cutout() OVERRIDE
    {
        HW::disable_measurement();
        bool have_packets = false;
        for (unsigned i = 0; i < HW::CHANNEL_COUNT; ++i)
        {
            while (LL_USART_IsActiveFlag_RXNE(HW::UART_BASE[i].Instance))
            {
                Debug::RailcomDataReceived::toggle();
                Debug::RailcomAnyData::set(true);
                Debug::RailcomCh2Data::set(true);
                if (!returnedPackets_[i])
                {
                    returnedPackets_[i] = this->alloc_new_packet(i);
                }
                if (!returnedPackets_[i])
                {
                    break;
                }
                uint8_t data = HW::UART_BASE[i].Instance->RDR;
                if (LL_USART_IsActiveFlag_FE(HW::UART_BASE[i].Instance)) {
                    Debug::RailcomError::toggle();
                    LL_USART_ClearFlag_FE(HW::UART_BASE[i].Instance);
                    //returnedPackets_[i]->add_ch2_data(0xF8);
                    continue;
                }
                if (data == 0xE0) {
                    Debug::RailcomE0::toggle();
                }
                returnedPackets_[i]->add_ch2_data(data);
            }
            LL_USART_SetTransferDirection(
                HW::UART_BASE[i].Instance, LL_USART_DIRECTION_NONE);
            Debug::RailcomRxActivate::set(false);
            if (returnedPackets_[i]) {
                have_packets = true;
                this->feedbackQueue_.commit_back();
                Debug::RailcomPackets::toggle();
                returnedPackets_[i] = nullptr;
                HAL_NVIC_SetPendingIRQ(HW::OS_INTERRUPT);
            }
        }
        if (!have_packets)
        {
            // Ensures that at least one feedback packet is sent back even when
            // it is with no railcom payload.
            auto *p = this->alloc_new_packet(0);
            if (p)
            {
                this->feedbackQueue_.commit_back();
                Debug::RailcomPackets::toggle();
                HAL_NVIC_SetPendingIRQ(HW::OS_INTERRUPT);
            }
        }
        Debug::RailcomCh2Data::set(false);
        Debug::RailcomDriverCutout::set(false);
    }

    void no_cutout() OVERRIDE
    {
        // Ensures that at least one feedback packet is sent back even when
        // it is with no railcom payload.
        auto *p = this->alloc_new_packet(0);
        if (p)
        {
            this->feedbackQueue_.commit_back();
            Debug::RailcomPackets::toggle();
            HAL_NVIC_SetPendingIRQ(HW::OS_INTERRUPT);
        }
    }
};

#endif // _FREERTOS_DRIVERS_ST_STM32RAILCOM_HXX_
