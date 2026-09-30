/* Предпросмотр печати: разбирает тот самый поток ESC/P, который уйдёт на
   принтер, и рисует его как листы бумаги — так на экране видно ровно то, что
   получит СМ6337 (шаг печати, жирный, двойная/расширенная печать, индексы,
   подчёркивание, переносы и разбивка на страницы), а не то, что показывает
   RichEdit. ANSI-сборка, как и остальной код (Win98 SE). */
#define _WIN32_WINNT 0x0400
#define WINVER 0x0400
#include <windows.h>
#include <commctrl.h>
#include <string.h>
#include "printer.h"
#include "preview.h"

#define PV_PAPER_W   794.0f   /* A4, 8.27" при 96 dpi */
#define PV_PICA_PX   9.6f     /* ширина знакоместа при 10 cpi (96 dpi) */
#define PV_LINE_PX   16.0f    /* 1/6" — стандартный интервал принтера */
#define PV_TOP_PX    16.0f
#define PV_GAP_PX    14.0f

#define GF_BOLD 1
#define GF_UNDER 2
#define GF_DSTRIKE 4
#define GF_SUP 8
#define GF_SUB 16

typedef struct {
    float x;       /* от левого края листа, px при 96 dpi */
    float cell;    /* ширина знакоместа, px */
    short page;
    short line;    /* номер строки на странице */
    unsigned char ch;    /* символ в CP1251, готовый к выводу */
    unsigned char flags;
} Glyph;

typedef struct { int h, w, weight; HFONT f; } FontEntry;

static Glyph *g_gl;
static int g_glCount;
static int g_pages;
static int g_linesPerPage;
static float g_lineH;
static int g_scrollY;
static FontEntry g_fonts[48];
static int g_fontCount;
static HWND g_hPv;
static HWND g_hPvTb;      /* панель кнопок сверху окна предпросмотра */
static int g_tbH;         /* её высота: лист рисуется ниже */
static float g_pageHeightPx = 16.0f * 70; /* физическая высота листа, px при 96 dpi */
static float g_originX = 12.0f; /* где начинается печать от левого края листа, px */
static unsigned char *g_job; /* копия потока — для кнопки «Печать» в окне */
static DWORD g_jobLen;
#define PV_CMD_PRINT 1
#define PV_CMD_CLOSE 2
#define PV_IMG_PRINT 14    /* ячейки полосы toolbar.bmp: принтер и крестик */
#define PV_IMG_CLOSE 5

static HFONT GetPvFont(int h, int w, int weight)
{
    int i;
    for (i = 0; i < g_fontCount; i++)
        if (g_fonts[i].h == h && g_fonts[i].w == w && g_fonts[i].weight == weight) return g_fonts[i].f;
    HFONT f = CreateFontA(-h, w, 0, 0, weight, 0, 0, 0, RUSSIAN_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
    if (g_fontCount < (int)(sizeof(g_fonts) / sizeof(g_fonts[0]))) {
        g_fonts[g_fontCount].h = h; g_fonts[g_fontCount].w = w;
        g_fonts[g_fontCount].weight = weight; g_fonts[g_fontCount].f = f;
        g_fontCount++;
    }
    return f;
}

static void FreePv(void)
{
    int i;
    for (i = 0; i < g_fontCount; i++) if (g_fonts[i].f) DeleteObject(g_fonts[i].f);
    g_fontCount = 0;
    if (g_gl) { HeapFree(GetProcessHeap(), 0, g_gl); g_gl = NULL; }
    if (g_job) { HeapFree(GetProcessHeap(), 0, g_job); g_job = NULL; }
    g_jobLen = 0;
    g_glCount = 0;
    g_hPv = NULL; g_hPvTb = NULL; g_tbH = 0;
}

/* Разбор потока байт ESC/P (только те команды, что шлёт BuildPrintJob). */
static BOOL ParseJob(const unsigned char *job, DWORD len, const Sm6337Settings *s)
{
    unsigned char rev[256];
    int i;
    for (i = 0; i < 256; i++) rev[i] = (unsigned char)i;
    for (i = 32; i < 256; i++) {
        unsigned char out;
        if (Sm6337_EncodeChar((unsigned char)i, s->codeTable, &out)) rev[out] = (unsigned char)i;
    }

    g_gl = (Glyph *)HeapAlloc(GetProcessHeap(), 0, (size_t)(len + 1) * sizeof(Glyph));
    if (!g_gl) return FALSE;
    g_glCount = 0;
    g_pages = 1;
    g_linesPerPage = s->lines > 0 ? s->lines : 70;
    g_lineH = (s->lineSpacing > 0) ? (float)s->lineSpacing * 1.3342f : PV_LINE_PX;
    if (s->pageMode == SM_PAGEMODE_PRINTER) {
        /* принтер сам режет страницы: ESC C n — длина формата в строках ТЕКУЩЕГО интервала */
        g_pageHeightPx = (float)g_linesPerPage * g_lineH;
    } else {
        /* лист стандартный: g_lines строк по 1/6", а сколько строк влезет при выбранном интервале —
           зависит от него: шире интервал — меньше строк на странице */
        g_pageHeightPx = (float)g_linesPerPage * PV_LINE_PX;
        g_linesPerPage = (int)(g_pageHeightPx / g_lineH + 0.001f);
        if (g_linesPerPage < 1) g_linesPerPage = 1;
    }

    /* строка из cols знаков по 10 cpi ставится по центру листа: 82 колонки — это 8,2", а A4 —
       8,27", так что фиксированный отступ слева выталкивал бы правый край за лист */
    g_originX = (PV_PAPER_W - (float)s->cols * PV_PICA_PX) / 2.0f;
    if (g_originX < 2.0f) g_originX = 2.0f;
    int margin = s->leftMargin;
    if (margin >= s->cols) margin = s->cols > 1 ? s->cols - 1 : 0;
    float marginPx = (float)margin * PV_PICA_PX;
    float rightPx = g_originX + (float)s->cols * PV_PICA_PX;
    BOOL bold = FALSE, under = FALSE, elite = FALSE, cond = FALSE, dstrike = FALSE, expanded = FALSE;
    int index = 0;
    int page = 0, line = 0;
    float x = marginPx;
    DWORD p = 0;

    while (p < len) {
        unsigned char b = job[p++];
        if (b == 0x1B) {
            if (p >= len) break;
            unsigned char c = job[p++];
            switch (c) {
            case '@': bold = under = elite = cond = dstrike = expanded = FALSE; index = 0; break;
            case 'M': elite = TRUE; break;
            case 'P': elite = FALSE; break;
            case 'E': bold = TRUE; break;
            case 'F': bold = FALSE; break;
            case 'G': dstrike = TRUE; break;
            case 'H': dstrike = FALSE; break;
            case 'T': index = 0; break;
            case '-': if (p < len) under = (job[p++] & 1) ? TRUE : FALSE; break;
            case 'W': if (p < len) expanded = (job[p++] & 1) ? TRUE : FALSE; break;
            case 'S': if (p < len) index = (job[p++] & 1) ? 2 : 1; break;
            case 'R': case 'U': case 'A': case 'l': case 'Q': case 'C':
                if (p < len) p++; /* параметр уже учтён в settings */
                break;
            default: break;
            }
            continue;
        }
        if (b == 0x0F) { cond = TRUE; continue; }
        if (b == 0x12) { cond = FALSE; continue; }
        if (b == 0x0D) { x = marginPx; continue; }
        if (b == 0x0A) {
            line++;
            if (line >= g_linesPerPage) { page++; line = 0; }
            continue;
        }
        if (b == 0x0C) { if (line > 0 || x > marginPx) { page++; line = 0; } x = marginPx; continue; }
        if (b < 32) continue;

        float cpi = elite ? 12.0f : 10.0f;
        if (cond) cpi *= 1.716f;
        float cell = (96.0f / cpi) * (expanded ? 2.0f : 1.0f);
        if (g_originX + x + cell > rightPx + 0.5f && s->pageMode == SM_PAGEMODE_PRINTER) {
            x = marginPx; line++;
            if (line >= g_linesPerPage) { page++; line = 0; }
        }
        Glyph *g = &g_gl[g_glCount++];
        g->x = g_originX + x; g->cell = cell; g->page = (short)page; g->line = (short)line;
        g->ch = rev[b];
        g->flags = (unsigned char)((bold ? GF_BOLD : 0) | (under ? GF_UNDER : 0) | (dstrike ? GF_DSTRIKE : 0) |
            (index == 1 ? GF_SUP : 0) | (index == 2 ? GF_SUB : 0));
        x += cell;
    }
    g_pages = page + 1;
    if (g_glCount == 0) g_pages = 1;
    return TRUE;
}

static float PvScale(int clientW)
{
    float sc = ((float)clientW - 2 * PV_GAP_PX) / PV_PAPER_W;
    if (sc > 1.5f) sc = 1.5f;
    if (sc < 0.3f) sc = 0.3f;
    return sc;
}

static int PvPageH(float sc) { return (int)((PV_TOP_PX * 2 + g_pageHeightPx) * sc); }

static int PvTotalH(float sc) { return (int)(PV_GAP_PX + (float)g_pages * ((float)PvPageH(sc) + PV_GAP_PX)); }

static void PvView(HWND hWnd, RECT *rc)
{
    GetClientRect(hWnd, rc);
    rc->top = g_tbH;
    if (rc->bottom < rc->top) rc->bottom = rc->top;
}

static void UpdateScroll(HWND hWnd)
{
    RECT rc; PvView(hWnd, &rc);
    float sc = PvScale(rc.right);
    int total = PvTotalH(sc);
    int viewH = rc.bottom - rc.top;
    int maxY = total - viewH; if (maxY < 0) maxY = 0;
    if (g_scrollY > maxY) g_scrollY = maxY;
    if (g_scrollY < 0) g_scrollY = 0;
    SCROLLINFO si; ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0; si.nMax = total; si.nPage = (UINT)viewH; si.nPos = g_scrollY;
    SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
}

static void PaintPv(HWND hWnd, HDC hdc)
{
    RECT rc; PvView(hWnd, &rc);
    IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
    float sc = PvScale(rc.right);
    int pageH = PvPageH(sc);
    int pageW = (int)(PV_PAPER_W * sc);
    int left = (rc.right - pageW) / 2;
    if (left < 0) left = 0;

    HBRUSH bg = CreateSolidBrush(RGB(128, 128, 128));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    int pg;
    for (pg = 0; pg < g_pages; pg++) {
        int top = rc.top + (int)(PV_GAP_PX + (float)pg * ((float)pageH + PV_GAP_PX)) - g_scrollY;
        if (top > rc.bottom || top + pageH < rc.top) continue;
        RECT r = { left, top, left + pageW, top + pageH };
        HBRUSH paper = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(hdc, &r, paper); DeleteObject(paper);
        FrameRect(hdc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));
    int i;
    for (i = 0; i < g_glCount; i++) {
        const Glyph *g = &g_gl[i];
        int top = rc.top + (int)(PV_GAP_PX + (float)g->page * ((float)pageH + PV_GAP_PX)) - g_scrollY;
        if (top > rc.bottom || top + pageH < rc.top) continue;
        float lineTop = PV_TOP_PX + (float)g->line * g_lineH;
        float glyphH = 16.0f;
        float yOff = 0.0f;
        if (g->flags & GF_SUP) { glyphH = 10.0f; }
        if (g->flags & GF_SUB) { glyphH = 10.0f; yOff = 6.0f; }
        int px = left + (int)(g->x * sc);
        int py = top + (int)((lineTop + yOff) * sc);
        int fh = (int)(glyphH * sc); if (fh < 4) fh = 4;
        int fw = (int)(g->cell * sc); if (fw < 2) fw = 2;
        int weight = (g->flags & GF_BOLD) ? FW_BOLD : FW_NORMAL;
        HFONT old = (HFONT)SelectObject(hdc, GetPvFont(fh, fw, weight));
        char c = (char)g->ch;
        TextOutA(hdc, px, py, &c, 1);
        if (g->flags & GF_DSTRIKE) TextOutA(hdc, px, py + 1, &c, 1); /* второй проход иглами чуть ниже */
        if (g->flags & GF_UNDER) {
            int uy = top + (int)((lineTop + 15.0f) * sc);
            MoveToEx(hdc, px, uy, NULL); LineTo(hdc, px + (int)(g->cell * sc), uy);
        }
        SelectObject(hdc, old);
    }
}

/* Панель кнопок сверху: «Печать» и «Закрыть» (картинки берём из той же полосы toolbar.bmp,
   что и главный тулбар; если файла нет — кнопки только с текстом). */
static void CreatePvToolbar(HWND hWnd)
{
    char path[MAX_PATH];
    HIMAGELIST himl = NULL;
    GetModuleFileNameA(NULL, path, MAX_PATH);
    { char *sl = strrchr(path, '\\'); if (sl) *(sl + 1) = 0; }
    strcat(path, "toolbar.bmp");
    HBITMAP hStrip = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    if (hStrip) {
        himl = ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK, 20, 4);
        ImageList_AddMasked(himl, hStrip, RGB(192, 192, 192));
        DeleteObject(hStrip);
    }
    g_hPvTb = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS,
        0, 0, 0, 0, hWnd, (HMENU)100, GetModuleHandleA(NULL), NULL);
    if (!g_hPvTb) return;
    SendMessageA(g_hPvTb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    if (himl) SendMessageA(g_hPvTb, TB_SETIMAGELIST, 0, (LPARAM)himl);
    SendMessageA(g_hPvTb, TB_SETMAXTEXTROWS, 1, 0);
    {
        static const char labels[] = "Печать\0Закрыть\0";
        int first = (int)SendMessageA(g_hPvTb, TB_ADDSTRINGA, 0, (LPARAM)labels);
        TBBUTTON b[2]; ZeroMemory(b, sizeof(b));
        b[0].iBitmap = himl ? PV_IMG_PRINT : 0; b[0].idCommand = PV_CMD_PRINT; b[0].fsState = TBSTATE_ENABLED; b[0].fsStyle = TBSTYLE_BUTTON; b[0].iString = first;
        b[1].iBitmap = himl ? PV_IMG_CLOSE : 0; b[1].idCommand = PV_CMD_CLOSE; b[1].fsState = TBSTATE_ENABLED; b[1].fsStyle = TBSTYLE_BUTTON; b[1].iString = first + 1;
        SendMessageA(g_hPvTb, TB_ADDBUTTONSA, 2, (LPARAM)b);
    }
    SendMessageA(g_hPvTb, TB_AUTOSIZE, 0, 0);
    { RECT r; GetWindowRect(g_hPvTb, &r); g_tbH = r.bottom - r.top; }
}

static LRESULT CALLBACK PvProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RECT rc;
    switch (msg) {
    case WM_CREATE:
        CreatePvToolbar(hWnd);
        return 0;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lParam;
        if (nh->code == TTN_GETDISPINFOA) {
            NMTTDISPINFOA *di = (NMTTDISPINFOA *)lParam;
            if (nh->idFrom == PV_CMD_PRINT) di->lpszText = "Напечатать (Ctrl+P)";
            else if (nh->idFrom == PV_CMD_CLOSE) di->lpszText = "Закрыть предпросмотр (Esc)";
        }
        return 0;
    }
    case WM_SIZE:
        if (g_hPvTb) {
            SendMessageA(g_hPvTb, WM_SIZE, 0, 0);
            { RECT r; GetWindowRect(g_hPvTb, &r); g_tbH = r.bottom - r.top; }
        }
        UpdateScroll(hWnd);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        PaintPv(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_VSCROLL: {
        PvView(hWnd, &rc);
        int viewH = rc.bottom - rc.top;
        int page = viewH > 40 ? viewH - 20 : viewH;
        int y = g_scrollY;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: y -= 32; break;
        case SB_LINEDOWN: y += 32; break;
        case SB_PAGEUP: y -= page; break;
        case SB_PAGEDOWN: y += page; break;
        case SB_TOP: y = 0; break;
        case SB_BOTTOM: y = 1 << 24; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: {
            SCROLLINFO si; ZeroMemory(&si, sizeof(si)); si.cbSize = sizeof(si); si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hWnd, SB_VERT, &si);
            y = si.nTrackPos; break;
        }
        }
        g_scrollY = y;
        UpdateScroll(hWnd);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == PV_CMD_PRINT && g_job) {
            char err[256] = "";
            if (!Sm6337_PrintRaw(g_job, g_jobLen, err, 256))
                MessageBoxA(hWnd, err, "Ошибка печати", MB_OK | MB_ICONERROR);
        } else if (LOWORD(wParam) == PV_CMD_CLOSE) {
            DestroyWindow(hWnd);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == 'P' && (GetKeyState(VK_CONTROL) & 0x8000)) { SendMessageA(hWnd, WM_COMMAND, PV_CMD_PRINT, 0); return 0; }
        switch (wParam) {
        case VK_ESCAPE: DestroyWindow(hWnd); return 0;
        case VK_UP: SendMessageA(hWnd, WM_VSCROLL, SB_LINEUP, 0); return 0;
        case VK_DOWN: SendMessageA(hWnd, WM_VSCROLL, SB_LINEDOWN, 0); return 0;
        case VK_PRIOR: SendMessageA(hWnd, WM_VSCROLL, SB_PAGEUP, 0); return 0;
        case VK_NEXT: SendMessageA(hWnd, WM_VSCROLL, SB_PAGEDOWN, 0); return 0;
        case VK_HOME: SendMessageA(hWnd, WM_VSCROLL, SB_TOP, 0); return 0;
        case VK_END: SendMessageA(hWnd, WM_VSCROLL, SB_BOTTOM, 0); return 0;
        }
        break;
    case WM_DESTROY:
        FreePv();
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

void Sm6337_ShowPreview(HWND owner, HINSTANCE hInst, const unsigned char *job, DWORD len, const Sm6337Settings *s)
{
    static BOOL registered = FALSE;
    if (g_hPv) { DestroyWindow(g_hPv); }
    if (!registered) {
        WNDCLASSA wc; ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = PvProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
        wc.lpszClassName = "SM6337PreviewClass";
        RegisterClassA(&wc);
        registered = TRUE;
    }
    g_job = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, len ? len : 1);
    if (g_job) { memcpy(g_job, job, len); g_jobLen = len; }
    if (!ParseJob(job, len, s)) {
        MessageBoxA(owner, "Не хватило памяти для предпросмотра.", "Предпросмотр", MB_OK | MB_ICONERROR);
        return;
    }
    g_scrollY = 0;

    /* в заголовке — то, что реально дошло до предпросмотра: интервал, поле, строк на листе */
    char title[200] = "Предпросмотр печати — страниц: ";
    char n[12]; UIntToStrA((unsigned long)g_pages, n);
    strcat(title, n);
    strcat(title, ", интервал: ");
    if (s->lineSpacing > 0) { UIntToStrA((unsigned long)s->lineSpacing, n); strcat(title, n); strcat(title, "x0,353 мм"); }
    else strcat(title, "стандартный");
    strcat(title, ", строк на листе: ");
    UIntToStrA((unsigned long)g_linesPerPage, n); strcat(title, n);
    if (s->leftMargin > 0) { strcat(title, ", поле: "); UIntToStrA((unsigned long)s->leftMargin, n); strcat(title, n); }
    HMENU hMenu = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    AppendMenuA(hFile, MF_STRING, PV_CMD_PRINT, "&Печать\tCtrl+P");
    AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFile, MF_STRING, PV_CMD_CLOSE, "&Закрыть\tEsc");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "&Файл");
    g_hPv = CreateWindowA("SM6337PreviewClass", title,
        WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 560, owner, hMenu, hInst, NULL);
    if (g_hPv) { UpdateScroll(g_hPv); SetFocus(g_hPv); }
}
