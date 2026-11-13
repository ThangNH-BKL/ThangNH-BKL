#include <afxwin.h>

int AFXAPI AfxWinMain(HINSTANCE, HINSTANCE, LPWSTR, int);

// Ninja does not apply Visual Studio's automatic MFC startup settings.
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR, int showCommand) {
    return AfxWinMain(instance, previous, GetCommandLineW(), showCommand);
}