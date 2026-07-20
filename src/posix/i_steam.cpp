/*
** i_steam.cpp
**
**---------------------------------------------------------------------------
** Copyright 2013 Braden Obrzut
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
**
*/

#include <limits.h>
#include <stdlib.h>
#include <utility>

#ifdef __APPLE__
#include "m_misc.h"
#endif // __APPLE__

#include "cmdlib.h"
#include "common/platform/steam_iwad_paths.h"

namespace
{
void PushSteamRoot(TArray<FString>& roots, const FString& candidate)
{
	if (candidate.IsEmpty() || !DirExists(candidate.GetChars())) return;

	FString normalized = candidate;
	char resolved[PATH_MAX];
	if (realpath(candidate.GetChars(), resolved) != nullptr) normalized = resolved;
	normalized.ReplaceChars('\\', '/');
	while (normalized.Len() > 1 && normalized.Back() == '/')
	{
		normalized.Truncate(normalized.Len() - 1);
	}

	for (const FString& root : roots)
	{
		if (root.CompareNoCase(normalized) == 0) return;
	}
	roots.Push(std::move(normalized));
}

void PushEnvironmentRoot(TArray<FString>& roots, const char* name)
{
	const char* value = getenv(name);
	if (value != nullptr && *value != 0) PushSteamRoot(roots, value);
}
}

TArray<FString> I_GetSteamPath()
{
	TArray<FString> steamRoots;
	PushEnvironmentRoot(steamRoots, "STEAM_DIR");
	PushEnvironmentRoot(steamRoots, "STEAM_HOME");

#ifdef __APPLE__
	const FString appSupportPath = M_GetMacAppSupportPath();
	PushSteamRoot(steamRoots, appSupportPath + "/Steam");
#else
	const char* home = getenv("HOME");
	const char* xdgDataHome = getenv("XDG_DATA_HOME");
	if (xdgDataHome != nullptr && *xdgDataHome != 0)
	{
		PushSteamRoot(steamRoots, FStringf("%s/Steam", xdgDataHome));
		PushSteamRoot(steamRoots, FStringf("%s/steam", xdgDataHome));
	}
	if (home != nullptr && *home != 0)
	{
		// Native Valve, distribution-packaged, XDG, Flatpak, and Snap layouts.
		PushSteamRoot(steamRoots, FStringf("%s/.steam/debian-installation", home));
		PushSteamRoot(steamRoots, FStringf("%s/.steam/root", home));
		PushSteamRoot(steamRoots, FStringf("%s/.steam/steam", home));
		PushSteamRoot(steamRoots, FStringf("%s/.steam", home));
		PushSteamRoot(steamRoots, FStringf("%s/.local/share/Steam", home));
		PushSteamRoot(steamRoots, FStringf("%s/.local/share/steam", home));
		PushSteamRoot(steamRoots, FStringf("%s/.var/app/com.valvesoftware.Steam/.steam/steam", home));
		PushSteamRoot(steamRoots, FStringf("%s/.var/app/com.valvesoftware.Steam/.local/share/Steam", home));
		PushSteamRoot(steamRoots, FStringf("%s/snap/steam/common/.local/share/Steam", home));
	}
#endif

	return I_GetSteamGameSearchPaths(steamRoots);
}

TArray<FString> I_GetGogPaths()
{
	// GOG's Doom games are Windows only at the moment
	return TArray<FString>();
}
