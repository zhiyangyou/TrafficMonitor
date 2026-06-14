// OptionsDlg.cpp -- COptionsDlg MFC dialog implementation.
// Reference: plan section 6 control table; PluginDemo/OptionsDlg.cpp.
// Detail: .claude/skills/claude-token-monitor/references/topics/wrapper-installer.md
//          and .../references/topics/session-aggregation.md
//
// Notes:
// - All comments are ASCII only (no CJK punctuation like 'x' or 'section') to avoid
//   C4819 (code page 936 warning) and C2601 (lexer false positive).
// - DDX is not used for the special controls (combo/list/color/spin) -- they are
//   initialized in OnInitDialog and read back in OnOK via direct method calls.
//   This matches PluginDemo's pattern of avoiding DDX for non-trivial controls.

#include "pch.h"
#include "afxdialogex.h"
#include "ClaudeTokenMonitor.h"
#include "OptionsDlg.h"
#include "StatuslineInstaller.h"
#include "DataManager.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

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
    // Bind controls (DDX not used for values -- we manage them manually for
    // multi-select lists, color buttons, spin ranges, and the CString combo
    // bound to a session_id).
    DDX_Control(pDX, IDC_CBO_AGGREGATE_MODE, m_cbo_aggregate_mode);
    DDX_Control(pDX, IDC_CBO_SINGLE_SESSION, m_cbo_single_session);
    DDX_Control(pDX, IDC_LST_IGNORED,        m_lst_ignored);
    DDX_Control(pDX, IDC_CLR_INPUT,          m_clr_input);
    DDX_Control(pDX, IDC_CLR_CACHE_WRITE,    m_clr_cache_write);
    DDX_Control(pDX, IDC_CLR_CACHE_READ,     m_clr_cache_read);
    DDX_Control(pDX, IDC_CLR_OUTPUT,         m_clr_output);
    DDX_Control(pDX, IDC_SPIN_REFRESH,       m_spin_refresh);
    DDX_Control(pDX, IDC_EDIT_REFRESH,       m_edit_refresh);
    DDX_Control(pDX, IDC_STATIC_STATUS,      m_static_status);
    DDX_Control(pDX, IDC_STATIC_WRAPPER_PATH,  m_static_wrapper_path);
    DDX_Control(pDX, IDC_STATIC_SIDECAR_PATH,  m_static_sidecar_path);
    DDX_Control(pDX, IDC_STATIC_SETTINGS_PATH, m_static_settings_path);
}

BEGIN_MESSAGE_MAP(COptionsDlg, CDialog)
    ON_BN_CLICKED(IDC_BTN_INSTALL, &COptionsDlg::OnBnClickedInstall)
    ON_BN_CLICKED(IDC_BTN_UNINSTALL, &COptionsDlg::OnBnClickedUninstall)
    ON_BN_CLICKED(IDC_BTN_REFRESH_SESSIONS, &COptionsDlg::OnBnClickedRefreshSessions)
END_MESSAGE_MAP()

// ---- helpers ---------------------------------------------------------------

// Format a path for display in a CStatic; ellipsize if longer than max_chars.
static CString FormatPathForDisplay(const std::wstring& path, int max_chars = 70)
{
    CString text(path.c_str());
    if (text.GetLength() > max_chars)
    {
        text = text.Right(max_chars - 3);
        text.Insert(0, _T("..."));
    }
    return text;
}

// Write status text + dynamic enable/disable of Install/Uninstall buttons
// according to CStatuslineInstaller::CheckInstalled().
void COptionsDlg::RefreshWrapperState()
{
    using St = CStatuslineInstaller::InstallState;
    St state = CStatuslineInstaller::CheckInstalled();

    CString status;
    CWnd* pBtnInstall   = GetDlgItem(IDC_BTN_INSTALL);
    CWnd* pBtnUninstall = GetDlgItem(IDC_BTN_UNINSTALL);
    if (!pBtnInstall || !pBtnUninstall)
    {
        return;
    }

    switch (state)
    {
    case St::Installed:
        status.Format(_T("Installed: %s"),
                      FormatPathForDisplay(CStatuslineInstaller::GetWrapperPath()));
        pBtnInstall->EnableWindow(FALSE);
        pBtnUninstall->EnableWindow(TRUE);
        break;
    case St::NotInstalled:
        status = _T("Not installed - click 'Install wrapper' to begin.");
        pBtnInstall->EnableWindow(TRUE);
        pBtnUninstall->EnableWindow(FALSE);
        break;
    case St::ClaudeCodeMissing:
        status = _T("Claude Code not detected. Install Claude Code first.");
        pBtnInstall->EnableWindow(FALSE);
        pBtnUninstall->EnableWindow(FALSE);
        break;
    }
    m_static_status.SetWindowText(status);

    // Path detail statics (always show the resolved paths, regardless of state).
    m_static_wrapper_path.SetWindowText(
        FormatPathForDisplay(CStatuslineInstaller::GetWrapperPath()));
    m_static_sidecar_path.SetWindowText(
        FormatPathForDisplay(CStatuslineInstaller::GetSidecarPath()));
    m_static_settings_path.SetWindowText(
        FormatPathForDisplay(CStatuslineInstaller::GetClaudeSettingsPath()));
}

// Fill the single-session combo + ignored list from CDataManager::GetActiveSessions().
// Preserve current selections where possible.
void COptionsDlg::ReloadSessionControls()
{
    auto sessions = CDataManager::Instance().GetActiveSessions();

    // Build a vector of session_id strings + a set for membership checks.
    std::vector<std::wstring> ids;
    ids.reserve(sessions.size());
    for (const auto& s : sessions)
    {
        ids.push_back(s.session_id);
    }

    // ---- IDC_CBO_SINGLE_SESSION ----
    m_cbo_single_session.ResetContent();
    int single_idx = CB_ERR;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        CString label(ids[i].c_str());
        int inserted = m_cbo_single_session.AddString(label);
        if (inserted >= 0 && ids[i] == m_data.single_session_id)
        {
            single_idx = static_cast<int>(inserted);
        }
    }
    if (single_idx != CB_ERR)
    {
        m_cbo_single_session.SetCurSel(single_idx);
    }
    else if (m_cbo_single_session.GetCount() > 0)
    {
        // No saved selection (or selection no longer present) -- default to first.
        m_cbo_single_session.SetCurSel(0);
        if (!ids.empty())
        {
            m_data.single_session_id = ids.front();
        }
    }

    // In SINGLE mode the combo is enabled; otherwise grey it out.
    BOOL enable_single = (m_data.aggregate_mode == AggregateMode::SINGLE);
    m_cbo_single_session.EnableWindow(enable_single);

    // ---- IDC_LST_IGNORED ----
    m_lst_ignored.ResetContent();
    // We need to remember which session_ids were already checked so we can
    // re-apply after ResetContent cleared all items.
    const std::vector<std::wstring>& prev_ignored = m_data.ignored_sessions;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        int inserted = m_lst_ignored.AddString(CString(ids[i].c_str()));
        if (inserted < 0) continue;
        // Preselect if either (a) previously ignored OR (b) the current
        // single_session_id (we never ignore the single-session target).
        bool should_check = std::find(prev_ignored.begin(), prev_ignored.end(),
                                      ids[i]) != prev_ignored.end();
        if (enable_single && ids[i] == m_data.single_session_id)
        {
            should_check = false;
        }
        m_lst_ignored.SetSel(inserted, should_check);
    }
}

// ---- message handlers ------------------------------------------------------

BOOL COptionsDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // 1. Wrapper install state (status text + button enable + path details).
    RefreshWrapperState();

    // 2. Aggregate mode combo (3 entries + select current).
    {
        // Note: list order must match AggregateMode enum values (ALL=0, ACTIVE=1, SINGLE=2).
        m_cbo_aggregate_mode.AddString(_T("Merge all sessions"));
        m_cbo_aggregate_mode.AddString(_T("Active only (1 min)"));
        m_cbo_aggregate_mode.AddString(_T("Single session"));
        int sel = static_cast<int>(m_data.aggregate_mode);
        if (sel < 0 || sel > 2) sel = 0;
        m_cbo_aggregate_mode.SetCurSel(sel);
    }

    // 3. Single-session combo + ignored list (filled from DataManager).
    ReloadSessionControls();

    // 4. Four color buttons.
    m_clr_input.SetColor(m_data.color_input);
    m_clr_cache_write.SetColor(m_data.color_cache_creation);
    m_clr_cache_read.SetColor(m_data.color_cache_read);
    m_clr_output.SetColor(m_data.color_output);

    // 5. Spin + edit for refresh interval (500..5000 ms, default from m_data).
    m_spin_refresh.SetRange(500, 5000);
    int clamped = m_data.refresh_interval_ms;
    if (clamped < 500)  clamped = 500;
    if (clamped > 5000) clamped = 5000;
    m_spin_refresh.SetPos(clamped);
    // Spin is the buddy of the edit control (UDS_SETBUDDYINT | UDS_ALIGNRIGHT),
    // so the edit value is kept in sync automatically. We still set the initial
    // text in case the buddy mechanic does not fire on the very first paint.
    {
        CString buf;
        buf.Format(_T("%d"), clamped);
        m_edit_refresh.SetWindowText(buf);
    }

    return TRUE;  // return TRUE unless you set the focus to a control
}

void COptionsDlg::OnOK()
{
    // ---- AggregateMode ----
    int mode_idx = m_cbo_aggregate_mode.GetCurSel();
    if (mode_idx >= 0 && mode_idx <= 2)
    {
        m_data.aggregate_mode = static_cast<AggregateMode>(mode_idx);
    }

    // ---- single_session_id ----
    {
        int idx = m_cbo_single_session.GetCurSel();
        CString sel_text;
        if (idx >= 0 && m_cbo_single_session.GetLBTextLen(idx) > 0)
        {
            m_cbo_single_session.GetLBText(idx, sel_text);
            m_data.single_session_id = std::wstring(sel_text.GetString());
        }
        // If combo is empty (no sessions ever seen), leave the saved value as-is.
    }

    // ---- ignored_sessions (multi-select list) ----
    {
        m_data.ignored_sessions.clear();
        int count = m_lst_ignored.GetCount();
        if (count > 0)
        {
            std::vector<int> sel(count, -1);
            int n_sel = m_lst_ignored.GetSelItems(count, sel.data());
            for (int i = 0; i < n_sel; ++i)
            {
                CString txt;
                m_lst_ignored.GetText(sel[i], txt);
                if (!txt.IsEmpty())
                {
                    m_data.ignored_sessions.emplace_back(txt.GetString());
                }
            }
        }
    }

    // ---- 4 colors ----
    m_data.color_input            = m_clr_input.GetColor();
    m_data.color_cache_creation   = m_clr_cache_write.GetColor();
    m_data.color_cache_read       = m_clr_cache_read.GetColor();
    m_data.color_output           = m_clr_output.GetColor();

    // ---- refresh interval (clamp 500..5000) ----
    {
        int pos = m_spin_refresh.GetPos();
        if (pos < 500)  pos = 500;
        if (pos > 5000) pos = 5000;
        m_data.refresh_interval_ms = pos;
    }

    CDialog::OnOK();
}

void COptionsDlg::OnBnClickedInstall()
{
    // Disable both buttons while the operation is in flight to prevent
    // re-entry (Install can take a few hundred ms while copying settings).
    if (CWnd* p = GetDlgItem(IDC_BTN_INSTALL))   p->EnableWindow(FALSE);
    if (CWnd* p = GetDlgItem(IDC_BTN_UNINSTALL)) p->EnableWindow(FALSE);

    bool ok = CStatuslineInstaller::Install();
    if (!ok)
    {
        AfxMessageBox(
            _T("Failed to install the statusline wrapper.\n\n")
            _T("Check that ~/.claude/settings.json exists and is writable,")
            _T(" and that %APPDATA%\\ClaudeTokenMonitor is creatable."),
            MB_ICONERROR | MB_OK);
    }
    RefreshWrapperState();
}

void COptionsDlg::OnBnClickedUninstall()
{
    if (CWnd* p = GetDlgItem(IDC_BTN_INSTALL))   p->EnableWindow(FALSE);
    if (CWnd* p = GetDlgItem(IDC_BTN_UNINSTALL)) p->EnableWindow(FALSE);

    bool ok = CStatuslineInstaller::Uninstall();
    if (!ok)
    {
        AfxMessageBox(
            _T("Failed to uninstall the statusline wrapper."),
            MB_ICONERROR | MB_OK);
    }
    RefreshWrapperState();
}

void COptionsDlg::OnBnClickedRefreshSessions()
{
    ReloadSessionControls();
}