#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <windows.h>

#define DLLEXPORT __declspec(dllexport)

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))

#define EDGE_THRESHOLD 4

struct WinInfo
{
    HWND hwnd;
    WNDPROC origWndProc;
    // Original dimensions before dragging. We use this to restore the window's
    // size after unsnapping it
    RECT origPos;
    BOOL snapped;
    POINT cursorPos;  // cursor position in window coordinates when drag started
    RECT savedPos;
    BOOL needsResizeHack;
};

static struct WinInfo winInfoTable[16] = {0};

static RECT workArea;
static RECT snapRect;
static BOOL mouseOnEdge;  // set when the mouse is on an edge, and the current window should snap when released

static void errmsg(const char *text)
{
    MessageBox(NULL, text, NULL, MB_ICONERROR | MB_OK);
}

static struct WinInfo *find_wininfo(HWND hwnd)
{
    unsigned int i;

    for (i = 0; i < ARRAY_COUNT(winInfoTable); i++)
    {
        if (winInfoTable[i].hwnd == hwnd)
            return &winInfoTable[i];
    }
    return NULL;
}

static struct WinInfo *add_wininfo(HWND hwnd)
{
    unsigned int i;

    for (i = 0; i < ARRAY_COUNT(winInfoTable); i++)
    {
        if (winInfoTable[i].hwnd == NULL)
        {
            winInfoTable[i].hwnd = hwnd;
            return &winInfoTable[i];
        }
    }
    errmsg("WinInfo table full");
    return NULL;
}

static void restore_window_rect(RECT *r, struct WinInfo *info)
{
    POINT p;

    GetCursorPos(&p);
    r->left = p.x - info->cursorPos.x;
    r->top = p.y - info->cursorPos.y;
    r->right = p.x - info->cursorPos.x + info->savedPos.right - info->savedPos.left;
    r->bottom = p.y - info->cursorPos.y + info->savedPos.bottom - info->savedPos.top;
}

static LRESULT CALLBACK new_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    struct WinInfo *info;

    switch (msg)
    {
      case WM_MOVING:
        {
            struct WinInfo *info;
            RECT *r = (RECT *)lParam;
            POINT cursorPos;

            info = find_wininfo(hwnd);
            info->needsResizeHack = TRUE;
            GetCursorPos(&cursorPos);
            snapRect = workArea;
            if (cursorPos.x <= EDGE_THRESHOLD)
            {
                snapRect.right /= 2;
            }
            else if (cursorPos.y <= EDGE_THRESHOLD)
            {
                snapRect.bottom /= 2;
            }
            else if (cursorPos.x >= workArea.right - 1 - EDGE_THRESHOLD)
            {
                snapRect.left = snapRect.right / 2;
            }
            else if (cursorPos.y >= workArea.bottom - 1 - EDGE_THRESHOLD)
            {
                snapRect.top = snapRect.bottom / 2;
            }
            else  // Not on an edge
            {
                if (mouseOnEdge)  // hide the snap rectangle
                {
                    mouseOnEdge = FALSE;
                    restore_window_rect(r, info);
                }
                else if (info->snapped)  // un-snap the window
                {
                    info->snapped = FALSE;
                    restore_window_rect(r, info);
                }
                else
                {
                    info->savedPos = *r;
                }
                break;
            }

            // Mouse is on edge. Adjust the snap rectangle
            mouseOnEdge = TRUE;
            *r = snapRect;
        }
        break;
      default:
        info = find_wininfo(hwnd);
        return CallWindowProc(info->origWndProc, hwnd, msg, wParam, lParam);
    }
    return 0;
}

DLLEXPORT void CALLBACK snaphook_event_proc(
  HWINEVENTHOOK hWinEventHook,
  DWORD         event,
  HWND          hwnd,
  LONG          idObject,
  LONG          idChild,
  DWORD         dwEventThread,
  DWORD         dwmsEventTime)
{
    DWORD style = GetWindowLongPtr(hwnd, GWL_STYLE);

    if (!(style & WS_THICKFRAME))  // only hook windows that can normally be resized
        return;

    // Drag begin
    if (event == EVENT_SYSTEM_MOVESIZESTART)
    {
        struct WinInfo *info = find_wininfo(hwnd);

        if (info == NULL)  // This is a new window
            info = add_wininfo(hwnd);

        if (!info->snapped)
        {
            RECT r;
            POINT p;
            GetWindowRect(hwnd, &r);
            GetCursorPos(&p);
            info->cursorPos.x = p.x - r.left;
            info->cursorPos.y = p.y - r.top;
            info->savedPos = r;
        }
        info->needsResizeHack = FALSE;
        mouseOnEdge = FALSE;
        info->hwnd = hwnd;

        // Install the new window procedure
        info->origWndProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
        SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)new_wnd_proc);

        SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    }
    // Drag release
    else if (event == EVENT_SYSTEM_MOVESIZEEND)
    {
        struct WinInfo *info = find_wininfo(hwnd);

        // Restore the old window procedure
        SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)info->origWndProc);
        if (mouseOnEdge)
        {
            SetWindowPos(hwnd, NULL,
              snapRect.left, snapRect.top, snapRect.right - snapRect.left, snapRect.bottom - snapRect.top,
              SWP_NOZORDER | SWP_NOOWNERZORDER);
            info->snapped = TRUE;
        }
        else
        {
            // For some reason, when "Show window contents while dragging" is disabled,
            // Windows reverts my changes to the window size after the window is
            // released. I have no idea why this happens, but here's a workaround.
            if (info->needsResizeHack)
            {
                SetWindowPos(hwnd, NULL,
                  info->savedPos.left, info->savedPos.top, info->savedPos.right - info->savedPos.left, info->savedPos.bottom - info->savedPos.top,
                  SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOMOVE);
            }
        }
    }
}
