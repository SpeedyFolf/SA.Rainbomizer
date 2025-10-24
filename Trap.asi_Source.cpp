#include <windows.h>

#define PROJECT_NAME "Rainbomizer"
#define PROJECT_RELEASE_URL "https://github.com/Parik27/SA.Rainbomizer/releases/latest"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
  switch (fdwReason) {
  case DLL_PROCESS_ATTACH:
          if (MessageBox(
              NULL,
              "You have downloaded the source code for " PROJECT_NAME " instead of "
              "the compiled version. Read the instructions in the README "
              "properly and download the version from releases.\n\nPress Yes "
              "to exit the game and open the releases page. Once you have "
              "installed it properly, delete Trap.asi from your game folder.",
              "Error!", MB_YESNO | MB_ICONHAND))
        {
          ShellExecute(NULL, "open", PROJECT_RELEASE_URL , NULL, NULL, SW_SHOWNORMAL);
          ExitProcess(0);
        }
          break;
        default:
            break;
  }
  return TRUE;
}
