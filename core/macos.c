/* macos.c */
/* implements Mac OS X specific functions */
#include <stdlib.h>
#include <dirent.h>
#include <fnmatch.h>
#include "dive.h"
#include "display.h"
#include <CoreFoundation/CoreFoundation.h>
#if !defined(__IPHONE_5_0)
#include <CoreServices/CoreServices.h>
#endif
#include <mach-o/dyld.h>
#include <sys/syslimits.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

void subsurface_user_info(struct user_info *info)
{
	(void) info;
	/* Nothing, let's use libgit2-20 on MacOS */
}

/* macos defines CFSTR to create a CFString object from a constant,
 * but no similar macros if a C string variable is supposed to be
 * the argument. We add this here (hardcoding the default allocator
 * and MacRoman encoding */
#define CFSTR_VAR(_var) CFStringCreateWithCStringNoCopy(kCFAllocatorDefault,               \
							(_var), kCFStringEncodingMacRoman, \
							kCFAllocatorNull)

#define SUBSURFACE_PREFERENCES CFSTR("org.hohndel.subsurface")
#define ICON_NAME "Subsurface.icns"
#define UI_FONT "Arial 12"

const char mac_system_divelist_default_font[] = "Arial";
const char *system_divelist_default_font = mac_system_divelist_default_font;
double system_divelist_default_font_size = -1.0;

void subsurface_OS_pref_setup(void)
{
	// nothing
}

bool subsurface_ignore_font(const char *font)
{
	(void) font;
	// there are no old default fonts to ignore
	return false;
}

static const char *system_default_path_append(const char *append)
{
	const char *home = getenv("HOME");
	const char *path = "/Library/Application Support/Subsurface";

	int len = strlen(home) + strlen(path) + 1;
	if (append)
		len += strlen(append) + 1;

	char *buffer = (char *)malloc(len);
	memset(buffer, 0, len);
	strcat(buffer, home);
	strcat(buffer, path);
	if (append) {
		strcat(buffer, "/");
		strcat(buffer, append);
	}

	return buffer;
}

const char *system_default_directory(void)
{
	static const char *path = NULL;
	if (!path)
		path = system_default_path_append(NULL);
	return path;
}

const char *system_default_filename(void)
{
	static char *filename = NULL;
	if (!filename) {
		const char *user = getenv("LOGNAME");
		if (same_string(user, ""))
			user = "username";
		filename = calloc(strlen(user) + 5, 1);
		strcat(filename, user);
		strcat(filename, ".xml");
	}
	static const char *path = NULL;
	if (!path)
		path = system_default_path_append(filename);
	return path;
}

int enumerate_devices(device_callback_t callback, void *userdata, int dc_type)
{
	int index = -1, entries = 0;
	DIR *dp = NULL;
	struct dirent *ep = NULL;
	size_t i;
	if (dc_type != DC_TYPE_UEMIS) {
		const char *dirname = "/dev";
		const char *patterns[] = {
			"tty.*",
			"usbserial",
			NULL
		};

		dp = opendir(dirname);
		if (dp == NULL) {
			return -1;
		}

		while ((ep = readdir(dp)) != NULL) {
			for (i = 0; patterns[i] != NULL; ++i) {
				if (fnmatch(patterns[i], ep->d_name, 0) == 0) {
					char filename[1024];
					int n = snprintf(filename, sizeof(filename), "%s/%s", dirname, ep->d_name);
					if (n >= (int)sizeof(filename)) {
						closedir(dp);
						return -1;
					}
					callback(filename, userdata);
					if (is_default_dive_computer_device(filename))
						index = entries;
					entries++;
					break;
				}
			}
		}
		closedir(dp);
	}
	if (dc_type != DC_TYPE_SERIAL) {
		const char *dirname = "/Volumes";
		int num_uemis = 0;
		dp = opendir(dirname);
		if (dp == NULL) {
			return -1;
		}

		while ((ep = readdir(dp)) != NULL) {
			if (fnmatch("UEMISSDA", ep->d_name, 0) == 0) {
				char filename[1024];
				int n = snprintf(filename, sizeof(filename), "%s/%s", dirname, ep->d_name);
				if (n >= (int)sizeof(filename)) {
					closedir(dp);
					return -1;
				}
				callback(filename, userdata);
				if (is_default_dive_computer_device(filename))
					index = entries;
				entries++;
				num_uemis++;
				break;
			}
		}
		closedir(dp);
		if (num_uemis == 1 && entries == 1) /* if we find exactly one entry and that's a Uemis, select it */
			index = 0;
	}
	return index;
}

/* NOP wrappers to comform with windows.c */
int subsurface_rename(const char *path, const char *newpath)
{
	return rename(path, newpath);
}

int subsurface_open(const char *path, int oflags, mode_t mode)
{
	return open(path, oflags, mode);
}

FILE *subsurface_fopen(const char *path, const char *mode)
{
	return fopen(path, mode);
}

void *subsurface_opendir(const char *path)
{
	return (void *)opendir(path);
}

int subsurface_access(const char *path, int mode)
{
	return access(path, mode);
}

struct zip *subsurface_zip_open_readonly(const char *path, int flags, int *errorp)
{
	return zip_open(path, flags, errorp);
}

int subsurface_zip_close(struct zip *zip)
{
	return zip_close(zip);
}

/* win32 console */
void subsurface_console_init(bool dedicated, bool logfile)
{
	(void)dedicated;
	(void)logfile;
	/* NOP */
}

void subsurface_console_exit(void)
{
	/* NOP */
}

bool subsurface_user_is_root()
{
	return (geteuid() == 0);
}
