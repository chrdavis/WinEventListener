// WinEventListener.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "WinEventListener.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define MAX_LOADSTRING 100

// Control IDs
#define IDC_FILTER_LABEL    2001
#define IDC_FILTER_EDIT     2002
#define IDC_PAUSE_CHECK     2003
#define IDC_AUTOSCROLL_CHECK 2004
#define IDC_CLEAR_BUTTON    2005
#define IDC_STATUS_LABEL    2006
#define IDC_EVENT_LIST      2007
#define IDT_REFRESH         1

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

struct EventEntry
{
    ULONGLONG seq;
    DWORD time;
    DWORD event;
    HWND hwnd;
    LONG idObject;
    LONG idChild;
    DWORD pid;
    DWORD tid;
    std::wstring className;
    std::wstring title;
    std::wstring processName;
    std::wstring windowType;
};

enum Column
{
    COL_SEQ, COL_TIME, COL_EVENT, COL_EVENTNAME, COL_HWND, COL_IDOBJECT, COL_IDCHILD,
    COL_PID, COL_TID, COL_PROCESS, COL_WINTYPE, COL_CLASS, COL_TITLE, COL_COUNT
};

static const struct { const wchar_t* name; int width; } kColumns[COL_COUNT] = {
    { L"#", 60 }, { L"Time", 80 }, { L"Event", 70 }, { L"Event Name", 220 }, { L"HWND", 90 },
    { L"idObject", 130 }, { L"idChild", 60 }, { L"PID", 60 }, { L"TID", 60 },
    { L"Process", 140 }, { L"Window Type", 120 }, { L"Class", 160 }, { L"Title", 260 },
};

struct FilterTerm
{
    std::wstring text;  // lowercase
    int column;         // -1 = any column
    bool exclude;
};

static const size_t kMaxEntries = 500000;
static const size_t kTrimEntries = 100000;

static HWINEVENTHOOK g_hook = nullptr;
static HWND g_hList = nullptr;
static HWND g_hFilterLabel = nullptr;
static HWND g_hFilterEdit = nullptr;
static HWND g_hPause = nullptr;
static HWND g_hAutoScroll = nullptr;
static HWND g_hClear = nullptr;
static HWND g_hStatus = nullptr;
static HFONT g_hFont = nullptr;

static std::vector<EventEntry> g_entries;
static std::vector<size_t> g_filtered;   // indices into g_entries
static std::vector<FilterTerm> g_filterTerms;
static std::unordered_map<DWORD, std::wstring> g_processNames;
static ULONGLONG g_nextSeq = 1;
static bool g_paused = false;
static bool g_dirty = false;

static const wchar_t* EventName(DWORD ev)
{
#define EV(x) case x: return L#x
    switch (ev)
    {
    EV(EVENT_SYSTEM_SOUND); EV(EVENT_SYSTEM_ALERT); EV(EVENT_SYSTEM_FOREGROUND);
    EV(EVENT_SYSTEM_MENUSTART); EV(EVENT_SYSTEM_MENUEND); EV(EVENT_SYSTEM_MENUPOPUPSTART);
    EV(EVENT_SYSTEM_MENUPOPUPEND); EV(EVENT_SYSTEM_CAPTURESTART); EV(EVENT_SYSTEM_CAPTUREEND);
    EV(EVENT_SYSTEM_MOVESIZESTART); EV(EVENT_SYSTEM_MOVESIZEEND); EV(EVENT_SYSTEM_CONTEXTHELPSTART);
    EV(EVENT_SYSTEM_CONTEXTHELPEND); EV(EVENT_SYSTEM_DRAGDROPSTART); EV(EVENT_SYSTEM_DRAGDROPEND);
    EV(EVENT_SYSTEM_DIALOGSTART); EV(EVENT_SYSTEM_DIALOGEND); EV(EVENT_SYSTEM_SCROLLINGSTART);
    EV(EVENT_SYSTEM_SCROLLINGEND); EV(EVENT_SYSTEM_SWITCHSTART); EV(EVENT_SYSTEM_SWITCHEND);
    EV(EVENT_SYSTEM_MINIMIZESTART); EV(EVENT_SYSTEM_MINIMIZEEND); EV(EVENT_SYSTEM_DESKTOPSWITCH);
    EV(EVENT_SYSTEM_ARRANGMENTPREVIEW); EV(EVENT_SYSTEM_IME_KEY_NOTIFICATION);
    EV(EVENT_SYSTEM_END);
    EV(EVENT_OBJECT_CREATE); EV(EVENT_OBJECT_DESTROY); EV(EVENT_OBJECT_SHOW); EV(EVENT_OBJECT_HIDE);
    EV(EVENT_OBJECT_REORDER); EV(EVENT_OBJECT_FOCUS); EV(EVENT_OBJECT_SELECTION);
    EV(EVENT_OBJECT_SELECTIONADD); EV(EVENT_OBJECT_SELECTIONREMOVE); EV(EVENT_OBJECT_SELECTIONWITHIN);
    EV(EVENT_OBJECT_STATECHANGE); EV(EVENT_OBJECT_LOCATIONCHANGE); EV(EVENT_OBJECT_NAMECHANGE);
    EV(EVENT_OBJECT_DESCRIPTIONCHANGE); EV(EVENT_OBJECT_VALUECHANGE); EV(EVENT_OBJECT_PARENTCHANGE);
    EV(EVENT_OBJECT_HELPCHANGE); EV(EVENT_OBJECT_DEFACTIONCHANGE); EV(EVENT_OBJECT_ACCELERATORCHANGE);
    EV(EVENT_OBJECT_INVOKED); EV(EVENT_OBJECT_TEXTSELECTIONCHANGED); EV(EVENT_OBJECT_CONTENTSCROLLED);
    EV(EVENT_OBJECT_CLOAKED); EV(EVENT_OBJECT_UNCLOAKED); EV(EVENT_OBJECT_LIVEREGIONCHANGED);
    EV(EVENT_OBJECT_HOSTEDOBJECTSINVALIDATED); EV(EVENT_OBJECT_DRAGSTART); EV(EVENT_OBJECT_DRAGCANCEL);
    EV(EVENT_OBJECT_DRAGCOMPLETE); EV(EVENT_OBJECT_DRAGENTER); EV(EVENT_OBJECT_DRAGLEAVE);
    EV(EVENT_OBJECT_DRAGDROPPED); EV(EVENT_OBJECT_IME_SHOW); EV(EVENT_OBJECT_IME_HIDE);
    EV(EVENT_OBJECT_IME_CHANGE); EV(EVENT_OBJECT_TEXTEDIT_CONVERSIONTARGETCHANGED);
    EV(EVENT_OBJECT_END);
    EV(EVENT_CONSOLE_CARET); EV(EVENT_CONSOLE_UPDATE_REGION); EV(EVENT_CONSOLE_UPDATE_SIMPLE);
    EV(EVENT_CONSOLE_UPDATE_SCROLL); EV(EVENT_CONSOLE_LAYOUT); EV(EVENT_CONSOLE_START_APPLICATION);
    EV(EVENT_CONSOLE_END_APPLICATION);
    }
#undef EV
    if (ev >= EVENT_AIA_START && ev <= EVENT_AIA_END) return L"(AIA event)";
    if (ev >= EVENT_OEM_DEFINED_START && ev <= EVENT_OEM_DEFINED_END) return L"(OEM event)";
    if (ev >= EVENT_UIA_EVENTID_START && ev <= EVENT_UIA_EVENTID_END) return L"(UIA event)";
    if (ev >= EVENT_UIA_PROPID_START && ev <= EVENT_UIA_PROPID_END) return L"(UIA property)";
    return L"(unknown)";
}

static const wchar_t* ObjectIdName(LONG id)
{
#define OI(x) case x: return L#x
    switch (id)
    {
    OI(OBJID_WINDOW); OI(OBJID_SYSMENU); OI(OBJID_TITLEBAR); OI(OBJID_MENU); OI(OBJID_CLIENT);
    OI(OBJID_VSCROLL); OI(OBJID_HSCROLL); OI(OBJID_SIZEGRIP); OI(OBJID_CARET); OI(OBJID_CURSOR);
    OI(OBJID_ALERT); OI(OBJID_SOUND); OI(OBJID_QUERYCLASSNAMEIDX); OI(OBJID_NATIVEOM);
    }
#undef OI
    return nullptr;
}

static std::wstring GetProcessNameForPid(DWORD pid)
{
    if (pid == 0) return L"";
    auto it = g_processNames.find(pid);
    if (it != g_processNames.end()) return it->second;

    std::wstring name;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc)
    {
        WCHAR path[MAX_PATH * 2];
        DWORD size = ARRAYSIZE(path);
        if (QueryFullProcessImageNameW(hProc, 0, path, &size))
        {
            const wchar_t* slash = wcsrchr(path, L'\\');
            name = slash ? slash + 1 : path;
        }
        CloseHandle(hProc);
    }
    if (name.empty()) name = L"<unknown>";
    g_processNames[pid] = name;
    return name;
}

static std::wstring GetColumnText(const EventEntry& e, int col)
{
    WCHAR buf[64];
    switch (col)
    {
    case COL_SEQ: swprintf_s(buf, L"%llu", e.seq); return buf;
    case COL_TIME: swprintf_s(buf, L"%lu", e.time); return buf;
    case COL_EVENT: swprintf_s(buf, L"0x%04X", e.event); return buf;
    case COL_EVENTNAME: return EventName(e.event);
    case COL_HWND: swprintf_s(buf, L"0x%08IX", (ULONG_PTR)e.hwnd); return buf;
    case COL_IDOBJECT:
    {
        const wchar_t* n = ObjectIdName(e.idObject);
        if (n) swprintf_s(buf, L"%ld (%s)", e.idObject, n);
        else swprintf_s(buf, L"%ld", e.idObject);
        return buf;
    }
    case COL_IDCHILD: swprintf_s(buf, L"%ld", e.idChild); return buf;
    case COL_PID: swprintf_s(buf, L"%lu", e.pid); return buf;
    case COL_TID: swprintf_s(buf, L"%lu", e.tid); return buf;
    case COL_PROCESS: return e.processName;
    case COL_WINTYPE: return e.windowType;
    case COL_CLASS: return e.className;
    case COL_TITLE: return e.title;
    }
    return L"";
}

static std::wstring ToLower(std::wstring s)
{
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

// Filter syntax: space separated terms, all must match.
//   text        - any column contains text (case-insensitive)
//   -text       - no column contains text
//   col:text    - specific column contains text (col = event, name, hwnd, obj, child, pid, tid, proc, class, title)
//   -col:text   - specific column does not contain text
static int ColumnFromKey(const std::wstring& key)
{
    static const struct { const wchar_t* key; int col; } map[] = {
        { L"seq", COL_SEQ }, { L"time", COL_TIME }, { L"event", COL_EVENT }, { L"ev", COL_EVENT },
        { L"name", COL_EVENTNAME }, { L"hwnd", COL_HWND }, { L"obj", COL_IDOBJECT },
        { L"idobject", COL_IDOBJECT }, { L"child", COL_IDCHILD }, { L"idchild", COL_IDCHILD },
        { L"pid", COL_PID }, { L"tid", COL_TID }, { L"proc", COL_PROCESS }, { L"process", COL_PROCESS },
        { L"type", COL_WINTYPE }, { L"class", COL_CLASS }, { L"title", COL_TITLE },
    };
    for (auto& m : map)
        if (key == m.key) return m.col;
    return -1;
}

static void ParseFilter(const std::wstring& text)
{
    g_filterTerms.clear();
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && iswspace(text[i])) ++i;
        size_t start = i;
        while (i < text.size() && !iswspace(text[i])) ++i;
        if (start == i) continue;

        std::wstring tok = ToLower(text.substr(start, i - start));
        FilterTerm term{ L"", -1, false };
        if (tok[0] == L'-' && tok.size() > 1)
        {
            term.exclude = true;
            tok.erase(0, 1);
        }
        size_t colon = tok.find(L':');
        if (colon != std::wstring::npos && colon > 0)
        {
            int col = ColumnFromKey(tok.substr(0, colon));
            if (col >= 0)
            {
                term.column = col;
                tok.erase(0, colon + 1);
            }
        }
        term.text = tok;
        if (!term.text.empty() || term.column >= 0)
            g_filterTerms.push_back(term);
    }
}

static bool MatchesFilter(const EventEntry& e)
{
    for (const auto& term : g_filterTerms)
    {
        bool found = false;
        if (term.column >= 0)
        {
            found = ToLower(GetColumnText(e, term.column)).find(term.text) != std::wstring::npos;
        }
        else
        {
            for (int c = 0; c < COL_COUNT && !found; ++c)
                found = ToLower(GetColumnText(e, c)).find(term.text) != std::wstring::npos;
        }
        if (found == term.exclude) return false;
    }
    return true;
}

static void UpdateStatus()
{
    WCHAR buf[128];
    swprintf_s(buf, L"Showing %zu of %zu events%s", g_filtered.size(), g_entries.size(),
        g_paused ? L" (paused)" : L"");
    SetWindowTextW(g_hStatus, buf);
}

static void RefreshList(bool rebuilt)
{
    if (rebuilt)
    {
        ListView_SetItemCountEx(g_hList, (int)g_filtered.size(), 0);
        InvalidateRect(g_hList, nullptr, FALSE);
    }
    else
    {
        ListView_SetItemCountEx(g_hList, (int)g_filtered.size(), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    }
    if (!g_filtered.empty() && Button_GetCheck(g_hAutoScroll) == BST_CHECKED)
        ListView_EnsureVisible(g_hList, (int)g_filtered.size() - 1, FALSE);
    UpdateStatus();
}

static void RebuildFiltered()
{
    g_filtered.clear();
    for (size_t i = 0; i < g_entries.size(); ++i)
        if (MatchesFilter(g_entries[i]))
            g_filtered.push_back(i);
    RefreshList(true);
    g_dirty = false;
}

static void ApplyFilterFromEdit()
{
    int len = GetWindowTextLengthW(g_hFilterEdit);
    std::wstring text(len, L'\0');
    if (len > 0) GetWindowTextW(g_hFilterEdit, &text[0], len + 1);
    ParseFilter(text);
    RebuildFiltered();
}

static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject,
    LONG idChild, DWORD idEventThread, DWORD dwmsEventTime)
{
    if (g_paused) return;

    EventEntry e;
    e.seq = g_nextSeq++;
    e.time = dwmsEventTime;
    e.event = event;
    e.hwnd = hwnd;
    e.idObject = idObject;
    e.idChild = idChild;
    e.pid = 0;
    e.tid = 0;

    if (hwnd && IsWindow(hwnd))
    {
        e.tid = GetWindowThreadProcessId(hwnd, &e.pid);
        WCHAR buf[512];
        if (GetClassNameW(hwnd, buf, ARRAYSIZE(buf))) e.className = buf;
        // Window is in another process (own process is skipped), so this does not send WM_GETTEXT.
        if (GetWindowTextW(hwnd, buf, ARRAYSIZE(buf))) e.title = buf;

        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        auto append = [&e](const wchar_t* s) {
            if (!e.windowType.empty()) e.windowType += L", ";
            e.windowType += s;
        };
        if (style & WS_CHILD) append(L"Child");
        if (style & WS_POPUP) append(L"Popup");
        if (exStyle & WS_EX_TOPMOST) append(L"Topmost");
        if (e.windowType.empty()) append(L"Overlapped");
    }
    if (e.pid == 0 && idEventThread)
    {
        e.tid = idEventThread;
        HANDLE hThread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, idEventThread);
        if (hThread)
        {
            e.pid = GetProcessIdOfThread(hThread);
            CloseHandle(hThread);
        }
    }
    e.processName = GetProcessNameForPid(e.pid);

    g_entries.push_back(std::move(e));
    if (MatchesFilter(g_entries.back()))
        g_filtered.push_back(g_entries.size() - 1);
    g_dirty = true;
}

static void TrimIfNeeded()
{
    if (g_entries.size() <= kMaxEntries) return;
    g_entries.erase(g_entries.begin(), g_entries.begin() + kTrimEntries);
    RebuildFiltered();
}

static void LayoutControls(HWND hWnd)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    const int pad = 6, h = 24, top = pad;
    int x = pad;
    MoveWindow(g_hFilterLabel, x, top + 4, 40, h - 4, TRUE); x += 44;
    int rightWidth = 80 + 100 + 70 + 220 + pad * 4;
    int editWidth = max(100, (int)(rc.right - x - rightWidth - pad));
    MoveWindow(g_hFilterEdit, x, top, editWidth, h, TRUE); x += editWidth + pad;
    MoveWindow(g_hPause, x, top, 80, h, TRUE); x += 80 + pad;
    MoveWindow(g_hAutoScroll, x, top, 100, h, TRUE); x += 100 + pad;
    MoveWindow(g_hClear, x, top, 70, h, TRUE); x += 70 + pad;
    MoveWindow(g_hStatus, x, top + 4, 220, h - 4, TRUE);
    int listTop = top + h + pad;
    MoveWindow(g_hList, 0, listTop, rc.right, max(0, (int)(rc.bottom - listTop)), TRUE);
}

static void CreateControls(HWND hWnd)
{
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    g_hFilterLabel = CreateWindowW(L"STATIC", L"Filter:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hWnd, (HMENU)IDC_FILTER_LABEL, hInst, nullptr);
    g_hFilterEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, hWnd, (HMENU)IDC_FILTER_EDIT, hInst, nullptr);
    SendMessageW(g_hFilterEdit, EM_SETCUEBANNER, TRUE,
        (LPARAM)L"e.g.  focus -proc:explorer type:topmost  (cols: event name hwnd obj child pid tid proc type class title)");
    g_hPause = CreateWindowW(L"BUTTON", L"Pause", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hWnd, (HMENU)IDC_PAUSE_CHECK, hInst, nullptr);
    g_hAutoScroll = CreateWindowW(L"BUTTON", L"Auto-scroll", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hWnd, (HMENU)IDC_AUTOSCROLL_CHECK, hInst, nullptr);
    Button_SetCheck(g_hAutoScroll, BST_CHECKED);
    g_hClear = CreateWindowW(L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, hWnd, (HMENU)IDC_CLEAR_BUTTON, hInst, nullptr);
    g_hStatus = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hWnd, (HMENU)IDC_STATUS_LABEL, hInst, nullptr);

    g_hList = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hWnd, (HMENU)IDC_EVENT_LIST, hInst, nullptr);
    ListView_SetExtendedListViewStyle(g_hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    for (int i = 0; i < COL_COUNT; ++i)
    {
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = (LPWSTR)kColumns[i].name;
        col.cx = kColumns[i].width;
        col.iSubItem = i;
        ListView_InsertColumn(g_hList, i, &col);
    }

    for (HWND h : { g_hFilterLabel, g_hFilterEdit, g_hPause, g_hAutoScroll, g_hClear, g_hStatus, g_hList })
        SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
}

static void CopySelectionToClipboard(HWND hWnd)
{
    std::wstring text;
    int idx = -1;
    while ((idx = ListView_GetNextItem(g_hList, idx, LVNI_SELECTED)) != -1)
    {
        if ((size_t)idx >= g_filtered.size()) break;
        const EventEntry& e = g_entries[g_filtered[idx]];
        for (int c = 0; c < COL_COUNT; ++c)
        {
            if (c) text += L'\t';
            text += GetColumnText(e, c);
        }
        text += L"\r\n";
    }
    if (text.empty() || !OpenClipboard(hWnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem)
    {
        memcpy(GlobalLock(hMem), text.c_str(), bytes);
        GlobalUnlock(hMem);
        if (!SetClipboardData(CF_UNICODETEXT, hMem)) GlobalFree(hMem);
    }
    CloseClipboard();
}

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_WINEVENTLISTENER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WINEVENTLISTENER));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_KEYDOWN && msg.hwnd == g_hList && msg.wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            CopySelectionToClipboard(GetParent(g_hList));
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.hwnd == g_hList && msg.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            ListView_SetItemState(g_hList, -1, LVIS_SELECTED, LVIS_SELECTED);
            continue;
        }
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINEVENTLISTENER));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_WINEVENTLISTENER);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, 1400, 800, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateControls(hWnd);
        g_hook = SetWinEventHook(EVENT_MIN, EVENT_MAX, nullptr, WinEventProc, 0, 0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (!g_hook)
            MessageBoxW(hWnd, L"SetWinEventHook failed.", szTitle, MB_ICONERROR);
        SetTimer(hWnd, IDT_REFRESH, 100, nullptr);
        UpdateStatus();
        break;
    case WM_SIZE:
        LayoutControls(hWnd);
        break;
    case WM_SETFOCUS:
        SetFocus(g_hFilterEdit);
        break;
    case WM_TIMER:
        if (wParam == IDT_REFRESH && g_dirty)
        {
            g_dirty = false;
            TrimIfNeeded();
            RefreshList(false);
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR hdr = (LPNMHDR)lParam;
            if (hdr->hwndFrom == g_hList && hdr->code == LVN_GETDISPINFOW)
            {
                static std::wstring s_text;
                NMLVDISPINFOW* di = (NMLVDISPINFOW*)lParam;
                if ((di->item.mask & LVIF_TEXT) && di->item.iItem >= 0 && (size_t)di->item.iItem < g_filtered.size())
                {
                    s_text = GetColumnText(g_entries[g_filtered[di->item.iItem]], di->item.iSubItem);
                    wcsncpy_s(di->item.pszText, di->item.cchTextMax, s_text.c_str(), _TRUNCATE);
                }
                return 0;
            }
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDC_FILTER_EDIT:
                if (HIWORD(wParam) == EN_CHANGE)
                    ApplyFilterFromEdit();
                break;
            case IDC_PAUSE_CHECK:
                g_paused = Button_GetCheck(g_hPause) == BST_CHECKED;
                UpdateStatus();
                break;
            case IDC_AUTOSCROLL_CHECK:
                break;
            case IDC_CLEAR_BUTTON:
                g_entries.clear();
                g_filtered.clear();
                g_processNames.clear();
                RefreshList(true);
                break;
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_DESTROY:
        KillTimer(hWnd, IDT_REFRESH);
        if (g_hook) UnhookWinEvent(g_hook);
        g_hook = nullptr;
        if (g_hFont) DeleteObject(g_hFont);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
