/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * X86 CMOS & RTC (Clock) Driver
 * http://wiki.osdev.org/RTC
 */
#define __MODULE "RTC0"
#define __TRACE

#include <hpet.h>
#include <interrupts.h>
#include <timers.h>
#include <debug.h>
#include <string.h>
#include <stdlib.h>
#include "../cmos.h"

extern void    CmosResetTicks(void);
extern clock_t CmosGetTicks(void);

InterruptStatus_t
RtcInterrupt(
        _In_ InterruptFunctionTable_t*  NotUsed,
        _In_ void*                      Context)
{
    Cmos_t *Chip = (Cmos_t*)Context;
    _CRT_UNUSED(NotUsed);

    // Update Peroidic Tick Counter
    Chip->NsCounter += Chip->NsTick;
    Chip->Ticks++;
    TimersInterrupt(Chip->Irq);

    // Acknowledge interrupt by reading register C
    CmosRead(CMOS_REGISTER_STATUS_C);
    return InterruptHandled;
}

OsStatus_t
RtcInitialize(
    _In_ Cmos_t* Chip)
{
    DeviceInterrupt_t Interrupt = { { 0 } };
    uint8_t StateB              = 0;
    uint8_t Rate                = 0x06; // must be between 3 and 15
    
    TRACE("RtcInitialize()");

    // Ms is .97, 1024 ints per sec
    // Frequency = 32768 >> (rate-1), 15 = 2, 14 = 4, 13 = 8/s (125 ms)
    Chip->NsTick     = 976;
    Chip->NsCounter  = 0;
    Chip->AlarmTicks = 0;

    // Disable RTC Irq
    StateB = CmosRead(CMOS_REGISTER_STATUS_B);
    StateB &= ~(0x70);
    CmosWrite(CMOS_REGISTER_STATUS_B, StateB);
    
    // Update state_b
    StateB = CmosRead(CMOS_REGISTER_STATUS_B);

    // Set Frequency
    CmosWrite(CMOS_REGISTER_STATUS_A, 0x20 | Rate);

    // Clear pending interrupt
    CmosRead(CMOS_REGISTER_STATUS_C);

    Interrupt.Line                  = CMOS_RTC_IRQ;
    Interrupt.Pin                   = INTERRUPT_NONE;
    Interrupt.Vectors[0]            = INTERRUPT_NONE;
    Interrupt.ResourceTable.Handler = RtcInterrupt;
    Interrupt.Context               = Chip;
    
    Chip->Irq = InterruptRegister(&Interrupt, INTERRUPT_EXCLUSIVE | INTERRUPT_KERNEL);
    if (Chip->Irq != UUID_INVALID) {
        TimersRegisterSystemTimer(Chip->Irq, Chip->NsTick, CmosGetTicks, CmosResetTicks);
    }
    else {
        ERROR("Failed to register interrupt for rtc");
    }

    // Detect whether or not we are emulated by the hpet
    if (HpHasLegacyController() == OsSuccess) {
    	// Counter 1 is the IRQ 8 emulator
        HpComparatorStart(1, 1000, 1, Interrupt.Line);
    }
    else {
        // Enable Periodic Interrupts
        StateB = CmosRead(CMOS_REGISTER_STATUS_B);
        StateB |= CMOSB_RTC_PERIODIC;
        CmosWrite(CMOS_REGISTER_STATUS_B, StateB);
    }

    // Clear pending interrupt again
    CmosRead(CMOS_REGISTER_STATUS_C);
    return OsSuccess;
}
