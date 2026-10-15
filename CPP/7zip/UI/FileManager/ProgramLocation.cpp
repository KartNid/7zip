// ProgramLocation.cpp

#include "StdAfx.h"

#include "../../../Windows/DLL.h"
#include "../../../Windows/Registry.h"

#include "../../../Common/StringConvert.h"

#include "ProgramLocation.h"

using namespace NWindows;

static bool Path_Exists(const UString &path)
{
  return (::GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES);
}

static void AddDirCandidate(UString &best, const UString &dir, const wchar_t *exeName)
{
  if (dir.IsEmpty())
    return;
  UString p = dir;
  if (p.Back() != L'\\' && p.Back() != L'/')
    p.Add_PathSepar();
  p += exeName;
  if (Path_Exists(p))
    best = p;
}

static bool ReadRegInstallDir(HKEY root, UString &dir)
{
  NRegistry::CKey key;
  if (key.Open(root, TEXT("Software\\7-Zip"), KEY_READ) != ERROR_SUCCESS)
    return false;
#ifdef _WIN64
  if (key.QueryValue(TEXT("Path64"), dir) == ERROR_SUCCESS)
    if (!dir.IsEmpty())
      return true;
#endif
  if (key.QueryValue(TEXT("Path"), dir) == ERROR_SUCCESS)
    return !dir.IsEmpty();
  return false;
}

UString Get7zProgramPath(const wchar_t *exeName)
{
  UString result = fs2us(NDLL::GetModuleDirPrefix()) + exeName;
  if (Path_Exists(result))
    return result;

  UString dir;
  if (ReadRegInstallDir(HKEY_LOCAL_MACHINE, dir))
    AddDirCandidate(result, dir, exeName);
  if (ReadRegInstallDir(HKEY_CURRENT_USER, dir))
    AddDirCandidate(result, dir, exeName);

  return result;
}
