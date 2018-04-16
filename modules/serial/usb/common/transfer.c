/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS MCore - USB Controller Scheduler
 * - Contains the implementation of a shared controller scheduker
 *   for all the usb drivers
 */
//#define __TRACE
#define __COMPILE_ASSERT

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "transfer.h"

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

// Globals
static UUId_t __GlbTransferId   = 0;

/* UsbManagerCreateTransfer
 * Creates a new transfer with the usb-manager.
 * Identifies and registers with neccessary services */
UsbManagerTransfer_t*
UsbManagerCreateTransfer(
    _In_ UsbTransfer_t*         Transfer,
    _In_ UUId_t                 Requester,
    _In_ int                    ResponsePort,
    _In_ UUId_t                 Device,
    _In_ UUId_t                 Pipe)
{
    // Variables
    UsbManagerTransfer_t *UsbTransfer = NULL;

    // Allocate a new instance
    UsbTransfer                 = (UsbManagerTransfer_t*)malloc(sizeof(UsbManagerTransfer_t));
    memset(UsbTransfer, 0, sizeof(UsbManagerTransfer_t));

    // Copy information over
    memcpy(&UsbTransfer->Transfer, Transfer, sizeof(UsbTransfer_t));
    UsbTransfer->Requester      = Requester;
    UsbTransfer->ResponsePort   = ResponsePort;
    UsbTransfer->DeviceId       = Device;
    UsbTransfer->Pipe           = Pipe;
    UsbTransfer->Id             = __GlbTransferId++;
    UsbTransfer->Status         = TransferNotProcessed;
    return UsbTransfer;
}

/* UsbManagerSendNotification 
 * Sends a notification to the subscribing process whenever a periodic
 * transaction has completed/failed. */
void
UsbManagerSendNotification(
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // If user doesn't want, ignore
    if (Transfer->Transfer.Flags & USB_TRANSFER_NO_NOTIFICATION) {
        return;
    }

    // Send interrupt
    InterruptDriver(
        Transfer->Requester,                        // Process
        (size_t)Transfer->Transfer.PeriodicData,    // Data pointer 
        Transfer->Status,                           // Status of transfer
        Transfer->CurrentDataIndex, 0);             // Data offset (not used in isoc)

    // Increase
    if (Transfer->Transfer.Type == InterruptTransfer) {
        Transfer->CurrentDataIndex = ADDLIMIT(0, Transfer->CurrentDataIndex,
            Transfer->Transfer.Transactions[0].Length, Transfer->Transfer.PeriodicBufferSize);
    }
}
