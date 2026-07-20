/*
** steam_iwad_paths.cpp
** Cross-platform Steam library and IWAD directory discovery
**
**---------------------------------------------------------------------------
** Copyright 2026 BiasedDoom contributors
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
*/

#include "steam_iwad_paths.h"

#include <ctype.h>
#include <utility>

#include "cmdlib.h"
#include "engineerrors.h"
#include "sc_man.h"

namespace
{
struct SteamIWADLocation
{
	int AppId;
	const char* DefaultInstallDir;
	const char* RelativePath;
};

// Relative paths are deliberately narrow. Recursively searching an entire
// Steam library can walk terabytes of unrelated games and misclassify PWADs.
// App manifests let localized or renamed installation directories work while
// these entries identify the small folders that can actually hold IWADs.
constexpr SteamIWADLocation SteamIWADLocations[] =
{
	{ 2280, "Ultimate Doom", "base" },
	{ 2280, "Ultimate Doom", "base/doom2" },
	{ 2280, "Ultimate Doom", "base/tnt" },
	{ 2280, "Ultimate Doom", "base/plutonia" },
	{ 2280, "Ultimate Doom", "rerelease/DOOM_Data/StreamingAssets" },
	{ 2280, "Ultimate Doom", "rerelease" },
	{ 2300, "Doom 2", "base" },
	{ 2300, "Doom 2", "rerelease/DOOM II_Data/StreamingAssets" },
	{ 2300, "Doom 2", "finaldoombase" },
	{ 2290, "Final Doom", "base" },
	{ 2390, "Heretic Shadow of the Serpent Riders", "base" },
	{ 2360, "Hexen", "base" },
	{ 2370, "Hexen Deathkings of the Dark Citadel", "base" },
	{ 208200, "DOOM 3 BFG Edition", "base/wads" },
	{ 317040, "Strife", "" },
	{ 9160, "Master Levels of Doom", "doom2" },
	{ 3286930, "Heretic + Hexen", "dos/base/heretic" },
	{ 3286930, "Heretic + Hexen", "dos/base/hexen" },
	{ 3286930, "Heretic + Hexen", "dos/base/hexendk" },
};

void NormalizePath(FString& path)
{
	path.ReplaceChars('\\', '/');
	while (path.Len() > 1 && path.Back() == '/')
	{
		path.Truncate(path.Len() - 1);
	}
}

void PushUnique(TArray<FString>& paths, FString path, bool mustExist)
{
	NormalizePath(path);
	if (path.IsEmpty() || (mustExist && !DirExists(path.GetChars()))) return;

	for (const FString& existing : paths)
	{
		if (existing.CompareNoCase(path) == 0) return;
	}
	paths.Push(std::move(path));
}

bool IsDecimal(const char* text)
{
	if (text == nullptr || *text == 0) return false;
	for (; *text != 0; ++text)
	{
		if (!isdigit(static_cast<unsigned char>(*text))) return false;
	}
	return true;
}

bool IsSafeInstallDir(const FString& installDir)
{
	// appmanifest values are expected to name one child of steamapps/common.
	// Do not let malformed or locally edited metadata escape that directory.
	return installDir.IsNotEmpty()
		&& installDir.Compare(".") != 0
		&& installDir.Compare("..") != 0
		&& installDir.IndexOf('/') < 0
		&& installDir.IndexOf('\\') < 0;
}

void SkipVDFBlock(FScanner& sc)
{
	int depth = 1;
	while (depth > 0 && sc.GetToken())
	{
		if (sc.TokenType == '{') ++depth;
		else if (sc.TokenType == '}') --depth;
	}
}

FString ReadLibraryEntry(FScanner& sc)
{
	FString path;
	while (sc.GetToken() && sc.TokenType != '}')
	{
		sc.TokenMustBe(TK_StringConst);
		FString key(sc.String);
		if (sc.CheckToken('{'))
		{
			SkipVDFBlock(sc);
		}
		else
		{
			sc.MustGetToken(TK_StringConst);
			if (key.CompareNoCase("path") == 0) path = sc.String;
		}
	}
	return path;
}

TArray<FString> ParseLibraryFolders(const FString& filename)
{
	TArray<FString> result;
	FScanner sc;
	if (!sc.OpenFile(filename.GetChars())) return result;
	sc.SetCMode(true);

	sc.MustGetToken(TK_StringConst);
	sc.MustGetToken('{');
	while (sc.GetToken() && sc.TokenType != '}')
	{
		sc.TokenMustBe(TK_StringConst);
		FString key(sc.String);
		if (sc.CheckToken('{'))
		{
			FString path = ReadLibraryEntry(sc);
			if (IsDecimal(key.GetChars())) PushUnique(result, path, true);
		}
		else
		{
			sc.MustGetToken(TK_StringConst);
			// Old Steam clients stored numeric-key/path pairs directly in the
			// libraryfolders block instead of nested records.
			if (IsDecimal(key.GetChars())) PushUnique(result, sc.String, true);
		}
	}
	return result;
}

TArray<FString> ParseLegacyConfigLibraries(const FString& filename)
{
	TArray<FString> result;
	FScanner sc;
	if (!sc.OpenFile(filename.GetChars())) return result;
	sc.SetCMode(true);

	while (sc.GetToken())
	{
		if (sc.TokenType != TK_StringConst) continue;
		FString key(sc.String);
		if (key.Left(18).CompareNoCase("BaseInstallFolder_") != 0) continue;
		sc.MustGetToken(TK_StringConst);
		PushUnique(result, sc.String, true);
	}
	return result;
}

FString ParseInstallDir(const FString& filename)
{
	FScanner sc;
	if (!sc.OpenFile(filename.GetChars())) return "";
	sc.SetCMode(true);

	sc.MustGetToken(TK_StringConst);
	sc.MustGetToken('{');
	while (sc.GetToken() && sc.TokenType != '}')
	{
		sc.TokenMustBe(TK_StringConst);
		FString key(sc.String);
		if (sc.CheckToken('{'))
		{
			SkipVDFBlock(sc);
		}
		else
		{
			sc.MustGetToken(TK_StringConst);
			if (key.CompareNoCase("installdir") == 0) return FString(sc.String);
		}
	}
	return "";
}

void AppendParsedLibraries(TArray<FString>& libraries, const FString& filename, bool legacyConfig)
{
	try
	{
		TArray<FString> parsed = legacyConfig
			? ParseLegacyConfigLibraries(filename)
			: ParseLibraryFolders(filename);
		for (FString& path : parsed) PushUnique(libraries, std::move(path), true);
	}
	catch (const CRecoverableError&)
	{
		// Steam rewrites these files in place. A truncated or newer-format file
		// must not prevent fallback roots and other libraries from being used.
	}
}

FString FindInstallDir(const FString& library, int appId, const char* fallback)
{
	FString manifest;
	manifest.Format("%s/steamapps/appmanifest_%d.acf", library.GetChars(), appId);
	try
	{
		FString installDir = ParseInstallDir(manifest);
		if (IsSafeInstallDir(installDir)) return installDir;
	}
	catch (const CRecoverableError&)
	{
	}
	return fallback;
}
}

TArray<FString> I_GetSteamGameSearchPaths(const TArray<FString>& steamRoots)
{
	TArray<FString> libraries;
	for (const FString& rootValue : steamRoots)
	{
		FString root = rootValue;
		NormalizePath(root);
		if (!DirExists(root.GetChars())) continue;
		PushUnique(libraries, root, true);
		AppendParsedLibraries(libraries, root + "/steamapps/libraryfolders.vdf", false);
		AppendParsedLibraries(libraries, root + "/config/libraryfolders.vdf", false);
		AppendParsedLibraries(libraries, root + "/config/config.vdf", true);
	}

	TArray<FString> result;
	for (const FString& library : libraries)
	{
		for (const SteamIWADLocation& location : SteamIWADLocations)
		{
			FString installDir = FindInstallDir(library, location.AppId, location.DefaultInstallDir);
			FString candidate;
			candidate.Format("%s/steamapps/common/%s", library.GetChars(), installDir.GetChars());
			if (*location.RelativePath != 0)
			{
				candidate += "/";
				candidate += location.RelativePath;
			}
			PushUnique(result, std::move(candidate), true);
		}
	}
	return result;
}
