// ProgramLocation.h

#ifndef ZIP7_INC_PROGRAM_LOCATION_H
#define ZIP7_INC_PROGRAM_LOCATION_H

#include "../../../Common/MyString.h"

// Resolve 7-Zip program (7zG.exe / 7zFM.exe / ...) under the real install
// directory. Tries: directory of the calling module, then HKLM/HKCU
// Software\7-Zip Path64 / Path, then App Paths. Always returns a path
// (last resort = module dir + name) so callers can show it in errors.
UString Get7zProgramPath(const wchar_t *exeName);

#endif
