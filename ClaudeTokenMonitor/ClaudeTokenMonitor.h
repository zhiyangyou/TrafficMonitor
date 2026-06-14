#pragma once
#include "pch.h"
#include <PluginInterface.h>
#include "TokenItem.h"

class CStatuslineInstaller;

// CClaudeTokenMonitorPlugin — ITMPlugin implementation; global singleton, returned
// from TMPluginGetInstance(). Owns 4 CTokenItem instances and a CStatuslineInstaller*.
// Reference: plan §2 / §4.1; PluginDemo/PluginDemo.h
class CClaudeTokenMonitorPlugin : public ITMPlugin
{
private:
    CClaudeTokenMonitorPlugin();

public:
    static CClaudeTokenMonitorPlugin& Instance();

    // ITMPlugin overrides — see include/PluginInterface.h:158-323
    virtual int GetAPIVersion() const override { return 7; }
    virtual IPluginItem* GetItem(int index) override;
    virtual void DataRequired() override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    virtual void OnInitialize(ITrafficMonitor* pApp) override;

private:
    // 4 token display items — stable IDs (DO NOT CHANGE — persisted in user settings):
    //   Token In:     CTM_TokenIn_v1
    //   Cache Write:  CTM_TokenCacheWrite_v1
    //   Cache Read:   CTM_TokenCacheRead_v1
    //   Token Out:    CTM_TokenOut_v1
    CTokenItem m_token_in;
    CTokenItem m_cache_write;
    CTokenItem m_cache_read;
    CTokenItem m_token_out;

    ITrafficMonitor* m_app{};

    static CClaudeTokenMonitorPlugin m_instance;
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();
#ifdef __cplusplus
}
#endif