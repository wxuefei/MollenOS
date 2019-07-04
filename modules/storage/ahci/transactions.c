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
 * Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */
//#define __TRACE

#include <assert.h>
#include "dispatch.h"
#include <ddk/utils.h>
#include "manager.h"
#include <stdlib.h>

static UUId_t TransactionId = 0;

static struct {
    int          Direction;
    int          DMA;
    int          AddressingMode;
    AtaCommand_t Command;
    size_t       SectorAlignment;
    size_t       MaxSectors;
} CommandTable[] = {
    { __STORAGE_OPERATION_READ, 0, 2, AtaPIOReadExt, 1, 0xFFFF },
    { __STORAGE_OPERATION_READ, 0, 1, AtaPIORead, 1, 0xFF },
    { __STORAGE_OPERATION_READ, 0, 0, AtaPIORead, 1, 0xFF },
    { __STORAGE_OPERATION_READ, 1, 2, AtaDMAReadExt, 1, 0xFFFF },
    { __STORAGE_OPERATION_READ, 1, 1, AtaDMARead, 1, 0xFF },
    { __STORAGE_OPERATION_READ, 1, 0, AtaDMARead, 1, 0xFF },
    
    { __STORAGE_OPERATION_WRITE, 0, 2, AtaPIOWriteExt, 1, 0xFFFF },
    { __STORAGE_OPERATION_WRITE, 0, 1, AtaPIOWrite, 1, 0xFF },
    { __STORAGE_OPERATION_WRITE, 0, 0, AtaPIOWrite, 1, 0xFF },
    { __STORAGE_OPERATION_WRITE, 1, 2, AtaDMAWriteExt, 1, 0xFFFF },
    { __STORAGE_OPERATION_WRITE, 1, 1, AtaDMAWrite, 1, 0xFF },
    { __STORAGE_OPERATION_WRITE, 1, 0, AtaDMAWrite, 1, 0xFF },
    { -1, -1, -1, 0, 0, 0 }
};

static void
GetSgMetricsFromOffset(
    _In_  AhciTransaction_t* Transaction,
    _In_  int                SgCount,
    _In_  size_t             Offset,
    _Out_ int*               SgIndexOut,
    _Out_ size_t*            SgOffsetOut)
{
    for (int i = 0; i < SgCount; i++) {
        if (Offset < Transaction->SgList[i].length) {
            *SgIndexOut  = i;
            *SgOffsetOut = Offset;
            break;
        }
        Offset -= Transaction->SgList[i].length;
    }
}

static OsStatus_t
QueueTransaction(
    _In_ AhciController_t*  Controller,
    _In_ AhciPort_t*        Port,
    _In_ AhciTransaction_t* Transaction)
{
    OsStatus_t Status;
    
    // OK so the transaction we just recieved needs to be queued up,
    // so we must initally see if we can allocate a new slot on the port
    Status = AhciPortAllocateCommandSlot(Port, &Transaction->Slot);
    CollectionAppend(Port->Transactions, &Transaction->Header);
    if (Status != OsSuccess) {
        Transaction->State = TransactionQueued;
        return OsSuccess;
    }
    
    // If we reach here we've successfully allocated a slot, now we should dispatch 
    // the transaction
    Transaction->State = TransactionInProgress;
    switch (Transaction->Type) {
        case TransactionRegisterFISH2D: {
            Status = AhciDispatchRegisterFIS(Controller, Port, Transaction);
        } break;
        
        default: {
            assert(0);
        } break;
    }
    
    if (Status != OsSuccess) {
        CollectionRemoveByNode(Port->Transactions, &Transaction->Header);
        AhciPortFreeCommandSlot(Port, Transaction->Slot);
    }
    return Status;
}

static OsStatus_t
AhciTransactionDestroy(
    _In_ AhciTransaction_t* Transaction)
{
    // Detach from our buffer reference
    dma_detach(&Transaction->DmaAttachment);
    free(Transaction);
    return OsSuccess;
}

OsStatus_t
AhciTransactionControlCreate(
    _In_ AhciDevice_t* Device,
    _In_ AtaCommand_t  Command,
    _In_ size_t        Length,
    _In_ int           Direction)
{
    AhciTransaction_t*    Transaction;
    int                   SgCount = 1;
    OsStatus_t            Status;
    
    if (!Device) {
        return OsInvalidParameters;
    }

    Transaction = (AhciTransaction_t*)malloc(
        sizeof(AhciTransaction_t) + (sizeof(struct dma_sg) * SgCount));
    if (!Transaction) {
        return OsOutOfMemory;
    }
    
    // Do not bother about zeroing the array
    memset(Transaction, 0, sizeof(AhciTransaction_t));
    dma_attach(Device->Port->InternalBuffer.handle, &Transaction->DmaAttachment);
    Transaction->Header.Key.Value.Id     = TransactionId++;
    Transaction->ResponseAddress.Process = UUID_INVALID;

    Transaction->Type      = TransactionRegisterFISH2D;
    Transaction->SgCount   = SgCount;
    Transaction->State     = TransactionCreated;
    Transaction->Slot      = -1;
    Transaction->Command   = Command;
    Transaction->BytesLeft = Length;
    Transaction->Direction = Direction;
    
    // Do not bother to check return code again, things should go ok now
    (void)dma_get_metrics(&Transaction->DmaAttachment,
        &Transaction->SgCount, &Transaction->SgList[0]);
    
    // The transaction is now prepared and ready for the dispatch
    Status = QueueTransaction(Device->Controller, Device->Port, Transaction);
    if (Status != OsSuccess) {
        AhciTransactionDestroy(Transaction);
    }
    return Status;
}

OsStatus_t
AhciTransactionStorageCreate(
    _In_ AhciDevice_t*         Device,
    _In_ MRemoteCallAddress_t* Address,
    _In_ StorageOperation_t*   Operation)
{
    struct dma_attachment DmaAttachment;
    AhciTransaction_t*    Transaction;
    int                   SgCount;
    OsStatus_t            Status;
    int                   i;
    
    if (!Device || !Address || !Operation) {
        return OsInvalidParameters;
    }
    
    Status = dma_attach(Operation->BufferHandle, &DmaAttachment);
    if (Status != OsSuccess) {
        return OsInvalidParameters;
    }
    
    (void)dma_get_metrics(&DmaAttachment, &SgCount, NULL);
    Transaction = (AhciTransaction_t*)malloc(
        sizeof(AhciTransaction_t) + (sizeof(struct dma_sg) * SgCount));
    if (!Transaction) {
        dma_detach(&DmaAttachment);
        return OsOutOfMemory;
    }
    
    // Do not bother about zeroing the array
    memset(Transaction, 0, sizeof(AhciTransaction_t));
    memcpy(&Transaction->ResponseAddress, Address, sizeof(MRemoteCallAddress_t));
    memcpy(&Transaction->DmaAttachment, &DmaAttachment, sizeof(struct dma_attachment));
    Transaction->Header.Key.Value.Id = TransactionId++;

    Transaction->Type    = TransactionRegisterFISH2D;
    Transaction->SgCount = SgCount;
    Transaction->Sector  = Operation->AbsoluteSector;
    Transaction->State   = TransactionCreated;
    Transaction->Slot    = -1;

    if (Operation->Direction == __STORAGE_OPERATION_READ) {
        Transaction->Direction = AHCI_XACTION_IN;
    }
    else {
        Transaction->Direction = AHCI_XACTION_OUT;
    }
    
    // Do not bother to check return code again, things should go ok now
    dma_get_metrics(&DmaAttachment, &Transaction->SgCount, &Transaction->SgList[0]);
    GetSgMetricsFromOffset(Transaction, SgCount, Operation->BufferOffset, 
        &Transaction->SgIndex, &Transaction->SgOffset);
    
    // Set upper bound on transaction
    if ((Transaction->Sector + Operation->SectorCount) >= Device->SectorCount) {
        Operation->SectorCount = Device->SectorCount - Transaction->Sector;
    }
    
    // Select the appropriate command
    i = 0;
    while (CommandTable[i].Direction != -1) {
        if (CommandTable[i].Direction      == Operation->Direction &&
            CommandTable[i].DMA            == Device->HasDMAEngine &&
            CommandTable[i].AddressingMode == Device->AddressingMode) {
            // Found the appropriate command
            Transaction->Command         = CommandTable[i].Command;
            Transaction->SectorAlignment = CommandTable[i].SectorAlignment;
            Transaction->BytesLeft       = MIN(Operation->SectorCount, CommandTable[i].MaxSectors) * Device->SectorSize;
            break;
        }
        i++;
    }
    
    // TODO: handle this
    assert(CommandTable[i].Direction != -1);
    assert(Transaction->BytesLeft != 0);
    
    // The transaction is now prepared and ready for the dispatch
    Status = QueueTransaction(Device->Controller, Device->Port, Transaction);
    if (Status != OsSuccess) {
        AhciTransactionDestroy(Transaction);
    }
    return Status;
}

OsStatus_t
AhciManagerCancelTransaction(
    _In_ AhciTransaction_t* Transaction)
{
    return OsNotSupported;
}

static OsStatus_t
VerifyRegisterFISD2H(
    _In_ AhciPort_t*        Port,
    _In_ AhciTransaction_t* Transaction)
{
    FISRegisterD2H_t* Result = (FISRegisterD2H_t*)&Transaction->Response.RegisterD2H;

    // Is the error bit set?
    if (Result->Status & ATA_STS_DEV_ERROR) {
        PrintTaskDataErrorString(Result->Error);
        return OsError;
    }

    // Is the fault bit set?
    if (Result->Status & ATA_STS_DEV_FAULT) {
        ERROR("AHCI::Port (%i): Device Fault, error 0x%x",
            Port->Id, (size_t)Result->Error);
        return OsError;
    }
    
    // Increase the sector with the number of sectors transferred
    Transaction->SectorsTransferred += Result->Count;
    return OsSuccess;
}

OsStatus_t
AhciTransactionHandleResponse(
    _In_ AhciController_t*  Controller,
    _In_ AhciPort_t*        Port,
    _In_ AhciTransaction_t* Transaction)
{
    StorageOperationResult_t Result = { 0 };
    TRACE("AhciCommandFinish()");
    
    // Verify the command execution
    if (Transaction->Type == TransactionRegisterFISH2D) {
        Result.Status = VerifyRegisterFISD2H(Port, Transaction);
    }
    else {
        assert(0);
    }

    // Is the transaction finished? (Or did it error?)
    if (Result.Status != OsSuccess || Transaction->BytesLeft == 0) {
        if (Transaction->ResponseAddress.Thread == UUID_INVALID) {
            AhciManagerHandleControlResponse(Port, Transaction);
        }
        else {
            Result.SectorsTransferred = Transaction->SectorsTransferred;
            RPCRespond(&Transaction->ResponseAddress, (void*)&Result, sizeof(StorageOperationResult_t));
        }
        return AhciTransactionDestroy(Transaction);
    }
    return QueueTransaction(Controller, Port, Transaction);
}
