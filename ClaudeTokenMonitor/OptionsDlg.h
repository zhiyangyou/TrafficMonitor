#pragma once
#include "pch.h"
#include "DataManager.h"

// COptionsDlg — MFC options dialog for ClaudeTokenMonitor.
// Reference: plan §6 OptionsDlg 控件表; PluginDemo/OptionsDlg.h
// All OnXxx handlers below are TODO skeletons; OK path writes back m_data to CDataManager.
// TODO detail: .claude/skills/claude-token-monitor/references/topics/wrapper-installer.md
class COptionsDlg : public CDialog
{
    DECLARE_DYNAMIC(COptionsDlg)

public:
    COptionsDlg(CWnd* pParent = nullptr);
    virtual ~COptionsDlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_OPTIONS_DIALOG };
#endif

    // Bound to CDataManager::Instance().m_setting_data before DoModal().
    // On IDOK, the caller copies this back into CDataManager (PluginDemo pattern).
    SettingData m_data;

protected:
    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;
    virtual void OnOK() override;

    DECLARE_MESSAGE_MAP()

public:
    // Status static — wrapper state label
    afx_msg void OnBnClickedInstall();
    afx_msg void OnBnClickedUninstall();
    afx_msg void OnBnClickedRefreshSessions();

    // TODO: bind CMFCColorButton controls (IDC_CLR_INPUT / IDC_CLR_CACHE_WRITE / IDC_CLR_CACHE_READ / IDC_CLR_OUTPUT)
    // TODO: bind CComboBox IDC_CBO_AGGREGATE_MODE and IDC_CBO_SINGLE_SESSION
    // TODO: bind CListCtrl IDC_LST_IGNORED with checkboxes
    // TODO: bind CSpinButtonCtrl IDC_SPIN_REFRESH for refresh_interval_ms
};