/* windows.c */
/* implements Windows specific functions */
#include <io.h>
#include "dive.h"
#include "display.h"
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x500
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <dirent.h>
#include <zip.h>
#include <lmcons.h>

const char non_standard_system_divelist_default_font[] = "Calibri";
const char current_system_divelist_default_font[] = "Segoe UI";
const char *system_divelist_default_font = non_standard_system_divelist_default_font;
double system_divelist_default_font_size = -1;

void subsurface_user_info(struct user_info *user)
{ /* Encourage use of at least libgit2-0.20 */ }

extern bool isWin7Or8();

void subsurface_OS_pref_setup(void)
{
	if (isWin7Or8())
		system_divelist_default_font = current_system_divelist_default_font;
}

bool subsurface_ignore_font(const char *font)
{
	// if this is running on a recent enough version of Windows and the font
	// passed in is the pre 4.3 default font, ignore it
	if (isWin7Or8() && strcmp(font, non_standard_system_divelist_default_font) == 0)
		return true;
	return false;
}

/* this function returns the Win32 Roaming path for the current user as UTF-8.
 * it never returns NULL but fallsback to .\ instead!
 * the append argument will append a wchar_t string to the end of the path.
 */
static const char *system_default_path_append(const wchar_t *append)
{
	wchar_t wpath[MAX_PATH] = { 0 };
	const char *fname = "system_default_path_append()";

	/* obtain the user path via SHGetFolderPathW.
	 * this API is deprecated but still supported on modern Win32.
	 * fallback to .\ if it fails.
	 */
	if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, wpath))) {
		fprintf(stderr, "%s: cannot obtain path!\n", fname);
		wpath[0] = L'.';
		wpath[1] = L'\0';
	}

	wcscat(wpath, L"\\Subsurface");
	if (append) {
		wcscat(wpath, L"\\");
		wcscat(wpath, append);
	}

	/* attempt to convert the UTF-16 string to UTF-8.
	 * resize the buffer and fallback to .\Subsurface if it fails.
	 */
	const int wsz = wcslen(wpath);
	const int sz = WideCharToMultiByte(CP_UTF8, 0, wpath, wsz, NULL, 0, NULL, NULL);
	char *path = (char *)malloc(sz + 1);
	if (!sz)
		goto fallback;
	if (WideCharToMultiByte(CP_UTF8, 0, wpath, wsz, path, sz, NULL, NULL)) {
		path[sz] = '\0';
		return path;
	}

fallback:
	fprintf(stderr, "%s: cannot obtain path as UTF-8!\n", fname);
	const char *local = ".\\Subsurface";
	const int len = strlen(local) + 1;
	path = (char *)realloc(path, len);
	memset(path, 0, len);
	strcat(path, local);
	return path;
}

/* by passing NULL to system_default_path_append() we obtain the pure path.
 * '\' not included at the end.
 */
const char *system_default_directory(void)
{
	static const char *path = NULL;
	if (!path)
		path = system_default_path_append(NULL);
	return path;
}

/* obtain the Roaming path and append "\\<USERNAME>.xml" to it.
 */
const char *system_default_filename(void)
{
	static wchar_t filename[UNLEN + 5] = { 0 };
	if (!*filename) {
		wchar_t username[UNLEN + 1] = { 0 };
		DWORD username_len = UNLEN + 1;
		GetUserNameW(username, &username_len);
		wcscat(filename, username);
		wcscat(filename, L".xml");
	}
	static const char *path = NULL;
	if (!path)
		path = system_default_path_append(filename);
	return path;
}

int enumerate_devices(device_callback_t callback, void *userdata, int dc_type)
{
	int index = -1;
	DWORD i;
	if (dc_type != DC_TYPE_UEMIS) {
		// Open the registry key.
		HKEY hKey;
		LONG rc = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_QUERY_VALUE, &hKey);
		if (rc != ERROR_SUCCESS) {
			return -1;
		}

		// Get the number of values.
		DWORD count = 0;
		rc = RegQueryInfoKey(hKey, NULL, NULL, NULL, NULL, NULL, NULL, &count, NULL, NULL, NULL, NULL);
		if (rc != ERROR_SUCCESS) {
			RegCloseKey(hKey);
			return -1;
		}
		for (i = 0; i < count; ++i) {
			// Get the value name, data and type.
			char name[512], data[512];
			DWORD name_len = sizeof(name);
			DWORD data_len = sizeof(data);
			DWORD type = 0;
			rc = RegEnumValue(hKey, i, name, &name_len, NULL, &type, (LPBYTE)data, &data_len);
			if (rc != ERROR_SUCCESS) {
				RegCloseKey(hKey);
				return -1;
			}

			// Ignore non-string values.
			if (type != REG_SZ)
				continue;

			// Prevent a possible buffer overflow.
			if (data_len >= sizeof(data)) {
				RegCloseKey(hKey);
				return -1;
			}

			// Null terminate the string.
			data[data_len] = 0;

			callback(data, userdata);
			index++;
			if (is_default_dive_computer_device(name))
				index = i;
		}

		RegCloseKey(hKey);
	}
	if (dc_type != DC_TYPE_SERIAL) {
		int i;
		int count_drives = 0;
		const int bufdef = 512;
		const char *dlabels[] = {"UEMISSDA", NULL};
		char bufname[bufdef], bufval[bufdef], *p;
		DWORD bufname_len;

		/* add drive letters that match labels */
		memset(bufname, 0, bufdef);
		bufname_len = bufdef;
		if (GetLogicalDriveStringsA(bufname_len, bufname)) {
			p = bufname;

			while (*p) {
				memset(bufval, 0, bufdef);
				if (GetVolumeInformationA(p, bufval, bufdef, NULL, NULL, NULL, NULL, 0)) {
					for (i = 0; dlabels[i] != NULL; i++)
						if (!strcmp(bufval, dlabels[i])) {
							char data[512];
							snprintf(data, sizeof(data), "%s (%s)", p, dlabels[i]);
							callback(data, userdata);
							if (is_default_dive_computer_device(p))
								index = count_drives;
							count_drives++;
						}
				}
				p = &p[strlen(p) + 1];
			}
			if (count_drives == 1) /* we found exactly one Uemis "drive" */
				index = 0; /* make it the selected "device" */
		}
	}
	return index;
}

/* this function converts a utf-8 string to win32's utf-16 2 byte string.
 * the caller function should manage the allocated memory.
 */
static wchar_t *utf8_to_utf16_fl(const char *utf8, char *file, int line)
{
	assert(utf8 != NULL);
	assert(file != NULL);
	assert(line);
	/* estimate buffer size */
	const int sz = strlen(utf8) + 1;
	wchar_t *utf16 = (wchar_t *)malloc(sizeof(wchar_t) * sz);
	if (!utf16) {
		fprintf(stderr, "%s:%d: %s %d.", file, line, "cannot allocate buffer of size", sz);
		return NULL;
	}
	if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, utf16, sz))
		return utf16;
	fprintf(stderr, "%s:%d: %s", file, line, "cannot convert string.");
	free((void *)utf16);
	return NULL;
}

#define utf8_to_utf16(s) utf8_to_utf16_fl(s, __FILE__, __LINE__)

/* bellow we provide a set of wrappers for some I/O functions to use wchar_t.
 * on win32 this solves the issue that we need paths to be utf-16 encoded.
 */
int subsurface_rename(const char *path, const char *newpath)
{
	int ret = -1;
	if (!path || !newpath)
		return ret;

	wchar_t *wpath = utf8_to_utf16(path);
	wchar_t *wnewpath = utf8_to_utf16(newpath);

	if (wpath && wnewpath)
		ret = _wrename(wpath, wnewpath);
	free((void *)wpath);
	free((void *)wnewpath);
	return ret;
}

// if the QDir based rename fails, we try this one
int subsurface_dir_rename(const char *path, const char *newpath)
{
	// check if the folder exists
	BOOL exists = FALSE;
	DWORD attrib = GetFileAttributes(path);
	if (attrib != INVALID_FILE_ATTRIBUTES && attrib & FILE_ATTRIBUTE_DIRECTORY)
		exists = TRUE;
	if (!exists && verbose) {
		fprintf(stderr, "folder not found or path is not a folder: %s\n", path);
		return EXIT_FAILURE;
	}

	// list of error codes:
	// https://msdn.microsoft.com/en-us/library/windows/desktop/ms681381(v=vs.85).aspx
	DWORD errorCode;

	// if this fails something has already obatained (more) exclusive access to the folder
	HANDLE h = CreateFile(path, GENERIC_WRITE, FILE_SHARE_WRITE |
			      FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
	if (h == INVALID_HANDLE_VALUE) {
		errorCode = GetLastError();
		if (verbose)
			fprintf(stderr, "cannot obtain exclusive write access for folder: %u\n", (unsigned int)errorCode );
		return EXIT_FAILURE;
	} else {
		if (verbose)
			fprintf(stderr, "exclusive write access obtained...closing handle!");
		CloseHandle(h);

		// attempt to rename
		BOOL result = MoveFile(path, newpath);
		if (!result) {
			errorCode = GetLastError();
			if (verbose)
				fprintf(stderr, "rename failed: %u\n", (unsigned int)errorCode);
			return EXIT_FAILURE;
		}
		if (verbose > 1)
			fprintf(stderr, "folder rename success: %s ---> %s\n", path, newpath);
	}
	return EXIT_SUCCESS;
}

int subsurface_open(const char *path, int oflags, mode_t mode)
{
	int ret = -1;
	if (!path)
		return ret;
	wchar_t *wpath = utf8_to_utf16(path);
	if (wpath)
		ret = _wopen(wpath, oflags, mode);
	free((void *)wpath);
	return ret;
}

FILE *subsurface_fopen(const char *path, const char *mode)
{
	FILE *ret = NULL;
	if (!path)
		return ret;
	wchar_t *wpath = utf8_to_utf16(path);
	if (wpath) {
		const int len = strlen(mode);
		wchar_t wmode[len + 1];
		for (int i = 0; i < len; i++)
			wmode[i] = (wchar_t)mode[i];
		wmode[len] = 0;
		ret = _wfopen(wpath, wmode);
	}
	free((void *)wpath);
	return ret;
}

/* here we return a void pointer instead of _WDIR or DIR pointer */
void *subsurface_opendir(const char *path)
{
	_WDIR *ret = NULL;
	if (!path)
		return ret;
	wchar_t *wpath = utf8_to_utf16(path);
	if (wpath)
		ret = _wopendir(wpath);
	free((void *)wpath);
	return (void *)ret;
}

int subsurface_access(const char *path, int mode)
{
	int ret = -1;
	if (!path)
		return ret;
	wchar_t *wpath = utf8_to_utf16(path);
	if (wpath)
		ret = _waccess(wpath, mode);
	free((void *)wpath);
	return ret;
}

#ifndef O_BINARY
#define O_BINARY 0
#endif

struct zip *subsurface_zip_open_readonly(const char *path, int flags, int *errorp)
{
#if defined(LIBZIP_VERSION_MAJOR)
	/* libzip 0.10 has zip_fdopen, let's use it since zip_open doesn't have a
	 * wchar_t version */
	int fd = subsurface_open(path, O_RDONLY | O_BINARY, 0);
	struct zip *ret = zip_fdopen(fd, flags, errorp);
	if (!ret)
		close(fd);
	return ret;
#else
	return zip_open(path, flags, errorp);
#endif
}

int subsurface_zip_close(struct zip *zip)
{
	return zip_close(zip);
}

/* win32 console */
static struct {
	bool allocated;
	UINT cp;
	FILE *out, *err;
} console_desc;

void subsurface_console_init(bool dedicated, bool logfile)
{
	(void)console_desc;
	/* if this is a console app already, do nothing */
#ifndef WIN32_CONSOLE_APP

	/* just in case of multiple calls */
	memset((void *)&console_desc, 0, sizeof(console_desc));
	/* the AttachConsole(..) call can be used to determine if the parent process
	 * is a terminal. if it succeeds, there is no need for a dedicated console
	 * window and we don't need to call the AllocConsole() function. on the other
	 * hand if the user has set the 'dedicated' flag to 'true' and if AttachConsole()
	 * has failed, we create a dedicated console window.
	 */
	console_desc.allocated = AttachConsole(ATTACH_PARENT_PROCESS);
	if (console_desc.allocated)
		dedicated = false;
	if (!console_desc.allocated && dedicated)
		console_desc.allocated = AllocConsole();
	if (!console_desc.allocated)
		return;

	console_desc.cp = GetConsoleCP();
	SetConsoleOutputCP(CP_UTF8); /* make the ouput utf8 */

	/* set some console modes; we don't need to reset these back.
	 * ENABLE_EXTENDED_FLAGS = 0x0080, ENABLE_QUICK_EDIT_MODE = 0x0040 */
	HANDLE h_in = GetStdHandle(STD_INPUT_HANDLE);
	if (h_in) {
		SetConsoleMode(h_in, 0x0080 | 0x0040);
		CloseHandle(h_in);
	}

	/* dedicated only; disable the 'x' button as it will close the main process as well */
	HWND h_cw = GetConsoleWindow();
	if (h_cw && dedicated) {
		SetWindowTextA(h_cw, "Subsurface Console");
		HMENU h_menu = GetSystemMenu(h_cw, 0);
		if (h_menu) {
			EnableMenuItem(h_menu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED);
			DrawMenuBar(h_cw);
		}
		SetConsoleCtrlHandler(NULL, TRUE); /* disable the CTRL handler */
	}

	const char *location_out = logfile ? "subsurface_out.log" : "CON";
	const char *location_err = logfile ? "subsurface_err.log" : "CON";

	/* redirect; on win32, CON is a reserved pipe target, like NUL */
	console_desc.out = freopen(location_out, "w", stdout);
	console_desc.err = freopen(location_err, "w", stderr);
	if (!dedicated)
		puts(""); /* add an empty line */
#endif
}

void subsurface_console_exit(void)
{
#ifndef WIN32_CONSOLE_APP
	if (!console_desc.allocated)
		return;

	/* close handles */
	if (console_desc.out)
		fclose(console_desc.out);
	if (console_desc.err)
		fclose(console_desc.err);

	/* reset code page and free */
	SetConsoleOutputCP(console_desc.cp);
	FreeConsole();
#endif
}

bool subsurface_user_is_root()
{
	/* FIXME: Detect admin rights */
	return (false);
}
