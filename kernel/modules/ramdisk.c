/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Kernel Module System
 *   - Implements loading and management of modules that exists on the initrd. 
 */

#define __MODULE "INRD"
//#define __TRACE

#include <modules/ramdisk.h>
#include <modules/modules.h>
#include <crc32.h>

/* ParseInitialRamdisk
 * Parses the supplied ramdisk by the bootloader. Without a ramdisk present only debug
 * functionality will be available. */
OsStatus_t
ParseInitialRamdisk(
    _In_ Multiboot_t* BootInformation)
{
    SystemRamdiskHeader_t* Ramdisk;
    SystemRamdiskEntry_t*  Entry;
    int                    Counter = 0;
    SystemModuleType_t     Type;

    TRACE("ParseInitialRamdisk(Address 0x%x, Size 0x%x)",
        BootInformation->RamdiskAddress, BootInformation->RamdiskSize);
    if (BootInformation->RamdiskAddress == 0 || BootInformation->RamdiskSize == 0) {
        return OsError;
    }
    
    // Initialize the pointer and read the signature value, must match
    Ramdisk = (SystemRamdiskHeader_t*)(uintptr_t)BootInformation->RamdiskAddress;
    if (Ramdisk->Magic != RAMDISK_MAGIC) {
        ERROR("Invalid magic in ramdisk - 0x%x", Ramdisk->Magic);
        return OsError;
    }
    if (Ramdisk->Version != RAMDISK_VERSION_1) {
        ERROR("Invalid ramdisk version - 0x%x", Ramdisk->Version);
        return OsError;
    }
    Entry   = (SystemRamdiskEntry_t*)
        (BootInformation->RamdiskAddress + sizeof(SystemRamdiskHeader_t));
    Counter = Ramdisk->FileCount;

    // Keep iterating untill we reach the end of counter
    TRACE("Parsing %i number of files in the ramdisk", Counter);
    while (Counter != 0) {
        if (Entry->Type == RAMDISK_MODULE || Entry->Type == RAMDISK_FILE) {
            SystemRamdiskModuleHeader_t* Header =
                (SystemRamdiskModuleHeader_t*)(uintptr_t)(BootInformation->RamdiskAddress + Entry->DataHeaderOffset);
            uint8_t* ModuleData;
            uint32_t CrcOfData;

            Type = ModuleResource;
            if (Header->Flags & RAMDISK_MODULE_SERVER) {
                Type = ServiceResource;
            }

            // Perform CRC validation
            ModuleData = (uint8_t*)(BootInformation->RamdiskAddress 
                + Entry->DataHeaderOffset + sizeof(SystemRamdiskModuleHeader_t));
            CrcOfData  = Crc32Generate(-1, ModuleData, Header->LengthOfData);
            if (CrcOfData == Header->Crc32OfData) {
                if (RegisterModule((const char*)&Entry->Name[0], (const void*)ModuleData, Type, Header->VendorId, 
                    Header->DeviceId, Header->DeviceType, Header->DeviceSubType) != OsSuccess) {
                    // ?
                    
                }
            }
            else
            {
                ERROR("CRC-Validation(%s): Failed (Calculated 0x%x != Stored 0x%x)",
                    &Entry->Name[0], CrcOfData, Header->Crc32OfData);
                break;
            }
        }
        else {
            WARNING("Unknown entry type: %u", Entry->Type);
        }
        Counter--;
        Entry++;
    }
    return OsSuccess;
}
