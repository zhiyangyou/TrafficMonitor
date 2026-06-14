// TokenItem.cpp — CTokenItem base class impl. 4 instances (Token In / Cache Write /
// Cache Read / Token Out) share this code, differing only in constructor args.
// Reference: plan section 4.3 DrawItem(); PluginDemo/CustomDrawItem.cpp
// Detail: .claude/skills/claude-token-monitor/references/topics/custom-draw.md

#include "pch.h"
#include "TokenItem.h"
#include "DataManager.h"

CTokenItem::CTokenItem(TokenCategory cat,
                       const wchar_t* id_str,
                       const wchar_t* name,
                       COLORREF color,
                       const wchar_t* sample_text)
    : m_category(cat)
    , m_id(id_str ? id_str : L"")
    , m_name(name ? name : L"")
    , m_color(color)
    , m_sample_text(sample_text ? sample_text : L"")
{
}

const wchar_t* CTokenItem::GetItemName() const
{
    // Reference: PluginDemo/CustomDrawItem.cpp:5-8 (returns StringRes)
    // Map category → string table id.
    // TODO: implement switch on m_category → IDS_TOKEN_IN / IDS_CACHE_WRITE / IDS_CACHE_READ / IDS_TOKEN_OUT.
    //   return CDataManager::Instance().StringRes(IDS_...).GetString();
    return m_name.c_str();
}

const wchar_t* CTokenItem::GetItemId() const
{
    // Reference: PluginDemo/CustomDrawItem.cpp:10-13 (returns literal wchar_t*)
    return m_id.c_str();
}

const wchar_t* CTokenItem::GetItemLableText() const
{
    // CustomDraw=true → this is ignored by main program (see include/PluginInterface.h:53).
    // Still must return non-null.
    return L"";
}

const wchar_t* CTokenItem::GetItemValueText() const
{
    // CustomDraw=true → ignored by main program, but DrawItem reads the same value-text cache
    // via CDataManager::GetValueText(m_category). So this is unused at draw time; keep returning
    // the live cached text so the API stays consistent if anyone calls it from non-custom path.
    static CString tmp;
    tmp = CDataManager::Instance().GetValueText(m_category);
    return tmp.GetString();
}

const wchar_t* CTokenItem::GetItemValueSampleText() const
{
    // Used to compute column width (PluginDemo returns L""; we want a non-empty sample so the
    // right-side text area reserves enough pixels).
    return m_sample_text.c_str();
}

bool CTokenItem::IsCustomDraw() const
{
    // MUST be true — see plan section 1 architecture discovery: main program scrolling-bar branch
    // (TaskBarDlg.cpp:397-398) doesn't accept IPluginItem*, so plugin must self-draw.
    // Reference: include/PluginInterface.h:53; TrafficMonitor/TaskBarDlg.cpp:426-457
    return true;
}

int CTokenItem::GetItemWidth() const
{
    // 96 DPI base width. Right side reserves 40px for value text; left side gets at least 56px
    // for bars. Reference: include/PluginInterface.h:64.
    return 96;
}

int CTokenItem::GetItemWidthEx(void* hDC) const
{
    // TODO: measure m_sample_text with CDC::FromHandle(hDC)→GetTextExtent; return max(96, text_w + bar_w).
    return GetItemWidth();
}

void CTokenItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    // Layout (per plan section 4.3):
    //   text_w = 40        // right side for value text "12.3k/s"
    //   bar_w  = w - text_w
    //   bar_rect  = (x, y, x + bar_w,       y + h)
    //   text_rect = (x + bar_w, y, x + w,    y + h)
    CDC* pDC = CDC::FromHandle((HDC)hDC);
    const int text_w = 40;
    const int bar_w = (w > text_w) ? (w - text_w) : 0;
    CRect bar_rect(x, y, x + bar_w, y + h);
    CRect text_rect(x + bar_w, y, x + w, y + h);

    // Color from SettingData (overrides constructor color if user changed it).
    COLORREF color = CDataManager::Instance().GetItemColor(m_category);
    if (dark_mode)
    {
        // Optional dark-mode shade shift — leave as identity for v1. Color stays
        // at user's chosen value; the plugin does not auto-darken.
    }

    // Draw bars (right-to-left, 1px wide per sample). Pull the ring buffer from
    // CDataManager for our token category. If the bar area is empty (w < text_w+1)
    // or the buffer is empty, nothing to draw — just show the value text.
    if (bar_w > 0)
    {
        const CRingBuffer<float, kHistoryCapacity>& buf =
            CDataManager::Instance().GetGraphHistory(m_category);
        const size_t n = buf.Size();
        if (n > 0)
        {
            const int bar_height = bar_rect.Height();
            // Cap iterations to bar_w so we never draw off the left edge.
            const size_t max_iter = (static_cast<size_t>(bar_w) < n)
                                    ? static_cast<size_t>(bar_w)
                                    : n;
            for (size_t i = 0; i < max_iter; ++i)
            {
                float v = buf.At(i);  // 0=oldest, n-1=newest
                if (v <= 0.0f) continue;  // skip zero-height bars (empty look)
                int col_x = bar_rect.right - static_cast<int>(i + 1);
                if (col_x < bar_rect.left) break;
                int col_h = static_cast<int>(v * bar_height);
                if (col_h < 1) col_h = 1;  // keep visible dots for tiny values
                if (col_h > bar_height) col_h = bar_height;
                int col_y_top = bar_rect.bottom - col_h;
                pDC->FillSolidRect(CRect(col_x, col_y_top, col_x + 1, bar_rect.bottom), color);
            }
        }
    }

    // Value text on the right.
    CString value_text = CDataManager::Instance().GetValueText(m_category);
    pDC->SetTextColor(color);
    pDC->SetBkMode(TRANSPARENT);
    pDC->DrawText(value_text, text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}
