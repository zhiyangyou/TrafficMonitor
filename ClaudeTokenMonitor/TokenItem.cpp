// TokenItem.cpp — CTokenItem base class impl. 4 instances (Token In / Cache Write /
// Cache Read / Token Out) share this code, differing only in constructor args.
// Reference: plan §4.3 DrawItem(); PluginDemo/CustomDrawItem.cpp
// TODO detail: .claude/skills/claude-token-monitor/references/topics/custom-draw.md

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
    // MUST be true — see plan §1 architecture discovery: main program scrolling-bar branch
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
    // Layout (per plan §4.3):
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
    // TODO: pull live color from CDataManager::Instance().GetItemColor(m_category) so user changes
    //   in COptionsDlg reflect immediately.
    COLORREF color = m_color;
    if (dark_mode)
    {
        // TODO: optional dark-mode shade shift (e.g. multiply RGB by 0.7) — leave as identity for v1.
    }

    // TODO: draw bars — iterate m_history (CRingBuffer<float>) via CDataManager::GetGraphValue
    //   walk; but ring buffer lives in CDataManager, not in CTokenItem. The cleanest path is to
    //   add a CDataManager::GetGraphHistory(TokenCategory) accessor returning const CRingBuffer*&.
    //   For skeleton: leave the iteration loop as a placeholder drawing nothing — call sites compile
    //   but no bars render yet.
    // Reference: plan §4.3 draw loop; CRingBuffer.h::At(i) where i=0 is oldest, Size()-1 is newest.

    // Value text on the right.
    // TODO: pick value_text_color based on dark_mode (light gray on dark, dark gray on light).
    //   Use EI_LABEL_TEXT_COLOR / EI_VALUE_TEXT_COLOR via OnExtenedInfo if you want main-program colors.
    CString value_text = CDataManager::Instance().GetValueText(m_category);
    pDC->SetTextColor(color);
    pDC->SetBkMode(TRANSPARENT);
    pDC->DrawText(value_text, text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}