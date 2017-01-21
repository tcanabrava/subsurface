/* profile.c */
/* creates all the necessary data for drawing the dive profile
 */
#include "gettext.h"
#include <limits.h>
#include <string.h>
#include <assert.h>

#include "dive.h"
#include "display.h"
#include "divelist.h"

#include "profile.h"
#include "gaspressures.h"
#include "deco.h"
#include "libdivecomputer/parser.h"
#include "libdivecomputer/version.h"
#include "membuffer.h"
#include "qthelperfromc.h"

//#define DEBUG_GAS 1

#define MAX_PROFILE_DECO 7200


int selected_dive = -1; /* careful: 0 is a valid value */
unsigned int dc_number = 0;

static struct plot_data *last_pi_entry_new = NULL;
void populate_pressure_information(struct dive *, struct divecomputer *, struct plot_info *, int);

extern bool in_planner();
extern pressure_t first_ceiling_pressure;

#ifdef DEBUG_PI
/* debugging tool - not normally used */
static void dump_pi(struct plot_info *pi)
{
	int i;

	printf("pi:{nr:%d maxtime:%d meandepth:%d maxdepth:%d \n"
	       "    maxpressure:%d mintemp:%d maxtemp:%d\n",
	       pi->nr, pi->maxtime, pi->meandepth, pi->maxdepth,
	       pi->maxpressure, pi->mintemp, pi->maxtemp);
	for (i = 0; i < pi->nr; i++) {
		struct plot_data *entry = &pi->entry[i];
		printf("    entry[%d]:{cylinderindex:%d sec:%d pressure:{%d,%d}\n"
		       "                time:%d:%02d temperature:%d depth:%d stopdepth:%d stoptime:%d ndl:%d smoothed:%d po2:%lf phe:%lf pn2:%lf sum-pp %lf}\n",
		       i, entry->cylinderindex, entry->sec,
		       entry->pressure[0], entry->pressure[1],
		       entry->sec / 60, entry->sec % 60,
		       entry->temperature, entry->depth, entry->stopdepth, entry->stoptime, entry->ndl, entry->smoothed,
		       entry->pressures.o2, entry->pressures.he, entry->pressures.n2,
		       entry->pressures.o2 + entry->pressures.he + entry->pressures.n2);
	}
	printf("   }\n");
}
#endif

#define ROUND_UP(x, y) ((((x) + (y) - 1) / (y)) * (y))
#define DIV_UP(x, y) (((x) + (y) - 1) / (y))

/*
 * When showing dive profiles, we scale things to the
 * current dive. However, we don't scale past less than
 * 30 minutes or 90 ft, just so that small dives show
 * up as such unless zoom is enabled.
 * We also need to add 180 seconds at the end so the min/max
 * plots correctly
 */
int get_maxtime(struct plot_info *pi)
{
	int seconds = pi->maxtime;

	int DURATION_THR = (pi->dive_type == FREEDIVING ? 60 : 600);
	int CEILING = (pi->dive_type == FREEDIVING ? 30 : 60);

	if (prefs.zoomed_plot) {
		/* Rounded up to one minute, with at least 2.5 minutes to
		 * spare.
		 * For dive times shorter than 10 minutes, we use seconds/4 to
		 * calculate the space dynamically.
		 * This is seamless since 600/4 = 150.
		 */
		if (seconds < DURATION_THR)
			return ROUND_UP(seconds + seconds / 4, CEILING);
		else
			return ROUND_UP(seconds + DURATION_THR/4, CEILING);
	} else {
#ifndef SUBSURFACE_MOBILE
		/* min 30 minutes, rounded up to 5 minutes, with at least 2.5 minutes to spare */
		return MAX(30 * 60, ROUND_UP(seconds + DURATION_THR/4, CEILING * 5));
#else
		/* just add 2.5 minutes so we have a consistant right margin */
		return seconds + DURATION_THR / 4;
#endif
	}
}

/* get the maximum depth to which we want to plot
 * take into account the additional vertical space needed to plot
 * partial pressure graphs */
int get_maxdepth(struct plot_info *pi)
{
	unsigned mm = pi->maxdepth;
	int md;

	if (prefs.zoomed_plot) {
		/* Rounded up to 10m, with at least 3m to spare */
		md = ROUND_UP(mm + 3000, 10000);
	} else {
		/* Minimum 30m, rounded up to 10m, with at least 3m to spare */
		md = MAX((unsigned)30000, ROUND_UP(mm + 3000, 10000));
	}
	md += pi->maxpp * 9000;
	return md;
}

/* collect all event names and whether we display them */
struct ev_select *ev_namelist;
int evn_allocated;
int evn_used;

#if WE_DONT_USE_THIS /* we need to implement event filters in Qt */
int evn_foreach (void (*callback)(const char *, bool *, void *), void *data) {
	int i;

	for (i = 0; i < evn_used; i++) {
		/* here we display an event name on screen - so translate */
		callback(translate("gettextFromC", ev_namelist[i].ev_name), &ev_namelist[i].plot_ev, data);
	}
	return i;
}
#endif /* WE_DONT_USE_THIS */

void clear_events(void)
{
	for (int i = 0; i < evn_used; i++)
		free(ev_namelist[i].ev_name);
	evn_used = 0;
}

void remember_event(const char *eventname)
{
	int i = 0, len;

	if (!eventname || (len = strlen(eventname)) == 0)
		return;
	while (i < evn_used) {
		if (!strncmp(eventname, ev_namelist[i].ev_name, len))
			return;
		i++;
	}
	if (evn_used == evn_allocated) {
		evn_allocated += 10;
		ev_namelist = realloc(ev_namelist, evn_allocated * sizeof(struct ev_select));
		if (!ev_namelist)
			/* we are screwed, but let's just bail out */
			return;
	}
	ev_namelist[evn_used].ev_name = strdup(eventname);
	ev_namelist[evn_used].plot_ev = true;
	evn_used++;
}

/* UNUSED! */
static int get_local_sac(struct plot_data *entry1, struct plot_data *entry2, struct dive *dive) __attribute__((unused));

/* Get local sac-rate (in ml/min) between entry1 and entry2 */
static int get_local_sac(struct plot_data *entry1, struct plot_data *entry2, struct dive *dive)
{
	int index = entry1->cylinderindex;
	cylinder_t *cyl;
	int duration = entry2->sec - entry1->sec;
	int depth, airuse;
	pressure_t a, b;
	double atm;

	if (entry2->cylinderindex != index)
		return 0;
	if (duration <= 0)
		return 0;
	a.mbar = GET_PRESSURE(entry1);
	b.mbar = GET_PRESSURE(entry2);
	if (!b.mbar || a.mbar <= b.mbar)
		return 0;

	/* Mean pressure in ATM */
	depth = (entry1->depth + entry2->depth) / 2;
	atm = depth_to_atm(depth, dive);

	cyl = dive->cylinder + index;

	airuse = gas_volume(cyl, a) - gas_volume(cyl, b);

	/* milliliters per minute */
	return airuse / atm * 60 / duration;
}

#define HALF_INTERVAL 9 * 30
/*
 * Run the min/max calculations: over a 9 minute interval
 * around the entry point (indices 0, 1, 2 respectively).
 */
static void analyze_plot_info_minmax(struct plot_info *pi, int entry_index)
{
	struct plot_data *plot_entry = pi->entry + entry_index;  // fixed
	struct plot_data *p = plot_entry;  // moves with 'entry'
	int start = p->sec - HALF_INTERVAL, end = p->sec + HALF_INTERVAL;
	int min, max;

	/* Go back 'seconds' in time */
	while (entry_index > 0) {
		if (p[-1].sec < start)
			break;
		entry_index--;
		p--;
	}

	// indexes to the min/max entries
	min = max = entry_index;

	/* Then go forward until we hit an entry past the time */
	while (entry_index < pi->nr) {
		int time = p->sec;
		int depth = p->depth;

		if (time > end)
			break;

		if (depth < pi->entry[min].depth)
			min = entry_index;
		if (depth > pi->entry[max].depth)
			max = entry_index;

		p++;
		entry_index++;
	}

	plot_entry->min = min;
	plot_entry->max = max;
}

static velocity_t velocity(int speed)
{
	velocity_t v;

	if (speed < -304) /* ascent faster than -60ft/min */
		v = CRAZY;
	else if (speed < -152) /* above -30ft/min */
		v = FAST;
	else if (speed < -76) /* -15ft/min */
		v = MODERATE;
	else if (speed < -25) /* -5ft/min */
		v = SLOW;
	else if (speed < 25) /* very hard to find data, but it appears that the recommendations
				for descent are usually about 2x ascent rate; still, we want
				stable to mean stable */
		v = STABLE;
	else if (speed < 152) /* between 5 and 30ft/min is considered slow */
		v = SLOW;
	else if (speed < 304) /* up to 60ft/min is moderate */
		v = MODERATE;
	else if (speed < 507) /* up to 100ft/min is fast */
		v = FAST;
	else /* more than that is just crazy - you'll blow your ears out */
		v = CRAZY;

	return v;
}

struct plot_info *analyze_plot_info(struct plot_info *pi)
{
	int i;
	int nr = pi->nr;

	/* Smoothing function: 5-point triangular smooth */
	for (i = 2; i < nr; i++) {
		struct plot_data *entry = pi->entry + i;
		int depth;

		if (i < nr - 2) {
			depth = entry[-2].depth + 2 * entry[-1].depth + 3 * entry[0].depth + 2 * entry[1].depth + entry[2].depth;
			entry->smoothed = (depth + 4) / 9;
		}
		/* vertical velocity in mm/sec */
		/* Linus wants to smooth this - let's at least look at the samples that aren't FAST or CRAZY */
		if (entry[0].sec - entry[-1].sec) {
			entry->speed = (entry[0].depth - entry[-1].depth) / (entry[0].sec - entry[-1].sec);
			entry->velocity = velocity(entry->speed);
			/* if our samples are short and we aren't too FAST*/
			if (entry[0].sec - entry[-1].sec < 15 && entry->velocity < FAST) {
				int past = -2;
				while (i + past > 0 && entry[0].sec - entry[past].sec < 15)
					past--;
				entry->velocity = velocity((entry[0].depth - entry[past].depth) /
							   (entry[0].sec - entry[past].sec));
			}
		} else {
			entry->velocity = STABLE;
			entry->speed = 0;
		}
	}

	/* get minmax data */
	for (i = 0; i < nr; i++)
		analyze_plot_info_minmax(pi, i);

	return pi;
}

/*
 * If the event has an explicit cylinder index,
 * we return that. If it doesn't, we return the best
 * match based on the gasmix.
 *
 * Some dive computers give cylinder indexes, some
 * give just the gas mix.
 */
int get_cylinder_index(struct dive *dive, struct event *ev)
{
	int best;
	struct gasmix *mix;

	if (ev->gas.index >= 0)
		return ev->gas.index;

	/*
	 * This should no longer happen!
	 *
	 * We now match up gas change events with their cylinders at dive
	 * event fixup time.
	 */
	fprintf(stderr, "Still looking up cylinder based on gas mix in get_cylinder_index()!\n");

	mix = get_gasmix_from_event(dive, ev);
	best = find_best_gasmix_match(mix, dive->cylinder, 0);
	return best < 0 ? 0 : best;
}

struct event *get_next_event(struct event *event, const char *name)
{
	if (!name || !*name)
		return NULL;
	while (event) {
		if (!strcmp(event->name, name))
			return event;
		event = event->next;
	}
	return event;
}

static int count_events(struct divecomputer *dc)
{
	int result = 0;
	struct event *ev = dc->events;
	while (ev != NULL) {
		result++;
		ev = ev->next;
	}
	return result;
}

static int set_cylinder_index(struct plot_info *pi, int i, int cylinderindex, int end)
{
	while (i < pi->nr) {
		struct plot_data *entry = pi->entry + i;
		if (entry->sec > end)
			break;
		if (entry->cylinderindex != cylinderindex) {
			entry->cylinderindex = cylinderindex;
			entry->pressure[0] = 0;
		}
		i++;
	}
	return i;
}

static int set_setpoint(struct plot_info *pi, int i, int setpoint, int end)
{
	while (i < pi->nr) {
		struct plot_data *entry = pi->entry + i;
		if (entry->sec > end)
			break;
		entry->o2pressure.mbar = setpoint;
		i++;
	}
	return i;
}

/* normally the first cylinder has index 0... if not, we need to fix this up here */
static int set_first_cylinder_index(struct plot_info *pi, int i, int cylinderindex, int end)
{
	while (i < pi->nr) {
		struct plot_data *entry = pi->entry + i;
		if (entry->sec > end)
			break;
		entry->cylinderindex = cylinderindex;
		i++;
	}
	return i;
}

static void check_gas_change_events(struct dive *dive, struct divecomputer *dc, struct plot_info *pi)
{
	int i = 0, cylinderindex = 0;
	struct event *ev = get_next_event(dc->events, "gaschange");

	// for dive computers that tell us their first gas as an event on the first sample
	// we need to make sure things are setup correctly
	cylinderindex = explicit_first_cylinder(dive, dc);
	set_first_cylinder_index(pi, 0, cylinderindex, INT_MAX);

	if (!ev)
		return;

	do {
		i = set_cylinder_index(pi, i, cylinderindex, ev->time.seconds);
		cylinderindex = get_cylinder_index(dive, ev);
		ev = get_next_event(ev->next, "gaschange");
	} while (ev);
	set_cylinder_index(pi, i, cylinderindex, INT_MAX);
}

static void check_setpoint_events(struct dive *dive, struct divecomputer *dc, struct plot_info *pi)
{
	int i = 0;
	pressure_t setpoint;
	(void) dive;
	setpoint.mbar = 0;
	struct event *ev = get_next_event(dc->events, "SP change");

	if (!ev)
		return;

	do {
		i = set_setpoint(pi, i, setpoint.mbar, ev->time.seconds);
		setpoint.mbar = ev->value;
		if (setpoint.mbar)
			dc->divemode = CCR;
		ev = get_next_event(ev->next, "SP change");
	} while (ev);
	set_setpoint(pi, i, setpoint.mbar, INT_MAX);
}


struct plot_info calculate_max_limits_new(struct dive *dive, struct divecomputer *given_dc)
{
	struct divecomputer *dc = &(dive->dc);
	bool seen = false;
	static struct plot_info pi;
	int maxdepth = dive->maxdepth.mm;
	unsigned int maxtime = 0;
	int maxpressure = 0, minpressure = INT_MAX;
	int maxhr = 0, minhr = INT_MAX;
	int mintemp = dive->mintemp.mkelvin;
	int maxtemp = dive->maxtemp.mkelvin;
	int cyl;

	/* Get the per-cylinder maximum pressure if they are manual */
	for (cyl = 0; cyl < MAX_CYLINDERS; cyl++) {
		int mbar = dive->cylinder[cyl].start.mbar;
		if (mbar > maxpressure)
			maxpressure = mbar;
		if (mbar < minpressure)
			minpressure = mbar;
	}

	/* Then do all the samples from all the dive computers */
	do {
		if (dc == given_dc)
			seen = true;
		int i = dc->samples;
		int lastdepth = 0;
		struct sample *s = dc->sample;
		struct event *ev;

		while (--i >= 0) {
			int depth = s->depth.mm;
			int pressure = s->cylinderpressure.mbar;
			int temperature = s->temperature.mkelvin;
			int heartbeat = s->heartbeat;

			if (!mintemp && temperature < mintemp)
				mintemp = temperature;
			if (temperature > maxtemp)
				maxtemp = temperature;

			if (pressure && pressure < minpressure)
				minpressure = pressure;
			if (pressure > maxpressure)
				maxpressure = pressure;
			if (heartbeat > maxhr)
				maxhr = heartbeat;
			if (heartbeat < minhr)
				minhr = heartbeat;

			if (depth > maxdepth)
				maxdepth = s->depth.mm;
			if ((depth > SURFACE_THRESHOLD || lastdepth > SURFACE_THRESHOLD) &&
			    s->time.seconds > maxtime)
				maxtime = s->time.seconds;
			lastdepth = depth;
			s++;
		}

		/* Make sure we can fit all events */
		ev = dc->events;
		while (ev) {
			if (ev->time.seconds > maxtime)
				maxtime = ev->time.seconds;
			ev = ev->next;
		}

		dc = dc->next;
		if (dc == NULL && !seen) {
			dc = given_dc;
			seen = true;
		}
	} while (dc != NULL);

	if (minpressure > maxpressure)
		minpressure = 0;
	if (minhr > maxhr)
		minhr = 0;

	memset(&pi, 0, sizeof(pi));
	pi.maxdepth = maxdepth;
	pi.maxtime = maxtime;
	pi.maxpressure = maxpressure;
	pi.minpressure = minpressure;
	pi.minhr = minhr;
	pi.maxhr = maxhr;
	pi.mintemp = mintemp;
	pi.maxtemp = maxtemp;
	return pi;
}

/* copy the previous entry (we know this exists), update time and depth
 * and zero out the sensor pressure (since this is a synthetic entry)
 * increment the entry pointer and the count of synthetic entries. */
#define INSERT_ENTRY(_time, _depth, _sac) \
	*entry = entry[-1];         \
	entry->sec = _time;         \
	entry->depth = _depth;      \
	entry->running_sum = (entry - 1)->running_sum + (_time - (entry - 1)->sec) * (_depth + (entry - 1)->depth) / 2; \
	SENSOR_PRESSURE(entry) = 0; \
	entry->sac = _sac;          \
	entry++;                    \
	idx++

struct plot_data *populate_plot_entries(struct dive *dive, struct divecomputer *dc, struct plot_info *pi)
{

	int idx, maxtime, nr, i;
	int lastdepth, lasttime, lasttemp = 0;
	struct plot_data *plot_data;
	struct event *ev = dc->events;
	(void) dive;
	maxtime = pi->maxtime;

	/*
	 * We want to have a plot_info event at least every 10s (so "maxtime/10+1"),
	 * but samples could be more dense than that (so add in dc->samples). We also
	 * need to have one for every event (so count events and add that) and
	 * additionally we want two surface events around the whole thing (thus the
	 * additional 4).  There is also one extra space for a final entry
	 * that has time > maxtime (because there can be surface samples
	 * past "maxtime" in the original sample data)
	 */
	nr = dc->samples + 6 + maxtime / 10 + count_events(dc);
	plot_data = calloc(nr, sizeof(struct plot_data));
	pi->entry = plot_data;
	if (!plot_data)
		return NULL;
	pi->nr = nr;
	idx = 2; /* the two extra events at the start */

	lastdepth = 0;
	lasttime = 0;
	/* skip events at time = 0 */
	while (ev && ev->time.seconds == 0)
		ev = ev->next;
	for (i = 0; i < dc->samples; i++) {
		struct plot_data *entry = plot_data + idx;
		struct sample *sample = dc->sample + i;
		int time = sample->time.seconds;
		int offset, delta;
		int depth = sample->depth.mm;
		int sac = sample->sac.mliter;

		/* Add intermediate plot entries if required */
		delta = time - lasttime;
		if (delta <= 0) {
			time = lasttime;
			delta = 1; // avoid divide by 0
		}
		for (offset = 10; offset < delta; offset += 10) {
			if (lasttime + offset > maxtime)
				break;

			/* Add events if they are between plot entries */
			while (ev && (int)ev->time.seconds < lasttime + offset) {
				INSERT_ENTRY(ev->time.seconds, interpolate(lastdepth, depth, ev->time.seconds - lasttime, delta), sac);
				ev = ev->next;
			}

			/* now insert the time interpolated entry */
			INSERT_ENTRY(lasttime + offset, interpolate(lastdepth, depth, offset, delta), sac);

			/* skip events that happened at this time */
			while (ev && (int)ev->time.seconds == lasttime + offset)
				ev = ev->next;
		}

		/* Add events if they are between plot entries */
		while (ev && (int)ev->time.seconds < time) {
			INSERT_ENTRY(ev->time.seconds, interpolate(lastdepth, depth, ev->time.seconds - lasttime, delta), sac);
			ev = ev->next;
		}

		entry->sec = time;
		entry->depth = depth;

		entry->running_sum = (entry - 1)->running_sum + (time - (entry - 1)->sec) * (depth + (entry - 1)->depth) / 2;
		entry->stopdepth = sample->stopdepth.mm;
		entry->stoptime = sample->stoptime.seconds;
		entry->ndl = sample->ndl.seconds;
		entry->tts = sample->tts.seconds;
		pi->has_ndl |= sample->ndl.seconds;
		entry->in_deco = sample->in_deco;
		entry->cns = sample->cns;
		if (dc->divemode == CCR) {
			entry->o2pressure.mbar = entry->o2setpoint.mbar = sample->setpoint.mbar;     // for rebreathers
			entry->o2sensor[0].mbar = sample->o2sensor[0].mbar; // for up to three rebreather O2 sensors
			entry->o2sensor[1].mbar = sample->o2sensor[1].mbar;
			entry->o2sensor[2].mbar = sample->o2sensor[2].mbar;
		} else {
			entry->pressures.o2 = sample->setpoint.mbar / 1000.0;
		}
		/* FIXME! sensor index -> cylinder index translation! */
		//		entry->cylinderindex = sample->sensor;
		SENSOR_PRESSURE(entry) = sample->cylinderpressure.mbar;
		O2CYLINDER_PRESSURE(entry) = sample->o2cylinderpressure.mbar;
		if (sample->temperature.mkelvin)
			entry->temperature = lasttemp = sample->temperature.mkelvin;
		else
			entry->temperature = lasttemp;
		entry->heartbeat = sample->heartbeat;
		entry->bearing = sample->bearing.degrees;
		entry->sac = sample->sac.mliter;
		if (sample->rbt.seconds)
			entry->rbt = sample->rbt.seconds;
		/* skip events that happened at this time */
		while (ev && (int)ev->time.seconds == time)
			ev = ev->next;
		lasttime = time;
		lastdepth = depth;
		idx++;

		if (time > maxtime)
			break;
	}

	/* Add any remaining events */
	while (ev) {
		struct plot_data *entry = plot_data + idx;
		int time = ev->time.seconds;

		if (time > lasttime) {
			INSERT_ENTRY(ev->time.seconds, 0, 0);
			lasttime = time;
		}
		ev = ev->next;
	}

	/* Add two final surface events */
	plot_data[idx++].sec = lasttime + 1;
	plot_data[idx++].sec = lasttime + 2;
	pi->nr = idx;

	return plot_data;
}

#undef INSERT_ENTRY

static void populate_cylinder_pressure_data(int idx, int start, int end, struct plot_info *pi, bool o2flag)
{
	int i;

	/* First: check that none of the entries has sensor pressure for this cylinder index */
	for (i = 0; i < pi->nr; i++) {
		struct plot_data *entry = pi->entry + i;
		if (entry->cylinderindex != idx && !o2flag)
			continue;
		if (CYLINDER_PRESSURE(o2flag, entry))
			return;
	}

	/* Then: populate the first entry with the beginning cylinder pressure */
	for (i = 0; i < pi->nr; i++) {
		struct plot_data *entry = pi->entry + i;
		if (entry->cylinderindex != idx && !o2flag)
			continue;
		if (o2flag)
			O2CYLINDER_PRESSURE(entry) = start;
		else
			SENSOR_PRESSURE(entry) = start;
		break;
	}

	/* .. and the last entry with the ending cylinder pressure */
	for (i = pi->nr; --i >= 0; /* nothing */) {
		struct plot_data *entry = pi->entry + i;
		if (entry->cylinderindex != idx && !o2flag)
			continue;
		if (o2flag)
			O2CYLINDER_PRESSURE(entry) = end;
		else
			SENSOR_PRESSURE(entry) = end;
		break;
	}
}

/*
 * Calculate the sac rate between the two plot entries 'first' and 'last'.
 *
 * Everything in between has a cylinder pressure, and it's all the same
 * cylinder.
 */
static int sac_between(struct dive *dive, struct plot_data *first, struct plot_data *last)
{
	int airuse;
	double pressuretime;
	pressure_t a, b;
	cylinder_t *cyl;

	if (first == last)
		return 0;

	/* Calculate air use - trivial */
	a.mbar = GET_PRESSURE(first);
	b.mbar = GET_PRESSURE(last);
	cyl = dive->cylinder + first->cylinderindex;
	airuse = gas_volume(cyl, a) - gas_volume(cyl, b);
	if (airuse <= 0)
		return 0;

	/* Calculate depthpressure integrated over time */
	pressuretime = 0.0;
	do {
		int depth = (first[0].depth + first[1].depth) / 2;
		int time = first[1].sec - first[0].sec;
		double atm = depth_to_atm(depth, dive);

		pressuretime += atm * time;
	} while (++first < last);

	/* Turn "atmseconds" into "atmminutes" */
	pressuretime /= 60;

	/* SAC = mliter per minute */
	return rint(airuse / pressuretime);
}

/*
 * Try to do the momentary sac rate for this entry, averaging over one
 * minute.
 */
static void fill_sac(struct dive *dive, struct plot_info *pi, int idx)
{
	struct plot_data *entry = pi->entry + idx;
	struct plot_data *first, *last;
	int time;

	if (entry->sac)
		return;

	if (!GET_PRESSURE(entry))
		return;

	/*
	 * Try to go back 30 seconds to get 'first'.
	 * Stop if the sensor changed, or if we went back too far.
	 */
	first = entry;
	time = entry->sec - 30;
	while (idx > 0) {
		struct plot_data *prev = first-1;
		if (prev->cylinderindex != first->cylinderindex)
			break;
		if (prev->depth < SURFACE_THRESHOLD && first->depth < SURFACE_THRESHOLD)
			break;
		if (prev->sec < time)
			break;
		if (!GET_PRESSURE(prev))
			break;
		idx--;
		first = prev;
	}

	/* Now find an entry a minute after the first one */
	last = first;
	time = first->sec + 60;
	while (++idx < pi->nr) {
		struct plot_data *next = last+1;
		if (next->cylinderindex != last->cylinderindex)
			break;
		if (next->depth < SURFACE_THRESHOLD && last->depth < SURFACE_THRESHOLD)
			break;
		if (next->sec > time)
			break;
		if (!GET_PRESSURE(next))
			break;
		last = next;
	}

	/* Ok, now calculate the SAC between 'first' and 'last' */
	entry->sac = sac_between(dive, first, last);
}

static void calculate_sac(struct dive *dive, struct plot_info *pi)
{
	for (int i = 0; i < pi->nr; i++)
		fill_sac(dive, pi, i);
}

static void populate_secondary_sensor_data(struct divecomputer *dc, struct plot_info *pi)
{
	(void) dc;
	(void) pi;
	/* We should try to see if it has interesting pressure data here */
}

static void setup_gas_sensor_pressure(struct dive *dive, struct divecomputer *dc, struct plot_info *pi)
{
	int i;
	struct divecomputer *secondary;

	/* First, populate the pressures with the manual cylinder data.. */
	for (i = 0; i < MAX_CYLINDERS; i++) {
		cylinder_t *cyl = dive->cylinder + i;
		int start = cyl->start.mbar ?: cyl->sample_start.mbar;
		int end = cyl->end.mbar ?: cyl->sample_end.mbar;

		if (!start || !end)
			continue;

		populate_cylinder_pressure_data(i, start, end, pi, dive->cylinder[i].cylinder_use == OXYGEN);
	}

	/*
	 * Here, we should try to walk through all the dive computers,
	 * and try to see if they have sensor data different from the
	 * primary dive computer (dc).
	 */
	secondary = &dive->dc;
	do {
		if (secondary == dc)
			continue;
		populate_secondary_sensor_data(dc, pi);
	} while ((secondary = secondary->next) != NULL);
}

#ifndef SUBSURFACE_MOBILE
/* calculate DECO STOP / TTS / NDL */
static void calculate_ndl_tts(struct plot_data *entry, struct dive *dive, double surface_pressure)
{
	/* FIXME: This should be configurable */
	/* ascent speed up to first deco stop */
	const int ascent_s_per_step = 1;
	const int ascent_mm_per_step = 200; /* 12 m/min */
	/* ascent speed between deco stops */
	const int ascent_s_per_deco_step = 1;
	const int ascent_mm_per_deco_step = 16; /* 1 m/min */
	/* how long time steps in deco calculations? */
	const int time_stepsize = 60;
	const int deco_stepsize = 3000;
	/* at what depth is the current deco-step? */
	int next_stop = ROUND_UP(deco_allowed_depth(tissue_tolerance_calc(dive, depth_to_bar(entry->depth, dive)),
						    surface_pressure, dive, 1), deco_stepsize);
	int ascent_depth = entry->depth;
	/* at what time should we give up and say that we got enuff NDL? */
	int cylinderindex = entry->cylinderindex;
	/* If iterating through a dive, entry->tts_calc needs to be reset */
	entry->tts_calc = 0;

	/* If we don't have a ceiling yet, calculate ndl. Don't try to calculate
	 * a ndl for lower values than 3m it would take forever */
	if (next_stop == 0) {
		if (entry->depth < 3000) {
			entry->ndl = MAX_PROFILE_DECO;
			return;
		}
		/* stop if the ndl is above max_ndl seconds, and call it plenty of time */
		while (entry->ndl_calc < MAX_PROFILE_DECO && deco_allowed_depth(tissue_tolerance_calc(dive, depth_to_bar(entry->depth, dive)), surface_pressure, dive, 1) <= 0) {
			entry->ndl_calc += time_stepsize;
			add_segment(depth_to_bar(entry->depth, dive),
						       &dive->cylinder[cylinderindex].gasmix, time_stepsize, entry->o2pressure.mbar, dive, prefs.bottomsac);
		}
		/* we don't need to calculate anything else */
		return;
	}

	/* We are in deco */
	entry->in_deco_calc = true;

	/* Add segments for movement to stopdepth */
	for (; ascent_depth > next_stop; ascent_depth -= ascent_mm_per_step, entry->tts_calc += ascent_s_per_step) {
		add_segment(depth_to_bar(ascent_depth, dive),
			    &dive->cylinder[cylinderindex].gasmix, ascent_s_per_step, entry->o2pressure.mbar, dive, prefs.decosac);
		next_stop = ROUND_UP(deco_allowed_depth(tissue_tolerance_calc(dive, depth_to_bar(ascent_depth, dive)), surface_pressure, dive, 1), deco_stepsize);
	}
	ascent_depth = next_stop;

	/* And how long is the current deco-step? */
	entry->stoptime_calc = 0;
	entry->stopdepth_calc = next_stop;
	next_stop -= deco_stepsize;

	/* And how long is the total TTS */
	while (next_stop >= 0) {
		/* save the time for the first stop to show in the graph */
		if (ascent_depth == entry->stopdepth_calc)
			entry->stoptime_calc += time_stepsize;

		entry->tts_calc += time_stepsize;
		if (entry->tts_calc > MAX_PROFILE_DECO)
			break;
		add_segment(depth_to_bar(ascent_depth, dive),
			    &dive->cylinder[cylinderindex].gasmix, time_stepsize, entry->o2pressure.mbar, dive, prefs.decosac);

		if (deco_allowed_depth(tissue_tolerance_calc(dive, depth_to_bar(ascent_depth,dive)), surface_pressure, dive, 1) <= next_stop) {
			/* move to the next stop and add the travel between stops */
			for (; ascent_depth > next_stop; ascent_depth -= ascent_mm_per_deco_step, entry->tts_calc += ascent_s_per_deco_step)
				add_segment(depth_to_bar(ascent_depth, dive),
					    &dive->cylinder[cylinderindex].gasmix, ascent_s_per_deco_step, entry->o2pressure.mbar, dive, prefs.decosac);
			ascent_depth = next_stop;
			next_stop -= deco_stepsize;
		}
	}
}

/* Let's try to do some deco calculations.
 */
void calculate_deco_information(struct dive *dive, struct divecomputer *dc, struct plot_info *pi, bool print_mode)
{
	int i, count_iteration = 0;
	double surface_pressure = (dc->surface_pressure.mbar ? dc->surface_pressure.mbar : get_surface_pressure_in_mbar(dive, true)) / 1000.0;
	bool first_iteration = true;
	int deco_time = 0, prev_deco_time = 10000000;
	char *cache_data_initial = NULL;
	/* For VPM-B outside the planner, cache the initial deco state for CVA iterations */
	if (decoMode() == VPMB && !in_planner())
		cache_deco_state(&cache_data_initial);
	/* For VPM-B outside the planner, iterate until deco time converges (usually one or two iterations after the initial)
	 * Set maximum number of iterations to 10 just in case */
	while ((abs(prev_deco_time - deco_time) >= 30) && (count_iteration < 10)) {
		int last_ndl_tts_calc_time = 0, first_ceiling = 0, current_ceiling, final_tts = 0 , time_clear_ceiling = 0, time_deep_ceiling = 0;
		for (i = 1; i < pi->nr; i++) {
			struct plot_data *entry = pi->entry + i;
			int j, t0 = (entry - 1)->sec, t1 = entry->sec;
			int time_stepsize = 20;

			entry->ambpressure = depth_to_bar(entry->depth, dive);
			entry->gfline = get_gf(entry->ambpressure, dive) * (100.0 - AMB_PERCENTAGE) + AMB_PERCENTAGE;
			if (t0 > t1) {
				fprintf(stderr, "non-monotonous dive stamps %d %d\n", t0, t1);
				int xchg = t1;
				t1 = t0;
				t0 = xchg;
			}
			if (t0 != t1 && t1 - t0 < time_stepsize)
				time_stepsize = t1 - t0;
			for (j = t0 + time_stepsize; j <= t1; j += time_stepsize) {
				int depth = interpolate(entry[-1].depth, entry[0].depth, j - t0, t1 - t0);
				add_segment(depth_to_bar(depth, dive),
					&dive->cylinder[entry->cylinderindex].gasmix, time_stepsize, entry->o2pressure.mbar, dive, entry->sac);
				if ((t1 - j < time_stepsize) && (j < t1))
					time_stepsize = t1 - j;
			}
			if (t0 == t1) {
				entry->ceiling = (entry - 1)->ceiling;
			} else {
				/* Keep updating the VPM-B gradients until the start of the ascent phase of the dive. */
				if (decoMode() == VPMB && !in_planner() && (entry - 1)->ceiling >= first_ceiling && first_iteration == true) {
					nuclear_regeneration(t1);
					vpmb_start_gradient();
					/* For CVA calculations, start by guessing deco time = dive time remaining */
					deco_time = pi->maxtime - t1;
					vpmb_next_gradient(deco_time, surface_pressure / 1000.0);
				}
				entry->ceiling = deco_allowed_depth(tissue_tolerance_calc(dive, depth_to_bar(entry->depth, dive)), surface_pressure, dive, !prefs.calcceiling3m);
				if (prefs.calcceiling3m)
					current_ceiling = deco_allowed_depth(tissue_tolerance_calc(dive, depth_to_bar(entry->depth, dive)), surface_pressure, dive, true);
				else
					current_ceiling = entry->ceiling;
				/* If using VPM-B outside the planner, take first_ceiling_pressure as the deepest ceiling */
				if (decoMode() == VPMB && !in_planner()) {
					if  (current_ceiling > first_ceiling) {
						time_deep_ceiling = t1;
						first_ceiling = current_ceiling;
						first_ceiling_pressure.mbar = depth_to_mbar(first_ceiling, dive);
						if (first_iteration) {
							nuclear_regeneration(t1);
							vpmb_start_gradient();
							/* For CVA calculations, start by guessing deco time = dive time remaining */
							deco_time = pi->maxtime - t1;
							vpmb_next_gradient(deco_time, surface_pressure / 1000.0);
						}
					}
					// Use the point where the ceiling clears as the end of deco phase for CVA calculations
					if (current_ceiling > 0)
						time_clear_ceiling = 0;
					else if (time_clear_ceiling == 0)
						time_clear_ceiling = t1;
				}
			}
			for (j = 0; j < 16; j++) {
				double m_value = buehlmann_inertgas_a[j] + entry->ambpressure / buehlmann_inertgas_b[j];
				entry->ceilings[j] = deco_allowed_depth(tolerated_by_tissue[j], surface_pressure, dive, 1);
				entry->percentages[j] = tissue_inertgas_saturation[j] < entry->ambpressure ?
								tissue_inertgas_saturation[j] / entry->ambpressure * AMB_PERCENTAGE :
								AMB_PERCENTAGE + (tissue_inertgas_saturation[j] - entry->ambpressure) / (m_value - entry->ambpressure) * (100.0 - AMB_PERCENTAGE);
			}

			/* should we do more calculations?
			* We don't for print-mode because this info doesn't show up there
			* If the ceiling hasn't cleared by the last data point, we need tts for VPM-B CVA calculation
			* It is not necessary to do these calculation on the first VPMB iteration, except for the last data point */
			if ((prefs.calcndltts && !print_mode && (decoMode() != VPMB || in_planner() || !first_iteration)) ||
			    (decoMode() == VPMB && !in_planner() && i == pi->nr - 1)) {
				/* only calculate ndl/tts on every 30 seconds */
				if ((entry->sec - last_ndl_tts_calc_time) < 30 && i != pi->nr - 1) {
					struct plot_data *prev_entry = (entry - 1);
					entry->stoptime_calc = prev_entry->stoptime_calc;
					entry->stopdepth_calc = prev_entry->stopdepth_calc;
					entry->tts_calc = prev_entry->tts_calc;
					entry->ndl_calc = prev_entry->ndl_calc;
					continue;
				}
				last_ndl_tts_calc_time = entry->sec;

				/* We are going to mess up deco state, so store it for later restore */
				char *cache_data = NULL;
				cache_deco_state(&cache_data);
				calculate_ndl_tts(entry, dive, surface_pressure);
				if (decoMode() == VPMB && !in_planner() && i == pi->nr - 1)
					final_tts = entry->tts_calc;
				/* Restore "real" deco state for next real time step */
				restore_deco_state(cache_data);
				free(cache_data);
			}
		}
		if (decoMode() == VPMB && !in_planner()) {
			prev_deco_time = deco_time;
			// Do we need to update deco_time?
			if (final_tts > 0)
				deco_time = pi->maxtime + final_tts - time_deep_ceiling;
			else if (time_clear_ceiling > 0)
				deco_time = time_clear_ceiling - time_deep_ceiling;
			vpmb_next_gradient(deco_time, surface_pressure / 1000.0);
			final_tts = 0;
			last_ndl_tts_calc_time = 0;
			first_ceiling = 0;
			first_iteration = false;
			count_iteration ++;
			restore_deco_state(cache_data_initial);
		} else {
			// With Buhlmann, or not in planner, iterating isn't needed.  This makes the while condition false.
			prev_deco_time = deco_time = 0;
		}
	}
	free(cache_data_initial);
#if DECO_CALC_DEBUG & 1
	dump_tissues();
#endif
}
#endif

/* Function calculate_ccr_po2: This function takes information from one plot_data structure (i.e. one point on
 * the dive profile), containing the oxygen sensor values of a CCR system and, for that plot_data structure,
 * calculates the po2 value from the sensor data. Several rules are applied, depending on how many o2 sensors
 * there are and the differences among the readings from these sensors.
 */
static int calculate_ccr_po2(struct plot_data *entry, struct divecomputer *dc)
{
	int sump = 0, minp = 999999, maxp = -999999;
	int diff_limit = 100; // The limit beyond which O2 sensor differences are considered significant (default = 100 mbar)
	int i, np = 0;

	for (i = 0; i < dc->no_o2sensors; i++)
		if (entry->o2sensor[i].mbar) { // Valid reading
			++np;
			sump += entry->o2sensor[i].mbar;
			minp = MIN(minp, entry->o2sensor[i].mbar);
			maxp = MAX(maxp, entry->o2sensor[i].mbar);
		}
	switch (np) {
	case 0: // Uhoh
		return entry->o2pressure.mbar;
	case 1: // Return what we have
		return sump;
	case 2: // Take the average
		return sump / 2;
	case 3:						   // Voting logic
		if (2 * maxp - sump + minp < diff_limit) { // Upper difference acceptable...
			if (2 * minp - sump + maxp)	// ...and lower difference acceptable
				return sump / 3;
			else
				return (sump - minp) / 2;
		} else {
			if (2 * minp - sump + maxp) // ...but lower difference acceptable
				return (sump - maxp) / 2;
			else
				return sump / 3;
		}
	default: // This should not happen
		assert(np <= 3);
		return 0;
	}
}

static void calculate_gas_information_new(struct dive *dive, struct plot_info *pi)
{
	int i;
	double amb_pressure;

	for (i = 1; i < pi->nr; i++) {
		int fn2, fhe;
		struct plot_data *entry = pi->entry + i;
		int cylinderindex = entry->cylinderindex;

		amb_pressure = depth_to_bar(entry->depth, dive);

		fill_pressures(&entry->pressures, amb_pressure, &dive->cylinder[cylinderindex].gasmix, entry->o2pressure.mbar / 1000.0, dive->dc.divemode);
		fn2 = (int)(1000.0 * entry->pressures.n2 / amb_pressure);
		fhe = (int)(1000.0 * entry->pressures.he / amb_pressure);

		/* Calculate MOD, EAD, END and EADD based on partial pressures calculated before
		 * so there is no difference in calculating between OC and CC
		 * END takes O₂ + N₂ (air) into account ("Narcotic" for trimix dives)
		 * EAD just uses N₂ ("Air" for nitrox dives) */
		pressure_t modpO2 = { .mbar = (int)(prefs.modpO2 * 1000) };
		entry->mod = (double)gas_mod(&dive->cylinder[cylinderindex].gasmix, modpO2, dive, 1).mm;
		entry->end = (entry->depth + 10000) * (1000 - fhe) / 1000.0 - 10000;
		entry->ead = (entry->depth + 10000) * fn2 / (double)N2_IN_AIR - 10000;
		entry->eadd = (entry->depth + 10000) *
				      (entry->pressures.o2 / amb_pressure * O2_DENSITY +
				       entry->pressures.n2 / amb_pressure * N2_DENSITY +
				       entry->pressures.he / amb_pressure * HE_DENSITY) /
				      (O2_IN_AIR * O2_DENSITY + N2_IN_AIR * N2_DENSITY) * 1000 - 10000;
		if (entry->mod < 0)
			entry->mod = 0;
		if (entry->ead < 0)
			entry->ead = 0;
		if (entry->end < 0)
			entry->end = 0;
		if (entry->eadd < 0)
			entry->eadd = 0;
	}
}

void fill_o2_values(struct divecomputer *dc, struct plot_info *pi, struct dive *dive)
/* In the samples from each dive computer, there may be uninitialised oxygen
 * sensor or setpoint values, e.g. when events were inserted into the dive log
 * or if the dive computer does not report o2 values with every sample. But
 * for drawing the profile a complete series of valid o2 pressure values is
 * required. This function takes the oxygen sensor data and setpoint values
 * from the structures of plotinfo and replaces the zero values with their
 * last known values so that the oxygen sensor data are complete and ready
 * for plotting. This function called by: create_plot_info_new() */
{
	int i, j;
	pressure_t last_sensor[3], o2pressure;
	pressure_t amb_pressure;

	for (i = 0; i < pi->nr; i++) {
		struct plot_data *entry = pi->entry + i;

		if (dc->divemode == CCR) {
			if (i == 0) { // For 1st iteration, initialise the last_sensor values
				for (j = 0; j < dc->no_o2sensors; j++)
					last_sensor[j].mbar = pi->entry->o2sensor[j].mbar;
			} else { // Now re-insert the missing oxygen pressure values
				for (j = 0; j < dc->no_o2sensors; j++)
					if (entry->o2sensor[j].mbar)
						last_sensor[j].mbar = entry->o2sensor[j].mbar;
					else
						entry->o2sensor[j].mbar = last_sensor[j].mbar;
			} // having initialised the empty o2 sensor values for this point on the profile,
			amb_pressure.mbar = depth_to_mbar(entry->depth, dive);
			o2pressure.mbar = calculate_ccr_po2(entry, dc); // ...calculate the po2 based on the sensor data
			entry->o2pressure.mbar = MIN(o2pressure.mbar, amb_pressure.mbar);
		} else {
			entry->o2pressure.mbar = 0; // initialise po2 to zero for dctype = OC
		}
	}
}

#ifdef DEBUG_GAS
/* A CCR debug function that writes the cylinder pressure and the oxygen values to the file debug_print_profiledata.dat:
 * Called in create_plot_info_new()
 */
static void debug_print_profiledata(struct plot_info *pi)
{
	FILE *f1;
	struct plot_data *entry;
	int i;
	if (!(f1 = fopen("debug_print_profiledata.dat", "w"))) {
		printf("File open error for: debug_print_profiledata.dat\n");
	} else {
		fprintf(f1, "id t1 gas gasint t2 t3 dil dilint t4 t5 setpoint sensor1 sensor2 sensor3 t6 po2 fo2\n");
		for (i = 0; i < pi->nr; i++) {
			entry = pi->entry + i;
			fprintf(f1, "%d gas=%8d %8d ; dil=%8d %8d ; o2_sp= %d %d %d %d PO2= %f\n", i, SENSOR_PRESSURE(entry),
				INTERPOLATED_PRESSURE(entry), O2CYLINDER_PRESSURE(entry), INTERPOLATED_O2CYLINDER_PRESSURE(entry),
				entry->o2pressure.mbar, entry->o2sensor[0].mbar, entry->o2sensor[1].mbar, entry->o2sensor[2].mbar, entry->pressures.o2);
		}
		fclose(f1);
	}
}
#endif

/*
 * Create a plot-info with smoothing and ranged min/max
 *
 * This also makes sure that we have extra empty events on both
 * sides, so that you can do end-points without having to worry
 * about it.
 */
void create_plot_info_new(struct dive *dive, struct divecomputer *dc, struct plot_info *pi, bool fast)
{
	int o2, he, o2max;
#ifndef SUBSURFACE_MOBILE
	init_decompression(dive);
#endif
	/* Create the new plot data */
	free((void *)last_pi_entry_new);

	get_dive_gas(dive, &o2, &he, &o2max);
	if (dc->divemode == FREEDIVE){
		pi->dive_type = FREEDIVE;
	} else 	if (he > 0) {
		pi->dive_type = TRIMIX;
	} else {
		if (o2)
			pi->dive_type = NITROX;
		else
			pi->dive_type = AIR;
	}

	last_pi_entry_new = populate_plot_entries(dive, dc, pi);

	check_gas_change_events(dive, dc, pi);   /* Populate the gas index from the gas change events */
	check_setpoint_events(dive, dc, pi);     /* Populate setpoints */
	setup_gas_sensor_pressure(dive, dc, pi); /* Try to populate our gas pressure knowledge */
	if (!fast) {
		populate_pressure_information(dive, dc, pi, false);	/* .. calculate missing pressure entries for all gasses except o2 */
		if (dc->divemode == CCR)					/* For CCR dives.. */
			populate_pressure_information(dive, dc, pi, true); /* .. calculate missing o2 gas pressure entries */
	}
	fill_o2_values(dc, pi, dive);			 /* .. and insert the O2 sensor data having 0 values. */
	calculate_sac(dive, pi);			 /* Calculate sac */
#ifndef SUBSURFACE_MOBILE
	calculate_deco_information(dive, dc, pi, false); /* and ceiling information, using gradient factor values in Preferences) */
#endif
	calculate_gas_information_new(dive, pi);	 /* Calculate gas partial pressures */

#ifdef DEBUG_GAS
	debug_print_profiledata(pi);
#endif

	pi->meandepth = dive->dc.meandepth.mm;
	analyze_plot_info(pi);
}

struct divecomputer *select_dc(struct dive *dive)
{
	unsigned int max = number_of_computers(dive);
	unsigned int i = dc_number;

	/* Reset 'dc_number' if we've switched dives and it is now out of range */
	if (i >= max)
		dc_number = i = 0;

	return get_dive_dc(dive, i);
}

static void plot_string(struct plot_info *pi, struct plot_data *entry, struct membuffer *b, bool has_ndl)
{
	int pressurevalue, mod, ead, end, eadd;
	const char *depth_unit, *pressure_unit, *temp_unit, *vertical_speed_unit;
	double depthvalue, tempvalue, speedvalue, sacvalue;
	int decimals;
	const char *unit;

	depthvalue = get_depth_units(entry->depth, NULL, &depth_unit);
	put_format(b, translate("gettextFromC", "@: %d:%02d\nD: %.1f%s\n"), FRACTION(entry->sec, 60), depthvalue, depth_unit);
	if (GET_PRESSURE(entry)) {
		pressurevalue = get_pressure_units(GET_PRESSURE(entry), &pressure_unit);
		put_format(b, translate("gettextFromC", "P: %d%s\n"), pressurevalue, pressure_unit);
	}
	if (entry->temperature) {
		tempvalue = get_temp_units(entry->temperature, &temp_unit);
		put_format(b, translate("gettextFromC", "T: %.1f%s\n"), tempvalue, temp_unit);
	}
	speedvalue = get_vertical_speed_units(abs(entry->speed), NULL, &vertical_speed_unit);
	/* Ascending speeds are positive, descending are negative */
	if (entry->speed > 0)
		speedvalue *= -1;
	put_format(b, translate("gettextFromC", "V: %.1f%s\n"), speedvalue, vertical_speed_unit);
	sacvalue = get_volume_units(entry->sac, &decimals, &unit);
	if (entry->sac && prefs.show_sac)
		put_format(b, translate("gettextFromC", "SAC: %.*f%s/min\n"), decimals, sacvalue, unit);
	if (entry->cns)
		put_format(b, translate("gettextFromC", "CNS: %u%%\n"), entry->cns);
	if (prefs.pp_graphs.po2)
		put_format(b, translate("gettextFromC", "pO%s: %.2fbar\n"), UTF8_SUBSCRIPT_2, entry->pressures.o2);
	if (prefs.pp_graphs.pn2)
		put_format(b, translate("gettextFromC", "pN%s: %.2fbar\n"), UTF8_SUBSCRIPT_2, entry->pressures.n2);
	if (prefs.pp_graphs.phe)
		put_format(b, translate("gettextFromC", "pHe: %.2fbar\n"), entry->pressures.he);
	if (prefs.mod) {
		mod = (int)get_depth_units(entry->mod, NULL, &depth_unit);
		put_format(b, translate("gettextFromC", "MOD: %d%s\n"), mod, depth_unit);
	}
	eadd = (int)get_depth_units(entry->eadd, NULL, &depth_unit);
	if (prefs.ead) {
		switch (pi->dive_type) {
		case NITROX:
			ead = (int)get_depth_units(entry->ead, NULL, &depth_unit);
			put_format(b, translate("gettextFromC", "EAD: %d%s\nEADD: %d%s\n"), ead, depth_unit, eadd, depth_unit);
			break;
		case TRIMIX:
			end = (int)get_depth_units(entry->end, NULL, &depth_unit);
			put_format(b, translate("gettextFromC", "END: %d%s\nEADD: %d%s\n"), end, depth_unit, eadd, depth_unit);
			break;
		case AIR:
		case FREEDIVING:
			/* nothing */
			break;
		}
	}
	if (entry->stopdepth) {
		depthvalue = get_depth_units(entry->stopdepth, NULL, &depth_unit);
		if (entry->ndl) {
			/* this is a safety stop as we still have ndl */
			if (entry->stoptime)
				put_format(b, translate("gettextFromC", "Safetystop: %umin @ %.0f%s\n"), DIV_UP(entry->stoptime, 60),
					   depthvalue, depth_unit);
			else
				put_format(b, translate("gettextFromC", "Safetystop: unkn time @ %.0f%s\n"),
					   depthvalue, depth_unit);
		} else {
			/* actual deco stop */
			if (entry->stoptime)
				put_format(b, translate("gettextFromC", "Deco: %umin @ %.0f%s\n"), DIV_UP(entry->stoptime, 60),
					   depthvalue, depth_unit);
			else
				put_format(b, translate("gettextFromC", "Deco: unkn time @ %.0f%s\n"),
					   depthvalue, depth_unit);
		}
	} else if (entry->in_deco) {
		put_string(b, translate("gettextFromC", "In deco\n"));
	} else if (has_ndl) {
		put_format(b, translate("gettextFromC", "NDL: %umin\n"), DIV_UP(entry->ndl, 60));
	}
	if (entry->tts)
		put_format(b, translate("gettextFromC", "TTS: %umin\n"), DIV_UP(entry->tts, 60));
	if (entry->stopdepth_calc && entry->stoptime_calc) {
		depthvalue = get_depth_units(entry->stopdepth_calc, NULL, &depth_unit);
		put_format(b, translate("gettextFromC", "Deco: %umin @ %.0f%s (calc)\n"), DIV_UP(entry->stoptime_calc, 60),
			   depthvalue, depth_unit);
	} else if (entry->in_deco_calc) {
		/* This means that we have no NDL left,
		 * and we have no deco stop,
		 * so if we just accend to the surface slowly
		 * (ascent_mm_per_step / ascent_s_per_step)
		 * everything will be ok. */
		put_string(b, translate("gettextFromC", "In deco (calc)\n"));
	} else if (prefs.calcndltts && entry->ndl_calc != 0) {
		if(entry->ndl_calc < MAX_PROFILE_DECO)
			put_format(b, translate("gettextFromC", "NDL: %umin (calc)\n"), DIV_UP(entry->ndl_calc, 60));
		else
			put_format(b, "%s", translate("gettextFromC", "NDL: >2h (calc)\n"));
	}
	if (entry->tts_calc) {
		if (entry->tts_calc < MAX_PROFILE_DECO)
			put_format(b, translate("gettextFromC", "TTS: %umin (calc)\n"), DIV_UP(entry->tts_calc, 60));
		else
			put_format(b, "%s", translate("gettextFromC", "TTS: >2h (calc)\n"));
	}
	if (entry->rbt)
		put_format(b, translate("gettextFromC", "RBT: %umin\n"), DIV_UP(entry->rbt, 60));
	if (entry->ceiling) {
		depthvalue = get_depth_units(entry->ceiling, NULL, &depth_unit);
		put_format(b, translate("gettextFromC", "Calculated ceiling %.0f%s\n"), depthvalue, depth_unit);
		if (prefs.calcalltissues) {
			int k;
			for (k = 0; k < 16; k++) {
				if (entry->ceilings[k]) {
					depthvalue = get_depth_units(entry->ceilings[k], NULL, &depth_unit);
					put_format(b, translate("gettextFromC", "Tissue %.0fmin: %.1f%s\n"), buehlmann_N2_t_halflife[k], depthvalue, depth_unit);
				}
			}
		}
	}
	if (entry->heartbeat && prefs.hrgraph)
		put_format(b, translate("gettextFromC", "heartbeat: %d\n"), entry->heartbeat);
	if (entry->bearing)
		put_format(b, translate("gettextFromC", "bearing: %d\n"), entry->bearing);
	if (entry->running_sum) {
		depthvalue = get_depth_units(entry->running_sum / entry->sec, NULL, &depth_unit);
		put_format(b, translate("gettextFromC", "mean depth to here %.1f%s\n"), depthvalue, depth_unit);
	}

	strip_mb(b);
}

struct plot_data *get_plot_details_new(struct plot_info *pi, int time, struct membuffer *mb)
{
	struct plot_data *entry = NULL;
	int i;

	for (i = 0; i < pi->nr; i++) {
		entry = pi->entry + i;
		if (entry->sec >= time)
			break;
	}
	if (entry)
		plot_string(pi, entry, mb, pi->has_ndl);
	return (entry);
}

/* Compare two plot_data entries and writes the results into a string */
void compare_samples(struct plot_data *e1, struct plot_data *e2, char *buf, int bufsize, int sum)
{
	struct plot_data *start, *stop, *data;
	const char *depth_unit, *pressure_unit, *vertical_speed_unit;
	char *buf2 = malloc(bufsize);
	int avg_speed, max_asc_speed, max_desc_speed;
	int delta_depth, avg_depth, max_depth, min_depth;
	int bar_used, last_pressure, pressurevalue, last_cylidx;
	int count, last_sec, delta_time;
	bool crossed_tankchange = false;

	double depthvalue, speedvalue;

	if (bufsize > 0)
		buf[0] = '\0';
	if (e1 == NULL || e2 == NULL) {
		free(buf2);
		return;
	}

	if (e1->sec < e2->sec) {
		start = e1;
		stop = e2;
	} else if (e1->sec > e2->sec) {
		start = e2;
		stop = e1;
	} else {
		free(buf2);
		return;
	}
	count = 0;
	avg_speed = 0;
	max_asc_speed = 0;
	max_desc_speed = 0;

	delta_depth = abs(start->depth - stop->depth);
	delta_time = abs(start->sec - stop->sec);
	avg_depth = 0;
	max_depth = 0;
	min_depth = INT_MAX;
	bar_used = 0;

	last_sec = start->sec;
	last_pressure = GET_PRESSURE(start);
	last_cylidx = start->cylinderindex;

	data = start;
	while (data != stop) {
		data = start + count;
		if (sum)
			avg_speed += abs(data->speed) * (data->sec - last_sec);
		else
			avg_speed += data->speed * (data->sec - last_sec);
		avg_depth += data->depth * (data->sec - last_sec);

		if (data->speed > max_desc_speed)
			max_desc_speed = data->speed;
		if (data->speed < max_asc_speed)
			max_asc_speed = data->speed;

		if (data->depth < min_depth)
			min_depth = data->depth;
		if (data->depth > max_depth)
			max_depth = data->depth;
		/* Try to detect gas changes - this hack might work for some side mount scenarios? */
		if (GET_PRESSURE(data) < last_pressure + 2000)
			bar_used += last_pressure - GET_PRESSURE(data);

		if (data->cylinderindex != last_cylidx)
			/* if we change tanks, don't try to do SAC rate later */
			crossed_tankchange = true;

		count += 1;
		last_sec = data->sec;
		last_pressure = GET_PRESSURE(data);
	}
	avg_depth /= stop->sec - start->sec;
	avg_speed /= stop->sec - start->sec;

	snprintf(buf, bufsize, translate("gettextFromC", "%sT: %d:%02d min"), UTF8_DELTA, delta_time / 60, delta_time % 60);
	memcpy(buf2, buf, bufsize);

	depthvalue = get_depth_units(delta_depth, NULL, &depth_unit);
	snprintf(buf, bufsize, translate("gettextFromC", "%s %sD:%.1f%s"), buf2, UTF8_DELTA, depthvalue, depth_unit);
	memcpy(buf2, buf, bufsize);

	depthvalue = get_depth_units(min_depth, NULL, &depth_unit);
	snprintf(buf, bufsize, translate("gettextFromC", "%s %sD:%.1f%s"), buf2, UTF8_DOWNWARDS_ARROW, depthvalue, depth_unit);
	memcpy(buf2, buf, bufsize);

	depthvalue = get_depth_units(max_depth, NULL, &depth_unit);
	snprintf(buf, bufsize, translate("gettextFromC", "%s %sD:%.1f%s"), buf2, UTF8_UPWARDS_ARROW, depthvalue, depth_unit);
	memcpy(buf2, buf, bufsize);

	depthvalue = get_depth_units(avg_depth, NULL, &depth_unit);
	snprintf(buf, bufsize, translate("gettextFromC", "%s %sD:%.1f%s\n"), buf2, UTF8_AVERAGE, depthvalue, depth_unit);
	memcpy(buf2, buf, bufsize);

	speedvalue = get_vertical_speed_units(abs(max_desc_speed), NULL, &vertical_speed_unit);
	snprintf(buf, bufsize, translate("gettextFromC", "%s%sV:%.2f%s"), buf2, UTF8_DOWNWARDS_ARROW, speedvalue, vertical_speed_unit);
	memcpy(buf2, buf, bufsize);

	speedvalue = get_vertical_speed_units(abs(max_asc_speed), NULL, &vertical_speed_unit);
	snprintf(buf, bufsize, translate("gettextFromC", "%s %sV:%.2f%s"), buf2, UTF8_UPWARDS_ARROW, speedvalue, vertical_speed_unit);
	memcpy(buf2, buf, bufsize);

	speedvalue = get_vertical_speed_units(abs(avg_speed), NULL, &vertical_speed_unit);
	snprintf(buf, bufsize, translate("gettextFromC", "%s %sV:%.2f%s"), buf2, UTF8_AVERAGE, speedvalue, vertical_speed_unit);
	memcpy(buf2, buf, bufsize);

	/* Only print if gas has been used */
	if (bar_used) {
		pressurevalue = get_pressure_units(bar_used, &pressure_unit);
		memcpy(buf2, buf, bufsize);
		snprintf(buf, bufsize, translate("gettextFromC", "%s %sP:%d %s"), buf2, UTF8_DELTA, pressurevalue, pressure_unit);
		cylinder_t *cyl = displayed_dive.cylinder + start->cylinderindex;
		/* if we didn't cross a tank change and know the cylidner size as well, show SAC rate */
		if (!crossed_tankchange && cyl->type.size.mliter) {
			double volume_value;
			int volume_precision;
			const char *volume_unit;
			struct plot_data *first = start;
			struct plot_data *last = stop;
			while (first < stop && GET_PRESSURE(first) == 0)
				first++;
			while (last > first && GET_PRESSURE(last) == 0)
				last--;

			pressure_t first_pressure = { GET_PRESSURE(first) };
			pressure_t stop_pressure = { GET_PRESSURE(last) };
			int volume_used = gas_volume(cyl, first_pressure) - gas_volume(cyl, stop_pressure);

			/* Mean pressure in ATM */
			double atm = depth_to_atm(avg_depth, &displayed_dive);

			/* milliliters per minute */
			int sac = volume_used / atm * 60 / delta_time;
			memcpy(buf2, buf, bufsize);
			volume_value = get_volume_units(sac, &volume_precision, &volume_unit);
			snprintf(buf, bufsize, translate("gettextFromC", "%s SAC:%.*f %s"), buf2, volume_precision, volume_value, volume_unit);
		}
	}

	free(buf2);
}
