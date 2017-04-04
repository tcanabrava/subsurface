#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "gettext.h"
#include <zip.h>
#include <time.h>

#include "dive.h"
#include "divelist.h"
#include "file.h"
#include "git-access.h"
#include "qthelperfromc.h"

/* For SAMPLE_* */
#include <libdivecomputer/parser.h>

/* to check XSLT version number */
#include <libxslt/xsltconfig.h>

/* Crazy windows sh*t */
#ifndef O_BINARY
#define O_BINARY 0
#endif

int readfile(const char *filename, struct memblock *mem)
{
	int ret, fd;
	struct stat st;
	char *buf;

	mem->buffer = NULL;
	mem->size = 0;

	fd = subsurface_open(filename, O_RDONLY | O_BINARY, 0);
	if (fd < 0)
		return fd;
	ret = fstat(fd, &st);
	if (ret < 0)
		goto out;
	ret = -EINVAL;
	if (!S_ISREG(st.st_mode))
		goto out;
	ret = 0;
	if (!st.st_size)
		goto out;
	buf = malloc(st.st_size + 1);
	ret = -1;
	errno = ENOMEM;
	if (!buf)
		goto out;
	mem->buffer = buf;
	mem->size = st.st_size;
	ret = read(fd, buf, mem->size);
	if (ret < 0)
		goto free;
	buf[ret] = 0;
	if (ret == (int)mem->size) // converting to int loses a bit but size will never be that big
		goto out;
	errno = EIO;
	ret = -1;
free:
	free(mem->buffer);
	mem->buffer = NULL;
	mem->size = 0;
out:
	close(fd);
	return ret;
}


static void zip_read(struct zip_file *file, const char *filename)
{
	int size = 1024, n, read = 0;
	char *mem = malloc(size);

	while ((n = zip_fread(file, mem + read, size - read)) > 0) {
		read += n;
		size = read * 3 / 2;
		mem = realloc(mem, size);
	}
	mem[read] = 0;
	(void) parse_xml_buffer(filename, mem, read, &dive_table, NULL);
	free(mem);
}

int try_to_open_zip(const char *filename)
{
	int success = 0;
	/* Grr. libzip needs to re-open the file, it can't take a buffer */
	struct zip *zip = subsurface_zip_open_readonly(filename, ZIP_CHECKCONS, NULL);

	if (zip) {
		int index;
		for (index = 0;; index++) {
			struct zip_file *file = zip_fopen_index(zip, index, 0);
			if (!file)
				break;
			/* skip parsing the divelogs.de pictures */
			if (strstr(zip_get_name(zip, index, 0), "pictures/"))
				continue;
			zip_read(file, filename);
			zip_fclose(file);
			success++;
		}
		subsurface_zip_close(zip);

		if (!success)
			return report_error(translate("gettextFromC", "No dives in the input file '%s'"), filename);
	}
	return success;
}

static int try_to_xslt_open_csv(const char *filename, struct memblock *mem, const char *tag)
{
	char *buf;

	if (mem->size == 0 && readfile(filename, mem) < 0)
		return report_error(translate("gettextFromC", "Failed to read '%s'"), filename);

	/* Surround the CSV file content with XML tags to enable XSLT
	 * parsing
	 *
	 * Tag markers take: strlen("<></>") = 5
	 */
	buf = realloc(mem->buffer, mem->size + 7 + strlen(tag) * 2);
	if (buf != NULL) {
		char *starttag = NULL;
		char *endtag = NULL;

		starttag = malloc(3 + strlen(tag));
		endtag = malloc(5 + strlen(tag));

		if (starttag == NULL || endtag == NULL) {
			/* this is fairly silly - so the malloc fails, but we strdup the error?
			 * let's complete the silliness by freeing the two pointers in case one malloc succeeded
			 *  and the other one failed - this will make static analysis tools happy */
			free(starttag);
			free(endtag);
			free(buf);
			return report_error("Memory allocation failed in %s", __func__);
		}

		sprintf(starttag, "<%s>", tag);
		sprintf(endtag, "\n</%s>", tag);

		memmove(buf + 2 + strlen(tag), buf, mem->size);
		memcpy(buf, starttag, 2 + strlen(tag));
		memcpy(buf + mem->size + 2 + strlen(tag), endtag, 5 + strlen(tag));
		mem->size += (6 + 2 * strlen(tag));
		mem->buffer = buf;

		free(starttag);
		free(endtag);
	} else {
		free(mem->buffer);
		return report_error("realloc failed in %s", __func__);
	}

	return 0;
}

int db_test_func(void *param, int columns, char **data, char **column)
{
	(void) param;
	(void) columns;
	(void) column;
	return *data[0] == '0';
}


static int try_to_open_db(const char *filename, struct memblock *mem)
{
	sqlite3 *handle;
	char dm4_test[] = "select count(*) from sqlite_master where type='table' and name='Dive' and sql like '%ProfileBlob%'";
	char dm5_test[] = "select count(*) from sqlite_master where type='table' and name='Dive' and sql like '%SampleBlob%'";
	char shearwater_test[] = "select count(*) from sqlite_master where type='table' and name='system' and sql like '%dbVersion%'";
	char cobalt_test[] = "select count(*) from sqlite_master where type='table' and name='TrackPoints' and sql like '%DepthPressure%'";
	char divinglog_test[] = "select count(*) from sqlite_master where type='table' and name='DBInfo' and sql like '%PrgName%'";
	int retval;

	retval = sqlite3_open(filename, &handle);

	if (retval) {
		fprintf(stderr, "Database connection failed '%s'.\n", filename);
		return 1;
	}

	/* Testing if DB schema resembles Suunto DM5 database format */
	retval = sqlite3_exec(handle, dm5_test, &db_test_func, 0, NULL);
	if (!retval) {
		retval = parse_dm5_buffer(handle, filename, mem->buffer, mem->size, &dive_table);
		sqlite3_close(handle);
		return retval;
	}

	/* Testing if DB schema resembles Suunto DM4 database format */
	retval = sqlite3_exec(handle, dm4_test, &db_test_func, 0, NULL);
	if (!retval) {
		retval = parse_dm4_buffer(handle, filename, mem->buffer, mem->size, &dive_table);
		sqlite3_close(handle);
		return retval;
	}

	/* Testing if DB schema resembles Shearwater database format */
	retval = sqlite3_exec(handle, shearwater_test, &db_test_func, 0, NULL);
	if (!retval) {
		retval = parse_shearwater_buffer(handle, filename, mem->buffer, mem->size, &dive_table);
		sqlite3_close(handle);
		return retval;
	}

	/* Testing if DB schema resembles Atomic Cobalt database format */
	retval = sqlite3_exec(handle, cobalt_test, &db_test_func, 0, NULL);
	if (!retval) {
		retval = parse_cobalt_buffer(handle, filename, mem->buffer, mem->size, &dive_table);
		sqlite3_close(handle);
		return retval;
	}

	/* Testing if DB schema resembles Divinglog database format */
	retval = sqlite3_exec(handle, divinglog_test, &db_test_func, 0, NULL);
	if (!retval) {
		retval = parse_divinglog_buffer(handle, filename, mem->buffer, mem->size, &dive_table);
		sqlite3_close(handle);
		return retval;
	}

	sqlite3_close(handle);

	return retval;
}

timestamp_t parse_date(const char *date)
{
	int hour, min, sec;
	struct tm tm;
	char *p;

	memset(&tm, 0, sizeof(tm));
	tm.tm_mday = strtol(date, &p, 10);
	if (tm.tm_mday < 1 || tm.tm_mday > 31)
		return 0;
	for (tm.tm_mon = 0; tm.tm_mon < 12; tm.tm_mon++) {
		if (!memcmp(p, monthname(tm.tm_mon), 3))
			break;
	}
	if (tm.tm_mon > 11)
		return 0;
	date = p + 3;
	tm.tm_year = strtol(date, &p, 10);
	if (date == p)
		return 0;
	if (tm.tm_year < 70)
		tm.tm_year += 2000;
	if (tm.tm_year < 100)
		tm.tm_year += 1900;
	if (sscanf(p, "%d:%d:%d", &hour, &min, &sec) != 3)
		return 0;
	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_sec = sec;
	return utc_mktime(&tm);
}

enum csv_format {
	CSV_DEPTH,
	CSV_TEMP,
	CSV_PRESSURE,
	POSEIDON_DEPTH,
	POSEIDON_TEMP,
	POSEIDON_SETPOINT,
	POSEIDON_SENSOR1,
	POSEIDON_SENSOR2,
	POSEIDON_PRESSURE,
	POSEIDON_O2CYLINDER,
	POSEIDON_NDL,
	POSEIDON_CEILING
};

static void add_sample_data(struct sample *sample, enum csv_format type, double val)
{
	switch (type) {
	case CSV_DEPTH:
		sample->depth.mm = feet_to_mm(val);
		break;
	case CSV_TEMP:
		sample->temperature.mkelvin = F_to_mkelvin(val);
		break;
	case CSV_PRESSURE:
		sample->cylinderpressure.mbar = psi_to_mbar(val * 4);
		break;
	case POSEIDON_DEPTH:
		sample->depth.mm = lrint(val * 0.5 * 1000);
		break;
	case POSEIDON_TEMP:
		sample->temperature.mkelvin = C_to_mkelvin(val * 0.2);
		break;
	case POSEIDON_SETPOINT:
		sample->setpoint.mbar = lrint(val * 10);
		break;
	case POSEIDON_SENSOR1:
		sample->o2sensor[0].mbar = lrint(val * 10);
		break;
	case POSEIDON_SENSOR2:
		sample->o2sensor[1].mbar = lrint(val * 10);
		break;
	case POSEIDON_PRESSURE:
		sample->cylinderpressure.mbar = lrint(val * 1000);
		break;
	case POSEIDON_O2CYLINDER:
		sample->o2cylinderpressure.mbar = lrint(val * 1000);
		break;
	case POSEIDON_NDL:
		sample->ndl.seconds = lrint(val * 60);
		break;
	case POSEIDON_CEILING:
		sample->stopdepth.mm = lrint(val * 1000);
		break;
	}
}

/*
 * Cochran comma-separated values: depth in feet, temperature in F, pressure in psi.
 *
 * They start with eight comma-separated fields like:
 *
 *   filename: {C:\Analyst4\can\T036785.can},{C:\Analyst4\can\K031892.can}
 *   divenr: %d
 *   datetime: {03Sep11 16:37:22},{15Dec11 18:27:02}
 *   ??: 1
 *   serialnr??: {CCI134},{CCI207}
 *   computer??: {GeminiII},{CommanderIII}
 *   computer??: {GeminiII},{CommanderIII}
 *   ??: 1
 *
 * Followed by the data values (all comma-separated, all one long line).
 */
static int try_to_open_csv(struct memblock *mem, enum csv_format type)
{
	char *p = mem->buffer;
	char *header[8];
	int i, time;
	timestamp_t date;
	struct dive *dive;
	struct divecomputer *dc;

	for (i = 0; i < 8; i++) {
		header[i] = p;
		p = strchr(p, ',');
		if (!p)
			return 0;
		p++;
	}

	date = parse_date(header[2]);
	if (!date)
		return 0;

	dive = alloc_dive();
	dive->when = date;
	dive->number = atoi(header[1]);
	dc = &dive->dc;

	time = 0;
	for (;;) {
		char *end;
		double val;
		struct sample *sample;

		errno = 0;
		val = strtod(p, &end); // FIXME == localization issue
		if (end == p)
			break;
		if (errno)
			break;

		sample = prepare_sample(dc);
		sample->time.seconds = time;
		add_sample_data(sample, type, val);
		finish_sample(dc);

		time++;
		dc->duration.seconds = time;
		if (*end != ',')
			break;
		p = end + 1;
	}
	record_dive(dive);
	return 1;
}

static int open_by_filename(const char *filename, const char *fmt, struct memblock *mem)
{
	// hack to be able to provide a comment for the translated string
	static char *csv_warning = QT_TRANSLATE_NOOP3("gettextFromC",
						      "Cannot open CSV file %s; please use Import log file dialog",
						      "'Import log file' should be the same text as corresponding label in Import menu");

	/* Suunto Dive Manager files: SDE, ZIP; divelogs.de files: DLD */
	if (!strcasecmp(fmt, "SDE") || !strcasecmp(fmt, "ZIP") || !strcasecmp(fmt, "DLD"))
		return try_to_open_zip(filename);

	/* CSV files */
	if (!strcasecmp(fmt, "CSV"))
		return report_error(translate("gettextFromC", csv_warning), filename);
	/* Truly nasty intentionally obfuscated Cochran Anal software */
	if (!strcasecmp(fmt, "CAN"))
		return try_to_open_cochran(filename, mem);
	/* Cochran export comma-separated-value files */
	if (!strcasecmp(fmt, "DPT"))
		return try_to_open_csv(mem, CSV_DEPTH);
	if (!strcasecmp(fmt, "LVD"))
		return try_to_open_liquivision(filename, mem);
	if (!strcasecmp(fmt, "TMP"))
		return try_to_open_csv(mem, CSV_TEMP);
	if (!strcasecmp(fmt, "HP1"))
		return try_to_open_csv(mem, CSV_PRESSURE);

	return 0;
}

static int parse_file_buffer(const char *filename, struct memblock *mem)
{
	int ret;
	char *fmt = strrchr(filename, '.');
	if (fmt && (ret = open_by_filename(filename, fmt + 1, mem)) != 0)
		return ret;

	if (!mem->size || !mem->buffer)
		return report_error("Out of memory parsing file %s\n", filename);

	return parse_xml_buffer(filename, mem->buffer, mem->size, &dive_table, NULL);
}

int check_git_sha(const char *filename, struct git_repository **git_p, const char **branch_p)
{
	struct git_repository *git;
	const char *branch = NULL;

	char *current_sha = strdup(saved_git_id);
	git = is_git_repository(filename, &branch, NULL, false);
	if (git_p)
		*git_p = git;
	if (branch_p)
		*branch_p = branch;
	if (prefs.cloud_git_url &&
	    strstr(filename, prefs.cloud_git_url)
	    && git == dummy_git_repository) {
		/* opening the cloud storage repository failed for some reason,
		 * so we don't know if there is additional data in the remote */
		free(current_sha);
		return 1;
	}
	/* if this is a git repository, do we already have this exact state loaded ?
	 * get the SHA and compare with what we currently have */
	if (git && git != dummy_git_repository) {
		const char *sha = get_sha(git, branch);
		if (!same_string(sha, "") &&
		    same_string(sha, current_sha)) {
			fprintf(stderr, "already have loaded SHA %s - don't load again\n", sha);
			free(current_sha);
			return 0;
		}
	}
	free(current_sha);
	return 1;
}

int parse_file(const char *filename)
{
	struct git_repository *git;
	const char *branch = NULL;
	char *current_sha = copy_string(saved_git_id);
	struct memblock mem;
	char *fmt;
	int ret;

	git = is_git_repository(filename, &branch, NULL, false);
	if (prefs.cloud_git_url &&
	    strstr(filename, prefs.cloud_git_url)
	    && git == dummy_git_repository) {
		/* opening the cloud storage repository failed for some reason
		 * give up here and don't send errors about git repositories */
		free(current_sha);
		return -1;
	}
	/* if this is a git repository, do we already have this exact state loaded ?
	 * get the SHA and compare with what we currently have */
	if (git && git != dummy_git_repository) {
		const char *sha = get_sha(git, branch);
		if (!same_string(sha, "") &&
		    same_string(sha, current_sha) &&
		    !unsaved_changes()) {
			fprintf(stderr, "already have loaded SHA %s - don't load again\n", sha);
			free(current_sha);
			return 0;
		}
	}
	free(current_sha);
	if (git)
		return git_load_dives(git, branch);

	if ((ret = readfile(filename, &mem)) < 0) {
		/* we don't want to display an error if this was the default file or the cloud storage */
		if ((prefs.default_filename && !strcmp(filename, prefs.default_filename)) ||
		    isCloudUrl(filename))
			return 0;

		return report_error(translate("gettextFromC", "Failed to read '%s'"), filename);
	} else if (ret == 0) {
		return report_error(translate("gettextFromC", "Empty file '%s'"), filename);
	}

	fmt = strrchr(filename, '.');
	if (fmt && (!strcasecmp(fmt + 1, "DB") || !strcasecmp(fmt + 1, "BAK") || !strcasecmp(fmt + 1, "SQL"))) {
		if (!try_to_open_db(filename, &mem)) {
			free(mem.buffer);
			return 0;
		}
	}

	/* Divesoft Freedom */
	if (fmt && (!strcasecmp(fmt + 1, "DLF"))) {
		if (!parse_dlf_buffer(mem.buffer, mem.size)) {
			free(mem.buffer);
			return 0;
		}
		return -1;
	}

	/* DataTrak/Wlog */
	if (fmt && !strcasecmp(fmt + 1, "LOG")) {
		datatrak_import(filename, &dive_table);
		return 0;
	}

	/* OSTCtools */
	if (fmt && (!strcasecmp(fmt + 1, "DIVE"))) {
		ostctools_import(filename, &dive_table);
		return 0;
	}

	ret = parse_file_buffer(filename, &mem);
	free(mem.buffer);
	return ret;
}

#define MATCH(buffer, pattern) \
	memcmp(buffer, pattern, strlen(pattern))

char *parse_mkvi_value(const char *haystack, const char *needle)
{
	char *lineptr, *valueptr, *endptr, *ret = NULL;

	if ((lineptr = strstr(haystack, needle)) != NULL) {
		if ((valueptr = strstr(lineptr, ": ")) != NULL) {
			valueptr += 2;
		}
		if ((endptr = strstr(lineptr, "\n")) != NULL) {
			char terminator = '\n';
			if (*(endptr - 1) == '\r') {
				--endptr;
				terminator = '\r';
			}
			*endptr = 0;
			ret = copy_string(valueptr);
			*endptr = terminator;

		}
	}
	return ret;
}

char *next_mkvi_key(const char *haystack)
{
	char *valueptr, *endptr, *ret = NULL;

	if ((valueptr = strstr(haystack, "\n")) != NULL) {
		valueptr += 1;
		if ((endptr = strstr(valueptr, ": ")) != NULL) {
			*endptr = 0;
			ret = strdup(valueptr);
			*endptr = ':';
		}
	}
	return ret;
}

int parse_txt_file(const char *filename, const char *csv)
{
	struct memblock memtxt, memcsv;

	if (readfile(filename, &memtxt) < 0) {
		return report_error(translate("gettextFromC", "Failed to read '%s'"), filename);
	}

	/*
	 * MkVI stores some information in .txt file but the whole profile and events are stored in .csv file. First
	 * make sure the input .txt looks like proper MkVI file, then start parsing the .csv.
	 */
	if (MATCH(memtxt.buffer, "MkVI_Config") == 0) {
		int d, m, y, he;
		int hh = 0, mm = 0, ss = 0;
		int prev_depth = 0, cur_sampletime = 0, prev_setpoint = -1, prev_ndl = -1;
		bool has_depth = false, has_setpoint = false, has_ndl = false;
		char *lineptr, *key, *value;
		int o2cylinder_pressure = 0, cylinder_pressure = 0, cur_cylinder_index = 0;
		unsigned int prev_time = 0;

		struct dive *dive;
		struct divecomputer *dc;
		struct tm cur_tm;

		value = parse_mkvi_value(memtxt.buffer, "Dive started at");
		if (sscanf(value, "%d-%d-%d %d:%d:%d", &y, &m, &d, &hh, &mm, &ss) != 6) {
			free(value);
			return -1;
		}
		free(value);
		cur_tm.tm_year = y;
		cur_tm.tm_mon = m - 1;
		cur_tm.tm_mday = d;
		cur_tm.tm_hour = hh;
		cur_tm.tm_min = mm;
		cur_tm.tm_sec = ss;

		dive = alloc_dive();
		dive->when = utc_mktime(&cur_tm);;
		dive->dc.model = strdup("Poseidon MkVI Discovery");
		value = parse_mkvi_value(memtxt.buffer, "Rig Serial number");
		dive->dc.deviceid = atoi(value);
		free(value);
		dive->dc.divemode = CCR;
		dive->dc.no_o2sensors = 2;

		dive->cylinder[cur_cylinder_index].cylinder_use = OXYGEN;
		dive->cylinder[cur_cylinder_index].type.size.mliter = 3000;
		dive->cylinder[cur_cylinder_index].type.workingpressure.mbar = 200000;
		dive->cylinder[cur_cylinder_index].type.description = strdup("3l Mk6");
		dive->cylinder[cur_cylinder_index].gasmix.o2.permille = 1000;
		cur_cylinder_index++;

		dive->cylinder[cur_cylinder_index].cylinder_use = DILUENT;
		dive->cylinder[cur_cylinder_index].type.size.mliter = 3000;
		dive->cylinder[cur_cylinder_index].type.workingpressure.mbar = 200000;
		dive->cylinder[cur_cylinder_index].type.description = strdup("3l Mk6");
		value = parse_mkvi_value(memtxt.buffer, "Helium percentage");
		he = atoi(value);
		free(value);
		value = parse_mkvi_value(memtxt.buffer, "Nitrogen percentage");
		dive->cylinder[cur_cylinder_index].gasmix.o2.permille = (100 - atoi(value) - he) * 10;
		free(value);
		dive->cylinder[cur_cylinder_index].gasmix.he.permille = he * 10;
		cur_cylinder_index++;

		lineptr = strstr(memtxt.buffer, "Dive started at");
		while (lineptr && *lineptr && (lineptr = strchr(lineptr, '\n')) && ++lineptr) {
			key = next_mkvi_key(lineptr);
			if (!key)
				break;
			value = parse_mkvi_value(lineptr, key);
			if (!value) {
				free(key);
				break;
			}
			add_extra_data(&dive->dc, key, value);
			free(key);
			free(value);
		}
		dc = &dive->dc;

		/*
		 * Read samples from the CSV file. A sample contains all the lines with same timestamp. The CSV file has
		 * the following format:
		 *
		 * timestamp, type, value
		 *
		 * And following fields are of interest to us:
		 *
		 * 	6	sensor1
		 * 	7	sensor2
		 * 	8	depth
		 *	13	o2 tank pressure
		 *	14	diluent tank pressure
		 *	20	o2 setpoint
		 *	39	water temp
		 */

		if (readfile(csv, &memcsv) < 0) {
			free(dive);
			return report_error(translate("gettextFromC", "Poseidon import failed: unable to read '%s'"), csv);
		}
		lineptr = memcsv.buffer;
		for (;;) {
			struct sample *sample;
			int type;
			int value;
			int sampletime;
			int gaschange = 0;

			/* Collect all the information for one sample */
			sscanf(lineptr, "%d,%d,%d", &cur_sampletime, &type, &value);

			has_depth = false;
			has_setpoint = false;
			has_ndl = false;
			sample = prepare_sample(dc);

			/*
			 * There was a bug in MKVI download tool that resulted in erroneous sample
			 * times. This fix should work similarly as the vendor's own.
			 */

			sample->time.seconds = cur_sampletime < 0xFFFF * 3 / 4 ? cur_sampletime : prev_time;
			prev_time = sample->time.seconds;

			do {
				int i = sscanf(lineptr, "%d,%d,%d", &sampletime, &type, &value);
				switch (i) {
				case 3:
					switch (type) {
					case 0:
						//Mouth piece position event: 0=OC, 1=CC, 2=UN, 3=NC
						switch (value) {
						case 0:
							add_event(dc, cur_sampletime, 0, 0, 0,
									QT_TRANSLATE_NOOP("gettextFromC", "Mouth piece position OC"));
							break;
						case 1:
							add_event(dc, cur_sampletime, 0, 0, 0,
									QT_TRANSLATE_NOOP("gettextFromC", "Mouth piece position CC"));
							break;
						case 2:
							add_event(dc, cur_sampletime, 0, 0, 0,
									QT_TRANSLATE_NOOP("gettextFromC", "Mouth piece position unknown"));
							break;
						case 3:
							add_event(dc, cur_sampletime, 0, 0, 0,
									QT_TRANSLATE_NOOP("gettextFromC", "Mouth piece position not connected"));
							break;
						}
						break;
					case 3:
						//Power Off event
						add_event(dc, cur_sampletime, 0, 0, 0,
								QT_TRANSLATE_NOOP("gettextFromC", "Power off"));
						break;
					case 4:
						//Battery State of Charge in %
#ifdef SAMPLE_EVENT_BATTERY
						add_event(dc, cur_sampletime, SAMPLE_EVENT_BATTERY, 0,
								value, QT_TRANSLATE_NOOP("gettextFromC", "battery"));
#endif
						break;
					case 6:
						//PO2 Cell 1 Average
						add_sample_data(sample, POSEIDON_SENSOR1, value);
						break;
					case 7:
						//PO2 Cell 2 Average
						add_sample_data(sample, POSEIDON_SENSOR2, value);
						break;
					case 8:
						//Depth * 2
						has_depth = true;
						prev_depth = value;
						add_sample_data(sample, POSEIDON_DEPTH, value);
						break;
						//9 Max Depth * 2
						//10 Ascent/Descent Rate * 2
					case 11:
						//Ascent Rate Alert >10 m/s
						add_event(dc, cur_sampletime, SAMPLE_EVENT_ASCENT, 0, 0,
								QT_TRANSLATE_NOOP("gettextFromC", "ascent"));
						break;
					case 13:
						//O2 Tank Pressure
						add_sample_data(sample, POSEIDON_O2CYLINDER, value);
						if (!o2cylinder_pressure) {
							dive->cylinder[0].sample_start.mbar = value * 1000;
							o2cylinder_pressure = value;
						} else
							o2cylinder_pressure = value;
						break;
					case 14:
						//Diluent Tank Pressure
						add_sample_data(sample, POSEIDON_PRESSURE, value);
						if (!cylinder_pressure) {
							dive->cylinder[1].sample_start.mbar = value * 1000;
							cylinder_pressure = value;
						} else
							cylinder_pressure = value;
						break;
						//16 Remaining dive time #1?
						//17 related to O2 injection
					case 20:
						//PO2 Setpoint
						has_setpoint = true;
						prev_setpoint = value;
						add_sample_data(sample, POSEIDON_SETPOINT, value);
						break;
					case 22:
						//End of O2 calibration Event: 0 = OK, 2 = Failed, rest of dive setpoint 1.0
						if (value == 2)
							add_event(dc, cur_sampletime, 0, SAMPLE_FLAGS_END, 0,
									QT_TRANSLATE_NOOP("gettextFromC", "O₂ calibration failed"));
						add_event(dc, cur_sampletime, 0, SAMPLE_FLAGS_END, 0,
								QT_TRANSLATE_NOOP("gettextFromC", "O₂ calibration"));
						break;
					case 25:
						//25 Max Ascent depth
						add_sample_data(sample, POSEIDON_CEILING, value);
						break;
					case 31:
						//Start of O2 calibration Event
						add_event(dc, cur_sampletime, 0, SAMPLE_FLAGS_BEGIN, 0,
								QT_TRANSLATE_NOOP("gettextFromC", "O₂ calibration"));
						break;
					case 37:
						//Remaining dive time #2?
						has_ndl = true;
						prev_ndl = value;
						add_sample_data(sample, POSEIDON_NDL, value);
						break;
					case 39:
						// Water Temperature in Celcius
						add_sample_data(sample, POSEIDON_TEMP, value);
						break;
					case 85:
						//He diluent part in %
						gaschange += value << 16;
						break;
					case 86:
						//O2 diluent part in %
						gaschange += value;
						break;
						//239 Unknown, maybe PO2 at sensor validation?
						//240 Unknown, maybe PO2 at sensor validation?
						//247 Unknown, maybe PO2 Cell 1 during pressure test
						//248 Unknown, maybe PO2 Cell 2 during pressure test
						//250 PO2 Cell 1
						//251 PO2 Cell 2
					default:
						break;
					} /* sample types */
					break;
				case EOF:
					break;
				default:
					printf("Unable to parse input: %s\n", lineptr);
					break;
				}

				lineptr = strchr(lineptr, '\n');
				if (!lineptr || !*lineptr)
					break;
				lineptr++;

				/* Grabbing next sample time */
				sscanf(lineptr, "%d,%d,%d", &cur_sampletime, &type, &value);
			} while (sampletime == cur_sampletime);

			if (gaschange)
				add_event(dc, cur_sampletime, SAMPLE_EVENT_GASCHANGE2, 0, gaschange,
						QT_TRANSLATE_NOOP("gettextFromC", "gaschange"));
			if (!has_depth)
				add_sample_data(sample, POSEIDON_DEPTH, prev_depth);
			if (!has_setpoint && prev_setpoint >= 0)
				add_sample_data(sample, POSEIDON_SETPOINT, prev_setpoint);
			if (!has_ndl && prev_ndl >= 0)
				add_sample_data(sample, POSEIDON_NDL, prev_ndl);
			if (cylinder_pressure)
				dive->cylinder[1].sample_end.mbar = cylinder_pressure * 1000;
			if (o2cylinder_pressure)
				dive->cylinder[0].sample_end.mbar = o2cylinder_pressure * 1000;
			finish_sample(dc);

			if (!lineptr || !*lineptr)
				break;
		}
		record_dive(dive);
		return 1;
	} else {
		return 0;
	}

	return 0;
}

#define MAXCOLDIGITS 10
#define DATESTR 9
#define TIMESTR 6

int parse_dan_format(const char *filename, char **params, int pnr)
{
	int ret = 0, i;
	size_t end_ptr = 0;
	struct memblock mem, mem_csv;
	char tmpbuf[MAXCOLDIGITS];

	char *ptr = NULL;
	char *NL = NULL;
	char *iter = NULL;

	if (readfile(filename, &mem) < 0)
		return report_error(translate("gettextFromC", "Failed to read '%s'"), filename);

	/* Determine NL (new line) character and the start of CSV data */
	if ((ptr = strstr(mem.buffer, "\r\n")) != NULL) {
		NL = "\r\n";
	} else if ((ptr = strstr(mem.buffer, "\n")) != NULL) {
		NL = "\n";
	} else {
		fprintf(stderr, "DEBUG: failed to detect NL\n");
		return -1;
	}

	while ((end_ptr < mem.size) && (ptr = strstr(mem.buffer + end_ptr, "ZDH"))) {

		/*
		 * Process the dives, but let the last round be parsed
		 * from C++ code
		 */

		if (end_ptr)
			process_dives(true, false);

		mem_csv.buffer = malloc(mem.size + 1);
		mem_csv.size = mem.size;

		//fprintf(stderr, "DEBUG: BEGIN end_ptr %d round %d <%s>\n", end_ptr, j++, ptr);
		iter = ptr + 1;
		for (i = 0; i <= 4 && iter; ++i) {
			iter = strchr(iter, '|');
			if (iter)
				++iter;
		}

		/* Setting date */
		memcpy(tmpbuf, iter, 8);
		tmpbuf[8] = 0;
		params[pnr] = "date";
		params[pnr + 1] = strdup(tmpbuf);

		/* Setting time, gotta prepend it with 1 to
		 * avoid octal parsing (this is stripped out in
		 * XSLT */
		tmpbuf[0] = '1';
		memcpy(tmpbuf + 1, iter + 8, 6);
		tmpbuf[7] = 0;
		params[pnr + 2] = "time";
		params[pnr + 3] = strdup(tmpbuf);
		params[pnr + 4] = NULL;

		ptr = strstr(ptr, "ZDP{");
		if (ptr)
			ptr = strstr(ptr, NL);
		if (ptr)
			ptr += strlen(NL);

		end_ptr = ptr - (char *)mem.buffer;

		/* Copy the current dive data to start of mem_csv buffer */
		memcpy(mem_csv.buffer, ptr, mem.size - (ptr - (char *)mem.buffer));
		ptr = strstr(mem_csv.buffer, "ZDP}");
		if (ptr) {
			*ptr = 0;
		} else {
			fprintf(stderr, "DEBUG: failed to find end ZDP\n");
			return -1;
		}
		mem_csv.size = ptr - (char*)mem_csv.buffer;

		if (try_to_xslt_open_csv(filename, &mem_csv, "csv"))
			return -1;

		ret |= parse_xml_buffer(filename, mem_csv.buffer, mem_csv.size, &dive_table, (const char **)params);
		end_ptr += ptr - (char *)mem_csv.buffer;
		free(mem_csv.buffer);
	}

	free(mem.buffer);
	for (i = 0; params[i]; i += 2)
		free(params[i + 1]);

	return ret;
}

int parse_csv_file(const char *filename, char **params, int pnr, const char *csvtemplate)
{
	int ret, i;
	struct memblock mem;
	time_t now;
	struct tm *timep = NULL;
	char tmpbuf[MAXCOLDIGITS];

	/* Increase the limits for recursion and variables on XSLT
	 * parsing */
	xsltMaxDepth = 30000;
#if LIBXSLT_VERSION > 10126
	xsltMaxVars = 150000;
#endif

	if (filename == NULL)
		return report_error("No CSV filename");

	mem.size = 0;
	if (!strcmp("DL7", csvtemplate)) {
		return parse_dan_format(filename, params, pnr);
	} else if (strcmp(params[0], "date")) {
		time(&now);
		timep = localtime(&now);

		strftime(tmpbuf, MAXCOLDIGITS, "%Y%m%d", timep);
		params[pnr++] = "date";
		params[pnr++] = strdup(tmpbuf);

		/* As the parameter is numeric, we need to ensure that the leading zero
		 * is not discarded during the transform, thus prepend time with 1 */

		strftime(tmpbuf, MAXCOLDIGITS, "1%H%M", timep);
		params[pnr++] = "time";
		params[pnr++] = strdup(tmpbuf);
		params[pnr++] = NULL;
	}

	if (try_to_xslt_open_csv(filename, &mem, csvtemplate))
		return -1;

	/*
	 * Lets print command line for manual testing with xsltproc if
	 * verbosity level is high enough. The printed line needs the
	 * input file added as last parameter.
	 */

#ifndef SUBSURFACE_MOBILE
	if (verbose >= 2) {
		fprintf(stderr, "(echo '<csv>'; cat %s;echo '</csv>') | xsltproc ", filename);
		for (i=0; params[i]; i+=2)
			fprintf(stderr, "--stringparam %s %s ", params[i], params[i+1]);
		fprintf(stderr, "%s/xslt/csv2xml.xslt -\n", SUBSURFACE_SOURCE);
	}
#endif
	ret = parse_xml_buffer(filename, mem.buffer, mem.size, &dive_table, (const char **)params);

	free(mem.buffer);
	for (i = 0; params[i]; i += 2)
		free(params[i + 1]);

	return ret;
}

#define SBPARAMS 40
int parse_seabear_csv_file(const char *filename, char **params, int pnr, const char *csvtemplate)
{
	int ret, i;
	struct memblock mem;
	time_t now;
	struct tm *timep = NULL;
	char *ptr, *ptr_old = NULL;
	char *NL = NULL;
	char tmpbuf[MAXCOLDIGITS];

	/* Increase the limits for recursion and variables on XSLT
	 * parsing */
	xsltMaxDepth = 30000;
#if LIBXSLT_VERSION > 10126
	xsltMaxVars = 150000;
#endif

	time(&now);
	timep = localtime(&now);

	strftime(tmpbuf, MAXCOLDIGITS, "%Y%m%d", timep);
	params[pnr++] = "date";
	params[pnr++] = strdup(tmpbuf);

	/* As the parameter is numeric, we need to ensure that the leading zero
	* is not discarded during the transform, thus prepend time with 1 */
	strftime(tmpbuf, MAXCOLDIGITS, "1%H%M", timep);
	params[pnr++] = "time";
	params[pnr++] = strdup(tmpbuf);


	if (filename == NULL)
		return report_error("No CSV filename");

	if (readfile(filename, &mem) < 0)
		return report_error(translate("gettextFromC", "Failed to read '%s'"), filename);

	/* Determine NL (new line) character and the start of CSV data */
	ptr = mem.buffer;
	while ((ptr = strstr(ptr, "\r\n\r\n")) != NULL) {
		ptr_old = ptr;
		ptr += 1;
		NL = "\r\n";
	}

	if (!ptr_old) {
		ptr = mem.buffer;
		while ((ptr = strstr(ptr, "\n\n")) != NULL) {
			ptr_old = ptr;
			ptr += 1;
			NL = "\n";
		}
		ptr_old += 2;
	} else
		ptr_old += 4;

	/*
	 * If file does not contain empty lines, it is not a valid
	 * Seabear CSV file.
	 */
	if (NL == NULL)
		return -1;

	/*
	 * On my current sample of Seabear DC log file, the date is
	 * without any identifier. Thus we must search for the previous
	 * line and step through from there. That is the line after
	 * Serial number.
	 */
	ptr = strstr(mem.buffer, "Serial number:");
	if (ptr)
		ptr = strstr(ptr, NL);

	/*
	 * Write date and time values to params array, if available in
	 * the CSV header
	 */

	if (ptr) {
		ptr += strlen(NL) + 2;
		/*
		 * pnr is the index of NULL on the params as filled by
		 * the init function. The two last entries should be
		 * date and time. Here we overwrite them with the data
		 * from the CSV header.
		 */

		memcpy(params[pnr - 3], ptr, 4);
		memcpy(params[pnr - 3] + 4, ptr + 5, 2);
		memcpy(params[pnr - 3] + 6, ptr + 8, 2);
		params[pnr - 3][8] = 0;

		memcpy(params[pnr - 1] + 1, ptr + 11, 2);
		memcpy(params[pnr - 1] + 3, ptr + 14, 2);
		params[pnr - 1][5] = 0;
	}

	params[pnr++] = NULL;

	/* Move the CSV data to the start of mem buffer */
	memmove(mem.buffer, ptr_old, mem.size - (ptr_old - (char*)mem.buffer));
	mem.size = (int)mem.size - (ptr_old - (char*)mem.buffer);

	if (try_to_xslt_open_csv(filename, &mem, csvtemplate))
		return -1;

	/*
	 * Lets print command line for manual testing with xsltproc if
	 * verbosity level is high enough. The printed line needs the
	 * input file added as last parameter.
	 */

	if (verbose >= 2) {
		fprintf(stderr, "xsltproc ");
		for (i=0; params[i]; i+=2)
			fprintf(stderr, "--stringparam %s %s ", params[i], params[i+1]);
		fprintf(stderr, "xslt/csv2xml.xslt\n");
	}

	ret = parse_xml_buffer(filename, mem.buffer, mem.size, &dive_table, (const char **)params);
	free(mem.buffer);
	for (i = 0; params[i]; i += 2)
		free(params[i + 1]);

	return ret;
}

int parse_manual_file(const char *filename, char **params, int pnr)
{
	struct memblock mem;
	time_t now;
	struct tm *timep;
	char curdate[9];
	char curtime[6];
	int ret, i;


	time(&now);
	timep = localtime(&now);
	strftime(curdate, DATESTR, "%Y%m%d", timep);

	/* As the parameter is numeric, we need to ensure that the leading zero
	* is not discarded during the transform, thus prepend time with 1 */
	strftime(curtime, TIMESTR, "1%H%M", timep);


	params[pnr++] = strdup("date");
	params[pnr++] = strdup(curdate);
	params[pnr++] = strdup("time");
	params[pnr++] = strdup(curtime);
	params[pnr++] = NULL;

	if (filename == NULL)
		return report_error("No manual CSV filename");

	mem.size = 0;
	if (try_to_xslt_open_csv(filename, &mem, "manualCSV"))
		return -1;

#ifndef SUBSURFACE_MOBILE
	if (verbose >= 2) {
		fprintf(stderr, "(echo '<manualCSV>'; cat %s;echo '</manualCSV>') | xsltproc ", filename);
		for (i=0; params[i]; i+=2)
			fprintf(stderr, "--stringparam %s %s ", params[i], params[i+1]);
		fprintf(stderr, "%s/xslt/manualcsv2xml.xslt -\n", SUBSURFACE_SOURCE);
	}
#endif
	ret = parse_xml_buffer(filename, mem.buffer, mem.size, &dive_table, (const char **)params);

	free(mem.buffer);
	for (i = 0; i < pnr - 2; ++i)
		free(params[i]);
	return ret;
}
