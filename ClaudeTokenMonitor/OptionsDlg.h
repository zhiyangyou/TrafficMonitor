#pragma once
#include "pch.h"
#include <afxcolorbutton.h>
#include "DataManager.h"

// COptionsDlg -- MFC options dialog for ClaudeTokenMonitor.
// Reference: plan section 6 control table; PluginDemo/OptionsDlg.h
// OnInitDialog fills all controls from m_data and reflects wrapper install state.
// OnOK pulls control state back into m_data; caller copies it into CDataManager.
// Detail: .claude/skills/claude-token-monitor/references/topics/wrapper-installer.md
class COptionsDlg : public CDialog
{
    DECLARE_DYNAMIC(COptionsDlg)

public:
    COptionsDlg(CWnd* pParent = nullptr);
    virtual ~COptionsDlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_OPTIONS_DIALOG };
#endif

    // Bound to CDataManager::Instance().Settings() before DoModal().
    // On IDOK the caller copies this back into CDataManager (PluginDemo pattern).
    SettingData m_data;

protected:
    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;
    virtual void OnOK() override;

    DECLARE_MESSAGE_MAP()

public:
    // Button handlers (BN_CLICKED)
    afx_msg void OnBnClickedInstall();
    afx_msg void OnBnClickedUninstall();
    afx_msg void OnBnClickedRefreshSessions();

    // Re-populate the session combo + ignored list from CDataManager::GetActiveSessions().
    // Preserves m_data.single_session_id + m_data.ignored_sessions selection where possible.
    void ReloadSessionControls();

    // Refresh the status static, path statics, and enable/disable Install/Uninstall
    // buttons based on CStatuslineInstaller::CheckInstalled() result.
    void RefreshWrapperState();

private:
    // DDX-bound control variables
    CComboBox       m_cbo_aggregate_mode;
    CComboBox       m_cbo_single_session;
    CListBox        m_lst_ignored;
    CMFCColorButton m_clr_input;
    CMFCColorButton m_clr_cache_write;
    CMFCColorButton m_clr_cache_read;
    CMFCColorButton m_clr_output;
    CSpinButtonCtrl m_spin_refresh;
    CEdit           m_edit_refresh;
    CStatic         m_static_status;
    CStatic         m_static_wrapper_path;
    CStatic         m_static_sidecar_path;
    CStatic         m_static_settings_path;
};