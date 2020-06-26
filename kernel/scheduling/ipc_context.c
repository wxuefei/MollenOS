/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * IP-Communication
 * - Implementation of inter-thread communication. 
 */

//#define __TRACE

#include <ddk/barrier.h>
#include <ddk/handle.h>
#include <ds/streambuffer.h>
#include <debug.h>
#include <futex.h>
#include <handle.h>
#include <handle_set.h>
#include <heap.h>
#include <ioevt.h>
#include <ipc_context.h>
#include <memoryspace.h>
#include <memory_region.h>
#include <string.h>
#include <threading.h>

typedef struct IpcContext {
    UUId_t          Handle;
    UUId_t          CreatorThreadHandle;
    UUId_t          MemoryRegionHandle;
    streambuffer_t* KernelStream;
} IpcContext_t;

static inline MCoreThread_t*
GetContextThread(
    _In_ IpcContext_t* context)
{
    return LookupHandleOfType(context->CreatorThreadHandle, HandleTypeThread);
}

static void
IpcContextDestroy(
    _In_ void* resource)
{
    IpcContext_t* context = resource;
    DestroyHandle(context->MemoryRegionHandle);
    kfree(context);
}

OsStatus_t
IpcContextCreate(
    _In_  size_t  Size,
    _Out_ UUId_t* HandleOut,
    _Out_ void**  UserContextOut)
{
    IpcContext_t* Context;
    OsStatus_t    Status;
    void*         KernelMapping;
    
    if (!HandleOut || !UserContextOut) {
        return OsInvalidParameters;
    }
    
    Context = kmalloc(sizeof(IpcContext_t));
    if (!Context) {
        return OsOutOfMemory;
    }
    
    Context->CreatorThreadHandle = GetCurrentThreadId();
    
    Status = MemoryRegionCreate(Size, Size, 0, &KernelMapping, UserContextOut,
        &Context->MemoryRegionHandle);
    if (Status != OsSuccess) {
        kfree(Context);
        return Status;
    }
    
    Context->Handle       = CreateHandle(HandleTypeIpcContext, IpcContextDestroy, Context);
    Context->KernelStream = (streambuffer_t*)KernelMapping;
    streambuffer_construct(Context->KernelStream, Size - sizeof(streambuffer_t), 
        STREAMBUFFER_GLOBAL | STREAMBUFFER_MULTIPLE_WRITERS);
    
    *HandleOut = Context->Handle;
    return Status;
}

struct message_state {
    unsigned int base;
    unsigned int state;
};

static OsStatus_t
AllocateMessage(
    _In_  struct ipmsg_desc*    SourceMessage,
    _In_  size_t                Timeout,
    _In_  struct message_state* State,
    _Out_ IpcContext_t**        TargetContext)
{
    IpcContext_t* Context;
    size_t        BytesAvailable;
    size_t        BytesToAllocate = sizeof(struct ipmsg_resp) + SourceMessage->base->header.length;
    TRACE("[ipc] [allocate] %u/%u", SourceMessage->base->header.protocol, SourceMessage->base->header.action);
    
    if (SourceMessage->address->type == IPMSG_ADDRESS_HANDLE) {
        Context = LookupHandleOfType(SourceMessage->address->data.handle, HandleTypeIpcContext);
    }
    else {
        UUId_t     Handle;
        OsStatus_t Status = LookupHandleByPath(SourceMessage->address->data.path, &Handle);
        if (Status != OsSuccess) {
            ERROR("[ipc] [allocate] could not find target path %s", 
                SourceMessage->address->data.path);
            return Status;
        }
        
        Context = LookupHandleOfType(Handle, HandleTypeIpcContext);
    }
    
    if (!Context) {
        ERROR("[ipc] [allocate] could not find target handle %u", 
            SourceMessage->address->data.handle);
        return OsDoesNotExist;
    }
    
    BytesAvailable = streambuffer_write_packet_start(Context->KernelStream,
        BytesToAllocate, 0, &State->base, &State->state);
    if (!BytesAvailable) {
        ERROR("[ipc] [allocate] timeout allocating space for message");
        return OsTimeout;
    }
    
    *TargetContext = Context;
    return OsSuccess;
}

static OsStatus_t
MapUntypedParameter(
    _In_ struct gracht_param* parameter,
    _In_ SystemMemorySpace_t* TargetMemorySpace)
{
    VirtualAddress_t CopyAddress;
    size_t     OffsetInPage = parameter->data.value % GetMemorySpacePageSize();
    OsStatus_t Status       = CloneMemorySpaceMapping(
        GetCurrentMemorySpace(), TargetMemorySpace,
        (VirtualAddress_t)parameter->data.buffer, &CopyAddress, parameter->length + OffsetInPage,
        MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_READONLY | MAPPING_PERSISTENT,
        MAPPING_VIRTUAL_PROCESS);
    if (Status != OsSuccess) {
        ERROR("[ipc] [map_untyped] Failed to clone ipc mapping");
        return Status;
    }
    
    // Update buffer pointer in untyped argument
    parameter->data.buffer = (void*)(CopyAddress + OffsetInPage);
    smp_wmb();
    
    return OsSuccess;
}

static OsStatus_t
WriteMessage(
    _In_ IpcContext_t*         Context,
    _In_ struct ipmsg_desc*    Message,
    _In_ struct message_state* State)
{
    int i;
    
    TRACE("[ipc] [write] %u/%u [%u, %u]", Message->base->header.protocol,
        Message->base->header.action, sizeof(struct ipmsg_resp),
        sizeof(struct gracht_message) + (
            (Message->base->header.param_in + Message->base->header.param_out)
                * sizeof(struct gracht_param)));
    
    // Write all members in the order of ipmsg
    streambuffer_write_packet_data(Context->KernelStream, 
        Message->response, sizeof(struct ipmsg_resp), &State->state);
    
    // Fixup all SHM buffer values before writing the base message
    for (i = 0; i < Message->base->header.param_in; i++) {
        if (Message->base->params[i].type == GRACHT_PARAM_SHM &&
            Message->base->params[i].length > 0) {
            MCoreThread_t* Thread = GetContextThread(Context);
            OsStatus_t     Status = MapUntypedParameter(&Message->base->params[i],
                Thread->MemorySpace);
            if (Status != OsSuccess) {
                // WHAT DO
                //CleanupMessage(Target, &IpcContext->Message);
                //atomic_store(&IpcContext->WriteSyncObject, 0);
                //(void)FutexWake(&IpcContext->WriteSyncObject, 1, 0);
                ERROR("[ipc] [initialize_message] failed to map parameter");
                return Status;
            }
        }
    }
    
    streambuffer_write_packet_data(Context->KernelStream, 
        Message->base, sizeof(struct gracht_message) + 
            ((Message->base->header.param_in + Message->base->header.param_out)
                * sizeof(struct gracht_param)),
        &State->state);
    
    // Handle all the buffer/shm parameters
    for (i = 0; i < Message->base->header.param_in; i++) {
        if (Message->base->params[i].type == GRACHT_PARAM_BUFFER &&
            Message->base->params[i].length > 0) {
            streambuffer_write_packet_data(Context->KernelStream,
                Message->base->params[i].data.buffer,
                Message->base->params[i].length,
                &State->state);
        }
    }
    return OsSuccess;
}

static inline void
SendMessage(
    _In_ IpcContext_t*         context,
    _In_ struct ipmsg_desc*    message,
    _In_ struct message_state* state)
{
    TRACE("[ipc] [send] %u/%u => %u",
        message->base->header.protocol, message->base->header.action,
        sizeof(struct ipmsg_resp) + message->base->header.length);
    
    streambuffer_write_packet_end(context->KernelStream, state->base,
        sizeof(struct ipmsg_resp) + message->base->header.length);
    MarkHandle(context->Handle, IOEVTIN);
}

static inline void
CleanupMessage(
    _In_ struct ipmsg* message)
{
    int i;
    TRACE("[ipc] [cleanup]");

    // Flush all the mappings granted in the argument phase
    for (i = 0; i < message->base.header.param_in; i++) {
        if (message->base.params[i].type == GRACHT_PARAM_SHM && 
            message->base.params[i].length > 0) {
            OsStatus_t status = MemorySpaceUnmap(GetCurrentMemorySpace(),
                (VirtualAddress_t)message->base.params[i].data.buffer,
                message->base.params[i].length);
            if (status != OsSuccess) {
                // LOG
            }
        }
    }
}

static void
WaitForMessageNotification(
    _In_ struct ipmsg_resp* response,
    _In_ size_t             timeout)
{
    TRACE("[ipc] [wait] start");
    
    if (response->notify_method == IPMSG_NOTIFY_NONE) {
        MCoreThread_t* thread = LookupHandleOfType(response->notify_data.handle, HandleTypeThread);
        SemaphoreWait(&thread->WaitObject, timeout);
    }
    else if (response->notify_method == IPMSG_NOTIFY_HANDLE_SET) {
        struct ioevt_event event;
        int                numberOfEvents;
        WaitForHandleSet(response->notify_data.handle, &event, 1, 0, timeout, &numberOfEvents);
    }
    
    TRACE("[ipc] [wait] end");
}

static void
SendNotification(
    _In_ struct ipmsg_resp* response,
    _In_ unsigned int       flags)
{
    TRACE("[ipc] [signal] 0x%x", flags);
    if (response->notify_method == IPMSG_NOTIFY_NONE &&
        MESSAGE_FLAG_TYPE(flags) == MESSAGE_FLAG_SYNC) {
        MCoreThread_t* thread = LookupHandleOfType(response->notify_data.handle, HandleTypeThread);
        if (thread) {
            TRACE("[ipc] [signal] signalling thread");
            SemaphoreSignal(&thread->WaitObject, 1);
        }
    }
    else if (response->notify_method == IPMSG_NOTIFY_HANDLE_SET) {
        MarkHandle(response->notify_data.handle, 0);
    }
    else if (response->notify_method == IPMSG_NOTIFY_SIGNAL) {
        SignalSend(response->notify_data.handle, SIGIPC, response->notify_context);
    }
    else if (response->notify_method == IPMSG_NOTIFY_THREAD) {
        NOTIMPLEMENTED("[ipc] [send_notification] IPC_NOTIFY_METHOD_THREAD missing implementation");
    }
}

// THIS STRUCTURE MUST MATCH THE STRUCTURE IN libgracht/client.c
struct gracht_message_descriptor {
    gracht_object_header_t header;
    int                    status;
    struct gracht_message  message;
};

static OsStatus_t
WriteShortResponse(
    _In_ struct ipmsg_desc* message,
    _In_ OsStatus_t         status)
{
    int error_code = GRACHT_MESSAGE_ERROR;
    
    // write status to ERROR
    if (message->response->dma_handle != UUID_INVALID) {
        size_t bytesWritten;
        size_t offset = offsetof(struct gracht_message_descriptor, status);

        MemoryRegionWrite(
            message->response->dma_handle,
            message->response->dma_offset + offset,
            &error_code, sizeof(int),
            &bytesWritten);
    }
    
    SendNotification(message->response, message->base->header.flags);
    return OsSuccess;
}

static OsStatus_t
WriteFullResponse(
    _In_ struct ipmsg*          message,
    _In_ struct gracht_message* messageDescriptor)
{
    TRACE("[ipc] [WriteFullResponse] dma_handle %u, dma_offset %u",
        message->response.dma_handle, message->response.dma_offset);
    if (message->response.dma_handle != UUID_INVALID) {
        size_t offset = message->response.dma_offset;
        size_t bytesWritten;
        int    i;
        
        offset += offsetof(struct gracht_message_descriptor, message);
        MemoryRegionWrite(
            message->response.dma_handle,
            offset,
            messageDescriptor,
            sizeof(struct gracht_message) + (messageDescriptor->header.param_in * sizeof(struct gracht_param)),
            &bytesWritten);
        
        offset += bytesWritten;
        
        TRACE("[ipc] [WriteFullResponse] wrote %u bytes, new offset %u",
            LODWORD(bytesWritten), offset);
        for (i = 0; i < messageDescriptor->header.param_in; i++) {
            struct gracht_param* param = &messageDescriptor->params[i];
            
            if (param->type == GRACHT_PARAM_BUFFER) {
                MemoryRegionWrite(message->response.dma_handle, offset,
                    param->data.buffer, param->length, &bytesWritten);
                offset += param->length;
                
                TRACE("[ipc] [WriteFullResponse] wrote %u bytes, new offset %u",
                    LODWORD(bytesWritten), offset);
            }
        }
    }
    
    SendNotification(&message->response, message->base.header.flags);
    return OsSuccess;
}

OsStatus_t
IpcContextSendMultiple(
    _In_ struct ipmsg_desc** Messages,
    _In_ int                 MessageCount,
    _In_ size_t              Timeout)
{
    struct message_state State;
    int                  i;
    TRACE("[ipc] [send] count %i, timeout %u", MessageCount, LODWORD(Timeout));
    
    if (!Messages || !MessageCount) {
        return OsInvalidParameters;
    }
    
    for (int i = 0; i < MessageCount; i++) {
        IpcContext_t* TargetContext;
        OsStatus_t    Status = AllocateMessage(Messages[i], Timeout, 
             &State, &TargetContext);
        if (Status != OsSuccess) {
            if (WriteShortResponse(Messages[i], Status) != OsSuccess) {
                WARNING("[ipc] [send_multiple] failed to write response");
            }
        }
        WriteMessage(TargetContext, Messages[i], &State);
        SendMessage(TargetContext, Messages[i], &State);
    }
    
    // Iterate all messages again and wait for response
    for (i = 0; i < MessageCount; i++) {
        if (MESSAGE_FLAG_TYPE(Messages[i]->base->header.flags) == MESSAGE_FLAG_SYNC) {
            WaitForMessageNotification(Messages[i]->response, Timeout);
        }
    }
    return OsSuccess;
}

OsStatus_t
IpcContextRespondMultiple(
    _In_ struct ipmsg**          messages,
    _In_ struct gracht_message** messageDescriptors,
    _In_ int                     messageCount)
{
    int i;
    
    TRACE("[ipc] [respond]");
    if (!messages || !messageDescriptors || !messageCount) {
        ERROR("[ipc] [respond] input was null");
        return OsInvalidParameters;
    }
    
    for (i = 0; i < messageCount; i++) {
        if (WriteFullResponse(messages[i], messageDescriptors[i]) != OsSuccess) {
            WARNING("[ipc] [respond] failed to write response");
        }
        CleanupMessage(messages[i]);
    }
    return OsSuccess;
}