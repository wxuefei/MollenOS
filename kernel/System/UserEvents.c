/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS User Events
* - Redirects events to userspace
* - Most cases this means Sapphire
*/

/* Includes */
#include <Arch.h>
#include <InputManager.h>
#include <Modules/Phoenix.h>
#include <stddef.h>

/* Globals */
int GlbEmInitialized = 0;
PhxId_t GlbEmWindowManager = 0;

/* Initialise Event Manager */
void EmInit(void)
{
	/* Set initialized */
	GlbEmInitialized = 1;
	GlbEmWindowManager = PHOENIX_NO_ASH;
}

/* Register */
void EmRegisterSystemTarget(PhxId_t ProcessId)
{
	/* Sanity, NO OVERRIDES */
	if (ProcessId != PHOENIX_NO_ASH
		&& GlbEmWindowManager != PHOENIX_NO_ASH)
		return;

	/* Set */
	GlbEmWindowManager = ProcessId;
}

/* Write data to pointer pipe */
void EmCreateEvent(MEventMessageBase_t *Event)
{
	/* Temp Buffer */
	uint8_t NotRecycleBin[64];

	/* Sanity */
	if (GlbEmInitialized != 1)
		EmInit();

	/* Sanity - More ! */
	if (GlbEmWindowManager != PHOENIX_NO_ASH)
	{
		/* Get process */
		MCoreAsh_t *Ash = PhoenixGetAsh(GlbEmWindowManager);
		
		/* Force space in buffer */
		while (PipeBytesLeft(Ash->Pipe) < (int)(Event->Length))
			PipeRead(Ash->Pipe, Event->Length, (uint8_t*)&NotRecycleBin, 0);

		/* Set sender as system */
		Event->Sender = 0;
		
		/* Write data to pipe */
		PipeWrite(Ash->Pipe, Event->Length, (uint8_t*)Event);
	}
}