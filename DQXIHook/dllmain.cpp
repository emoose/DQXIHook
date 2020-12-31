#include "stdafx.h"
#include <stdint.h>
#include <stdio.h>

#include "MinHook/MinHook.h"
#include "proxy.h"

bool FileExists(LPCWSTR path)
{
	DWORD dwAttrib = GetFileAttributesW(path);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

// uncomment SKIP_VERSION_CHECK if you're changing the addresses below
// re-comment once you update IsSupportedGameVersion()
//#define SKIP_VERSION_CHECK 1

char* exe_base = nullptr;

const size_t AddrStaticConstructObject_Internal = 0xF4E960;
const size_t AddrGEngine = 0x5DA17D8;
const size_t AddrUGameViewportClient__SetupInitialLocalPlayer = 0x1B02430;
const size_t OffsetUEngine_ConsoleClass = 0x108; // offset to ConsoleClass field inside UEngine class
const size_t OffsetUGameViewportClient_ViewportConsole = 0x48; // offset to ViewportConsole field inside UGameViewportClient class

typedef void* (*TStaticConstructObject_Internal)(void* InClass, void* InOuter, void* InName, void* InFlags, void* InternalSetFlags, void* InTemplate, bool bCopyTransientsFromClassDefaults, void* InInstanceGraph, bool bAssumeTemplateIsArchetype);
typedef void* (*TUGameViewportClient__SetupInitialLocalPlayer)(void* thisptr, void* OutError);
TUGameViewportClient__SetupInitialLocalPlayer RealUGameViewportClient__SetupInitialLocalPlayer;

// UGameViewportClient::SetupInitialLocalPlayer hook: in UE4 builds with ALLOW_CONSOLE=1, this function inits the UGameViewportClient::ViewportConsole field
// so we just recreate the code that does that, and luckily that's all we need to re-enable the console!
void* __fastcall HookUGameViewportClient__SetupInitialLocalPlayer(char* thisptr, void* OutError)
{
	// create UConsole class
	TStaticConstructObject_Internal StaticConstructObject_Internal = (TStaticConstructObject_Internal)(exe_base + AddrStaticConstructObject_Internal);

	char* engine = *(char**)(exe_base + AddrGEngine);
	void* consoleClass = *(void**)(engine + OffsetUEngine_ConsoleClass);

	void* console = StaticConstructObject_Internal(consoleClass, thisptr, 0, 0, 0, 0, 0, 0, 0);

	// set ViewportConsole field in this UGameViewportClient instance
	*(void**)(thisptr + OffsetUGameViewportClient_ViewportConsole) = console;

	// call rest of SetupInitialLocalPlayer
	auto ret = RealUGameViewportClient__SetupInitialLocalPlayer(thisptr, OutError);
	return ret;
}

const size_t AddrFPakPlatformFile__FindFileInPakFiles = 0x1FC7A30; // address of FPakPlatformFile::FindFileInPakFiles func (hooked)
const size_t AddrFPakPlatformFile__IsNonPakFilenameAllowed = 0x1FCA930; // address of FPakPlatformFile::IsNonPakFilenameAllowed func (hooked)
const size_t AddrWindowTitle = 0x3FC1BC8;

typedef void* (*TFPakPlatformFile__FindFileInPakFiles)(void* Paks, void* Filename, void** OutPakFile);
typedef void* (*TFPakPlatformFile__IsNonPakFilenameAllowed)(void* thisptr, void* Filename);

const wchar_t* gameDataStart = L"../../../"; // seems to be at the start of every game path

// FPakPlatformFile::FindFileInPakFiles hook: this will check for any loose file with the same filename
// If a loose file is found will return null (ie: saying that the .pak doesn't contain it)
// 90% of UE4 games will then try loading loose files, luckily DQXI is part of that 90% :D
TFPakPlatformFile__FindFileInPakFiles RealFPakPlatformFile__FindFileInPakFiles;
void* __fastcall HookFPakPlatformFile__FindFileInPakFiles(void* Paks, TCHAR* Filename, void** OutPakFile)
{
	if (OutPakFile)
		*OutPakFile = nullptr;

	if (Filename && wcsstr(Filename, gameDataStart) && FileExists(Filename))
			return 0; // file exists loosely, return false so the game thinks that it doesn't exist in the .pak

	return RealFPakPlatformFile__FindFileInPakFiles(Paks, Filename, OutPakFile);
}


// FPakPlatformFile::IsNonPakFilenameAllowed hook: seems there are policies devs can set to stop certain file types being loaded from outside a .pak
// This just skips checking against those policies and always allows files to be loaded from wherever
TFPakPlatformFile__IsNonPakFilenameAllowed RealFPakPlatformFile__IsNonPakFilenameAllowed;
__int64 __fastcall HookFPakPlatformFile__IsNonPakFilenameAllowed(void* thisptr, void* Filename)
{
	return 1;
}

void HookPakFile()
{
	char* UGameViewportClient__SetupInitialLocalPlayer = exe_base + AddrUGameViewportClient__SetupInitialLocalPlayer;
	MH_CreateHook((void*)UGameViewportClient__SetupInitialLocalPlayer, HookUGameViewportClient__SetupInitialLocalPlayer, (LPVOID*)&RealUGameViewportClient__SetupInitialLocalPlayer);

	char* FPakPlatformFile__FindFileInPakFiles = exe_base + AddrFPakPlatformFile__FindFileInPakFiles;
	MH_CreateHook((void*)FPakPlatformFile__FindFileInPakFiles, HookFPakPlatformFile__FindFileInPakFiles, (LPVOID*)&RealFPakPlatformFile__FindFileInPakFiles);

	char* FPakPlatformFile__IsNonPakFilenameAllowed = exe_base + AddrFPakPlatformFile__IsNonPakFilenameAllowed;
	MH_CreateHook((void*)FPakPlatformFile__IsNonPakFilenameAllowed, HookFPakPlatformFile__IsNonPakFilenameAllowed, (LPVOID*)&RealFPakPlatformFile__IsNonPakFilenameAllowed);

	MH_EnableHook(MH_ALL_HOOKS);
}

bool CheckGameAddress(size_t address, uint32_t expectedValue)
{
	if (!address)
		return true; // addr isn't set, don't bother checking it

	return *(uint32_t*)(exe_base + address) == expectedValue;
}

bool IsSupportedGameVersion()
{
#ifndef SKIP_VERSION_CHECK
	// check bytes of the various functions we call/hook, make sure they're what we expect
	if (!CheckGameAddress(AddrFPakPlatformFile__FindFileInPakFiles, 0x4CC48B48))
		return false;
	if (!CheckGameAddress(AddrFPakPlatformFile__IsNonPakFilenameAllowed, 0x245C8948))
		return false;
#endif
	return true;
}

void InitHooks()
{
	exe_base = (char*)GetModuleHandleA("DRAGON QUEST XI S.exe");
	if (!exe_base)
	{
		exe_base = (char*)GetModuleHandleA("DRAGON QUEST XI S Demo.exe");
	}

	if (!exe_base || !IsSupportedGameVersion())
	{
		MessageBoxA(0, "DQXISHook: unsupported game version, DQXISHook features disabled.\r\nPlease check for a new DQXISHook build!", "DQXISHook by emoose", 0);
		return;
	}

	MH_Initialize();

	HookPakFile();

	// Change window title as indication that DQXISHook is active
	wchar_t* windowTitle = (wchar_t*)(exe_base + AddrWindowTitle);
	DWORD oldProtect = 0;
	VirtualProtect(windowTitle, 0x30, PAGE_EXECUTE_WRITECOPY, &oldProtect);
	wcscpy_s(windowTitle, 0x18, L"DQXIS (hooked)");
	VirtualProtect(windowTitle, 0x30, oldProtect, nullptr);
}

HMODULE ourModule = 0;

BOOL APIENTRY DllMain(HMODULE hModule, int ul_reason_for_call, LPVOID lpReserved)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		ourModule = hModule;
		Proxy_Attach();

		InitHooks();
	}
	if (ul_reason_for_call == DLL_PROCESS_DETACH)
		Proxy_Detach();

	return TRUE;
}
