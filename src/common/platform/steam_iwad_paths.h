#pragma once

#include "tarray.h"
#include "zstring.h"

// Expands Steam installation roots into the app-specific directories that can
// contain supported IWADs. This understands modern/legacy libraryfolders.vdf,
// appmanifest installdir values, and the known layouts shipped by id Software.
TArray<FString> I_GetSteamGameSearchPaths(const TArray<FString>& steamRoots);
