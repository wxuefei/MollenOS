/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Remote Procedure Definitions & Structures
 * - This header describes the base remote procedure-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _RPC_INTERFACE_H_
#define _RPC_INTERFACE_H_

#ifndef _IPC_INTERFACE_H_
#error "You must include ipc.h and not this directly"
#endif

/* Includes 
 * - System */
#include <os/osdefs.h>

/* Includes
 * - Library */
#include <string.h>

/* The argument package that can be passed
 * to an IPC function request, we support up
 * to 5 arguments */
PACKED_TYPESTRUCT(RPCArgument, {
	int						Type;
	union {
		__CONST void		*Buffer;
		size_t				Value;
	} Data;
	size_t					Length;
});

/* The base event message structure, any IPC
 * action going through pipes in MollenOS must
 * inherit from this structure for security */
PACKED_TYPESTRUCT(MRemoteCall, {
	int						Version;
	int						Function;
	int						Port;
	int						ResponsePort;
	size_t					Length;		/* Excluding this header */
	UUId_t					Sender;		/* Automatically set by OS */
	RPCArgument_t			Arguments[IPC_MAX_ARGUMENTS];
	RPCArgument_t			Result;
});

_CODE_BEGIN
/* RPCInitialize 
 * Initializes a new RPC message of the 
 * given type and length */
SERVICEAPI
void
SERVICEABI
RPCInitialize(
	_InOut_ MRemoteCall_t *RemoteCall, 
	_In_ int Version, 
	_In_ int Port, 
	_In_ int Function)
{
	// Initialize structure
	memset((void*)RemoteCall, 0, sizeof(MRemoteCall_t));
	RemoteCall->Version = Version;
	RemoteCall->Function = Function;
	RemoteCall->Port = Port;

	// Standard response port
	RemoteCall->ResponsePort = PIPE_RPCIN;
}

/* RPCSetArgument
 * Adds a new argument for the RPC request at
 * the given argument index. It's not possible to override 
 * a current argument */
SERVICEAPI
void
SERVICEABI
RPCSetArgument(
	_InOut_ MRemoteCall_t *RemoteCall,
	_In_ int Index, 
	_In_ __CONST void *Data, 
	_In_ size_t Length)
{
	// Sanitize the index and the current argument
	if (Index >= IPC_MAX_ARGUMENTS || Index < 0 
        || RemoteCall->Arguments[Index].Type != ARGUMENT_NOTUSED) {
		return;
    }
    
    // We must have room for the argument
    if ((RemoteCall->Length + Length) > IPC_MAX_MESSAGELENGTH
        || Length == 0) {
        return;
    }

	// Determine the type of argument
	if (Length <= sizeof(size_t)) {
		RemoteCall->Arguments[Index].Type = ARGUMENT_REGISTER;

		if (Length == 1) {
			RemoteCall->Arguments[Index].Data.Value = *((uint8_t*)Data);
		}
		else if (Length == 2) {
			RemoteCall->Arguments[Index].Data.Value = *((uint16_t*)Data);
		}
		else if (Length == 4) {
			RemoteCall->Arguments[Index].Data.Value = *((uint32_t*)Data);
		}
		else if (Length == 8) {
			RemoteCall->Arguments[Index].Data.Value = *((size_t*)Data);
		}
	}
	else {
		RemoteCall->Arguments[Index].Type = ARGUMENT_BUFFER;
		RemoteCall->Arguments[Index].Data.Buffer = Data;
		RemoteCall->Arguments[Index].Length = Length;
	}
	RemoteCall->Length += Length;
}

/* RPCSetResult
 * Installs a result buffer that will be filled
 * with the response from the RPC request */
SERVICEAPI 
void
SERVICEABI
RPCSetResult(
	_InOut_ MRemoteCall_t *RemoteCall,
	_In_ __CONST void *Data, 
	_In_ size_t Length)
{
	// Always a buffer element as we need
	// a target to copy the data into
	RemoteCall->Result.Type = ARGUMENT_BUFFER;
	RemoteCall->Result.Data.Buffer = Data;
	RemoteCall->Result.Length = Length;
}

/* RPCListen 
 * Call this to wait for a new RPC message, it automatically
 * reads the message, and all the arguments. To avoid freeing
 * an argument, set InUse to 0 */
CRTDECL(
OsStatus_t,
RPCListen(
	_In_ MRemoteCall_t  *Message,
    _In_ void           *ArgumentBuffer));

/* RPCExecute/RPCEvent
 * To get a reply from the RPC request, the user
 * must use RPCExecute, this will automatically wait
 * for a reply, whereas RPCEvent will send the request
 * and not block/wait for reply */
CRTDECL( 
OsStatus_t,
RPCExecute(
	_In_ MRemoteCall_t *Rpc, 
	_In_ UUId_t Target));

CRTDECL(
OsStatus_t,
RPCEvent(
	_In_ MRemoteCall_t *Rpc, 
	_In_ UUId_t Target));

/* RPCRespond
 * This is a wrapper to return a respond message/buffer to the
 * sender of the message, it's good practice to always wait for
 * a result when there is going to be one */
CRTDECL( 
OsStatus_t,
RPCRespond(
	_In_ MRemoteCall_t *Rpc,
	_In_ __CONST void *Buffer, 
	_In_ size_t Length));
_CODE_END

#endif //!_RPC_INTERFACE_H_