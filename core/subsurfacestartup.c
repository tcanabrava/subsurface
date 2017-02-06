#include "subsurfacestartup.h"
#include "version.h"
#include <stdbool.h>
#include <string.h>
#include "gettext.h"
#include "qthelperfromc.h"
#include "git-access.h"
#include "libdivecomputer/version.h"

struct preferences prefs, git_prefs;
struct preferences default_prefs = {
	.cloud_base_url = "https://cloud.subsurface-divelog.org/",
#if defined(SUBSURFACE_MOBILE)
	.git_local_only = true,
#endif
	.units = SI_UNITS,
	.unit_system = METRIC,
	.coordinates_traditional = true,
	.pp_graphs = {
		.po2 = false,
		.pn2 = false,
		.phe = false,
		.po2_threshold = 1.6,
		.pn2_threshold = 4.0,
		.phe_threshold = 13.0,
	},
	.mod = false,
	.modpO2 = 1.6,
	.ead = false,
	.hrgraph = false,
	.percentagegraph = false,
	.dcceiling = true,
	.redceiling = false,
	.calcceiling = false,
	.calcceiling3m = false,
	.calcndltts = false,
	.gflow = 30,
	.gfhigh = 75,
	.animation_speed = 500,
	.gf_low_at_maxdepth = false,
	.show_ccr_setpoint = false,
	.show_ccr_sensors = false,
	.font_size = -1,
	.display_invalid_dives = false,
	.show_sac = false,
	.display_unused_tanks = false,
	.show_average_depth = true,
	.ascrate75 = 9000 / 60,
	.ascrate50 = 6000 / 60,
	.ascratestops = 6000 / 60,
	.ascratelast6m = 1000 / 60,
	.descrate = 18000 / 60,
	.bottompo2 = 1400,
	.decopo2 = 1600,
	.bestmixend.mm = 30000,
	.doo2breaks = false,
	.drop_stone_mode = false,
	.switch_at_req_stop = false,
	.min_switch_duration = 60,
	.last_stop = false,
	.verbatim_plan = false,
	.display_runtime = true,
	.display_duration = true,
	.display_transitions = true,
	.safetystop = true,
	.bottomsac = 20000,
	.decosac = 17000,
	.reserve_gas=40000,
	.o2consumption = 720,
	.pscr_ratio = 100,
	.show_pictures_in_profile = true,
	.tankbar = false,
	.facebook = {
		.user_id = NULL,
		.album_id = NULL,
		.access_token = NULL
	},
	.defaultsetpoint = 1100,
	.cloud_background_sync = true,
	.geocoding = {
		.enable_geocoding = true,
		.parse_dive_without_gps = false,
		.tag_existing_dives = false,
		.category = { 0 }
	},
	.locale = {
		.use_system_language = true,
	},
	.planner_deco_mode = BUEHLMANN,
	.vpmb_conservatism = 3,
	.distance_threshold = 1000,
	.time_threshold = 600,
	.cloud_timeout = 5
};

int run_survey;

struct units *get_units()
{
	return &prefs.units;
}

/* random helper functions, used here or elsewhere */
static int sortfn(const void *_a, const void *_b)
{
	const struct dive *a = (const struct dive *)*(void **)_a;
	const struct dive *b = (const struct dive *)*(void **)_b;

	if (a->when < b->when)
		return -1;
	if (a->when > b->when)
		return 1;
	return 0;
}

void sort_table(struct dive_table *table)
{
	qsort(table->dives, table->nr, sizeof(struct dive *), sortfn);
}

const char *weekday(int wday)
{
	static const char wday_array[7][7] = {
		/*++GETTEXT: these are three letter days - we allow up to six code bytes */
		QT_TRANSLATE_NOOP("gettextFromC", "Sun"), QT_TRANSLATE_NOOP("gettextFromC", "Mon"), QT_TRANSLATE_NOOP("gettextFromC", "Tue"), QT_TRANSLATE_NOOP("gettextFromC", "Wed"), QT_TRANSLATE_NOOP("gettextFromC", "Thu"), QT_TRANSLATE_NOOP("gettextFromC", "Fri"), QT_TRANSLATE_NOOP("gettextFromC", "Sat")
	};
	return translate("gettextFromC", wday_array[wday]);
}

const char *monthname(int mon)
{
	static const char month_array[12][7] = {
		/*++GETTEXT: these are three letter months - we allow up to six code bytes*/
		QT_TRANSLATE_NOOP("gettextFromC", "Jan"), QT_TRANSLATE_NOOP("gettextFromC", "Feb"), QT_TRANSLATE_NOOP("gettextFromC", "Mar"), QT_TRANSLATE_NOOP("gettextFromC", "Apr"), QT_TRANSLATE_NOOP("gettextFromC", "May"), QT_TRANSLATE_NOOP("gettextFromC", "Jun"),
		QT_TRANSLATE_NOOP("gettextFromC", "Jul"), QT_TRANSLATE_NOOP("gettextFromC", "Aug"), QT_TRANSLATE_NOOP("gettextFromC", "Sep"), QT_TRANSLATE_NOOP("gettextFromC", "Oct"), QT_TRANSLATE_NOOP("gettextFromC", "Nov"), QT_TRANSLATE_NOOP("gettextFromC", "Dec"),
	};
	return translate("gettextFromC", month_array[mon]);
}

/*
 * track whether we switched to importing dives
 */
bool imported = false;

static void print_version()
{
	printf("Subsurface v%s, ", subsurface_git_version());
	printf("built with libdivecomputer v%s\n", dc_version(NULL));
}

void print_files()
{
	const char *branch = 0;
	const char *remote = 0;
	const char *filename, *local_git;

	filename = cloud_url();

	is_git_repository(filename, &branch, &remote, true);
	printf("\nFile locations:\n\n");
	if (branch && remote) {
		local_git = get_local_dir(remote, branch);
		printf("Local git storage: %s\n", local_git);
	} else {
		printf("Unable to get local git directory\n");
	}
	char *tmp = cloud_url();
	printf("Cloud URL: %s\n", tmp);
	free(tmp);
	tmp = hashfile_name_string();
	printf("Image hashes: %s\n", tmp);
	free(tmp);
	tmp = picturedir_string();
	printf("Local picture directory: %s\n\n", tmp);
	free(tmp);
}

static void print_help()
{
	print_version();
	printf("\nUsage: subsurface [options] [logfile ...] [--import logfile ...]");
	printf("\n\noptions include:");
	printf("\n --help|-h             This help text");
	printf("\n --import logfile ...  Logs before this option is treated as base, everything after is imported");
	printf("\n --verbose|-v          Verbose debug (repeat to increase verbosity)");
	printf("\n --version             Prints current version");
	printf("\n --survey              Offer to submit a user survey");
	printf("\n --user=<test>         Choose configuration space for user <test>");
	printf("\n --cloud-timeout=<nr>  Set timeout for cloud connection (0 < timeout < 60)");
	printf("\n --win32console        Create a dedicated console if needed (Windows only). Add option before everything else");
	printf("\n --win32log            Write the program output to subsurface.log (Windows only). Add option before everything else\n\n");
}

void parse_argument(const char *arg)
{
	const char *p = arg + 1;

	do {
		switch (*p) {
		case 'h':
			print_help();
			exit(0);
		case 'v':
			verbose++;
			continue;
		case 'q':
			quit++;
			continue;
		case '-':
			/* long options with -- */
			/* first test for --user=bla which allows the use of user specific settings */
			if (strncmp(arg, "--user=", sizeof("--user=") - 1) == 0) {
				settings_suffix = strdup(arg + sizeof("--user=") - 1);
				return;
			}
			if (strncmp(arg, "--cloud-timeout=", sizeof("--cloud-timeout=") - 1) == 0) {
				const char *timeout = arg + sizeof("--cloud-timeout=") - 1;
				int to = strtol(timeout, NULL, 10);
				if (0 < to && to < 60)
					default_prefs.cloud_timeout = to;
				return;
			}
			if (strcmp(arg, "--help") == 0) {
				print_help();
				exit(0);
			}
			if (strcmp(arg, "--import") == 0) {
				imported = true; /* mark the dives so far as the base, * everything after is imported */
				return;
			}
			if (strcmp(arg, "--verbose") == 0) {
				verbose++;
				return;
			}
			if (strcmp(arg, "--version") == 0) {
				print_version();
				exit(0);
			}
			if (strcmp(arg, "--survey") == 0) {
				run_survey = true;
				return;
			}
			if (strcmp(arg, "--allow_run_as_root") == 0) {
				++force_root;
				return;
			}
			if (strcmp(arg, "--win32console") == 0)
				return;
			if (strcmp(arg, "--win32log") == 0)
				return;
		/* fallthrough */
		case 'p':
			/* ignore process serial number argument when run as native macosx app */
			if (strncmp(arg, "-psQT_TR_NOOP(", 5) == 0) {
				return;
			}
		/* fallthrough */
		default:
			fprintf(stderr, "Bad argument '%s'\n", arg);
			exit(1);
		}
	} while (*++p);
}

/*
 * Under a POSIX setup, the locale string should have a format
 * like [language[_territory][.codeset][@modifier]].
 *
 * So search for the underscore, and see if the "territory" is
 * US, and turn on imperial units by default.
 *
 * I guess Burma and Liberia should trigger this too. I'm too
 * lazy to look up the territory names, though.
 */
void setup_system_prefs(void)
{
	const char *env;

	subsurface_OS_pref_setup();
	default_prefs.divelist_font = strdup(system_divelist_default_font);
	default_prefs.font_size = system_divelist_default_font_size;

#if !defined(SUBSURFACE_MOBILE)
	default_prefs.default_filename = system_default_filename();
#endif
	env = getenv("LC_MEASUREMENT");
	if (!env)
		env = getenv("LC_ALL");
	if (!env)
		env = getenv("LANG");
	if (!env)
		return;
	env = strchr(env, '_');
	if (!env)
		return;
	env++;
	if (strncmp(env, "US", 2))
		return;

	default_prefs.units = IMPERIAL_units;
}

/* copy a preferences block, including making copies of all included strings */
void copy_prefs(struct preferences *src, struct preferences *dest)
{
	*dest = *src;
	dest->divelist_font = copy_string(src->divelist_font);
	dest->default_filename = copy_string(src->default_filename);
	dest->default_cylinder = copy_string(src->default_cylinder);
	dest->cloud_base_url = copy_string(src->cloud_base_url);
	dest->cloud_git_url = copy_string(src->cloud_git_url);
	dest->userid = copy_string(src->userid);
	dest->proxy_host = copy_string(src->proxy_host);
	dest->proxy_user = copy_string(src->proxy_user);
	dest->proxy_pass = copy_string(src->proxy_pass);
	dest->time_format = copy_string(src->time_format);
	dest->date_format = copy_string(src->date_format);
	dest->date_format_short = copy_string(src->date_format_short);
	dest->cloud_storage_password = copy_string(src->cloud_storage_password);
	dest->cloud_storage_newpassword = copy_string(src->cloud_storage_newpassword);
	dest->cloud_storage_email = copy_string(src->cloud_storage_email);
	dest->cloud_storage_email_encoded = copy_string(src->cloud_storage_email_encoded);
	dest->facebook.access_token = copy_string(src->facebook.access_token);
	dest->facebook.user_id = copy_string(src->facebook.user_id);
	dest->facebook.album_id = copy_string(src->facebook.album_id);
}

/*
 * Free strduped prefs before exit.
 *
 * These are not real leaks but they plug the holes found by eg.
 * valgrind so you can find the real leaks.
 */
void free_prefs(void)
{
	// nop
}
