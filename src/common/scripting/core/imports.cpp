/*
** thingdef_data.cpp
**
** DECORATE data tables
**
**---------------------------------------------------------------------------
** Copyright 2002-2020 Christoph Oelckers
** Copyright 2004-2008 Randy Heit
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
*/

#include "gstrings.h"
#include "v_font.h"
#include "menu.h"
#include "types.h"
#include "dictionary.h"
#include "vm.h"
#include "symbols.h"

static TArray<AFuncDesc> AFTable;
static TArray<FieldDesc> FieldTable;


//==========================================================================
//
//
//
//==========================================================================

template <typename Desc>
static int CompareClassNames(const char* const aname, const Desc& b)
{
	// ++ to get past the prefix letter of the native class name, which gets omitted by the FName for the class.
	const char* bname = b.ClassName;
	if ('\0' != *bname) ++bname;
	return stricmp(aname, bname);
}

template <typename Desc>
static int CompareClassNames(const Desc& a, const Desc& b)
{
	// ++ to get past the prefix letter of the native class name, which gets omitted by the FName for the class.
	const char* aname = a.ClassName;
	if ('\0' != *aname) ++aname;
	return CompareClassNames(aname, b);
}

//==========================================================================
//
// Find a function by name using a binary search
//
//==========================================================================

AFuncDesc *FindFunction(PContainerType *cls, const char * string)
{
	int min = 0, max = AFTable.Size() - 1;

	while (min <= max)
	{
		int mid = (min + max) / 2;
		int lexval = CompareClassNames(cls->TypeName.GetChars(), AFTable[mid]);
		if (lexval == 0) lexval = stricmp(string, AFTable[mid].FuncName);
		if (lexval == 0)
		{
			return &AFTable[mid];
		}
		else if (lexval > 0)
		{
			min = mid + 1;
		}
		else
		{
			max = mid - 1;
		}
	}
	return nullptr;
}

// Try a loose match if the class-scoped lookup fails: match by function name only.
// This is called by the higher-level binding code when an exact match cannot be
// found. It reduces the chance of "function not exported" errors when a mixin
// declares private native functions that the engine implements for base classes.
AFuncDesc *FindFunctionLoose(const char * string);

// Broad fallback: if exact lookup failed, try to find any native with the same
// function name regardless of class. This allows engine natives defined for
// base engine classes (e.g. Actor) to satisfy private native declarations
// attached to mod classes (e.g. NeoPlayer) when no exact match exists.
AFuncDesc *FindFunctionLoose(const char * string)
{
	for (auto &afd : AFTable)
	{
		if (stricmp(afd.FuncName, string) == 0)
		{
			return &afd;
		}
	}
	return nullptr;
}

// Fallback lookup: try to find a native function exported for an ancestor/native base class
AFuncDesc *FindFunctionFallback(PContainerType *cls, const char * string)
{
	// Try to resolve the class into a PClass so we can walk its parent chain
	PClass *scriptClass = nullptr;

	// cls->TypeName holds the class name used by the compiler. Try to find the corresponding PClass.
	if (cls != nullptr && cls->TypeName != NAME_None)
	{
		scriptClass = PClass::FindClass(cls->TypeName);
	}

	// If we found a script class, walk its parent chain and try to match an AF entry whose class name
	// (skipping the leading native prefix letter) equals the parent class's TypeName.
	if (scriptClass != nullptr)
	{
		for (PClass *p = scriptClass; p != nullptr; p = p->ParentClass)
		{
			const char *parentName = p->TypeName.GetChars();
			if (parentName == nullptr) continue;

			// Linear scan of AFTable to find the function name and matching native class
			for (auto &afd : AFTable)
			{
				const char *afClass = afd.ClassName;
				if (afClass == nullptr) continue;
				// Skip prefix letter if present
				if (*afClass != '\0') afClass++;
				if (stricmp(afClass, parentName) == 0 && stricmp(afd.FuncName, string) == 0)
				{
					return &afd;
				}
			}
		}
	}

	return nullptr;
}

//==========================================================================
//
// Find a function by name using a binary search
//
//==========================================================================

FieldDesc *FindField(PContainerType *cls, const char * string)
{
	int min = 0, max = FieldTable.Size() - 1;
	const char * cname = cls ? cls->TypeName.GetChars() : "";

	while (min <= max)
	{
		int mid = (min + max) / 2;
		int lexval = CompareClassNames(cname, FieldTable[mid]);
		if (lexval == 0) lexval = stricmp(string, FieldTable[mid].FieldName);
		if (lexval == 0)
		{
			return &FieldTable[mid];
		}
		else if (lexval > 0)
		{
			min = mid + 1;
		}
		else
		{
			max = mid - 1;
		}
	}
	return nullptr;
}


//==========================================================================
//
// Find an action function in AActor's table
//
//==========================================================================

VMFunction *FindVMFunction(PClass *cls, const char *name)
{
	auto f = dyn_cast<PFunction>(cls->FindSymbol(name, true));
	return f == nullptr ? nullptr : f->Variants[0].Implementation;
}


//==========================================================================
//
// Find an action function in AActor's table from a qualified name
// This cannot search in structs. sorry. :(
//
//==========================================================================

VMFunction* FindVMFunction( const char* name)
{
	auto p = strchr(name, '.');
	if (p == nullptr) return nullptr;
	std::string clsname(name, p - name);
	auto cls = PClass::FindClass(clsname.c_str());
	if (cls == nullptr) return nullptr;
	return FindVMFunction(cls, p + 1);
}


//==========================================================================
//
// Sorting helpers
//
//==========================================================================

static int funccmp(const void * a, const void * b)
{
	int res = CompareClassNames(*(AFuncDesc*)a, *(AFuncDesc*)b);
	if (res == 0) res = stricmp(((AFuncDesc*)a)->FuncName, ((AFuncDesc*)b)->FuncName);
	return res;
}

static int fieldcmp(const void * a, const void * b)
{
	int res = CompareClassNames(*(FieldDesc*)a, *(FieldDesc*)b);
	if (res == 0) res = stricmp(((FieldDesc*)a)->FieldName, ((FieldDesc*)b)->FieldName);
	return res;
}

//==========================================================================
//
// Initialization
//
//==========================================================================

void InitImports()
{
	auto fontstruct = NewStruct("Font", nullptr, true);
	fontstruct->Size = sizeof(FFont);
	fontstruct->Align = alignof(FFont);
	NewPointer(fontstruct, false)->InstallHandlers(
		[](FSerializer &ar, const char *key, const void *addr)
		{
			ar(key, *(FFont **)addr);
		},
			[](FSerializer &ar, const char *key, void *addr)
		{
			Serialize<FFont>(ar, key, *(FFont **)addr, nullptr);
			return true;
		}
	);

	// Create a sorted list of native action functions
	AFTable.Clear();
	if (AFTable.Size() == 0)
	{
		AutoSegs::ActionFunctons.ForEach([](AFuncDesc *afunc)
		{
			assert(afunc->VMPointer != NULL);
			*(afunc->VMPointer) = new VMNativeFunction(afunc->Function, afunc->FuncName);
			(*(afunc->VMPointer))->QualifiedName = ClassDataAllocator.Strdup(FStringf("%s.%s", afunc->ClassName + 1, afunc->FuncName).GetChars());
			(*(afunc->VMPointer))->PrintableName = ClassDataAllocator.Strdup(FStringf("%s.%s [Native]", afunc->ClassName+1, afunc->FuncName).GetChars());
			(*(afunc->VMPointer))->DirectNativeCall = afunc->DirectNative;
			AFTable.Push(*afunc);
		});
		AFTable.ShrinkToFit();
			// Debug: list registered native functions to help diagnose lookup issues
			printf("InitImports: Registered %d native functions:\n", AFTable.Size());
			for (auto &afd : AFTable)
			{
				const char *cn = afd.ClassName ? afd.ClassName : "";
				// Skip leading native prefix letter when printing
				if (*cn != '\0') cn++;
				printf("  AF: class='%s' func='%s'\n", cn, afd.FuncName);
			}

		// Ensure GLTF native functions are present in the AFTable. Some build/linker
		// setups may not place the automatically generated AFuncDesc pointers into
		// the auto-seg table correctly; if the GLTF natives exist as static hooks
		// we add them explicitly here so scripts can bind to them.
		extern AFuncDesc const *const AActor_NativePlayAnimation_HookPtr;
		extern AFuncDesc const *const AActor_NativeStopAnimation_HookPtr;
		extern AFuncDesc const *const AActor_NativePauseAnimation_HookPtr;
		extern AFuncDesc const *const AActor_NativeResumeAnimation_HookPtr;
		extern AFuncDesc const *const AActor_NativeSetAnimationSpeed_HookPtr;
		extern AFuncDesc const *const AActor_NativeSetPBREnabled_HookPtr;
		extern AFuncDesc const *const AActor_NativeSetMetallicFactor_HookPtr;
		extern AFuncDesc const *const AActor_NativeSetRoughnessFactor_HookPtr;
		extern AFuncDesc const *const AActor_NativeSetEmissive_HookPtr;
		extern AFuncDesc const *const AActor_NativeUpdateModel_HookPtr;

		auto EnsurePush = [&AFTable](AFuncDesc const *const *hookPtr)
		{
			if (hookPtr == nullptr || *hookPtr == nullptr) return;
			const AFuncDesc &hv = **hookPtr;
			// check if an equivalent entry already exists
			for (auto &e : AFTable) if (stricmp(e.FuncName, hv.FuncName) == 0 && stricmp(e.ClassName + (e.ClassName && *e.ClassName ? 1 : 0), hv.ClassName + (hv.ClassName && *hv.ClassName ? 1 : 0)) == 0) return;
			AFTable.Push(hv);
		};

		EnsurePush(&AActor_NativePlayAnimation_HookPtr);
		EnsurePush(&AActor_NativeStopAnimation_HookPtr);
		EnsurePush(&AActor_NativePauseAnimation_HookPtr);
		EnsurePush(&AActor_NativeResumeAnimation_HookPtr);
		EnsurePush(&AActor_NativeSetAnimationSpeed_HookPtr);
		EnsurePush(&AActor_NativeSetPBREnabled_HookPtr);
		EnsurePush(&AActor_NativeSetMetallicFactor_HookPtr);
		EnsurePush(&AActor_NativeSetRoughnessFactor_HookPtr);
		EnsurePush(&AActor_NativeSetEmissive_HookPtr);
		EnsurePush(&AActor_NativeUpdateModel_HookPtr);

		// Diagnostic: print addresses of AutoSegs collector and the GLTF hook pointers
		// This helps determine whether the hook pointers live in the auto-seg region
		// and are discoverable by the AutoSegs collector.
		printf("InitImports: AutoSegs::ActionFunctons addr=%p\n", (void*)&AutoSegs::ActionFunctons);
		printf("InitImports: symbol AActor_NativePlayAnimation_HookPtr addr=%p value=%p\n", (void*)&AActor_NativePlayAnimation_HookPtr, (void*)AActor_NativePlayAnimation_HookPtr);
		printf("InitImports: symbol AActor_NativeStopAnimation_HookPtr addr=%p value=%p\n", (void*)&AActor_NativeStopAnimation_HookPtr, (void*)AActor_NativeStopAnimation_HookPtr);
		printf("InitImports: symbol AActor_NativePauseAnimation_HookPtr addr=%p value=%p\n", (void*)&AActor_NativePauseAnimation_HookPtr, (void*)AActor_NativePauseAnimation_HookPtr);
		printf("InitImports: symbol AActor_NativeResumeAnimation_HookPtr addr=%p value=%p\n", (void*)&AActor_NativeResumeAnimation_HookPtr, (void*)AActor_NativeResumeAnimation_HookPtr);
		printf("InitImports: symbol AActor_NativeSetAnimationSpeed_HookPtr addr=%p value=%p\n", (void*)&AActor_NativeSetAnimationSpeed_HookPtr, (void*)AActor_NativeSetAnimationSpeed_HookPtr);
		printf("InitImports: symbol AActor_NativeSetPBREnabled_HookPtr addr=%p value=%p\n", (void*)&AActor_NativeSetPBREnabled_HookPtr, (void*)AActor_NativeSetPBREnabled_HookPtr);
		printf("InitImports: symbol AActor_NativeSetMetallicFactor_HookPtr addr=%p value=%p\n", (void*)&AActor_NativeSetMetallicFactor_HookPtr, (void*)AActor_NativeSetMetallicFactor_HookPtr);
		printf("InitImports: symbol AActor_NativeSetRoughnessFactor_HookPtr addr=%p value=%p\n", (void*)&AActor_NativeSetRoughnessFactor_HookPtr, (void*)AActor_NativeSetRoughnessFactor_HookPtr);
		printf("InitImports: symbol AActor_NativeSetEmissive_HookPtr addr=%p value=%p\n", (void*)&AActor_NativeSetEmissive_HookPtr, (void*)AActor_NativeSetEmissive_HookPtr);
		printf("InitImports: symbol AActor_NativeUpdateModel_HookPtr addr=%p value=%p\n", (void*)&AActor_NativeUpdateModel_HookPtr, (void*)AActor_NativeUpdateModel_HookPtr);
		qsort(&AFTable[0], AFTable.Size(), sizeof(AFTable[0]), funccmp);
	}

	FieldTable.Clear();
	if (FieldTable.Size() == 0)
	{
		AutoSegs::ClassFields.ForEach([](FieldDesc *afield)
		{
			FieldTable.Push(*afield);
		});
		FieldTable.ShrinkToFit();
		qsort(&FieldTable[0], FieldTable.Size(), sizeof(FieldTable[0]), fieldcmp);
	}
}

