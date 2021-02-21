#include "StdInc.h"
#include "CrossLibraryInterfaces.h"
#include "InputHook.h"
#include "Hooking.h"

#include <nutsnbolts.h>

#include <CfxState.h>

#include <CrossBuildRuntime.h>

static WNDPROC origWndProc;

static bool g_isFocused = true;
static bool g_enableSetCursorPos = false;
static bool g_isFocusStolen = false;

static int* g_inputOffset;
static int* g_mouseX;
static int* g_mouseY;
static int* g_mouseButtons;
static int* g_mouseWheel;

static void(*disableFocus)();

static void DisableFocus()
{
	if (!g_isFocusStolen)
	{
		disableFocus();
	}
}

static void(*enableFocus)();

static void EnableFocus()
{
	if (!g_isFocusStolen)
	{
		enableFocus();
	}
}

static char* g_gameKeyArray;

void InputHook::SetGameMouseFocus(bool focus)
{
	g_isFocusStolen = !focus;

	if (g_isFocusStolen)
	{
		memset(g_gameKeyArray, 0, 256);
	}

	return (focus) ? enableFocus() : disableFocus();
}

void InputHook::EnableSetCursorPos(bool enabled) {
	g_enableSetCursorPos = enabled;
}

#include <LaunchMode.h>

static std::initializer_list<InputHook::ControlBypass> g_controlBypasses;

void InputHook::SetControlBypasses(std::initializer_list<ControlBypass> bypasses)
{
	g_controlBypasses = bypasses;
}

LRESULT APIENTRY grcWindowProcedure(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CREATE)
	{
		SetWindowText(FindWindow(L"grcWindow", nullptr), (CfxIsSinglePlayer()) ? L"Grand Theft Auto V (FiveM SP)" : L"FiveM");
	}

	if (uMsg == WM_ACTIVATEAPP)
	{
		g_isFocused = (wParam) ? true : false;
	}

	if (uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST)
	{
		if (!g_isFocused)
		{
			return 0;
		}
	}

	bool pass = true;
	LRESULT lresult;

	InputHook::DeprecatedOnWndProc(hwnd, uMsg, wParam, lParam, pass, lresult);

	// prevent infinite looping of WM_IME_COMPOSITION caused by ImmSetCompositionStringW in game code
	if (uMsg == WM_IME_COMPOSITION || uMsg == WM_IME_STARTCOMPOSITION || uMsg == WM_IME_ENDCOMPOSITION)
	{
		pass = false;
		lresult = FALSE;
	}

	if (uMsg == WM_PARENTNOTIFY)
	{
		pass = false;
		lresult = DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	if (g_isFocusStolen)
	{
		if (uMsg == WM_LBUTTONDOWN || uMsg == WM_RBUTTONDOWN || uMsg == WM_MBUTTONDOWN || uMsg == WM_XBUTTONDOWN || uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONUP || uMsg == WM_MBUTTONUP || uMsg == WM_XBUTTONUP)
		{
			auto buttonIdx = 0;

			if (uMsg == WM_LBUTTONUP || uMsg == WM_LBUTTONDOWN)
			{
				buttonIdx = 1;
			}
			else if (uMsg == WM_RBUTTONUP || uMsg == WM_RBUTTONDOWN)
			{
				buttonIdx = 2;
			}
			else if (uMsg == WM_MBUTTONUP || uMsg == WM_MBUTTONDOWN)
			{
				buttonIdx = 4;
			}
			else if (uMsg == WM_XBUTTONUP || uMsg == WM_XBUTTONDOWN)
			{
				buttonIdx = GET_XBUTTON_WPARAM(wParam) * 8;
			}

			for (auto bypass : g_controlBypasses)
			{
				if (bypass.isMouse && (bypass.ctrlIdx & 0xFF) == buttonIdx)
				{
					if (uMsg == WM_LBUTTONUP || uMsg == WM_MBUTTONUP || uMsg == WM_RBUTTONUP || uMsg == WM_XBUTTONUP)
					{
						*g_mouseButtons &= ~buttonIdx;
					}
					else
					{
						*g_mouseButtons |= buttonIdx;
					}

					break;
				}
			}
		}
	}

	if (!pass)
	{
		bool shouldPassAnyway = false;

		if (uMsg == WM_KEYUP || uMsg == WM_KEYDOWN)
		{
			for (auto bypass : g_controlBypasses)
			{
				if (!bypass.isMouse && bypass.ctrlIdx == wParam)
				{
					shouldPassAnyway = true;
				}
			}
		}

		if (!shouldPassAnyway)
		{
			return lresult;
		}
	}

	//return CallWindowProc(origWndProc, hwnd, uMsg, wParam, lParam);
	lresult = origWndProc(hwnd, uMsg, wParam, lParam);

	return lresult;
}

BOOL WINAPI ClipCursorWrap(const RECT* lpRekt)
{
	static RECT lastRect;
	static RECT* lastRectPtr;

	int may = 1;
	InputHook::QueryMayLockCursor(may);

	if (!may)
	{
		lpRekt = nullptr;
	}

	if ((lpRekt && !lastRectPtr) ||
		(lastRectPtr && !lpRekt) ||
		(lpRekt && !EqualRect(&lastRect, lpRekt)))
	{
		// update last rect
		if (lpRekt)
		{
			lastRect = *lpRekt;
			lastRectPtr = &lastRect;
		}
		else
		{
			memset(&lastRect, 0xCC, 0);
			lastRectPtr = nullptr;
		}

		return ClipCursor(lpRekt);
	}

	return TRUE;
}

HKL WINAPI ActivateKeyboardLayoutWrap(IN HKL hkl, IN UINT flags)
{
	return hkl;
}

BOOL WINAPI SetCursorPosWrap(int X, int Y)
{
	if (!g_isFocused || g_enableSetCursorPos)
	{
		return SetCursorPos(X, Y);
	}

	return TRUE;
}

#include <HostSharedData.h>
#include <ReverseGameData.h>

static void(*origSetInput)(int, void*, void*, void*);

struct ReverseGameInputState
{
	uint8_t keyboardState[256];
	int mouseX;
	int mouseY;
	int mouseWheel;
	int mouseButtons;

	ReverseGameInputState()
	{
		memset(keyboardState, 0, sizeof(keyboardState));
		mouseX = 0;
		mouseY = 0;
		mouseWheel = 0;
		mouseButtons = 0;
	}

	explicit ReverseGameInputState(const ReverseGameData& data)
	{
		memcpy(keyboardState, data.keyboardState, sizeof(keyboardState));
		mouseX = data.mouseX;
		mouseY = data.mouseY;
		mouseWheel = data.mouseWheel;
		mouseButtons = data.mouseButtons;
	}
};

static ReverseGameInputState lastInput;
static ReverseGameInputState curInput;

static bool g_mainThreadId;

#include <queue>

static HookFunction setOffsetsHookFunction([]()
{
	g_inputOffset = hook::get_address<int*>(hook::get_pattern("89 3D ? ? ? ? EB 0F 48 8B CB", 2));
	g_mouseX = (int*)hook::get_adjusted(0x140000000 + *hook::get_pattern<int32_t>("48 63 D0 8B 46 24 41 01 84 90", 10));
	g_mouseY = g_mouseX + 2;
	g_mouseButtons = hook::get_address<int*>(hook::get_pattern("FF 15 ? ? ? ? 85 C0 8B 05 ? ? ? ? 74 05", 10));
	g_mouseWheel = hook::get_address<int*>(hook::get_pattern("C1 E8 1F 03 D0 01 15", 7));
});

static void SetInputWrap(int a1, void* a2, void* a3, void* a4)
{
	static HostSharedData<ReverseGameData> rgd("CfxReverseGameData");

	WaitForSingleObject(rgd->inputMutex, INFINITE);

	std::vector<InputTarget*> inputTargets;
	static bool lastCaught;
	bool caught = !InputHook::QueryInputTarget(inputTargets);

	if (caught)
	{
		if (rgd->mouseX < 0)
		{
			rgd->mouseX = 0;
		}
		else if (rgd->mouseX >= rgd->width)
		{
			rgd->mouseX = rgd->width;
		}

		if (rgd->mouseY < 0)
		{
			rgd->mouseY = 0;
		}
		else if (rgd->mouseY >= rgd->height)
		{
			rgd->mouseY = rgd->height;
		}
	}

	curInput = ReverseGameInputState{ *rgd };

	static POINT mousePos[2];

	if (caught != lastCaught)
	{
		mousePos[caught ? 1 : 0] = { curInput.mouseX, curInput.mouseY };

		curInput.mouseX = mousePos[lastCaught ? 1 : 0].x;
		curInput.mouseY = mousePos[lastCaught ? 1 : 0].y;

		rgd->mouseX = curInput.mouseX;
		rgd->mouseY = curInput.mouseY;
	}

	lastCaught = caught;

	auto loopTargets = [&inputTargets](const auto& fn)
	{
		for (InputTarget* t : inputTargets)
		{
			fn(t);
		}
	};

	// attempt input synthesis for standard key up/down and mouse events
	for (size_t i = 0; i < std::size(curInput.keyboardState); i++)
	{
		bool pass = true;
		LRESULT lr;

		// TODO: key repeat
		if (curInput.keyboardState[i] && !lastInput.keyboardState[i])
		{
			loopTargets([i](InputTarget* target)
			{
				target->KeyDown(i, 0);
			});
		}
		else if (!curInput.keyboardState[i] && lastInput.keyboardState[i])
		{
			loopTargets([i](InputTarget* target)
			{
				target->KeyUp(i, 0);
			});
		}
	}

	// mouse buttons
	for (int i = 1; i <= 3; i++)
	{
		bool pass = true;
		LRESULT lr;

		int mx = curInput.mouseX;
		int my = curInput.mouseY;

		if ((curInput.mouseButtons & i) && !(lastInput.mouseButtons & i))
		{
			loopTargets([i, mx, my](InputTarget* target)
			{
				target->MouseDown(i - 1, mx, my);
			});
		}
		else if (!(curInput.mouseButtons & i) && (lastInput.mouseButtons & i))
		{
			loopTargets([i, mx, my](InputTarget* target)
			{
				target->MouseUp(i - 1, mx, my);
			});
		}
	}

	// mouse wheel
	if (curInput.mouseWheel != lastInput.mouseWheel)
	{
		int mw = curInput.mouseWheel;

		loopTargets([mw](InputTarget* target)
		{
			target->MouseWheel(mw);
		});
	}

	// mouse movement
	if (curInput.mouseX != lastInput.mouseX || curInput.mouseY != lastInput.mouseY)
	{
		int mx = curInput.mouseX;
		int my = curInput.mouseY;

		loopTargets([mx, my](InputTarget* target)
		{
			target->MouseMove(mx, my);
		});
	}

	lastInput = curInput;

	if (!a1 && !caught)
	{
		int off = ((*g_inputOffset - 1) & 1) ? 1 : 0;

		// TODO: handle flush of keyboard
		memcpy(g_gameKeyArray, rgd->keyboardState, 256);
		g_mouseX[off] = curInput.mouseX;
		g_mouseY[off] = curInput.mouseY;
		*g_mouseButtons = rgd->mouseButtons;
		*g_mouseWheel = rgd->mouseWheel;

		origSetInput(a1, a2, a3, a4);

		off = 0;

		memcpy(rgd->keyboardState, g_gameKeyArray, 256);
		rgd->mouseX = g_mouseX[off];
		rgd->mouseY = g_mouseY[off];
		rgd->mouseButtons = *g_mouseButtons;
		rgd->mouseWheel = *g_mouseWheel;
	}

	ReleaseMutex(rgd->inputMutex);
}

static HookFunction hookFunction([] ()
{
	static int* captureCount = hook::get_address<int*>(hook::get_pattern("48 3B 05 ? ? ? ? 0F 45 CA 89 0D ? ? ? ? 48 83 C4 28", 12));

	OnGameFrame.Connect([]()
	{
		SetInputWrap(-1, NULL, NULL, NULL);

		int may = 1;
		InputHook::QueryMayLockCursor(may);

		if (!may)
		{
			ClipCursorWrap(nullptr);
			*captureCount = 0;
		}
	});

	// window procedure
	char* location = hook::pattern("48 8D 05 ? ? ? ? 33 C9 44 89 75 20 4C 89 7D").count(1).get(0).get<char>(3);

	origWndProc = (WNDPROC)(location + *(int32_t*)location + 4);

	*(int32_t*)location = (intptr_t)(hook::AllocateFunctionStub(grcWindowProcedure)) - (intptr_t)location - 4;

	// disable mouse focus function
	void* patternMatch = hook::pattern("74 0D 38 1D ? ? ? ? 74 05 E8 ? ? ? ? 48 39").count(1).get(0).get<void>(10);
	hook::set_call(&disableFocus, patternMatch);
	hook::call(patternMatch, DisableFocus);

	patternMatch = hook::pattern("74 0D 38 1D ? ? ? ? 74 05 E8 ? ? ? ? 33 C9 E8").count(1).get(0).get<void>(10);
	hook::set_call(&enableFocus, patternMatch);
	hook::call(patternMatch, EnableFocus);

	// game key array
	location = hook::pattern("BF 00 01 00 00 48 8D 1D ? ? ? ? 48 3B 05").count(1).get(0).get<char>(8);

	g_gameKeyArray = (char*)(location + *(int32_t*)location + 4);

	// disable directinput keyboard handling
	// TODO: change for Five
	//hook::return_function(hook::pattern("A1 ? ? ? ? 83 EC 14 53 33 DB").count(1).get(0).get<void>());

	// focus testing
	//hook::call(hook::pattern("83 7E 0C 00 0F 84 23 01 00 00 83 7E 08 00 0F 84").count(1).get(0).get<void>(20), AreWeFocused);
	//hook::jump(hook::pattern("8B 51 08 50 FF D2 84 C0 74 06 B8").count(1).get(0).get<void>(-17), AreWeFocused);

	// force input to be handled using WM_KEYUP/KEYDOWN, not DInput/RawInput

	if (!Is372())
	{
		// disable DInput device creation
		char* dinputCreate = hook::pattern("45 33 C9 FF 50 18 BF 26").count(1).get(0).get<char>(0);
		hook::nop(dinputCreate, 200); // that's a lot of nops!
		hook::nop(dinputCreate + 212, 6);
		hook::nop(dinputCreate + 222, 6);

		// jump over raw input keyboard handling
		hook::put<uint8_t>(hook::pattern("44 39 2E 75 ? B8 FF 00 00 00").count(1).get(0).get<void>(3), 0xEB);

		// default international keyboard mode to on
		// (this will always use a US layout to map VKEY scan codes, instead of using the local layout)
		hook::put<uint8_t>(hook::get_pattern("8D 48 EF 41 3B CE 76 0C", 6), 0xEB);
	}

	// fix repeated ClipCursor calls (causing DWM load)
	hook::iat("user32.dll", ClipCursorWrap, "ClipCursor");
	hook::iat("user32.dll", ActivateKeyboardLayoutWrap, "ActivateKeyboardLayout");

	// don't allow SetCursorPos during focus
	hook::iat("user32.dll", SetCursorPosWrap, "SetCursorPos");

	// NOTE: this specific flow should *only* be used if RG!
	// there's a call above in OnGameFrame which handles all other ancillary responsibilities
	static HostSharedData<CfxState> initState("CfxInitState");

	if (initState->isReverseGame)
	{
		auto location = hook::get_pattern("45 33 C9 44 8A C7 40 8A D7 33 C9 E8", 11);
		hook::set_call(&origSetInput, location);
		hook::call(location, SetInputWrap);
	}
});

fwEvent<HWND, UINT, WPARAM, LPARAM, bool&, LRESULT&> InputHook::DeprecatedOnWndProc;

fwEvent<std::vector<InputTarget*>&> InputHook::QueryInputTarget;

fwEvent<int&> InputHook::QueryMayLockCursor;
