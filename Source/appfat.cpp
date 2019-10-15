#include "diablo.h"
#include "../3rdParty/Storm/Source/storm.h"
#ifndef SWITCH
#include <config.h>
#endif

DEVILUTION_BEGIN_NAMESPACE

char sz_error_buf[256];
BOOL terminating;
int cleanup_thread_id;

void __cdecl app_fatal(const char *pszFmt, ...)
{
	va_list va;

	va_start(va, pszFmt);
	FreeDlg();

	if (pszFmt)
		MsgBox(pszFmt, va);

	va_end(va);

	dx_cleanup();
	init_cleanup();
	exit(1);
}

void MsgBox(const char *pszFmt, va_list va)
{
	char Text[256];

	wvsprintf(Text, pszFmt, va);

	UiErrorOkDialog("Error", Text);
}

void FreeDlg()
{
	if (terminating && cleanup_thread_id != GetCurrentThreadId())
		Sleep(20000);

	terminating = TRUE;
	cleanup_thread_id = GetCurrentThreadId();

	if (gbMaxPlayers > 1) {
		if (SNetLeaveGame(3))
			Sleep(2000);
	}

	SNetDestroy();
}

void __cdecl DrawDlg(char *pszFmt, ...)
{
	char text[256];
	va_list arglist;

	va_start(arglist, pszFmt);
	wvsprintf(text, pszFmt, arglist);
	va_end(arglist);

#ifdef SWITCH
	UiErrorOkDialog("DevilutionX", text, false);
#else
	UiErrorOkDialog(PROJECT_NAME, text, false);
#endif
}

#ifdef _DEBUG
void assert_fail(int nLineNo, const char *pszFile, const char *pszFail)
{
	app_fatal("assertion failed (%d:%s)\n%s", nLineNo, pszFile, pszFail);
}
#endif

void ErrDlg(const char *title, const char *error, char *log_file_path, int log_line_nr)
{
	char text[1024];

	FreeDlg();

	snprintf(text, 1024, "%s\n\nThe error occurred at: %s line %d", error, log_file_path, log_line_nr);

	UiErrorOkDialog(title, text);
	app_fatal(NULL);
}


void FileErrDlg(const char *error)
{
	char text[1024];

	FreeDlg();

	if (!error)
		error = "";
	snprintf(
	    text,
	    1024,
	    "Unable to open a required file.\n"
	    "\n"
	    "Verify that the MD5 of diabdat.mpq matches on of the following values\n"
	    "011bc6518e6166206231080a4440b373\n"
	    "68f049866b44688a7af65ba766bef75a\n"
	    "\n"
	    "The problem occurred when loading:\n%s",
	    error);

	UiErrorOkDialog("Data File Error", text);
	app_fatal(NULL);
}

void InsertCDDlg(const char *fileName)
{
	char text[1024];
	snprintf(
	    text,
	    1024,
	    "Unable to open %s.\n"
	    "\n"
	    "Make sure that it is in the game folder and that the file name is in all lowercase.",
	    fileName);

	UiErrorOkDialog("Date File Error", text);
	app_fatal(NULL);
}

void DirErrorDlg(char *error)
{
	char text[1024];

	snprintf(text, 1024, "Unable to write to location:\n%s", error);

	UiErrorOkDialog("Read-Only Directory Error", text);
	app_fatal(NULL);
}

DEVILUTION_END_NAMESPACE
