// OptionsDlg.cpp — COptionsDlg MFC dialog implementation skeleton.
// Reference: plan §6 控件表; PluginDemo/OptionsDlg.cpp
// TODO detail: .claude/skills/claude-token-monitor/references/topics/wrapper-installer.md

#include "pch.h"
#include "ClaudeTokenMonitor.h"
#include "OptionsDlg.h"
#include "StatuslineInstaller.h"
#include "afxdialogex.h"

IMPLEMENT_DYNAMIC(COptionsDlg, CDialog)

COptionsDlg::COptionsDlg(CWnd* pParent /*=nullptr*/)
    : CDialog(IDD_OPTIONS_DIALOG, pParent)
{
}

COptionsDlg::~COptionsDlg()
{
}

void COptionsDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    // TODO: DDX bindings for CMFCColorButton × 4 (IDC_CLR_INPUT/CACHE_WRITE/CACHE_READ/OUTPUT),
    //       CComboBox × 2 (IDC_CBO_AGGREGATE_MODE / IDC_CBO_SINGLE_SESSION),
    //       CListCtrl (IDC_LST_IGNORED), CSpinButtonCtrl (IDC_SPIN_REFRESH).
    // Reference: plan §6 控件表
}

BEGIN_MESSAGE_MAP(COptionsDlg, CDialog)
    ON_BN_CLICKED(IDC_BTN_INSTALL, &COptionsDlg::OnBnClickedInstall)
    ON_BN_CLICKED(IDC_BTN_UNINSTALL, &COptionsDlg::OnBnClickedUninstall)
    ON_BN_CLICKED(IDC_BTN_REFRESH_SESSIONS, &COptionsDlg::OnBnClickedRefreshSessions)
    // TODO: ON_CBN_SELCHANGE for combo boxes, ON_LBN_SELCHANGE for ignored list, color button notifications
END_MESSAGE_MAP()

BOOL COptionsDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // TODO: full UI init per plan §6:
    //   1. Refresh IDC_STATIC_STATUS text from CStatuslineInstaller::CheckInstalled()
    //      (Installed → "wrapper 已装"; NotInstalled → "wrapper 未装"; ClaudeCodeMissing → "Claude Code 未检测到").
    //      Disable IDC_BTN_INSTALL if ClaudeCodeMissing.
    //   2. Populate IDC_CBO_AGGREGATE_MODE with 3 entries (合并所有 / 仅活动 / 单个 session)
    //      and select m_data.aggregate_mode.
    //   3. Populate IDC_CBO_SINGLE_SESSION from CDataManager::Instance().GetActiveSessions();
    //      enable only when aggregate_mode == SINGLE.
    //   4. Populate IDC_LST_IGNORED with checkboxes; pre-check entries in m_data.ignored_sessions.
    //   5. Set CMFCColorButton × 4 colors from m_data.color_*.
    //   6. Set CSpinButtonCtrl IDC_SPIN_REFRESH range (500..5000) and pos = m_data.refresh_interval_ms.
    // See: plan §6; references/topics/wrapper-installer.md

    return TRUE;
}

void COptionsDlg::OnOK()
{
    // TODO: pull UI state back into m_data:
    //   - aggregate_mode from IDC_CBO_AGGREGATE_MODE cur sel
    //   - single_session_id from IDC_CBO_SINGLE_SESSION
    //   - color_* from CMFCColorButton::GetColor()
    //   - refresh_interval_ms from spin
    //   - ignored_sessions from IDC_LST_IGNORED checked items
    // Then CDialog::OnOK() so caller sees IDOK.
    CDialog::OnOK();
}

void COptionsDlg::OnBnClickedInstall()
{
    // TODO: call CStatuslineInstaller::Install(); on success refresh status static;
    //       on failure show AfxMessageBox with error text.
    // See: plan §4.4
}

void COptionsDlg::OnBnClickedUninstall()
{
    // TODO: confirm with AfxMessageBox; call CStatuslineInstaller::Uninstall(); refresh status static.
    // See: plan §4.5
}

void COptionsDlg::OnBnClickedRefreshSessions()
{
    // TODO: re-populate IDC_CBO_SINGLE_SESSION + IDC_LST_IGNORED from CDataManager::Instance().GetActiveSessions().
    // See: plan §6 刷新 sessions 按钮
}