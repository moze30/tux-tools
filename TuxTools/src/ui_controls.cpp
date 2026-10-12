/*
 * Tux-Tools - native Win32 edition
 *
 * Lightweight custom-drawn controls for the light Docker-Desktop-style theme:
 *  - the section list is a navigation list (white background, rounded hover and
 *    light-blue selected state with an accent bar);
 *  - the value list is a report-mode list view with a custom light header and
 *    row hover / selection highlighting.
 *
 * Colours, spacing, type and radii come from theme.h.
 */

#include "ui_controls.h"
#include "theme.h"
#include "dpi.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <string>
#include "i18n.h"

namespace ui {

const wchar_t* const kSectionListClass = L"ViewerSectionList";
const wchar_t* const kSearchBoxClass = L"ViewerSearchBox";

namespace {

struct SectionData {
    std::vector<std::wstring> titles;
    std::vector<int> counts;
    int selected = 0;
    int hot = -1;
};

struct SearchData {
    std::wstring text;
    std::wstring placeholder = i18n::tr(L"搜索属性...");
    bool focused = false;
};

struct ValueData {
    int hot = -1;
    // The property-name column width, kept here so the row painter never has to
    // ask the control for it. See valueListCustomDraw.
    int nameWidth = 0;
};

SectionData* sectionData(HWND hwnd) {
    return reinterpret_cast<SectionData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

SearchData* searchData(HWND hwnd) {
    return reinterpret_cast<SearchData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

// The list's own data hangs off a window property rather than only off the
// subclass reference, because two different window procedures need it: the
// subclass handles the header, and the parent window - the real destination of
// the list's custom draw - paints the rows.
const wchar_t* const kValueDataProp = L"TuxToolsValueData";

ValueData* valueData(HWND hwnd) {
    return reinterpret_cast<ValueData*>(GetPropW(hwnd, kValueDataProp));
}

// One cached font per (size, weight, text-scale) triple, made on first use.
// There are only a handful in the whole program, so a small fixed table is
// simpler than a map and avoids allocating during a paint.
//
// The generation is part of the key rather than a reason to clear the table:
// when the user changes the text size the old entries are simply abandoned and
// the next lookup at that size makes a new face. Keeping them costs a few font
// objects and saves rebuilding everything on every step of the slider.
struct FontEntry {
    int size;
    bool strong;
    int generation;
    HFONT font;
};

const int kFontCacheSlots = 16;
FontEntry g_fontCache[kFontCacheSlots] = {};
int g_fontCacheUsed = 0;

}  // namespace

// Drops every face made for an older text size. Faces of the current size are
// kept: the value list still has one selected, and deleting it underneath the
// control would leave it drawing with a dead object.
void resetFontCache() {
    const int generation = theme::fontGeneration();
    int write = 0;
    for (int read = 0; read < g_fontCacheUsed; ++read) {
        FontEntry& entry = g_fontCache[read];
        if (entry.generation == generation) {
            g_fontCache[write++] = entry;
        } else if (entry.font) {
            DeleteObject(entry.font);
        }
    }
    for (int i = write; i < g_fontCacheUsed; ++i) g_fontCache[i] = {};
    g_fontCacheUsed = write;
}

HFONT font(int designPx, bool semibold) {
    const int generation = theme::fontGeneration();
    for (int i = 0; i < g_fontCacheUsed; ++i) {
        if (g_fontCache[i].size == designPx && g_fontCache[i].strong == semibold &&
            g_fontCache[i].generation == generation) {
            return g_fontCache[i].font;
        }
    }

    HFONT made = CreateFontW(-theme::font(designPx), 0, 0, 0,
                             semibold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH,
                             L"Segoe UI Variable Text");
    if (g_fontCacheUsed < kFontCacheSlots) {
        FontEntry& slot = g_fontCache[g_fontCacheUsed++];
        slot.size = designPx;
        slot.strong = semibold;
        slot.generation = generation;
        slot.font = made;
    }
    return made;
}

namespace {

HFONT cachedFont(int designPx, bool semibold) { return font(designPx, semibold); }

void fillRect(HDC dc, const RECT& rc, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

void fillRoundRect(HDC dc, const RECT& rc, int radius, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_NULL, 0, fill);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void drawTextClipped(HDC dc, const std::wstring& text, RECT rc, COLORREF color, HFONT font,
                     UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc, format);
    SelectObject(dc, oldFont);
}

int sectionIndexFromPoint(HWND hwnd, POINT pt) {
    SectionData* data = sectionData(hwnd);
    if (!data) return -1;
    int index = (pt.y - theme::kSpaceS) / theme::kNavRowHeight;
    if (index < 0 || index >= static_cast<int>(data->titles.size())) return -1;
    return index;
}

void notifyParent(HWND hwnd) {
    HWND parent = GetParent(hwnd);
    if (!parent) return;
    int id = GetDlgCtrlID(hwnd);
    SendMessageW(parent, WM_COMMAND,
                 MAKEWPARAM(id, LBN_SELCHANGE), reinterpret_cast<LPARAM>(hwnd));
}

void notifySearchChanged(HWND hwnd) {
    HWND parent = GetParent(hwnd);
    if (!parent) return;
    int id = GetDlgCtrlID(hwnd);
    SendMessageW(parent, WM_COMMAND,
                 MAKEWPARAM(id, EN_CHANGE), reinterpret_cast<LPARAM>(hwnd));
}

LRESULT sectionProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SectionData* data = sectionData(hwnd);
    switch (msg) {
        case WM_CREATE:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(new SectionData()));
            return 0;
        case WM_DESTROY:
            delete data;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        case WM_MOUSEMOVE: {
            if (!data) return 0;
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            int idx = sectionIndexFromPoint(hwnd, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (idx != data->hot) {
                data->hot = idx;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (data && data->hot != -1) {
                data->hot = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            if (!data) return 0;
            int idx = sectionIndexFromPoint(hwnd, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (idx >= 0 && idx != data->selected) {
                data->selected = idx;
                InvalidateRect(hwnd, nullptr, FALSE);
                notifyParent(hwnd);
            }
            SetFocus(hwnd);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HDC memDc = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

            fillRect(memDc, rc, theme::kSidebarBg);

            if (data) {
                HFONT font = cachedFont(theme::kFontBody, false);
                HFONT boldFont = cachedFont(theme::kFontBody, true);
                const int padH = theme::kSpaceL;
                const int accentBar = 3;

                int y = theme::kSpaceS;
                for (size_t i = 0; i < data->titles.size(); ++i) {
                    RECT row = {theme::kSpaceS, y, rc.right - theme::kSpaceS,
                                y + theme::kNavRowHeight};
                    bool isSelected = static_cast<int>(i) == data->selected;
                    bool isHot = static_cast<int>(i) == data->hot;

                    if (isSelected) {
                        fillRoundRect(memDc, row, theme::kRadiusControl, theme::kNavSelected);
                        RECT bar = {row.left, y + 8, row.left + accentBar,
                                    y + theme::kNavRowHeight - 8};
                        fillRoundRect(memDc, bar, 2, theme::kAccent);
                    } else if (isHot) {
                        fillRoundRect(memDc, row, theme::kRadiusControl, theme::kNavHover);
                    }

                    bool hasCount = i < data->counts.size() && data->counts[i] >= 0;
                    RECT textRc = {row.left + padH, y, row.right - (hasCount ? 56 : padH),
                                   y + theme::kNavRowHeight};
                    drawTextClipped(memDc, data->titles[i], textRc,
                                    isSelected ? theme::kAccent : theme::kSidebarText,
                                    isSelected ? boldFont : font);

                    if (hasCount) {
                        RECT countRc = {row.right - 56, y, row.right - theme::kSpaceM,
                                        y + theme::kNavRowHeight};
                        drawTextClipped(memDc, std::to_wstring(data->counts[i]), countRc,
                                        theme::kSidebarMuted, font,
                                        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    }
                    y += theme::kNavRowHeight;
                }
            }

            BitBlt(dc, 0, 0, rc.right, rc.bottom, memDc, 0, 0, SRCCOPY);
            SelectObject(memDc, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(memDc);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// --- Value list (report-mode list view, light theme) ------------------------

void drawListHeaderItem(HDC dc, HWND header, int index) {
    RECT itemRc;
    if (!Header_GetItemRect(header, index, &itemRc)) return;
    fillRect(dc, itemRc, theme::kContentBg);

    wchar_t text[128] = {};
    HDITEMW item = {};
    item.mask = HDI_TEXT;
    item.pszText = text;
    item.cchTextMax = 128;
    Header_GetItem(header, index, &item);

    HFONT font = cachedFont(theme::kFontTable, true);
    RECT textRc = itemRc;
    textRc.left += theme::kSpaceM;
    drawTextClipped(dc, text, textRc, theme::kTableHeaderText, font);

    RECT bottom = {itemRc.left, itemRc.bottom - 1, itemRc.right, itemRc.bottom};
    fillRect(dc, bottom, theme::kDivider);
}

// Reads one cell's text, growing the buffer until the control stops reporting
// that it truncated. The values in this table are driver strings, and a fixed
// buffer silently cut the longest of them off at the point where they are most
// interesting.
std::wstring valueListCellText(HWND list, int item, int subItem) {
    std::wstring text;
    for (int cap = 256; cap <= 65536; cap *= 2) {
        text.assign(static_cast<size_t>(cap), L'\0');
        LVITEMW it = {};
        it.iSubItem = subItem;
        it.pszText = &text[0];
        it.cchTextMax = cap;
        const int n = static_cast<int>(
            SendMessageW(list, LVM_GETITEMTEXTW, static_cast<WPARAM>(item),
                         reinterpret_cast<LPARAM>(&it)));
        if (n < cap - 1) {
            text.resize(n > 0 ? static_cast<size_t>(n) : 0);
            return text;
        }
    }
    return text;
}

}  // namespace

LRESULT valueListCustomDraw(HWND list, NMHDR* hdr) {
    if (!list || !hdr) return CDRF_DODEFAULT;
    LPNMLVCUSTOMDRAW lvcd = reinterpret_cast<LPNMLVCUSTOMDRAW>(hdr);
    ValueData* data = valueData(list);

    switch (lvcd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;

        case CDDS_ITEMPREPAINT: {
            const int item = static_cast<int>(lvcd->nmcd.dwItemSpec);
            HDC dc = lvcd->nmcd.hdc;

            // The row's top and bottom come from the notification, because they
            // track the scroll position; its left and right do not.
            //
            // The width the control reports is not trustworthy. It is derived
            // from a total width the control caches and only refreshes from
            // HDN_ITEMCHANGED; while the subclass was swallowing those, the list
            // kept the total it computed at creation while it was still
            // zero-sized and handed every row over as a twelve-pixel sliver, so
            // the text drawn into it was invisible and the table looked empty.
            // The columns we set are the authority instead.
            RECT client;
            GetClientRect(list, &client);
            const int right = client.right - GetSystemMetrics(SM_CXVSCROLL);
            RECT row = lvcd->nmcd.rc;
            row.left = 0;
            row.right = right;

            const bool selected = (lvcd->nmcd.uItemState & CDIS_SELECTED) != 0;
            const bool hot = data && item == data->hot;

            COLORREF textColor;
            if (selected) {
                fillRect(dc, row, theme::kTableSelected);
                textColor = theme::kAccent;
            } else if (hot) {
                fillRect(dc, row, theme::kTableRowHover);
                textColor = theme::kTableText;
            } else {
                fillRect(dc, row, theme::kContentBg);
                textColor = theme::kTableText;
            }

            int nameWidth = data ? data->nameWidth : 0;
            if (nameWidth <= 0) nameWidth = right * 42 / 100;
            const int pad = theme::kSpaceM;
            HFONT face = cachedFont(theme::kFontTable, false);

            RECT nameRc = {pad, row.top, nameWidth - pad, row.bottom};
            if (nameRc.right < nameRc.left) nameRc.right = nameRc.left;
            drawTextClipped(dc, valueListCellText(list, item, 0), nameRc, textColor, face);

            RECT valueRc = {nameWidth + pad, row.top, right - pad, row.bottom};
            if (valueRc.right < valueRc.left) valueRc.right = valueRc.left;
            drawTextClipped(dc, valueListCellText(list, item, 1), valueRc, textColor, face);

            RECT line = {0, row.bottom - 1, right, row.bottom};
            fillRect(dc, line, theme::kDivider);

            // Both columns are drawn above, so the control must not draw the
            // item itself; skipping the default is also what keeps it from
            // painting a default selection highlight over the row.
            return CDRF_SKIPDEFAULT;
        }
    }
    return CDRF_DODEFAULT;
}

namespace {

LRESULT CALLBACK valueListSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR /*ref*/, DWORD_PTR refData) {
    ValueData* data = reinterpret_cast<ValueData*>(refData);
    switch (msg) {
        case WM_MOUSEMOVE: {
            if (!data) break;
            LVHITTESTINFO hit = {};
            hit.pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int item = ListView_HitTest(hwnd, &hit);
            if ((hit.flags & LVHT_ONITEM) == 0) item = -1;
            if (item != data->hot) {
                data->hot = item;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            break;
        }
        case WM_MOUSELEAVE:
            if (data && data->hot != -1) {
                data->hot = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_NCDESTROY:
            RemovePropW(hwnd, kValueDataProp);
            delete data;
            break;
        case WM_NOTIFY: {
            LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lParam);
            // Only the header's custom draw is handled here, and everything else
            // the header sends has to reach the list itself.
            //
            // The header is a child of the list view, so its notifications pass
            // through this subclass on the way to their real recipient, which is
            // the list view. Returning zero for them - which is what this did -
            // swallows HDN_ITEMCHANGED among others, and the list is then left
            // with the widths it cached at creation: it never learns the columns
            // were resized, and the rows are clipped into whatever total width
            // the very first, zero-sized layout produced.
            //
            // A resize still has to be tracked here, because the row painter
            // needs the current width. It is read back from the header rather
            // than recomputed: setting the columns from inside this notification
            // would raise another one and the two would trade it back and forth.
            if (hdr->code == HDN_ITEMCHANGEDW || hdr->code == HDN_ITEMCHANGEDA) {
                HWND hd = ListView_GetHeader(hwnd);
                RECT r0;
                if (hd && Header_GetItemRect(hd, 0, &r0)) {
                    if (ValueData* d = valueData(hwnd)) {
                        d->nameWidth = r0.right - r0.left;
                    }
                }
                break;
            }
            if (hdr->code != NM_CUSTOMDRAW) break;

            HWND header = ListView_GetHeader(hwnd);
            LPNMCUSTOMDRAW cd = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);
            if (hdr->hwndFrom == header) {
                switch (cd->dwDrawStage) {
                    case CDDS_PREPAINT: {
                        RECT rc;
                        GetClientRect(header, &rc);
                        fillRect(cd->hdc, rc, theme::kContentBg);
                        return CDRF_NOTIFYITEMDRAW;
                    }
                    case CDDS_ITEMPREPAINT:
                        drawListHeaderItem(cd->hdc, header,
                                           static_cast<int>(cd->dwItemSpec));
                        return CDRF_SKIPDEFAULT;
                }
                return CDRF_DODEFAULT;
            }

            // The list's own custom draw never arrives here: a control notifies
            // its parent, and this window is not the list's parent - the main
            // window is. Rows are painted there, by valueListCustomDraw.
            return CDRF_DODEFAULT;
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            fillRect(reinterpret_cast<HDC>(wParam), rc, theme::kContentBg);
            return 1;
        }
        case WM_SIZE:
            // The two columns divide whatever width the control has, and the
            // scrollbar takes a slice of that whenever it appears. Without this
            // the widths are only correct at the moment the rows were set, and a
            // resize leaves them either short of the edge or wide enough to
            // summon a horizontal scrollbar of their own.
            valueListResizeColumns(hwnd);
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

}  // namespace

// --- Search box (magnifier + placeholder) -----------------------------------

namespace {

void drawMagnifier(HDC dc, int cx, int cy, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, cx - 6, cy - 6, cx + 3, cy + 3);
    MoveToEx(dc, cx + 2, cy + 2, nullptr);
    LineTo(dc, cx + 6, cy + 6);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

LRESULT CALLBACK searchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SearchData* data = searchData(hwnd);
    switch (msg) {
        case WM_CREATE:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(new SearchData()));
            return 0;
        case WM_DESTROY:
            delete searchData(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            if (data) {
                data->focused = (msg == WM_SETFOCUS);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_CHAR: {
            if (!data) return 0;
            wchar_t ch = static_cast<wchar_t>(wParam);
            if (ch == L'\b') {
                if (!data->text.empty()) data->text.pop_back();
            } else if (ch >= 32) {
                data->text.push_back(ch);
            } else {
                return 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            notifySearchChanged(hwnd);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            fillRoundRect(dc, rc, theme::kRadiusControl, theme::kSearchFill);

            int cy = (rc.top + rc.bottom) / 2;
            drawMagnifier(dc, rc.left + 16, cy, theme::kSidebarMuted);

            HFONT font = cachedFont(theme::kFontBody, false);
            RECT textRc = {rc.left + 30, rc.top, rc.right - theme::kSpaceM, rc.bottom};
            if (data && !data->text.empty()) {
                drawTextClipped(dc, data->text, textRc, theme::kSidebarText, font);
                RECT caret = {textRc.left, 0, 0, 0};
                SIZE size = {};
                HGDIOBJ old = SelectObject(dc, font);
                GetTextExtentPoint32W(dc, data->text.c_str(),
                                      static_cast<int>(data->text.size()), &size);
                SelectObject(dc, old);
                caret.left = textRc.left + size.cx + 1;
                if (data->focused) {
                    caret.top = rc.top + 7;
                    caret.right = caret.left + 1;
                    caret.bottom = rc.bottom - 7;
                    fillRect(dc, caret, theme::kSidebarText);
                }
            } else if (data) {
                drawTextClipped(dc, data->placeholder, textRc, theme::kSidebarMuted, font);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

void registerControlClasses(HINSTANCE instance) {
    WNDCLASSW section = {};
    section.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    section.lpfnWndProc = sectionProc;
    section.hInstance = instance;
    section.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    section.lpszClassName = kSectionListClass;
    RegisterClassW(&section);

    WNDCLASSW search = {};
    search.style = CS_HREDRAW | CS_VREDRAW;
    search.lpfnWndProc = searchProc;
    search.hInstance = instance;
    search.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    search.lpszClassName = kSearchBoxClass;
    RegisterClassW(&search);
}

HWND createSearchBox(HWND parent, HINSTANCE instance, int id) {
    return CreateWindowExW(0, kSearchBoxClass, L"",
                           WS_CHILD | WS_VISIBLE,
                           0, 0, 0, 0, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           instance, nullptr);
}

void searchBoxGetText(HWND hwnd, std::wstring& out) {
    SearchData* data = searchData(hwnd);
    out = data ? data->text : std::wstring();
}

void searchBoxSetText(HWND hwnd, const std::wstring& text) {
    SearchData* data = searchData(hwnd);
    if (!data) return;
    data->text = text;
    InvalidateRect(hwnd, nullptr, FALSE);
}

HWND createValueList(HWND parent, HINSTANCE instance, int id) {
    // WS_VSCROLL is what the table needs, and there is a reason it has to be
    // asked for here rather than inherited.
    //
    // The control's non-client area is removed so the table sits flush in the
    // content card, and the built-in scrollbar lives in that area; something had
    // to put it back. A hand-drawn bar was tried first and abandoned: it had to
    // guess its own hit rectangle from the client area while the control's real
    // geometry — the header sitting on the top of the client rectangle — meant
    // the guess was off by the header's height, so the pointer had to be moved
    // sideways before a press registered. It also only moved a row at a time.
    // The system's own bar has neither problem, so the table uses it and pays
    // for it with a permanent gutter, which is the honest cost of a control
    // that behaves the way the pointer expects.
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LVS_REPORT |
                                    LVS_SINGLESEL | LVS_SHOWSELALWAYS |
                                    LVS_NOSORTHEADER,
                                0, 0, 0, 0, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                instance, nullptr);
    ListView_SetExtendedListViewStyle(list,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    // The rows are drawn by the subclass, but the font has to be set on the
    // control: it is what decides the row height and the left offset the list
    // computes for each item. Leaving it unset gave the table the default system
    // font, visibly smaller than every other label on screen.
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<LPWSTR>(i18n::tr(L"属性"));
    col.cx = 300;
    ListView_InsertColumn(list, 0, &col);
    col.pszText = const_cast<LPWSTR>(i18n::tr(L"值"));
    col.cx = 400;
    ListView_InsertColumn(list, 1, &col);

    // After the columns exist: this sets the font on the list and the header and
    // then divides the width between the two columns.
    valueListApplyFont(list);

    ValueData* data = new ValueData();
    // Stored as a window property as well as in the subclass reference: the
    // parent window paints the rows and has no other way to reach this.
    SetPropW(list, kValueDataProp, reinterpret_cast<HANDLE>(data));
    SetWindowSubclass(list, valueListSubclass, 1, reinterpret_cast<DWORD_PTR>(data));
    return list;
}

void valueListApplyFont(HWND valueList) {
    if (!valueList) return;
    // TRUE so the control redraws itself: the row height is derived from this
    // font, so without the redraw the rows keep their old size until something
    // else happens to invalidate them.
    SendMessageW(valueList, WM_SETFONT,
                 reinterpret_cast<WPARAM>(font(theme::kFontTable, false)), TRUE);
    // The header is a separate control and derives its own height from its
    // font, so it has to be told separately or it stays at the old size while
    // the rows grow below it.
    HWND header = ListView_GetHeader(valueList);
    if (header) {
        SendMessageW(header, WM_SETFONT,
                     reinterpret_cast<WPARAM>(font(theme::kFontTable, true)), TRUE);
    }
    valueListResizeColumns(valueList);
}

void sectionListSetItems(HWND hwnd, const std::vector<std::wstring>& titles,
                         const std::vector<int>& counts) {
    SectionData* data = sectionData(hwnd);
    if (!data) return;
    data->titles = titles;
    data->counts = counts;
    if (data->selected >= static_cast<int>(titles.size())) data->selected = 0;
    InvalidateRect(hwnd, nullptr, TRUE);
}

int sectionListGetSelection(HWND hwnd) {
    SectionData* data = sectionData(hwnd);
    return data ? data->selected : 0;
}

void sectionListSetSelection(HWND hwnd, int index) {
    SectionData* data = sectionData(hwnd);
    if (!data || index < 0 || index >= static_cast<int>(data->titles.size())) return;
    data->selected = index;
    InvalidateRect(hwnd, nullptr, TRUE);
}

void valueListSetRows(HWND hwnd, const std::vector<ValueRow>& rows) {
    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hwnd);
    for (size_t i = 0; i < rows.size(); ++i) {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<LPWSTR>(rows[i].name.c_str());
        ListView_InsertItem(hwnd, &item);
        ListView_SetItemText(hwnd, static_cast<int>(i), 1,
                             const_cast<LPWSTR>(rows[i].value.c_str()));
    }
    valueListResizeColumns(hwnd);
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void valueListResizeColumns(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    // The vertical scrollbar's width comes off the top before the two columns
    // are divided.
    //
    // This is not optional. The bar is a child of the client area, so the client
    // rectangle does not shrink when it appears; sizing the columns to the full
    // client width therefore leaves them summing to more than the space that is
    // actually left once the bar shows up, and the list answers that by putting
    // up a horizontal scrollbar as well. Reserving the width unconditionally
    // also keeps the columns from shifting by seventeen pixels every time the
    // bar comes and goes.
    const int usable = rc.right - GetSystemMetrics(SM_CXVSCROLL);

    // The control is created at zero size and only moved into place once the
    // main window has laid itself out, so this runs at least once with nothing
    // to divide. Without this guard the arithmetic below produces negative
    // widths, LVM_SETCOLUMNWIDTH rejects them, and the control is left holding
    // whatever the header happened to contain - the state the table then fails
    // to recover from on a control whose item width is only recomputed when it
    // is told the columns changed.
    if (usable <= 0) return;

    // The property-name column holds a name, so its floor follows the text
    // size: at 160% a 160-pixel column would ellipsise most of them.
    int nameWidth = usable * 42 / 100;
    const int minNameWidth = theme::metric(160);
    if (nameWidth < minNameWidth) nameWidth = minNameWidth;
    if (nameWidth > usable) nameWidth = usable;
    ListView_SetColumnWidth(hwnd, 0, nameWidth);
    ListView_SetColumnWidth(hwnd, 1, usable - nameWidth);

    // Remembered for the row painter. It draws its own geometry rather than
    // trusting the control's idea of how wide an item is: on Wine that value is
    // not refreshed by LVM_SETCOLUMNWIDTH, so it can lag arbitrarily far behind
    // the columns actually on screen.
    if (ValueData* data = valueData(hwnd)) {
        data->nameWidth = nameWidth;
    }
}

}  // namespace ui
