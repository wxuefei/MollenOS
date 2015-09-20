/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS Input Manager
*/

#ifndef _MCORE_INPUT_MANAGER_H_
#define _MCORE_INPUT_MANAGER_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Structures */
typedef struct _ImPointerEvent
{
	/* Pointer Type */
	uint32_t Type;

	/* Axis Data 
	 * Must be relative */
	int32_t xRelative;
	int32_t yRelative;
	int32_t zRelative;

	/* Rotation Data */

} ImPointerEvent_t;

/* Input Types */
#define MCORE_INPUT_TYPE_UNKNOWN	0x0
#define MCORE_INPUT_TYPE_MOUSE		0x1
#define MCORE_INPUT_TYPE_KEYBOARD	0x2
#define MCORE_INPUT_TYPE_KEYPAD		0x3
#define MCORE_INPUT_TYPE_JOYSTICK	0x4
#define MCORE_INPUT_TYPE_GAMEPAD	0x5
#define MCORE_INPUT_TYPE_OTHER		0x6

typedef struct _ImButtonEvent
{
	/* Button Type */
	uint32_t Type;

	/* Button Data (Keycode) */
	uint32_t Data;

	/* Button State (Press / Release) */
	uint32_t State;

} ImButtonEvent_t;

/* Event Types */
#define MCORE_INPUT_LEFT_MOUSEBUTTON	0x1
#define MCORE_INPUT_RIGHT_MOUSEBUTTON	0x2
#define MCORE_INPUT_MIDDLE_MOUSEBUTTON	0x4

#define MCORE_INPUT_BUTTON_RELEASED		0x0
#define MCORE_INPUT_BUTTON_CLICKED		0x1

/* Prototypes */

/* Write data to pointer pipe */
_CRT_EXTERN void InputManagerCreatePointerEvent(ImPointerEvent_t *Event);

/* Write data to button pipe */
_CRT_EXTERN void InputManagerCreateButtonEvent(ImButtonEvent_t *Event);

#endif // !_MCORE_INPUT_MANAGER_H_