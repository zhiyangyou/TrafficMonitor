#pragma once
#include "pch.h"
#include <PluginInterface.h>
#include <string>

// Token category enum — maps to Anthropic current_usage.* 4 fields.
// Reference: plan §4.2; .claude/skills/claude-token-monitor/references/overview/01-context.md §12
enum class TokenCategory
{
    Input,         // current_usage.input_tokens
    CacheWrite,    // current_usage.cache_creation_input_tokens
    CacheRead,     // current_usage.cache_read_input_tokens
    Output         // current_usage.output_tokens
};

// CTokenItem — base IPluginItem impl for all 4 token display items.
// The 4 items are 4 instances of this class with different constructor args.
// Reference: include/PluginInterface.h:9-152; PluginDemo/CustomDrawItem.cpp
// Self-draw: IsCustomDraw()=true, DrawItem renders a scrolling bar chart.
// TODO detail: see .claude/skills/claude-token-monitor/references/topics/custom-draw.md
class CTokenItem : public IPluginItem
{
public:
    // id_str         — stable ID, see "stable ID" note in plan §1
    //                  (CTM_TokenIn_v1 / CTM_TokenCacheWrite_v1 / CTM_TokenCacheRead_v1 / CTM_TokenOut_v1)
    // name           — display name returned by GetItemName() (used as fallback label)
    // color          — bar color (overridden by SettingData via CDataManager::GetItemColor)
    // sample_text    — used by GetItemValueSampleText() for width calc (e.g. L"999.9k/s")
    CTokenItem(TokenCategory cat,
               const wchar_t* id_str,
               const wchar_t* name,
               COLORREF color,
               const wchar_t* sample_text);

    // IPluginItem overrides — see include/PluginInterface.h:16-88
    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;
    virtual bool IsCustomDraw() const override;
    virtual int GetItemWidth() const override;
    virtual int GetItemWidthEx(void* hDC) const override;
    virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;

    TokenCategory GetCategory() const { return m_category; }
    COLORREF GetColor() const { return m_color; }
    void SetColor(COLORREF c) { m_color = c; }

private:
    TokenCategory m_category{};
    std::wstring m_id;
    std::wstring m_name;
    COLORREF m_color{ RGB(0, 200, 80) };
    std::wstring m_sample_text;
};