/** @file
  Main file for NULL named library for install1 shell command functions.

  Copyright (c) 2010 - 2013, Intel Corporation. All rights reserved.
  Copyright (c) 2014, ARM Limited. All rights reserved.
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "UefiRunAxfLib.h"

STATIC CONST CHAR16 mFileName[] = L"ShellCommands";
EFI_HANDLE gRunAxfHiiHandle = NULL;

#define RUNAXF_HII_GUID \
  { \
  0xf5a6413b, 0x78d5, 0x448e, { 0xa2, 0x15, 0x22, 0x82, 0x8e, 0xbc, 0x61, 0x61 } \
  }

EFI_GUID gRunAxfHiiGuid = RUNAXF_HII_GUID;

/**
  Function to get the filename with help context if HII will not be used.

  @return   The filename with help text in it.
**/
CONST CHAR16*
EFIAPI
UefiRunAxfLibGetManFileName (
  VOID
  )
{
  return (mFileName);
}

/**
  Constructor for the Shell Level 1 Commands library.

  Install the handlers for level 1 UEFI Shell 2.0 commands.

  @param ImageHandle    the image handle of the process
  @param SystemTable    the EFI System Table pointer

  @retval EFI_SUCCESS        the shell command handlers were installed sucessfully
  @retval EFI_UNSUPPORTED    the shell level required was not found.
**/
EFI_STATUS
EFIAPI
UefiRunAxfLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  //
  // 3rd parameter 'HII strings array' must be name of .uni strings file followed
  // by 'Strings', e.g. mycommands.uni must be specified as 'mycommandsStrings'
  // because the build Autogen process defines this as a string array for the strings
  // in your .uni file.  Examine your Build folder under your package's DEBUG folder
  // and you will find it defined in a xxxStrDefs.h file.
  //
  gRunAxfHiiHandle = HiiAddPackages (&gRunAxfHiiGuid, gImageHandle, UefiRunAxfLibStrings, NULL);
  if (gRunAxfHiiHandle == NULL) {
    return (EFI_DEVICE_ERROR);
  }

  //
  // Install our shell command handlers that are always installed
  //
  ShellCommandRegisterCommandName (L"runaxf", ShellCommandRunRunAxf, UefiRunAxfLibGetManFileName, 0, L"", FALSE, gRunAxfHiiHandle, STRING_TOKEN(STR_GET_HELP_RUNAXF));

  return (EFI_SUCCESS);
}

/**
  Destructor for the library.  free any resources.

  @param ImageHandle            The image handle of the process.
  @param SystemTable            The EFI System Table pointer.
**/
EFI_STATUS
EFIAPI
UefiRunAxfLibDestructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  if (gRunAxfHiiHandle != NULL) {
    HiiRemovePackages (gRunAxfHiiHandle);
  }
  return (EFI_SUCCESS);
}
