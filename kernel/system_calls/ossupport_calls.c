/* MollenOS
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
 * MollenOS MCore - System Calls
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <modules/manager.h>
#include <memoryspace.h>
#include <ds/mstring.h>
#include <threading.h>
#include <handle.h>
#include <debug.h>
#include <heap.h>

OsStatus_t
ScCreateMemoryHandler(
    _In_  Flags_t    Flags,
    _In_  size_t     Length,
    _Out_ UUId_t*    HandleOut,
    _Out_ uintptr_t* AddressBaseOut)
{
    SystemMemorySpace_t* Space = GetCurrentMemorySpace();
    if (Space->HeapSpace != NULL) {
        SystemMemoryMappingHandler_t* Handler = (SystemMemoryMappingHandler_t*)kmalloc(sizeof(SystemMemoryMappingHandler_t));
        Handler->Handle  = CreateHandle(HandleGeneric, 0, Handler);
        Handler->Address = AllocateBlocksInBlockmap(Space->HeapSpace, __MASK, Length);
        Handler->Length  = Length;
        return CollectionAppend(Space->MemoryHandlers, &Handler->Header);
    }
    return OsInvalidPermissions;
}

OsStatus_t
ScDestroyMemoryHandler(
    _In_ UUId_t Handle)
{
    SystemMemoryMappingHandler_t* Handler = (SystemMemoryMappingHandler_t*)LookupHandle(Handle);
    SystemMemorySpace_t*          Space   = GetCurrentMemorySpace();
    if (Space->MemoryHandlers != NULL && Handler != NULL) {
        CollectionRemoveByNode(Space->MemoryHandlers, &Handler->Header);
        ReleaseBlockmapRegion(Space->HeapSpace, Handler->Address, Handler->Length);
        DestroyHandle(Handle);
        kfree(Handler);
        return OsSuccess;
    }
    return OsDoesNotExist;
}

OsStatus_t
ScInstallSignalHandler(
    _In_ uintptr_t Handler) 
{
    SystemMemorySpace_t* Space = GetCurrentMemorySpace();
    Space->SignalHandler = Handler;
    return OsSuccess;
}

OsStatus_t
ScRaiseSignal(
    _In_ UUId_t ThreadHandle, 
    _In_ int    Signal)
{
    MCoreThread_t* Thread = GetThread(ThreadHandle);
    if (Thread != NULL) {
        return SignalCreate(ThreadHandle, Signal);
    }
    return OsDoesNotExist;
}

OsStatus_t
ScDestroyHandle(
    _In_ UUId_t Handle)
{
    if (Handle == 0 || Handle == UUID_INVALID) {
        return OsError;
    }
    return DestroyHandle(Handle);
}
