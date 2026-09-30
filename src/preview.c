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
#include "resource.h"

#define PV_PORTRAIT_W  794.0f    /* A4 книжный: 210 мм = 8,27" при 96 dpi */
#define PV_LANDSCAPE_W 1123.0f   /* A4 альбомный: 297 мм */
#define PV_PAPER_W   g_paperW    /* ширина листа текущей ориентации, px */
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
static float g_paperW = 794.0f; /* ширина листа: книжный 794, альбомный 1123 px */
/* ширина всего, что надо показать: лист плюс то, что выходит за его правый край (если строка длиннее
   листа — например, лимит 120 знаков на листе, где помещается 82). Выступающее рисуем за краем листа. */
static float g_contentW = 794.0f;
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

    /* Печать начинается у левого края листа с небольшого отступа (6 px = 1,6 мм), а не по центру:
       иначе при малом числе колонок строка уезжала бы вправо, хотя левое поле 0. Если строка
       из cols знаков по 10 cpi не помещается с таким отступом (82 колонки — это 8,2", A4 — 8,27"),
       отступ уменьшаем, чтобы правый край не вылезал за лист. */
    g_paperW = s->landscape ? PV_LANDSCAPE_W : PV_PORTRAIT_W;
    g_originX = PV_PAPER_W - (float)s->cols * PV_PICA_PX;
    if (g_originX > 6.0f) g_originX = 6.0f;
    if (g_originX < 0.0f) g_originX = 0.0f;
    int margin = s->leftMargin;
    if (margin >= s->cols) margin = s->cols > 1 ? s->cols - 1 : 0;
    float marginPx = (float)margin * PV_PICA_PX;
    float rightPx = g_originX + (float)s->cols * PV_PICA_PX;
    BOOL bold = FALSE, under = FALSE, elite = FALSE, cond = FALSE, dstrike = FALSE, expanded = FALSE;
    int index = 0;
    int page = 0, line = 0;
    float x = marginPx;
    float maxRight = 0.0f; /* самый правый край знаков, px от левого края листа */
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
        if (g->x + cell > maxRight) maxRight = g->x + cell;
    }
    g_contentW = PV_PAPER_W;
    if (maxRight + 6.0f > g_contentW) g_contentW = maxRight + 6.0f; /* строка шире листа: показать, насколько */
    g_pages = page + 1;
    if (g_glCount == 0) g_pages = 1;
    return TRUE;
}

/* Масштаб: 0 — «по ширине окна», иначе фиксированный (1,0 = 96 dpi, 100%). При масштабе, при
   котором лист шире окна, появляется горизонтальная прокрутка — так мелкие знаки можно увеличить. */
static float g_zoom = 0.0f;
static int g_scrollX = 0;
static BOOL g_inUpdateScroll = FALSE;

#define PV_CMD_ZOOMIN  3
#define PV_CMD_ZOOMOUT 4
#define PV_CMD_FIT     5
#define PV_IMG_ZOOM    12   /* лупа из полосы toolbar.bmp */
#define PV_IMG_FIT     9    /* лист с лупой */

static float PvScale(int viewW)
{
    float sc;
    if (g_zoom > 0.0f) return g_zoom;
    sc = ((float)viewW - 2 * PV_GAP_PX) / g_contentW;
    if (sc > 1.5f) sc = 1.5f;
    if (sc < 0.3f) sc = 0.3f;
    return sc;
}

static int PvPageH(float sc) { return (int)((PV_TOP_PX * 2 + g_pageHeightPx) * sc); }

static int PvTotalH(float sc) { return (int)(PV_GAP_PX + (float)g_pages * ((float)PvPageH(sc) + PV_GAP_PX)); }

static int PvTotalW(float sc) { return (int)(g_contentW * sc + 2 * PV_GAP_PX); }

static void PvView(HWND hWnd, RECT *rc)
{
    GetClientRect(hWnd, rc);
    rc->top = g_tbH;
    if (rc->bottom < rc->top) rc->bottom = rc->top;
}

static void UpdateScroll(HWND hWnd)
{
    RECT rc;
    float sc;
    int totalH, totalW, viewH, viewW, maxY, maxX;
    SCROLLINFO si;
    if (g_inUpdateScroll) return;   /* SetScrollInfo меняет клиентскую область и зовёт WM_SIZE */
    g_inUpdateScroll = TRUE;

    PvView(hWnd, &rc);
    viewW = rc.right - rc.left;
    viewH = rc.bottom - rc.top;
    sc = PvScale(viewW);
    totalH = PvTotalH(sc);
    totalW = PvTotalW(sc);
    maxY = totalH - viewH; if (maxY < 0) maxY = 0;
    maxX = totalW - viewW; if (maxX < 0) maxX = 0;
    if (g_scrollY > maxY) g_scrollY = maxY;
    if (g_scrollY < 0) g_scrollY = 0;
    if (g_scrollX > maxX) g_scrollX = maxX;
    if (g_scrollX < 0) g_scrollX = 0;

    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0; si.nMax = totalH; si.nPage = (UINT)viewH; si.nPos = g_scrollY;
    SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
    si.nMax = totalW; si.nPage = (UINT)viewW; si.nPos = g_scrollX;
    SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);

    g_inUpdateScroll = FALSE;
}

/* Рисуем в память и переносим одним блоком: так при растягивании окна и прокрутке нет мерцания
   и «лишних линий» от недорисованных кусков. */
static void PaintPv(HWND hWnd, HDC hdc)
{
    RECT rc;
    int viewW, viewH, pageH, pageW, contentPx, left, pg, i;
    float sc;
    HDC mem;
    HBITMAP bmp, oldBmp;
    HBRUSH bg, paper, outside, edge;
    RECT all;

    PvView(hWnd, &rc);
    viewW = rc.right - rc.left;
    viewH = rc.bottom - rc.top;
    if (viewW <= 0 || viewH <= 0) return;

    mem = CreateCompatibleDC(hdc);
    bmp = CreateCompatibleBitmap(hdc, viewW, viewH);
    oldBmp = (HBITMAP)SelectObject(mem, bmp);

    sc = PvScale(viewW);
    pageH = PvPageH(sc);
    pageW = (int)(PV_PAPER_W * sc);
    contentPx = (int)(g_contentW * sc);
    left = (contentPx + 2 * (int)PV_GAP_PX <= viewW) ? (viewW - contentPx) / 2 : (int)PV_GAP_PX - g_scrollX;

    all.left = 0; all.top = 0; all.right = viewW; all.bottom = viewH;
    bg = CreateSolidBrush(RGB(128, 128, 128));
    FillRect(mem, &all, bg);
    DeleteObject(bg);

    paper = CreateSolidBrush(RGB(255, 255, 255));
    outside = CreateSolidBrush(RGB(255, 218, 218));   /* то, что выходит за правый край листа */
    edge = CreateSolidBrush(RGB(200, 0, 0));
    for (pg = 0; pg < g_pages; pg++) {
        RECT r;
        int top = (int)(PV_GAP_PX + (float)pg * ((float)pageH + PV_GAP_PX)) - g_scrollY;
        if (top > viewH || top + pageH < 0) continue;
        if (contentPx > pageW) {
            /* строка шире листа: светло-красная полоса за краем листа и красная черта по краю бумаги —
               видно, что окажется «на валике» и насколько далеко */
            RECT o, e;
            o.left = left + pageW; o.top = top; o.right = left + contentPx; o.bottom = top + pageH;
            FillRect(mem, &o, outside);
            e.left = left + pageW - 1; e.top = top; e.right = left + pageW + 1; e.bottom = top + pageH;
            FillRect(mem, &e, edge);
        }
        r.left = left; r.top = top; r.right = left + pageW; r.bottom = top + pageH;
        FillRect(mem, &r, paper);
        FrameRect(mem, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        if (contentPx > pageW) {
            RECT e;
            e.left = left + pageW - 1; e.top = top; e.right = left + pageW + 1; e.bottom = top + pageH;
            FillRect(mem, &e, edge);   /* красная граница поверх чёрной рамки */
        }
    }
    DeleteObject(paper);
    DeleteObject(outside);
    DeleteObject(edge);

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(0, 0, 0));
    for (i = 0; i < g_glCount; i++) {
        const Glyph *g = &g_gl[i];
        int top = (int)(PV_GAP_PX + (float)g->page * ((float)pageH + PV_GAP_PX)) - g_scrollY;
        float lineTop, glyphH = 16.0f, yOff = 0.0f;
        int px, py, fh, fw, weight;
        HFONT old;
        char c;
        if (top > viewH || top + pageH < 0) continue;
        lineTop = PV_TOP_PX + (float)g->line * g_lineH;
        if (g->flags & GF_SUP) { glyphH = 10.0f; }
        if (g->flags & GF_SUB) { glyphH = 10.0f; yOff = 6.0f; }
        px = left + (int)(g->x * sc + 0.5f);
        py = top + (int)((lineTop + yOff) * sc + 0.5f);
        if (px > viewW || px + (int)(g->cell * sc) < 0) continue;
        fh = (int)(glyphH * sc + 0.5f); if (fh < 5) fh = 5;
        fw = (int)(g->cell * sc + 0.5f); if (fw < 3) fw = 3;
        weight = (g->flags & GF_BOLD) ? FW_BOLD : FW_NORMAL;
        old = (HFONT)SelectObject(mem, GetPvFont(fh, fw, weight));
        c = (char)g->ch;
        TextOutA(mem, px, py, &c, 1);
        if (g->flags & GF_DSTRIKE) TextOutA(mem, px, py + 1, &c, 1); /* второй проход иглами чуть ниже */
        if (g->flags & GF_UNDER) {
            int uy = top + (int)((lineTop + 15.0f) * sc);
            MoveToEx(mem, px, uy, NULL); LineTo(mem, px + (int)(g->cell * sc), uy);
        }
        SelectObject(mem, old);
    }

    BitBlt(hdc, rc.left, rc.top, viewW, viewH, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

/* Панель кнопок сверху: Печать, масштаб, Закрыть (картинки — из той же полосы toolbar.bmp, что и у
   главного тулбара; если файла нет — кнопки только с текстом). */
static void CreatePvToolbar(HWND hWnd)
{
    HIMAGELIST himl = NULL;
    HBITMAP hStrip = (HBITMAP)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDB_TOOLBAR), IMAGE_BITMAP, 0, 0, 0);
    if (hStrip) {
        himl = ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK, 20, 4);
        ImageList_AddMasked(himl, hStrip, RGB(192, 192, 192));
        DeleteObject(hStrip);
    }
    g_hPvTb = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER,
        0, 0, 0, 0, hWnd, (HMENU)100, GetModuleHandleA(NULL), NULL);
    if (!g_hPvTb) return;
    SendMessageA(g_hPvTb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    if (himl) SendMessageA(g_hPvTb, TB_SETIMAGELIST, 0, (LPARAM)himl);
    SendMessageA(g_hPvTb, TB_SETMAXTEXTROWS, 1, 0);
    {
        static const char labels[] = "Печать\0Крупнее\0Мельче\0По ширине\0Закрыть\0";
        int first = (int)SendMessageA(g_hPvTb, TB_ADDSTRINGA, 0, (LPARAM)labels);
        TBBUTTON b[7]; ZeroMemory(b, sizeof(b));
        b[0].iBitmap = himl ? PV_IMG_PRINT : 0; b[0].idCommand = PV_CMD_PRINT;   b[0].fsState = TBSTATE_ENABLED; b[0].fsStyle = TBSTYLE_BUTTON; b[0].iString = first;
        b[1].fsStyle = TBSTYLE_SEP;
        b[2].iBitmap = himl ? PV_IMG_ZOOM : 0;  b[2].idCommand = PV_CMD_ZOOMIN;  b[2].fsState = TBSTATE_ENABLED; b[2].fsStyle = TBSTYLE_BUTTON; b[2].iString = first + 1;
        b[3].iBitmap = himl ? PV_IMG_ZOOM : 0;  b[3].idCommand = PV_CMD_ZOOMOUT; b[3].fsState = TBSTATE_ENABLED; b[3].fsStyle = TBSTYLE_BUTTON; b[3].iString = first + 2;
        b[4].iBitmap = himl ? PV_IMG_FIT : 0;   b[4].idCommand = PV_CMD_FIT;     b[4].fsState = TBSTATE_ENABLED; b[4].fsStyle = TBSTYLE_BUTTON; b[4].iString = first + 3;
        b[5].fsStyle = TBSTYLE_SEP;
        b[6].iBitmap = himl ? PV_IMG_CLOSE : 0; b[6].idCommand = PV_CMD_CLOSE;   b[6].fsState = TBSTATE_ENABLED; b[6].fsStyle = TBSTYLE_BUTTON; b[6].iString = first + 4;
        SendMessageA(g_hPvTb, TB_ADDBUTTONSA, 7, (LPARAM)b);
    }
    SendMessageA(g_hPvTb, TB_AUTOSIZE, 0, 0);
    { RECT r; GetWindowRect(g_hPvTb, &r); g_tbH = r.bottom - r.top; }
}

/* Смена масштаба с сохранением положения на листе (прокрутка пересчитывается пропорционально). */
static void PvSetZoom(HWND hWnd, float newZoom)
{
    RECT rc;
    float oldSc, newSc;
    PvView(hWnd, &rc);
    oldSc = PvScale(rc.right - rc.left);
    if (newZoom > 3.0f) newZoom = 3.0f;
    if (newZoom > 0.0f && newZoom < 0.3f) newZoom = 0.3f;
    g_zoom = newZoom;
    newSc = PvScale(rc.right - rc.left);
    if (oldSc > 0.0f) {
        g_scrollY = (int)((float)g_scrollY * newSc / oldSc);
        g_scrollX = (int)((float)g_scrollX * newSc / oldSc);
    }
    if (newZoom == 0.0f) g_scrollX = 0;
    UpdateScroll(hWnd);
    InvalidateRect(hWnd, NULL, FALSE);
}

static void PvZoomBy(HWND hWnd, float factor)
{
    RECT rc;
    float cur;
    PvView(hWnd, &rc);
    cur = PvScale(rc.right - rc.left);
    PvSetZoom(hWnd, cur * factor);
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
            else if (nh->idFrom == PV_CMD_ZOOMIN) di->lpszText = "Увеличить (+)";
            else if (nh->idFrom == PV_CMD_ZOOMOUT) di->lpszText = "Уменьшить (-)";
            else if (nh->idFrom == PV_CMD_FIT) di->lpszText = "Лист по ширине окна (0)";
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
    case WM_ERASEBKGND: {
        /* Лист мы рисуем целиком сами, а вот плоский тулбар (TBSTYLE_FLAT) просит у родителя фон под
           кнопками. Если на этот запрос не отвечать, под нажатой или подсвеченной кнопкой остаются
           разводы от предыдущей отрисовки. Поэтому полосу под панелью красим цветом кнопок. */
        RECT band;
        GetClientRect(hWnd, &band);
        band.bottom = g_tbH;
        FillRect((HDC)wParam, &band, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }
    case WM_VSCROLL: {
        int viewH, page, y;
        PvView(hWnd, &rc);
        viewH = rc.bottom - rc.top;
        page = viewH > 40 ? viewH - 20 : viewH;
        y = g_scrollY;
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
    case WM_HSCROLL: {
        int viewW, page, x;
        PvView(hWnd, &rc);
        viewW = rc.right - rc.left;
        page = viewW > 40 ? viewW - 20 : viewW;
        x = g_scrollX;
        switch (LOWORD(wParam)) {
        case SB_LINELEFT: x -= 32; break;
        case SB_LINERIGHT: x += 32; break;
        case SB_PAGELEFT: x -= page; break;
        case SB_PAGERIGHT: x += page; break;
        case SB_LEFT: x = 0; break;
        case SB_RIGHT: x = 1 << 24; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: {
            SCROLLINFO si; ZeroMemory(&si, sizeof(si)); si.cbSize = sizeof(si); si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hWnd, SB_HORZ, &si);
            x = si.nTrackPos; break;
        }
        }
        g_scrollX = x;
        UpdateScroll(hWnd);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case PV_CMD_PRINT:
            if (g_job) {
                char err[256] = "";
                if (!Sm6337_PrintRaw(g_job, g_jobLen, err, 256))
                    MessageBoxA(hWnd, err, "Ошибка печати", MB_OK | MB_ICONERROR);
            }
            break;
        case PV_CMD_CLOSE: DestroyWindow(hWnd); break;
        case PV_CMD_ZOOMIN: PvZoomBy(hWnd, 1.25f); break;
        case PV_CMD_ZOOMOUT: PvZoomBy(hWnd, 0.8f); break;
        case PV_CMD_FIT: PvSetZoom(hWnd, 0.0f); break;
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
        case VK_LEFT: SendMessageA(hWnd, WM_HSCROLL, SB_LINELEFT, 0); return 0;
        case VK_RIGHT: SendMessageA(hWnd, WM_HSCROLL, SB_LINERIGHT, 0); return 0;
        case VK_ADD: case VK_OEM_PLUS: PvZoomBy(hWnd, 1.25f); return 0;
        case VK_SUBTRACT: case VK_OEM_MINUS: PvZoomBy(hWnd, 0.8f); return 0;
        case '0': case VK_NUMPAD0: PvSetZoom(hWnd, 0.0f); return 0;
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
    char title[200] = "Предпросмотр печати — страниц: ";
    char n[12];
    HMENU hMenu, hFile;
    if (g_hPv) { DestroyWindow(g_hPv); }
    if (!registered) {
        WNDCLASSA wc; ZeroMemory(&wc, sizeof(wc));
        wc.style = CS_HREDRAW | CS_VREDRAW;
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
    g_scrollY = 0; g_scrollX = 0; g_zoom = 0.0f;

    /* в заголовке — то, что реально дошло до предпросмотра: интервал, поле, строк на листе */
    UIntToStrA((unsigned long)g_pages, n);
    strcat(title, n);
    strcat(title, ", интервал: ");
    if (s->lineSpacing > 0) { UIntToStrA((unsigned long)s->lineSpacing, n); strcat(title, n); strcat(title, "x0,353 мм"); }
    else strcat(title, "стандартный");
    strcat(title, ", строк на листе: ");
    UIntToStrA((unsigned long)g_linesPerPage, n); strcat(title, n);
    if (s->leftMargin > 0) { strcat(title, ", поле: "); UIntToStrA((unsigned long)s->leftMargin, n); strcat(title, n); }
    if (s->landscape) strcat(title, ", альбомная");
    if (g_contentW > PV_PAPER_W + 0.5f) {
        /* сколько колонок (по 10 знаков на дюйм) выходит за правый край листа */
        int over = (int)((g_contentW - 6.0f - PV_PAPER_W) / PV_PICA_PX + 0.5f);
        if (over < 1) over = 1;
        UIntToStrA((unsigned long)over, n);
        strcat(title, ", за краем листа: "); strcat(title, n); strcat(title, " кол.");
    }
    hMenu = CreateMenu();
    hFile = CreatePopupMenu();
    AppendMenuA(hFile, MF_STRING, PV_CMD_PRINT, "&Печать\tCtrl+P");
    AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFile, MF_STRING, PV_CMD_CLOSE, "&Закрыть\tEsc");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "&Файл");
    g_hPv = CreateWindowA("SM6337PreviewClass", title,
        WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL | WS_CLIPCHILDREN | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, s->landscape ? 960 : 760, 600, owner, hMenu, hInst, NULL);
    if (g_hPv) { UpdateScroll(g_hPv); SetFocus(g_hPv); }
}
