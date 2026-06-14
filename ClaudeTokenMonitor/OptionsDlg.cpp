// OptionsDlg.cpp — COptionsDlg MFC dialog implementation skeleton.
// Reference: plan section 6 control table; PluginDemo/OptionsDlg.cpp
// TODO detail: .claude/skills/claude-token-monitor/references/topics/wrapper-installer.md

#include "pch.h"
#include "afxdialogex.h"
#include "ClaudeTokenMonitor.h"
#include "OptionsDlg.h"
#include "StatuslineInstaller.h"

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

    // TODO: full UI init per plan section 6.

    return TRUE;
}

void COptionsDlg::OnOK()
{
    // TODO: pull UI state back into m_data.
    CDialog::OnOK();
}

void COptionsDlg::OnBnClickedInstall()
{
    // TODO: call CStatuslineInstaller::Install().
}

void COptionsDlg::OnBnClickedUninstall()
{
    // TODO: call CStatuslineInstaller::Uninstall().
}

void COptionsDlg::OnBnClickedRefreshSessions()
{
    // TODO: re-populate session list from CDataManager::Instance().GetActiveSessions().
}