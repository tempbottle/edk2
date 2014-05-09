/** @file
*
*  Shell command for launching AXF files.
*
*  Copyright (c) 2014, ARM Limited. All rights reserved.
*
*  This program and the accompanying materials
*  are licensed and made available under the terms and conditions of the BSD License
*  which accompanies this distribution.  The full text of the license may be found at
*  http://opensource.org/licenses/bsd-license.php
*
*  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
*  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*
**/

#include "UefiRunAxfLib.h"
#include <Guid/GlobalVariable.h>
#include <Library/PrintLib.h>
#include <Library/HandleParsingLib.h>
#include <Library/DevicePathLib.h>
#include <Library/ShellLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>

#include <ShellBase.h>

#include <Library/ArmLib.h>
#include "ElfLoader.h"

// Provide arguments to AXF?
typedef VOID (*ELF_ENTRYPOINT)(UINTN arg0, UINTN arg1,
                               UINTN arg2, UINTN arg3);


STATIC
EFI_STATUS
ShutdownUefiBootServices (
  VOID
  )
{
  EFI_STATUS              Status;
  UINTN                   MemoryMapSize;
  EFI_MEMORY_DESCRIPTOR   *MemoryMap;
  UINTN                   MapKey;
  UINTN                   DescriptorSize;
  UINT32                  DescriptorVersion;
  UINTN                   Pages;

  MemoryMap = NULL;
  MemoryMapSize = 0;
  Pages = 0;

  do {
    Status = gBS->GetMemoryMap (
                    &MemoryMapSize,
                    MemoryMap,
                    &MapKey,
                    &DescriptorSize,
                    &DescriptorVersion
                    );
    if (Status == EFI_BUFFER_TOO_SMALL) {

      Pages = EFI_SIZE_TO_PAGES (MemoryMapSize) + 1;
      MemoryMap = AllocatePages (Pages);

      //
      // Get System MemoryMap
      //
      Status = gBS->GetMemoryMap (
                      &MemoryMapSize,
                      MemoryMap,
                      &MapKey,
                      &DescriptorSize,
                      &DescriptorVersion
                      );
    }

    // Don't do anything between the GetMemoryMap() and ExitBootServices()
    if (!EFI_ERROR (Status)) {
      Status = gBS->ExitBootServices (gImageHandle, MapKey);
      if (EFI_ERROR (Status)) {
        FreePages (MemoryMap, Pages);
        MemoryMap = NULL;
        MemoryMapSize = 0;
      }
    }
  } while (EFI_ERROR (Status));

  return Status;
}


STATIC
EFI_STATUS
PreparePlatformHardware (
  VOID
  )
{
  //Note: Interrupts will be disabled by the GIC driver when ExitBootServices() will be called.

  // Clean before Disable else the Stack gets corrupted with old data.
  ArmCleanDataCache ();
  ArmDisableDataCache ();
  // Invalidate all the entries that might have snuck in.
  ArmInvalidateDataCache ();

  // Disable and invalidate the instruction cache
  ArmDisableInstructionCache ();
  ArmInvalidateInstructionCache ();

  // Turn off MMU
  ArmDisableMmu();

  return EFI_SUCCESS;
}

// Process arguments to pass to AXF?
STATIC CONST SHELL_PARAM_ITEM ParamList[] = {
  {NULL, TypeMax}
};


/**
  Load and start an ARM Executable File (AXF).

  If this function succeeds in loading the AXF it is not expected to ever return
  to UEFI.

  @param[in]  ImageHandle     The image handle.
  @param[in]  SystemTable     The system table.

  @retval SHELL_SUCCESS            Command completed successfully.
  @retval SHELL_INVALID_PARAMETER  Command usage error.
  @retval SHELL_DEVICE_ERROR       Failed to load file.
**/
SHELL_STATUS
EFIAPI
ShellCommandRunRunAxf (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  LIST_ENTRY        *ParamPackage;
  EFI_STATUS         Status;
  SHELL_STATUS       ShellStatus;
  SHELL_FILE_HANDLE  FileHandle;
  ELF_ENTRYPOINT     StartElf;
  CONST CHAR16      *FileName;
  EFI_FILE_INFO     *Info;
  UINTN              FileSize;
  UINTN              LoadedSize;
  VOID              *FileData;
  VOID              *Entrypoint;

  ShellStatus = SHELL_SUCCESS;

  //
  // Initialize the shell lib (we must be in non-auto-init...)
  //
  Status = ShellInitialize ();
  ASSERT_EFI_ERROR (Status);

  //
  // Fix local copies of the protocol pointers
  //
  Status = CommandInit ();
  ASSERT_EFI_ERROR (Status);

  // Add support to load AXF with optipnal args?

  //
  // Process Command Line arguments
  //
  Status = ShellCommandLineParse (ParamList, &ParamPackage, NULL, TRUE);
  if (EFI_ERROR (Status)) {
    ShellPrintHiiEx (-1, -1, NULL, STRING_TOKEN (STR_RUNAXF_INVALID_ARG), gRunAxfHiiHandle);
    ShellStatus = SHELL_INVALID_PARAMETER;
  } else {
    //
    // Check for "-?"
    //
    if ((ShellCommandLineGetFlag (ParamPackage, L"-?")) ||
        (ShellCommandLineGetRawValue (ParamPackage, 1) == NULL)) {
      //
      // We didn't get a file to load
      //
      ShellPrintHiiEx (-1, -1, NULL, STRING_TOKEN (STR_RUNAXF_INVALID_ARG), gRunAxfHiiHandle);
      ShellStatus = SHELL_INVALID_PARAMETER;
    } else {
      // For the moment we assume we only ever get one file to load with no arguments.
      FileName = ShellCommandLineGetRawValue (ParamPackage, 1);
      Status = ShellOpenFileByName (FileName, &FileHandle, EFI_FILE_MODE_READ, 0);
      if (EFI_ERROR(Status)) {
        ShellPrintHiiEx (-1, -1, NULL, STRING_TOKEN (STR_RUNAXF_FILE_NOT_FOUND), gRunAxfHiiHandle);
        ShellStatus = SHELL_INVALID_PARAMETER;
      } else {
        Info = ShellGetFileInfo (FileHandle);
        FileSize = (UINTN) Info->FileSize;
        FreePool (Info);

        //
        // Allocate buffer to read file
        //
        FileData = AllocateZeroPool (FileSize);
        if (FileData == NULL) {
          ShellPrintHiiEx (-1, -1, NULL, STRING_TOKEN (STR_RUNAXF_NO_MEM), gRunAxfHiiHandle);
          ShellStatus = SHELL_OUT_OF_RESOURCES;
        } else {
          //
          // Read file into Buffer
          //
          Status = ShellReadFile (FileHandle, &FileSize, FileData);
          ShellCloseFile (&FileHandle);
          FileHandle = NULL;
          if (EFI_ERROR (Status)) {
            ShellPrintHiiEx (-1, -1, NULL, STRING_TOKEN (STR_RUNAXF_READ_FAIL), gRunAxfHiiHandle);
            SHELL_FREE_NON_NULL (FileData);
            ShellStatus = SHELL_DEVICE_ERROR;
          } else {
            //  Do some validation on the ELF file before we hit the point
            //  of no return.
            Status = ElfCheckFile (FileData);
            if (EFI_ERROR (Status)) {
              ShellPrintHiiEx (-1, -1, NULL, STRING_TOKEN (STR_RUNAXF_BAD_FILE), gRunAxfHiiHandle);
              SHELL_FREE_NON_NULL (FileData);
              ShellStatus = SHELL_UNSUPPORTED;
            } else {
              // Exit boot services here. This means we cannot return, but also that
              // we have to worry less about overwritting UEFI code in order to get
              // the ELF to load properly.
              Status = ShutdownUefiBootServices ();
              if (EFI_ERROR (Status)) {
                DEBUG ((EFI_D_ERROR,"ERROR: Can not shutdown UEFI boot services. Status=0x%X\n",
                        Status));
                SHELL_FREE_NON_NULL (FileData);
              } else {
                // We cannot return now. DEBUG messages don't work anymore either.
                Status = ElfLoadFile ((VOID*)FileData, &Entrypoint, &LoadedSize);
                ASSERT_EFI_ERROR (Status);

                //
                // Switch off interrupts, caches, mmu, etc
                //
                Status = PreparePlatformHardware ();
                ASSERT_EFI_ERROR (Status);

                StartElf = (ELF_ENTRYPOINT)Entrypoint;
                StartElf (0,0,0,0);

                // We should never get here.. But if we do, spin..
                ASSERT (FALSE);
                while (1);
              }
            }
          }
        }
      }
    }

    //
    // Free the command line package
    //
    ShellCommandLineFreeVarList (ParamPackage);
  }

  if (EFI_ERROR (Status) && ShellStatus == SHELL_SUCCESS) {
    ShellStatus = SHELL_DEVICE_ERROR;
  }

  return ShellStatus;
}
