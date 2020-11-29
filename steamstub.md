Some code I used to get around steamstub in other games, maybe it'd come in useful for later DQXI / UE4 games.

IAT patching can probably be replaced with normal MinHook hooks or something similar.

```C
typedef void(*GetSystemTimeAsFileTime_ptr)(LPFILETIME lpSystemTimeAsFileTime);
GetSystemTimeAsFileTime_ptr GetSystemTimeAsFileTime_orig = NULL;
GetSystemTimeAsFileTime_ptr* GetSystemTimeAsFileTime_iat = NULL;

typedef BOOL(*QueryPerformanceCounter_ptr)(LARGE_INTEGER* lpPerformanceCount);
QueryPerformanceCounter_ptr QueryPerformanceCounter_orig = NULL;
QueryPerformanceCounter_ptr* QueryPerformanceCounter_iat = NULL;

static void GetSystemTimeAsFileTime_Hook(LPFILETIME lpSystemTimeAsFileTime)
{
	// call original hooked func
	GetSystemTimeAsFileTime_orig(lpSystemTimeAsFileTime);

	SafeWrite(GetSystemTimeAsFileTime_iat, GetSystemTimeAsFileTime_orig);
	SafeWrite(QueryPerformanceCounter_iat, QueryPerformanceCounter_orig);

	// run our code :)
	Patcher_PatchGame();
}

static BOOL QueryPerformanceCounter_hook(LARGE_INTEGER* lpPerformanceCount)
{
	// patch iats back to original
	SafeWrite(GetSystemTimeAsFileTime_iat, GetSystemTimeAsFileTime_orig);
	SafeWrite(QueryPerformanceCounter_iat, QueryPerformanceCounter_orig);

	// run our code :)
	Patcher_PatchGame();

	// call original hooked func
	return QueryPerformanceCounter_orig(lpPerformanceCount);
}

void Patcher_InitSteamStub()
{
	// Hook the GetSystemTimeAsFileTime function, in most games this seems to be one of the first imports called once SteamStub has finished.
	bool hooked = false;
	GetSystemTimeAsFileTime_iat = (GetSystemTimeAsFileTime_ptr*)GetIATPointer(exe_handle_, "KERNEL32.DLL", "GetSystemTimeAsFileTime");
	if (GetSystemTimeAsFileTime_iat)
	{
		// Found IAT address, hook the function to run our own code instead
		dlog("GetSystemTimeAsFileTime @ %p (-> %p)", GetSystemTimeAsFileTime_iat, *GetSystemTimeAsFileTime_iat);

		GetSystemTimeAsFileTime_orig = *GetSystemTimeAsFileTime_iat;
		SafeWrite(GetSystemTimeAsFileTime_iat, GetSystemTimeAsFileTime_Hook);

		dlog("-> %p", *GetSystemTimeAsFileTime_iat);
		hooked = true;
	}

	// As a backup we'll also hook QueryPerformanceCounter, almost every game makes use of this
	QueryPerformanceCounter_iat = (QueryPerformanceCounter_ptr*)GetIATPointer(exe_handle_, "KERNEL32.DLL", "QueryPerformanceCounter");
	if (QueryPerformanceCounter_iat)
	{
		// Found IAT address, hook the function to run our own code instead
		dlog("QueryPerformanceCounter @ %p (-> %p)", QueryPerformanceCounter_iat, *QueryPerformanceCounter_iat);

		QueryPerformanceCounter_orig = *QueryPerformanceCounter_iat;
		SafeWrite(QueryPerformanceCounter_iat, QueryPerformanceCounter_hook);

		dlog("-> %p", *QueryPerformanceCounter_iat);
		hooked = true;
	}

	// If we failed to hook, try patching immediately
	if (!hooked)
	{
		dlog("Patcher_InitSteamStub failed, patching directly...");
		Patcher_PatchGame();
	}
}
```
