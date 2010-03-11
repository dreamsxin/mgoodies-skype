/*
 * RegisterOptions
 *
 * This function tells Miranda to add the configuration section of this plugin in
 * the Options-dialog.
 */
int RegisterOptions(WPARAM wParam, LPARAM lParam);
/*
 * OptionsDlgProc
 *
 * This callback function is called, when the options dialog in Miranda is shown
 * The function contains all necessary stuff to process the options in the dialog
 * and store them in the database, when changed, and fill out the settings-dialog
 * correctly according to the current settings
 */
int CALLBACK OptionsDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

int CALLBACK OptionsDefaultDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
int CALLBACK OptionsAdvancedDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
int CALLBACK OptionsProxyDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
int CALLBACK OptPopupDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

/*
* Procedure to call when the option page is asked
*
*/
int OnDetailsInit( WPARAM wParam, LPARAM lParam );

/*
* Dialog to change avatar in user details.
*
*
*/
BOOL CALLBACK AvatarDlgProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/*
* Dialog to change infos in user details.
*
*
*/
BOOL CALLBACK DetailsDlgProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/*
 * Helper functions
 *
 */
void DoAutoDetect(HWND dlg);

;