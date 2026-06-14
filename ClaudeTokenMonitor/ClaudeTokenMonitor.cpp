// ClaudeTokenMonitor.cpp — CClaudeTokenMonitorPlugin (ITMPlugin impl) + DLL export.
// Reference: plan §4.1; PluginDemo/PluginDemo.cpp

#include "pch.h"
#include "ClaudeTokenMonitor.h"
#include "DataManager.h"
#include "OptionsDlg.h"
#include "StatuslineInstaller.h"
#include "TaskbarItemRegistrar.h"

CClaudeTokenMonitorPlugin CClaudeTokenMonitorPlugin::m_instance;

CClaudeTokenMonitorPlugin::CClaudeTokenMonitorPlugin()
    // Stable IDs (DO NOT CHANGE — persisted in user settings across versions):
    //   Token In:     CTM_TokenIn_v1
    //   Cache Write:  CTM_TokenCacheWrite_v1
    //   Cache Read:   CTM_TokenCacheRead_v1
    //   Token Out:    CTM_TokenOut_v1
    // Default colors match SettingData defaults (DataManager.h SettingData{}).
    : m_token_in(TokenCategory::Input,      L"CTM_TokenIn_v1",         L"Token In",     RGB(0, 200, 80),  L"999.9k/s")
    , m_cache_write(TokenCategory::CacheWrite, L"CTM_TokenCacheWrite_v1", L"Cache Write", RGB(230, 180, 0), L"999.9k/s")
    , m_cache_read(TokenCategory::CacheRead,  L"CTM_TokenCacheRead_v1",  L"Cache Read",  RGB(80, 140, 230), L"999.9k/s")
    , m_token_out(TokenCategory::Output,     L"CTM_TokenOut_v1",        L"Token Out",    RGB(180, 80, 220), L"999.9k/s")
{
}

CClaudeTokenMonitorPlugin& CClaudeTokenMonitorPlugin::Instance()
{
    return m_instance;
}

IPluginItem* CClaudeTokenMonitorPlugin::GetItem(int index)
{
    // 4 items: 0=Token In, 1=Cache Write, 2=Cache Read, 3=Token Out
    switch (index)
    {
    case 0: return &m_token_in;
    case 1: return &m_cache_write;
    case 2: return &m_cache_read;
    case 3: return &m_token_out;
    default: break;
    }
    return nullptr;
}

void CClaudeTokenMonitorPlugin::DataRequired()
{
    // 1Hz tick — main program TrafficMonitorDlg.cpp:1504 invokes this on the monitor timer.
    // TODO: forward to CDataManager::Instance().Tick() once it's implemented.
    // Reference: plan §4.2; 03-data-flow.md §跳 4-7
    CDataManager::Instance().Tick();
}

ITMPlugin::OptionReturn CClaudeTokenMonitorPlugin::ShowOptionsDialog(void* hParent)
{
    // Reference: PluginDemo/PluginDemo.cpp:76-87 (modal dialog pattern)
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    COptionsDlg dlg(CWnd::FromHandle((HWND)hParent));
    dlg.m_data = CDataManager::Instance().Settings();
    if (dlg.DoModal() == IDOK)
    {
        CDataManager::Instance().Settings() = dlg.m_data;
        return ITMPlugin::OR_OPTION_CHANGED;
    }
    return ITMPlugin::OR_OPTION_UNCHANGED;
}

const wchar_t* CClaudeTokenMonitorPlugin::GetInfo(PluginInfoIndex index)
{
    // Reference: PluginDemo/PluginDemo.cpp:49-74
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    static CString str;
    switch (index)
    {
    case TMI_NAME:
        str.LoadString(IDS_PLUGIN_NAME);
        return str.GetString();
    case TMI_DESCRIPTION:
        str.LoadString(IDS_PLUGIN_DESCRIPTION);
        return str.GetString();
    case TMI_AUTHOR:
        return L"youzhiyang";
    case TMI_COPYRIGHT:
        return L"Copyright (C) ClaudeTokenMonitor Plugin";
    case TMI_VERSION:
        return L"0.1";
    case TMI_URL:
        return L"";
    default:
        break;
    }
    return L"";
}

void CClaudeTokenMonitorPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    // Reference: PluginDemo/PluginDemo.cpp:90-102
    switch (index)
    {
    case ITMPlugin::EI_CONFIG_DIR:
        // Main program passes the plugin config directory here (PluginManager.cpp:94).
        // We forward to CDataManager to load <config_dir>\ClaudeTokenMonitor.dll.ini.
        CDataManager::Instance().LoadConfig(std::wstring(data));
        break;
    default:
        break;
    }
}

void CClaudeTokenMonitorPlugin::OnInitialize(ITrafficMonitor* pApp)
{
    // Reference: PluginDemo/PluginDemo.cpp:104-110
    m_app = pApp;

    // Auto-register the 4 item IDs into main program's config.ini so they
    // appear on the taskbar after the next restart. Idempotent — no-op once
    // the IDs are already there. See TaskbarItemRegistrar.h / plan §5.
    CTaskbarItemRegistrar::EnsureRegistered(pApp->GetPluginConfigDir(), pApp);

    // Hand the ITrafficMonitor* and config_dir to DataManager so Tick() can
    // re-patch the ini every 5s (main program SaveConfig() clobbers it).
    CDataManager::Instance().SetRegistrarContext(pApp, pApp->GetPluginConfigDir());
    CTaskbarItemRegistrar::ResetThrottle();

    // FreeLibrary+LoadLibrary reload protection: clear stale delta state.
    CDataManager::Instance().ResetRuntimeState();
    // See: plan §7 risk table; references/topics/risk-and-edge-cases.md
}

// DLL export — required by TrafficMonitor plugin loader.
// Reference: include/PluginInterface.h:426-431; PluginDemo/PluginDemo.cpp:112-116
ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &CClaudeTokenMonitorPlugin::Instance();
}