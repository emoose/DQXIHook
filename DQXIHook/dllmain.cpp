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
const size_t addr_UEngine = 0x42AA3A8; // GEngine address (UEngine global pointer)

const size_t offs_UEngine_ConsoleClass = 0x140; // offset to ConsoleClass field inside UEngine class
const size_t offs_UGameViewportClient_ViewportConsole = 0x48; // offset to ViewportConsole field inside UGameViewportClient class

const size_t addr_StaticConstructObject_Internal_Address = 0x14A5AF0; // address of StaticConstructObject_Internal func

const size_t addr_FOutputDeviceRedirector__Get = 0x132C930; // address of FOutputDeviceRedirector::Get func
const size_t addr_FOutputDeviceRedirector__AddOutputDevice = 0x131F740; // address of FOutputDeviceRedirector::AddOutputDevice func

const size_t addr_UGameViewportClient__SetupInitialLocalPlayer = 0x207A280; // address of UGameViewportClient::SetupInitialLocalPlayer func (hooked)

const size_t addr_Pakfile__Find = 0x291FB50; // address of PakFile::Find func (hooked)
const size_t addr_FPakPlatformFile__IsNonPakFilenameAllowed = 0x29223A0; // address of FPakPlatformFile::IsNonPakFilenameAllowed func (hooked)

const size_t addr_UInputSettings__PostInitProperties_check = 0x2388D8A; // address of the ConsoleKeys.Num() == 1 checks JNZ inside UInputSettings::PostInitProperties
const size_t addr_UInputSettings__PostInitProperties_check2 = 0x2388DA4; // address of the ConsoleKeys[0] == Tilde checks JNZ inside UInputSettings::PostInitProperties

typedef void*(*StaticConstructObject_Internal_ptr)(void* Class, void* InOuter, void* Name, void* SetFlags, void* InternalSetFlags, void* Template, bool bCopyTransientsFromClassDefaults, struct FObjectInstancingGraph* InstanceGraph);
typedef void*(*FOutputDeviceRedirector__Get__ptr)(void);
typedef void(*FOutputDeviceRedirector__AddOutputDevice__ptr)(void* thisptr, void* OutputDevice);
typedef void*(*SetupInitialLocalPlayer_ptr)(void* thisptr, void* OutError);
typedef void*(*PakFile__Find_ptr)(void* thisptr, void* Filename);

SetupInitialLocalPlayer_ptr SetupInitialLocalPlayer_orig;

// UGameViewportClient::SetupInitialLocalPlayer hook: in UE4 builds with ALLOW_CONSOLE=1, this function inits the UGameViewportClient::ViewportConsole field
// so we just recreate the code that does that, and luckily that's all we need to re-enable the console!
void* __fastcall SetupInitialLocalPlayer_hook(char* thisptr, void* OutError)
{
	// create UConsole class
	StaticConstructObject_Internal_ptr StaticConstructObject_Internal = (StaticConstructObject_Internal_ptr)(exe_base + addr_StaticConstructObject_Internal_Address);

	char* engine = *(char**)(exe_base + addr_UEngine);
	void* consoleClass = *(void**)(engine + offs_UEngine_ConsoleClass);

	void* console = StaticConstructObject_Internal(consoleClass, thisptr, 0, 0, 0, 0, 0, 0);

	// set ViewportConsole field in this UGameViewportClient instance
	*(void**)(thisptr + offs_UGameViewportClient_ViewportConsole) = console;

	// UE4 calls this, but doesn't seem to actually redirect any logs, probably all stripped in shipping builds
	if (addr_FOutputDeviceRedirector__Get)
	{
		FOutputDeviceRedirector__Get__ptr FOutputDeviceRedirector__Get = (FOutputDeviceRedirector__Get__ptr)(exe_base + addr_FOutputDeviceRedirector__Get);
		FOutputDeviceRedirector__AddOutputDevice__ptr FOutputDeviceRedirector__AddOutputDevice = (FOutputDeviceRedirector__AddOutputDevice__ptr)(exe_base + addr_FOutputDeviceRedirector__AddOutputDevice);
		void* redirector = FOutputDeviceRedirector__Get();
		FOutputDeviceRedirector__AddOutputDevice(redirector, console);
	}

	// call rest of SetupInitialLocalPlayer
	return SetupInitialLocalPlayer_orig(thisptr, OutError);
}

void HookConsole()
{
	char* UGameViewportClient__SetupInitialLocalPlayer = exe_base + addr_UGameViewportClient__SetupInitialLocalPlayer;
	MH_CreateHook((void*)UGameViewportClient__SetupInitialLocalPlayer, SetupInitialLocalPlayer_hook, (LPVOID*)&SetupInitialLocalPlayer_orig);
	MH_EnableHook((void*)UGameViewportClient__SetupInitialLocalPlayer);

	if (addr_UInputSettings__PostInitProperties_check)
	{
		// change UInputSettings::PostInitProperties's ConsoleKeys.Num() == 1 check to be ConsoleKeys.Num() >= 1
		// this way the non-english bindings will be added if ConsoleKeys has at least 1 binding
		// so emptying ConsoleKeys will stop non-english bindings being added (for people who want to disable console I guess)
		// but having 1 or more binding (in DQXIs case, 2) will still let the game add the non-english ones
		// (previously it would never add them, since DQXI BaseInput.ini sets ConsoleKeys = {Tilde, Atmark}, which is > 1)
		DWORD new_prot = PAGE_READWRITE;
		DWORD old_prot;
		VirtualProtect(exe_base + addr_UInputSettings__PostInitProperties_check, 2, new_prot, &old_prot);
		*(uint16_t*)(exe_base + addr_UInputSettings__PostInitProperties_check) = 0x8C0F; // JNZ -> JL
		VirtualProtect(exe_base + addr_UInputSettings__PostInitProperties_check, 2, old_prot, &new_prot);

		/*VirtualProtect(exe_base + addr_UInputSettings__PostInitProperties_check, 6, new_prot, &old_prot);
		*(uint32_t*)(exe_base + addr_UInputSettings__PostInitProperties_check) = 0x90909090;
		*(uint16_t*)(exe_base + addr_UInputSettings__PostInitProperties_check + 4) = 0x9090;
		VirtualProtect(exe_base + addr_UInputSettings__PostInitProperties_check, 6, old_prot, &new_prot);*/
	}

	/*if (addr_UInputSettings__PostInitProperties_check2)
	{
		// removes ConsoleKeys[0] == Tilde check for the non-english bindings to be added
		DWORD new_prot = PAGE_READWRITE;
		DWORD old_prot;
		VirtualProtect(exe_base + addr_UInputSettings__PostInitProperties_check2, 6, new_prot, &old_prot);
		*(uint32_t*)(exe_base + addr_UInputSettings__PostInitProperties_check2) = 0x90909090;
		*(uint16_t*)(exe_base + addr_UInputSettings__PostInitProperties_check2 + 4) = 0x9090;
		VirtualProtect(exe_base + addr_UInputSettings__PostInitProperties_check2, 6, old_prot, &new_prot);
	}*/
}

const wchar_t* gameDataStart = L"../../../"; // seems to be at the start of every game path
PakFile__Find_ptr PakFile__Find_orig;

// PakFile::Find hook: this will check for any loose file with the same filename, and if a loose file is found will return false (ie: saying that the .pak doesn't contain it)
// 90% of UE4 games will then try loading loose files, luckily DQXI is part of that 90% :D
void* __fastcall PakFile__Find_hook(void* thisptr, void* Filename)
{
	const TCHAR* fname = *(TCHAR**)Filename;

	if (wcsstr(fname, gameDataStart) && FileExists(fname))
		return 0; // file exists loosely, return false so the game thinks that it doesn't exist in the .pak

	return PakFile__Find_orig(thisptr, Filename);
}

PakFile__Find_ptr IsNonPakFilenameAllowed_orig;

// FPakPlatformFile::IsNonPakFilenameAllowed hook: seems there are policies devs can set to stop certain file types being loaded from outside a .pak
// This just skips checking against those policies and always allows files to be loaded from wherever
__int64 __fastcall IsNonPakFilenameAllowed_hook(void* thisptr, void* Filename)
{
	return 1;
}

void HookPakFile()
{
	char* PakFile__Find = exe_base + addr_Pakfile__Find;
	MH_CreateHook((void*)PakFile__Find, PakFile__Find_hook, (LPVOID*)&PakFile__Find_orig);
	MH_EnableHook((void*)PakFile__Find);

	char* FPakPlatformFile__IsNonPakFilenameAllowed = exe_base + addr_FPakPlatformFile__IsNonPakFilenameAllowed;
	MH_CreateHook((void*)FPakPlatformFile__IsNonPakFilenameAllowed, IsNonPakFilenameAllowed_hook, (LPVOID*)&IsNonPakFilenameAllowed_orig);
	MH_EnableHook((void*)FPakPlatformFile__IsNonPakFilenameAllowed);
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
	if (!CheckGameAddress(addr_StaticConstructObject_Internal_Address, 0xEE462BE9))
		return false;
	if (!CheckGameAddress(addr_FOutputDeviceRedirector__Get, 0x6439EBE9))
		return false;
	if (!CheckGameAddress(addr_FOutputDeviceRedirector__AddOutputDevice, 0x245C8948))
		return false;
	if (!CheckGameAddress(addr_UGameViewportClient__SetupInitialLocalPlayer, 0x245C8948))
		return false;
	if (!CheckGameAddress(addr_Pakfile__Find, 0x245C8948))
		return false;
	if (!CheckGameAddress(addr_FPakPlatformFile__IsNonPakFilenameAllowed, 0x245C8948))
		return false;
	if (!CheckGameAddress(addr_UInputSettings__PostInitProperties_check, 0x0237850F))
		return false;
	if (!CheckGameAddress(addr_UInputSettings__PostInitProperties_check2, 0x021D850F))
		return false;
#endif
	return true;
}

void InitHooks()
{
	exe_base = (char*)GetModuleHandleA("DRAGON QUEST XI.exe");
	if (!exe_base)
		return;

	if (!IsSupportedGameVersion())
	{
		MessageBoxA(0, "DQXIHook: unsupported DQXI version, DQXIHook features disabled.\r\nPlease check for a new DQXIHook build!", "DQXIHook", 0);
		return;
	}

	MH_Initialize();

	HookPakFile();
	HookConsole();
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
