/* dive.c */
/* maintains the internal dive list structure */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "gettext.h"
#include "dive.h"
#include "libdivecomputer.h"
#include "device.h"
#include "divelist.h"
#include "qthelperfromc.h"

/* one could argue about the best place to have this variable -
 * it's used in the UI, but it seems to make the most sense to have it
 * here */
struct dive displayed_dive;
struct dive_site displayed_dive_site;

struct tag_entry *g_tag_list = NULL;

static const char *default_tags[] = {
	QT_TRANSLATE_NOOP("gettextFromC", "boat"), QT_TRANSLATE_NOOP("gettextFromC", "shore"), QT_TRANSLATE_NOOP("gettextFromC", "drift"),
	QT_TRANSLATE_NOOP("gettextFromC", "deep"), QT_TRANSLATE_NOOP("gettextFromC", "cavern"), QT_TRANSLATE_NOOP("gettextFromC", "ice"),
	QT_TRANSLATE_NOOP("gettextFromC", "wreck"), QT_TRANSLATE_NOOP("gettextFromC", "cave"), QT_TRANSLATE_NOOP("gettextFromC", "altitude"),
	QT_TRANSLATE_NOOP("gettextFromC", "pool"), QT_TRANSLATE_NOOP("gettextFromC", "lake"), QT_TRANSLATE_NOOP("gettextFromC", "river"),
	QT_TRANSLATE_NOOP("gettextFromC", "night"), QT_TRANSLATE_NOOP("gettextFromC", "fresh"), QT_TRANSLATE_NOOP("gettextFromC", "student"),
	QT_TRANSLATE_NOOP("gettextFromC", "instructor"), QT_TRANSLATE_NOOP("gettextFromC", "photo"), QT_TRANSLATE_NOOP("gettextFromC", "video"),
	QT_TRANSLATE_NOOP("gettextFromC", "deco")
};

const char *cylinderuse_text[] = {
	QT_TRANSLATE_NOOP("gettextFromC", "OC-gas"), QT_TRANSLATE_NOOP("gettextFromC", "diluent"), QT_TRANSLATE_NOOP("gettextFromC", "oxygen"), QT_TRANSLATE_NOOP("gettextFromC", "not used")
};
const char *divemode_text[] = { "OC", "CCR", "PSCR", "Freedive" };

int event_is_gaschange(struct event *ev)
{
	return ev->type == SAMPLE_EVENT_GASCHANGE ||
		ev->type == SAMPLE_EVENT_GASCHANGE2;
}

struct event *add_event(struct divecomputer *dc, unsigned int time, int type, int flags, int value, const char *name)
{
	int gas_index = -1;
	struct event *ev, **p;
	unsigned int size, len = strlen(name);

	size = sizeof(*ev) + len + 1;
	ev = malloc(size);
	if (!ev)
		return NULL;
	memset(ev, 0, size);
	memcpy(ev->name, name, len);
	ev->time.seconds = time;
	ev->type = type;
	ev->flags = flags;
	ev->value = value;

	/*
	 * Expand the events into a sane format. Currently
	 * just gas switches
	 */
	switch (type) {
	case SAMPLE_EVENT_GASCHANGE2:
		/* High 16 bits are He percentage */
		ev->gas.mix.he.permille = (value >> 16) * 10;

		/* Extension to the GASCHANGE2 format: cylinder index in 'flags' */
		if (flags > 0 && flags <= MAX_CYLINDERS)
			gas_index = flags-1;
	/* Fallthrough */
	case SAMPLE_EVENT_GASCHANGE:
		/* Low 16 bits are O2 percentage */
		ev->gas.mix.o2.permille = (value & 0xffff) * 10;
		ev->gas.index = gas_index;
		break;
	}

	p = &dc->events;

	/* insert in the sorted list of events */
	while (*p && (*p)->time.seconds <= time)
		p = &(*p)->next;
	ev->next = *p;
	*p = ev;
	remember_event(name);
	return ev;
}

static int same_event(struct event *a, struct event *b)
{
	if (a->time.seconds != b->time.seconds)
		return 0;
	if (a->type != b->type)
		return 0;
	if (a->flags != b->flags)
		return 0;
	if (a->value != b->value)
		return 0;
	return !strcmp(a->name, b->name);
}

void remove_event(struct event *event)
{
	struct event **ep = &current_dc->events;
	while (ep && !same_event(*ep, event))
		ep = &(*ep)->next;
	if (ep) {
		/* we can't link directly with event->next
		 * because 'event' can be a copy from another
		 * dive (for instance the displayed_dive
		 * that we use on the interface to show things). */
		struct event *temp = (*ep)->next;
		free(*ep);
		*ep = temp;
	}
}

/* since the name is an array as part of the structure (how silly is that?) we
 * have to actually remove the existing event and replace it with a new one.
 * WARNING, WARNING... this may end up freeing event in case that event is indeed
 * WARNING, WARNING... part of this divecomputer on this dive! */
void update_event_name(struct dive *d, struct event *event, char *name)
{
	if (!d || !event)
		return;
	struct divecomputer *dc = get_dive_dc(d, dc_number);
	if (!dc)
		return;
	struct event **removep = &dc->events;
	struct event *remove;
	while ((*removep)->next && !same_event(*removep, event))
		removep = &(*removep)->next;
	if (!same_event(*removep, event))
		return;
	remove = *removep;
	*removep = (*removep)->next;
	add_event(dc, event->time.seconds, event->type, event->flags, event->value, name);
	free(remove);
	invalidate_dive_cache(d);
}

void add_extra_data(struct divecomputer *dc, const char *key, const char *value)
{
	struct extra_data **ed = &dc->extra_data;

	while (*ed)
		ed = &(*ed)->next;
	*ed = malloc(sizeof(struct extra_data));
	if (*ed) {
		(*ed)->key = strdup(key);
		(*ed)->value = strdup(value);
		(*ed)->next = NULL;
	}
}

/* this returns a pointer to static variable - so use it right away after calling */
struct gasmix *get_gasmix_from_event(struct dive *dive, struct event *ev)
{
	static struct gasmix dummy;
	if (ev && event_is_gaschange(ev)) {
		int index = ev->gas.index;
		if (index >= 0 && index <= MAX_CYLINDERS)
			return &dive->cylinder[index].gasmix;
		return &ev->gas.mix;
	}

	return &dummy;
}

int get_pressure_units(int mb, const char **units)
{
	int pressure;
	const char *unit;
	struct units *units_p = get_units();

	switch (units_p->pressure) {
	case PASCAL:
		pressure = mb * 100;
		unit = translate("gettextFromC", "pascal");
		break;
	case BAR:
	default:
		pressure = (mb + 500) / 1000;
		unit = translate("gettextFromC", "bar");
		break;
	case PSI:
		pressure = mbar_to_PSI(mb);
		unit = translate("gettextFromC", "psi");
		break;
	}
	if (units)
		*units = unit;
	return pressure;
}

double get_temp_units(unsigned int mk, const char **units)
{
	double deg;
	const char *unit;
	struct units *units_p = get_units();

	if (units_p->temperature == FAHRENHEIT) {
		deg = mkelvin_to_F(mk);
		unit = UTF8_DEGREE "F";
	} else {
		deg = mkelvin_to_C(mk);
		unit = UTF8_DEGREE "C";
	}
	if (units)
		*units = unit;
	return deg;
}

double get_volume_units(unsigned int ml, int *frac, const char **units)
{
	int decimals;
	double vol;
	const char *unit;
	struct units *units_p = get_units();

	switch (units_p->volume) {
	case LITER:
	default:
		vol = ml / 1000.0;
		unit = translate("gettextFromC", "ℓ");
		decimals = 1;
		break;
	case CUFT:
		vol = ml_to_cuft(ml);
		unit = translate("gettextFromC", "cuft");
		decimals = 2;
		break;
	}
	if (frac)
		*frac = decimals;
	if (units)
		*units = unit;
	return vol;
}

int units_to_sac(double volume)
{
	if (get_units()->volume == CUFT)
		return rint(cuft_to_l(volume) * 1000.0);
	else
		return rint(volume * 1000);
}

unsigned int units_to_depth(double depth)
{
	if (get_units()->length == METERS)
		return rint(depth * 1000);
	return feet_to_mm(depth);
}

double get_depth_units(int mm, int *frac, const char **units)
{
	int decimals;
	double d;
	const char *unit;
	struct units *units_p = get_units();

	switch (units_p->length) {
	case METERS:
	default:
		d = mm / 1000.0;
		unit = translate("gettextFromC", "m");
		decimals = d < 20;
		break;
	case FEET:
		d = mm_to_feet(mm);
		unit = translate("gettextFromC", "ft");
		decimals = 0;
		break;
	}
	if (frac)
		*frac = decimals;
	if (units)
		*units = unit;
	return d;
}

double get_vertical_speed_units(unsigned int mms, int *frac, const char **units)
{
	double d;
	const char *unit;
	const struct units *units_p = get_units();
	const double time_factor = units_p->vertical_speed_time == MINUTES ? 60.0 : 1.0;

	switch (units_p->length) {
	case METERS:
	default:
		d = mms / 1000.0 * time_factor;
		if (units_p->vertical_speed_time == MINUTES)
			unit = translate("gettextFromC", "m/min");
		else
			unit = translate("gettextFromC", "m/s");
		break;
	case FEET:
		d = mm_to_feet(mms) * time_factor;
		if (units_p->vertical_speed_time == MINUTES)
			unit = translate("gettextFromC", "ft/min");
		else
			unit = translate("gettextFromC", "ft/s");
		break;
	}
	if (frac)
		*frac = d < 10;
	if (units)
		*units = unit;
	return d;
}

double get_weight_units(unsigned int grams, int *frac, const char **units)
{
	int decimals;
	double value;
	const char *unit;
	struct units *units_p = get_units();

	if (units_p->weight == LBS) {
		value = grams_to_lbs(grams);
		unit = translate("gettextFromC", "lbs");
		decimals = 0;
	} else {
		value = grams / 1000.0;
		unit = translate("gettextFromC", "kg");
		decimals = 1;
	}
	if (frac)
		*frac = decimals;
	if (units)
		*units = unit;
	return value;
}

bool has_hr_data(struct divecomputer *dc)
{
	int i;
	struct sample *sample;

	if (!dc)
		return false;

	sample = dc->sample;
	for (i = 0; i < dc->samples; i++)
		if (sample[i].heartbeat)
			return true;
	return false;
}

struct dive *alloc_dive(void)
{
	struct dive *dive;

	dive = malloc(sizeof(*dive));
	if (!dive)
		exit(1);
	memset(dive, 0, sizeof(*dive));
	dive->id = dive_getUniqID(dive);
	return dive;
}

static void free_dc(struct divecomputer *dc);
static void free_dc_contents(struct divecomputer *dc);
static void free_pic(struct picture *picture);

/* this is very different from the copy_divecomputer later in this file;
 * this function actually makes full copies of the content */
static void copy_dc(struct divecomputer *sdc, struct divecomputer *ddc)
{
	*ddc = *sdc;
	ddc->model = copy_string(sdc->model);
	copy_samples(sdc, ddc);
	copy_events(sdc, ddc);
}

/* copy an element in a list of pictures */
static void copy_pl(struct picture *sp, struct picture *dp)
{
	*dp = *sp;
	dp->filename = copy_string(sp->filename);
	dp->hash = copy_string(sp->hash);
}

/* copy an element in a list of tags */
static void copy_tl(struct tag_entry *st, struct tag_entry *dt)
{
	dt->tag = malloc(sizeof(struct divetag));
	dt->tag->name = copy_string(st->tag->name);
	dt->tag->source = copy_string(st->tag->source);
}

/* Clear everything but the first element;
 * this works for taglist, picturelist, even dive computers */
#define STRUCTURED_LIST_FREE(_type, _start, _free) \
	{                                          \
		_type *_ptr = _start;              \
		while (_ptr) {                     \
			_type *_next = _ptr->next; \
			_free(_ptr);               \
			_ptr = _next;              \
		}                                  \
	}

#define STRUCTURED_LIST_COPY(_type, _first, _dest, _cpy) \
	{                                                \
		_type *_sptr = _first;                   \
		_type **_dptr = &_dest;                  \
		while (_sptr) {                          \
			*_dptr = malloc(sizeof(_type));  \
			_cpy(_sptr, *_dptr);             \
			_sptr = _sptr->next;             \
			_dptr = &(*_dptr)->next;         \
		}                                        \
		*_dptr = 0;                              \
	}

/* copy_dive makes duplicates of many components of a dive;
 * in order not to leak memory, we need to free those .
 * copy_dive doesn't play with the divetrip and forward/backward pointers
 * so we can ignore those */
void clear_dive(struct dive *d)
{
	if (!d)
		return;
	/* free the strings */
	free(d->buddy);
	free(d->divemaster);
	free(d->notes);
	free(d->suit);
	/* free tags, additional dive computers, and pictures */
	taglist_free(d->tag_list);
	free_dc_contents(&d->dc);
	STRUCTURED_LIST_FREE(struct divecomputer, d->dc.next, free_dc);
	STRUCTURED_LIST_FREE(struct picture, d->picture_list, free_pic);
	for (int i = 0; i < MAX_CYLINDERS; i++)
		free((void *)d->cylinder[i].type.description);
	for (int i = 0; i < MAX_WEIGHTSYSTEMS; i++)
		free((void *)d->weightsystem[i].description);
	memset(d, 0, sizeof(struct dive));
}

/* make a true copy that is independent of the source dive;
 * all data structures are duplicated, so the copy can be modified without
 * any impact on the source */
void copy_dive(struct dive *s, struct dive *d)
{
	clear_dive(d);
	/* simply copy things over, but then make actual copies of the
	 * relevant components that are referenced through pointers,
	 * so all the strings and the structured lists */
	*d = *s;
	invalidate_dive_cache(d);
	d->buddy = copy_string(s->buddy);
	d->divemaster = copy_string(s->divemaster);
	d->notes = copy_string(s->notes);
	d->suit = copy_string(s->suit);
	for (int i = 0; i < MAX_CYLINDERS; i++)
		d->cylinder[i].type.description = copy_string(s->cylinder[i].type.description);
	for (int i = 0; i < MAX_WEIGHTSYSTEMS; i++)
		d->weightsystem[i].description = copy_string(s->weightsystem[i].description);
	STRUCTURED_LIST_COPY(struct picture, s->picture_list, d->picture_list, copy_pl);
	STRUCTURED_LIST_COPY(struct tag_entry, s->tag_list, d->tag_list, copy_tl);

	// Copy the first dc explicitly, then the list of subsequent dc's
	copy_dc(&s->dc, &d->dc);
	STRUCTURED_LIST_COPY(struct divecomputer, s->dc.next, d->dc.next, copy_dc);
}

/* make a clone of the source dive and clean out the source dive;
 * this is specifically so we can create a dive in the displayed_dive and then
 * add it to the divelist.
 * Note the difference to copy_dive() / clean_dive() */
struct dive *clone_dive(struct dive *s)
{
	struct dive *dive = alloc_dive();
	*dive = *s;			   // so all the pointers in dive point to the things s pointed to
	memset(s, 0, sizeof(struct dive)); // and now the pointers in s are gone
	return dive;
}

#define CONDITIONAL_COPY_STRING(_component) \
	if (what._component)                \
		d->_component = copy_string(s->_component)

// copy elements, depending on bits in what that are set
void selective_copy_dive(struct dive *s, struct dive *d, struct dive_components what, bool clear)
{
	if (clear)
		clear_dive(d);
	CONDITIONAL_COPY_STRING(notes);
	CONDITIONAL_COPY_STRING(divemaster);
	CONDITIONAL_COPY_STRING(buddy);
	CONDITIONAL_COPY_STRING(suit);
	if (what.rating)
		d->rating = s->rating;
	if (what.visibility)
		d->visibility = s->visibility;
	if (what.divesite)
		d->dive_site_uuid = s->dive_site_uuid;
	if (what.tags)
		STRUCTURED_LIST_COPY(struct tag_entry, s->tag_list, d->tag_list, copy_tl);
	if (what.cylinders)
		copy_cylinders(s, d, false);
	if (what.weights)
		for (int i = 0; i < MAX_WEIGHTSYSTEMS; i++) {
			free((void *)d->weightsystem[i].description);
			d->weightsystem[i] = s->weightsystem[i];
			d->weightsystem[i].description = copy_string(s->weightsystem[i].description);
		}
}
#undef CONDITIONAL_COPY_STRING

struct event *clone_event(const struct event *src_ev)
{
	struct event *ev;
	if (!src_ev)
		return NULL;

	size_t size = sizeof(*src_ev) + strlen(src_ev->name) + 1;
	ev = (struct event*) malloc(size);
	if (!ev)
		exit(1);
	memcpy(ev, src_ev, size);
	ev->next = NULL;

	return ev;
}

/* copies all events in this dive computer */
void copy_events(struct divecomputer *s, struct divecomputer *d)
{
	struct event *ev, **pev;
	if (!s || !d)
		return;
	ev = s->events;
	pev = &d->events;
	while (ev != NULL) {
		struct event *new_ev = clone_event(ev);
		*pev = new_ev;
		pev = &new_ev->next;
		ev = ev->next;
	}
	*pev = NULL;
}

int nr_cylinders(struct dive *dive)
{
	int nr;

	for (nr = MAX_CYLINDERS; nr; --nr) {
		cylinder_t *cylinder = dive->cylinder + nr - 1;
		if (!cylinder_nodata(cylinder))
			break;
	}
	return nr;
}

int nr_weightsystems(struct dive *dive)
{
	int nr;

	for (nr = MAX_WEIGHTSYSTEMS; nr; --nr) {
		weightsystem_t *ws = dive->weightsystem + nr - 1;
		if (!weightsystem_none(ws))
			break;
	}
	return nr;
}

/* copy the equipment data part of the cylinders */
void copy_cylinders(struct dive *s, struct dive *d, bool used_only)
{
	int i,j;
	if (!s || !d)
		return;
	for (i = 0; i < MAX_CYLINDERS; i++) {
		free((void *)d->cylinder[i].type.description);
		memset(&d->cylinder[i], 0, sizeof(cylinder_t));
	}
	for (i = j = 0; i < MAX_CYLINDERS; i++) {
		if (!used_only || is_cylinder_used(s, i)) {
			d->cylinder[j].type = s->cylinder[i].type;
			d->cylinder[j].type.description = copy_string(s->cylinder[i].type.description);
			d->cylinder[j].gasmix = s->cylinder[i].gasmix;
			d->cylinder[j].depth = s->cylinder[i].depth;
			d->cylinder[j].cylinder_use = s->cylinder[i].cylinder_use;
			d->cylinder[j].manually_added = true;
			j++;
		}
	}
}

int cylinderuse_from_text(const char *text)
{
	for (enum cylinderuse i = 0; i < NUM_GAS_USE; i++) {
		if (same_string(text, cylinderuse_text[i]) || same_string(text, translate("gettextFromC", cylinderuse_text[i])))
			return i;
	}
	return -1;
}

void copy_samples(struct divecomputer *s, struct divecomputer *d)
{
	/* instead of carefully copying them one by one and calling add_sample
	 * over and over again, let's just copy the whole blob */
	if (!s || !d)
		return;
	int nr = s->samples;
	d->samples = nr;
	d->alloc_samples = nr;
	// We expect to be able to read the memory in the other end of the pointer
	// if its a valid pointer, so don't expect malloc() to return NULL for
	// zero-sized malloc, do it ourselves.
	d->sample = NULL;

	if(!nr)
		return;

	d->sample = malloc(nr * sizeof(struct sample));
	if (d->sample)
		memcpy(d->sample, s->sample, nr * sizeof(struct sample));
}

struct sample *prepare_sample(struct divecomputer *dc)
{
	if (dc) {
		int nr = dc->samples;
		int alloc_samples = dc->alloc_samples;
		struct sample *sample;
		if (nr >= alloc_samples) {
			struct sample *newsamples;

			alloc_samples = (alloc_samples * 3) / 2 + 10;
			newsamples = realloc(dc->sample, alloc_samples * sizeof(struct sample));
			if (!newsamples)
				return NULL;
			dc->alloc_samples = alloc_samples;
			dc->sample = newsamples;
		}
		sample = dc->sample + nr;
		memset(sample, 0, sizeof(*sample));
		return sample;
	}
	return NULL;
}

void finish_sample(struct divecomputer *dc)
{
	dc->samples++;
}

/*
 * So when we re-calculate maxdepth and meandepth, we will
 * not override the old numbers if they are close to the
 * new ones.
 *
 * Why? Because a dive computer may well actually track the
 * max depth and mean depth at finer granularity than the
 * samples it stores. So it's possible that the max and mean
 * have been reported more correctly originally.
 *
 * Only if the values calculated from the samples are clearly
 * different do we override the normal depth values.
 *
 * This considers 1m to be "clearly different". That's
 * a totally random number.
 */
static void update_depth(depth_t *depth, int new)
{
	if (new) {
		int old = depth->mm;

		if (abs(old - new) > 1000)
			depth->mm = new;
	}
}

static void update_temperature(temperature_t *temperature, int new)
{
	if (new) {
		int old = temperature->mkelvin;

		if (abs(old - new) > 1000)
			temperature->mkelvin = new;
	}
}

/*
 * Calculate how long we were actually under water, and the average
 * depth while under water.
 *
 * This ignores any surface time in the middle of the dive.
 */
void fixup_dc_duration(struct divecomputer *dc)
{
	int duration, i;
	int lasttime, lastdepth, depthtime;

	duration = 0;
	lasttime = 0;
	lastdepth = 0;
	depthtime = 0;
	for (i = 0; i < dc->samples; i++) {
		struct sample *sample = dc->sample + i;
		int time = sample->time.seconds;
		int depth = sample->depth.mm;

		/* We ignore segments at the surface */
		if (depth > SURFACE_THRESHOLD || lastdepth > SURFACE_THRESHOLD) {
			duration += time - lasttime;
			depthtime += (time - lasttime) * (depth + lastdepth) / 2;
		}
		lastdepth = depth;
		lasttime = time;
	}
	if (duration) {
		dc->duration.seconds = duration;
		dc->meandepth.mm = (depthtime + duration / 2) / duration;
	}
}

/* Which cylinders had gas used? */
#define SOME_GAS 5000
static unsigned int get_cylinder_used(struct dive *dive)
{
	int i;
	unsigned int mask = 0;

	for (i = 0; i < MAX_CYLINDERS; i++) {
		cylinder_t *cyl = dive->cylinder + i;
		int start_mbar, end_mbar;

		if (cylinder_nodata(cyl))
			continue;
		start_mbar = cyl->start.mbar ?: cyl->sample_start.mbar;
		end_mbar = cyl->end.mbar ?: cyl->sample_end.mbar;

		// More than 5 bar used? This matches statistics.c
		// heuristics
		if (start_mbar > end_mbar + SOME_GAS)
			mask |= 1 << i;
	}
	return mask;
}

/* Which cylinders do we know usage about? */
static unsigned int get_cylinder_known(struct dive *dive, struct divecomputer *dc)
{
	unsigned int mask = 0;
	struct event *ev;

	/* We know about using the O2 cylinder in a CCR dive */
	if (dc->divemode == CCR) {
		int o2_cyl = get_cylinder_idx_by_use(dive, OXYGEN);
		if (o2_cyl >= 0)
			mask |= 1 << o2_cyl;
	}

	/* We know about the explicit first cylinder (or first) */
	mask |= 1 << explicit_first_cylinder(dive, dc);

	/* And we have possible switches to other gases */
	ev = get_next_event(dc->events, "gaschange");
	while (ev) {
		int i = get_cylinder_index(dive, ev);
		if (i >= 0)
			mask |= 1 << i;
		ev = get_next_event(ev->next, "gaschange");
	}

	return mask;
}

void per_cylinder_mean_depth(struct dive *dive, struct divecomputer *dc, int *mean, int *duration)
{
	int i;
	int depthtime[MAX_CYLINDERS] = { 0, };
	uint32_t lasttime = 0;
	int lastdepth = 0;
	int idx = 0;
	unsigned int used_mask, known_mask;

	for (i = 0; i < MAX_CYLINDERS; i++)
		mean[i] = duration[i] = 0;
	if (!dc)
		return;

	/*
	 * There is no point in doing per-cylinder information
	 * if we don't actually know about the usage of all the
	 * used cylinders.
	 */
	used_mask = get_cylinder_used(dive);
	known_mask = get_cylinder_known(dive, dc);
	if (used_mask & ~known_mask) {
		/*
		 * If we had more than one used cylinder, but
		 * do not know usage of them, we simply cannot
		 * account mean depth to them.
		 *
		 * The "x & (x-1)" test shows if it's not a pure
		 * power of two.
		 */
		if (used_mask & (used_mask-1))
			return;

		/*
		 * For a single cylinder, use the overall mean
		 * and duration
		 */
		for (i = 0; i < MAX_CYLINDERS; i++) {
			if (used_mask & (1 << i)) {
				mean[i] = dc->meandepth.mm;
				duration[i] = dc->duration.seconds;
			}
		}

		return;
	}
	if (!dc->samples)
		dc = fake_dc(dc, false);
	struct event *ev = get_next_event(dc->events, "gaschange");
	for (i = 0; i < dc->samples; i++) {
		struct sample *sample = dc->sample + i;
		uint32_t time = sample->time.seconds;
		int depth = sample->depth.mm;

		/* Make sure to move the event past 'lasttime' */
		while (ev && lasttime >= ev->time.seconds) {
			idx = get_cylinder_index(dive, ev);
			ev = get_next_event(ev->next, "gaschange");
		}

		/* Do we need to fake a midway sample at an event? */
		if (ev && time > ev->time.seconds) {
			int newtime = ev->time.seconds;
			int newdepth = interpolate(lastdepth, depth, newtime - lasttime, time - lasttime);

			time = newtime;
			depth = newdepth;
			i--;
		}
		/* We ignore segments at the surface */
		if (depth > SURFACE_THRESHOLD || lastdepth > SURFACE_THRESHOLD) {
			duration[idx] += time - lasttime;
			depthtime[idx] += (time - lasttime) * (depth + lastdepth) / 2;
		}
		lastdepth = depth;
		lasttime = time;
	}
	for (i = 0; i < MAX_CYLINDERS; i++) {
		if (duration[i])
			mean[i] = (depthtime[i] + duration[i] / 2) / duration[i];
	}
}

static void update_min_max_temperatures(struct dive *dive, temperature_t temperature)
{
	if (temperature.mkelvin) {
		if (!dive->maxtemp.mkelvin || temperature.mkelvin > dive->maxtemp.mkelvin)
			dive->maxtemp = temperature;
		if (!dive->mintemp.mkelvin || temperature.mkelvin < dive->mintemp.mkelvin)
			dive->mintemp = temperature;
	}
}

int gas_volume(cylinder_t *cyl, pressure_t p)
{
	double bar = p.mbar / 1000.0;
	double z_factor = gas_compressibility_factor(&cyl->gasmix, bar);
	return rint(cyl->type.size.mliter * bar_to_atm(bar) / z_factor);
}

/*
 * If the cylinder tank pressures are within half a bar
 * (about 8 PSI) of the sample pressures, we consider it
 * to be a rounding error, and throw them away as redundant.
 */
static int same_rounded_pressure(pressure_t a, pressure_t b)
{
	return abs(a.mbar - b.mbar) <= 500;
}

/* Some dive computers (Cobalt) don't start the dive with cylinder 0 but explicitly
 * tell us what the first gas is with a gas change event in the first sample.
 * Sneakily we'll use a return value of 0 (or FALSE) when there is no explicit
 * first cylinder - in which case cylinder 0 is indeed the first cylinder */
int explicit_first_cylinder(struct dive *dive, struct divecomputer *dc)
{
	if (dc) {
		struct event *ev = get_next_event(dc->events, "gaschange");
		if (ev && ((dc->sample && ev->time.seconds == dc->sample[0].time.seconds) || ev->time.seconds <= 1))
			return get_cylinder_index(dive, ev);
		else if (dc->divemode == CCR)
			return MAX(get_cylinder_idx_by_use(dive, DILUENT), 0);
	}
	return 0;
}

/* this gets called when the dive mode has changed (so OC vs. CC)
 * there are two places we might have setpoints... events or in the samples
 */
void update_setpoint_events(struct dive *dive, struct divecomputer *dc)
{
	struct event *ev;
	int new_setpoint = 0;

	if (dc->divemode == CCR)
		new_setpoint = prefs.defaultsetpoint;

	if (dc->divemode == OC &&
	    (same_string(dc->model, "Shearwater Predator") ||
	     same_string(dc->model, "Shearwater Petrel") ||
	     same_string(dc->model, "Shearwater Nerd"))) {
		// make sure there's no setpoint in the samples
		// this is an irreversible change - so switching a dive to OC
		// by mistake when it's actually CCR is _bad_
		// So we make sure, this comes from a Predator or Petrel and we only remove
		// pO2 values we would have computed anyway.
		struct event *ev = get_next_event(dc->events, "gaschange");
		struct gasmix *gasmix = get_gasmix_from_event(dive, ev);
		struct event *next = get_next_event(ev, "gaschange");

		for (int i = 0; i < dc->samples; i++) {
			struct gas_pressures pressures;
			if (next && dc->sample[i].time.seconds >= next->time.seconds) {
				ev = next;
				gasmix = get_gasmix_from_event(dive, ev);
				next = get_next_event(ev, "gaschange");
			}
			fill_pressures(&pressures, calculate_depth_to_mbar(dc->sample[i].depth.mm, dc->surface_pressure, 0), gasmix ,0, OC);
			if (abs(dc->sample[i].setpoint.mbar - (int)(1000 * pressures.o2)) <= 50)
				dc->sample[i].setpoint.mbar = 0;
		}
	}

	// an "SP change" event at t=0 is currently our marker for OC vs CCR
	// this will need to change to a saner setup, but for now we can just
	// check if such an event is there and adjust it, or add that event
	ev = get_next_event(dc->events, "SP change");
	if (ev && ev->time.seconds == 0) {
		ev->value = new_setpoint;
	} else {
		if (!add_event(dc, 0, SAMPLE_EVENT_PO2, 0, new_setpoint, "SP change"))
			fprintf(stderr, "Could not add setpoint change event\n");
	}
}

void sanitize_gasmix(struct gasmix *mix)
{
	unsigned int o2, he;

	o2 = mix->o2.permille;
	he = mix->he.permille;

	/* Regular air: leave empty */
	if (!he) {
		if (!o2)
			return;
		/* 20.8% to 21% O2 is just air */
		if (gasmix_is_air(mix)) {
			mix->o2.permille = 0;
			return;
		}
	}

	/* Sane mix? */
	if (o2 <= 1000 && he <= 1000 && o2 + he <= 1000)
		return;
	fprintf(stderr, "Odd gasmix: %u O2 %u He\n", o2, he);
	memset(mix, 0, sizeof(*mix));
}

/*
 * See if the size/workingpressure looks like some standard cylinder
 * size, eg "AL80".
 *
 * NOTE! We don't take compressibility into account when naming
 * cylinders. That makes a certain amount of sense, since the
 * cylinder name is independent from the gasmix, and different
 * gasmixes have different compressibility.
 */
static void match_standard_cylinder(cylinder_type_t *type)
{
	double cuft, bar;
	int psi, len;
	const char *fmt;
	char buffer[40], *p;

	/* Do we already have a cylinder description? */
	if (type->description)
		return;

	bar = type->workingpressure.mbar / 1000.0;
	cuft = ml_to_cuft(type->size.mliter);
	cuft *= bar_to_atm(bar);
	psi = to_PSI(type->workingpressure);

	switch (psi) {
	case 2300 ... 2500: /* 2400 psi: LP tank */
		fmt = "LP%d";
		break;
	case 2600 ... 2700: /* 2640 psi: LP+10% */
		fmt = "LP%d";
		break;
	case 2900 ... 3100: /* 3000 psi: ALx tank */
		fmt = "AL%d";
		break;
	case 3400 ... 3500: /* 3442 psi: HP tank */
		fmt = "HP%d";
		break;
	case 3700 ... 3850: /* HP+10% */
		fmt = "HP%d+";
		break;
	default:
		return;
	}
	len = snprintf(buffer, sizeof(buffer), fmt, (int)rint(cuft));
	p = malloc(len + 1);
	if (!p)
		return;
	memcpy(p, buffer, len + 1);
	type->description = p;
}


/*
 * There are two ways to give cylinder size information:
 *  - total amount of gas in cuft (depends on working pressure and physical size)
 *  - physical size
 *
 * where "physical size" is the one that actually matters and is sane.
 *
 * We internally use physical size only. But we save the workingpressure
 * so that we can do the conversion if required.
 */
static void sanitize_cylinder_type(cylinder_type_t *type)
{
	double volume_of_air, volume;

	/* If we have no working pressure, it had *better* be just a physical size! */
	if (!type->workingpressure.mbar)
		return;

	/* No size either? Nothing to go on */
	if (!type->size.mliter)
		return;

	if (xml_parsing_units.volume == CUFT) {
		double bar = type->workingpressure.mbar / 1000.0;
		/* confusing - we don't really start from ml but millicuft !*/
		volume_of_air = cuft_to_l(type->size.mliter);
		/* milliliters at 1 atm: not corrected for compressibility! */
		volume = volume_of_air / bar_to_atm(bar);
		type->size.mliter = rint(volume);
	}

	/* Ok, we have both size and pressure: try to match a description */
	match_standard_cylinder(type);
}

static void sanitize_cylinder_info(struct dive *dive)
{
	int i;

	for (i = 0; i < MAX_CYLINDERS; i++) {
		sanitize_gasmix(&dive->cylinder[i].gasmix);
		sanitize_cylinder_type(&dive->cylinder[i].type);
	}
}

/* some events should never be thrown away */
static bool is_potentially_redundant(struct event *event)
{
	if (!strcmp(event->name, "gaschange"))
		return false;
	if (!strcmp(event->name, "bookmark"))
		return false;
	if (!strcmp(event->name, "heading"))
		return false;
	return true;
}

/* match just by name - we compare the details in the code that uses this helper */
static struct event *find_previous_event(struct divecomputer *dc, struct event *event)
{
	struct event *ev = dc->events;
	struct event *previous = NULL;

	if (same_string(event->name, ""))
		return NULL;
	while (ev && ev != event) {
		if (same_string(ev->name, event->name))
			previous = ev;
		ev = ev->next;
	}
	return previous;
}

static void fixup_surface_pressure(struct dive *dive)
{
	struct divecomputer *dc;
	int sum = 0, nr = 0;

	for_each_dc (dive, dc) {
		if (dc->surface_pressure.mbar) {
			sum += dc->surface_pressure.mbar;
			nr++;
		}
	}
	if (nr)
		dive->surface_pressure.mbar = (sum + nr / 2) / nr;
}

static void fixup_water_salinity(struct dive *dive)
{
	struct divecomputer *dc;
	int sum = 0, nr = 0;

	for_each_dc (dive, dc) {
		if (dc->salinity) {
			if (dc->salinity < 500)
				dc->salinity += FRESHWATER_SALINITY;
			sum += dc->salinity;
			nr++;
		}
	}
	if (nr)
		dive->salinity = (sum + nr / 2) / nr;
}

static void fixup_meandepth(struct dive *dive)
{
	struct divecomputer *dc;
	int sum = 0, nr = 0;

	for_each_dc (dive, dc) {
		if (dc->meandepth.mm) {
			sum += dc->meandepth.mm;
			nr++;
		}
	}
	if (nr)
		dive->meandepth.mm = (sum + nr / 2) / nr;
}

static void fixup_duration(struct dive *dive)
{
	struct divecomputer *dc;
	unsigned int duration = 0;

	for_each_dc (dive, dc)
		duration = MAX(duration, dc->duration.seconds);

	dive->duration.seconds = duration;
}

/*
 * What do the dive computers say the water temperature is?
 * (not in the samples, but as dc property for dcs that support that)
 */
unsigned int dc_watertemp(struct divecomputer *dc)
{
	int sum = 0, nr = 0;

	do {
		if (dc->watertemp.mkelvin) {
			sum += dc->watertemp.mkelvin;
			nr++;
		}
	} while ((dc = dc->next) != NULL);
	if (!nr)
		return 0;
	return (sum + nr / 2) / nr;
}

static void fixup_watertemp(struct dive *dive)
{
	if (!dive->watertemp.mkelvin)
		dive->watertemp.mkelvin = dc_watertemp(&dive->dc);
}

/*
 * What do the dive computers say the air temperature is?
 */
unsigned int dc_airtemp(struct divecomputer *dc)
{
	int sum = 0, nr = 0;

	do {
		if (dc->airtemp.mkelvin) {
			sum += dc->airtemp.mkelvin;
			nr++;
		}
	} while ((dc = dc->next) != NULL);
	if (!nr)
		return 0;
	return (sum + nr / 2) / nr;
}

static void fixup_cylinder_use(struct dive *dive) // for CCR dives, store the indices
{						  // of the oxygen and diluent cylinders
	dive->oxygen_cylinder_index = get_cylinder_idx_by_use(dive, OXYGEN);
	dive->diluent_cylinder_index = get_cylinder_idx_by_use(dive, DILUENT);
}

static void fixup_airtemp(struct dive *dive)
{
	if (!dive->airtemp.mkelvin)
		dive->airtemp.mkelvin = dc_airtemp(&dive->dc);
}

/* zero out the airtemp in the dive structure if it was just created by
 * running fixup on the dive. keep it if it had been edited by hand */
static void un_fixup_airtemp(struct dive *a)
{
	if (a->airtemp.mkelvin && a->airtemp.mkelvin == dc_airtemp(&a->dc))
		a->airtemp.mkelvin = 0;
}

/*
 * events are stored as a linked list, so the concept of
 * "consecutive, identical events" is somewhat hard to
 * implement correctly (especially given that on some dive
 * computers events are asynchronous, so they can come in
 * between what would be the non-constant sample rate).
 *
 * So what we do is that we throw away clearly redundant
 * events that are fewer than 61 seconds apart (assuming there
 * is no dive computer with a sample rate of more than 60
 * seconds... that would be pretty pointless to plot the
 * profile with)
 *
 * We first only mark the events for deletion so that we
 * still know when the previous event happened.
 */
static void fixup_dc_events(struct divecomputer *dc)
{
	struct event *event;

	event = dc->events;
	while (event) {
		struct event *prev;
		if (is_potentially_redundant(event)) {
			prev = find_previous_event(dc, event);
			if (prev && prev->value == event->value &&
			    prev->flags == event->flags &&
			    event->time.seconds - prev->time.seconds < 61)
				event->deleted = true;
		}
		event = event->next;
	}
	event = dc->events;
	while (event) {
		if (event->next && event->next->deleted) {
			struct event *nextnext = event->next->next;
			free(event->next);
			event->next = nextnext;
		} else {
			event = event->next;
		}
	}
}

static int interpolate_depth(struct divecomputer *dc, int idx, int lastdepth, int lasttime, int now)
{
	int i;
	int nextdepth = lastdepth;
	int nexttime = now;

	for (i = idx+1; i < dc->samples; i++) {
		struct sample *sample = dc->sample + i;
		if (sample->depth.mm < 0)
			continue;
		nextdepth = sample->depth.mm;
		nexttime = sample->time.seconds;
		break;
	}
	return interpolate(lastdepth, nextdepth, now-lasttime, nexttime-lasttime);
}

static void fixup_dc_depths(struct dive *dive, struct divecomputer *dc)
{
	int i;
	int maxdepth = dc->maxdepth.mm;
	int lasttime = 0, lastdepth = 0;

	for (i = 0; i < dc->samples; i++) {
		struct sample *sample = dc->sample + i;
		int time = sample->time.seconds;
		int depth = sample->depth.mm;

		if (depth < 0) {
			depth = interpolate_depth(dc, i, lastdepth, lasttime, time);
			sample->depth.mm = depth;
		}

		if (depth > SURFACE_THRESHOLD) {
			if (depth > maxdepth)
				maxdepth = depth;
		}

		lastdepth = depth;
		lasttime = time;
		if (sample->cns > dive->maxcns)
			dive->maxcns = sample->cns;
	}

	update_depth(&dc->maxdepth, maxdepth);
	if (maxdepth > dive->maxdepth.mm)
		dive->maxdepth.mm = maxdepth;
}

static void fixup_dc_temp(struct dive *dive, struct divecomputer *dc)
{
	int i;
	int mintemp = 0, lasttemp = 0;

	for (i = 0; i < dc->samples; i++) {
		struct sample *sample = dc->sample + i;
		int temp = sample->temperature.mkelvin;

		if (temp) {
			/*
			 * If we have consecutive identical
			 * temperature readings, throw away
			 * the redundant ones.
			 */
			if (lasttemp == temp)
				sample->temperature.mkelvin = 0;
			else
				lasttemp = temp;

			if (!mintemp || temp < mintemp)
				mintemp = temp;
		}

		update_min_max_temperatures(dive, sample->temperature);
	}
	update_temperature(&dc->watertemp, mintemp);
	update_min_max_temperatures(dive, dc->watertemp);
}

/* Remove redundant pressure information */
static void simplify_dc_pressures(struct divecomputer *dc)
{
	int i;
	int lastindex = -1;
	int lastpressure = 0, lasto2pressure = 0;

	for (i = 0; i < dc->samples; i++) {
		struct sample *sample = dc->sample + i;
		int pressure = sample->cylinderpressure.mbar;
		int o2_pressure = sample->o2cylinderpressure.mbar;
		int index;

		index = sample->sensor;
		if (index == lastindex) {
			/* Remove duplicate redundant pressure information */
			if (pressure == lastpressure)
				sample->cylinderpressure.mbar = 0;
			if (o2_pressure == lasto2pressure)
				sample->o2cylinderpressure.mbar = 0;
		}
		lastindex = index;
		lastpressure = pressure;
		lasto2pressure = o2_pressure;
	}
}

/* FIXME! sensor -> cylinder mapping? */
static void fixup_start_pressure(struct dive *dive, int idx, pressure_t p)
{
	if (idx >= 0 && idx < MAX_CYLINDERS) {
		cylinder_t *cyl = dive->cylinder + idx;
		if (p.mbar && !cyl->sample_start.mbar)
			cyl->sample_start = p;
	}
}

static void fixup_end_pressure(struct dive *dive, int idx, pressure_t p)
{
	if (idx >= 0 && idx < MAX_CYLINDERS) {
		cylinder_t *cyl = dive->cylinder + idx;
		if (p.mbar && !cyl->sample_end.mbar)
			cyl->sample_end = p;
	}
}

/*
 * Check the cylinder pressure sample information and fill in the
 * overall cylinder pressures from those.
 *
 * We ignore surface samples for tank pressure information.
 *
 * At the beginning of the dive, let the cylinder cool down
 * if the diver starts off at the surface. And at the end
 * of the dive, there may be surface pressures where the
 * diver has already turned off the air supply (especially
 * for computers like the Uemis Zurich that end up saving
 * quite a bit of samples after the dive has ended).
 */
static void fixup_dive_pressures(struct dive *dive, struct divecomputer *dc)
{
	int i, o2index = -1;

	if (dive->dc.divemode == CCR)
		o2index = get_cylinder_idx_by_use(dive, OXYGEN);

	/* Walk the samples from the beginning to find starting pressures.. */
	for (i = 0; i < dc->samples; i++) {
		struct sample *sample = dc->sample + i;

		if (sample->depth.mm < SURFACE_THRESHOLD)
			continue;

		fixup_start_pressure(dive, sample->sensor, sample->cylinderpressure);
		fixup_start_pressure(dive, o2index, sample->o2cylinderpressure);
	}

	/* ..and from the end for ending pressures */
	for (i = dc->samples; --i >= 0; ) {
		struct sample *sample = dc->sample + i;

		if (sample->depth.mm < SURFACE_THRESHOLD)
			continue;

		fixup_end_pressure(dive, sample->sensor, sample->cylinderpressure);
		fixup_end_pressure(dive, o2index, sample->o2cylinderpressure);
	}

	simplify_dc_pressures(dc);
}

int find_best_gasmix_match(struct gasmix *mix, cylinder_t array[], unsigned int used)
{
	int i;
	int best = -1, score = INT_MAX;

	for (i = 0; i < MAX_CYLINDERS; i++) {
		const cylinder_t *match;
		int distance;

		if (used & (1 << i))
			continue;
		match = array + i;
		if (cylinder_nodata(match))
			continue;
		distance = gasmix_distance(mix, &match->gasmix);
		if (distance >= score)
			continue;
		best = i;
		score = distance;
	}
	return best;
}

/*
 * Match a gas change event against the cylinders we have
 */
static bool validate_gaschange(struct dive *dive, struct event *event)
{
	int index;
	int o2, he, value;

	/* We'll get rid of the per-event gasmix, but for now sanitize it */
	if (gasmix_is_air(&event->gas.mix))
		event->gas.mix.o2.permille = 0;

	/* Do we already have a cylinder index for this gasmix? */
	if (event->gas.index >= 0)
		return true;

	index = find_best_gasmix_match(&event->gas.mix, dive->cylinder, 0);
	if (index < 0)
		return false;

	/* Fix up the event to have the right information */
	event->gas.index = index;
	event->gas.mix = dive->cylinder[index].gasmix;

	/* Convert to odd libdivecomputer format */
	o2 = get_o2(&event->gas.mix);
	he = get_he(&event->gas.mix);

	o2 = (o2 + 5) / 10;
	he = (he + 5) / 10;
	value = o2 + (he << 16);

	event->value = value;
	if (he)
		event->type = SAMPLE_EVENT_GASCHANGE2;

	return true;
}

/* Clean up event, return true if event is ok, false if it should be dropped as bogus */
static bool validate_event(struct dive *dive, struct event *event)
{
	if (event_is_gaschange(event))
		return validate_gaschange(dive, event);
	return true;
}

static void fixup_dc_gasswitch(struct dive *dive, struct divecomputer *dc)
{
	struct event **evp, *event;

	evp = &dc->events;
	while ((event = *evp) != NULL) {
		if (validate_event(dive, event)) {
			evp = &event->next;
			continue;
		}

		/* Delete this event and try the next one */
		*evp = event->next;
	}
}

static void fixup_dive_dc(struct dive *dive, struct divecomputer *dc)
{
	/* Add device information to table */
	if (dc->deviceid && (dc->serial || dc->fw_version))
		create_device_node(dc->model, dc->deviceid, dc->serial, dc->fw_version, "");

	/* Fixup duration and mean depth */
	fixup_dc_duration(dc);

	/* Fix up sample depth data */
	fixup_dc_depths(dive, dc);

	/* Fix up dive temperatures based on dive computer samples */
	fixup_dc_temp(dive, dc);

	/* Fix up gas switch events */
	fixup_dc_gasswitch(dive, dc);

	/* Fix up cylinder pressures based on DC info */
	fixup_dive_pressures(dive, dc);

	fixup_dc_events(dc);
}

struct dive *fixup_dive(struct dive *dive)
{
	int i;
	struct divecomputer *dc;

	sanitize_cylinder_info(dive);
	dive->maxcns = dive->cns;

	/*
	 * Use the dive's temperatures for minimum and maximum in case
	 * we do not have temperatures recorded by DC.
	 */

	update_min_max_temperatures(dive, dive->watertemp);

	for_each_dc (dive, dc)
		fixup_dive_dc(dive, dc);

	fixup_water_salinity(dive);
	fixup_surface_pressure(dive);
	fixup_meandepth(dive);
	fixup_duration(dive);
	fixup_watertemp(dive);
	fixup_airtemp(dive);
	fixup_cylinder_use(dive); // store indices for CCR oxygen and diluent cylinders
	for (i = 0; i < MAX_CYLINDERS; i++) {
		cylinder_t *cyl = dive->cylinder + i;
		add_cylinder_description(&cyl->type);
		if (same_rounded_pressure(cyl->sample_start, cyl->start))
			cyl->start.mbar = 0;
		if (same_rounded_pressure(cyl->sample_end, cyl->end))
			cyl->end.mbar = 0;
	}
	update_cylinder_related_info(dive);
	for (i = 0; i < MAX_WEIGHTSYSTEMS; i++) {
		weightsystem_t *ws = dive->weightsystem + i;
		add_weightsystem_description(ws);
	}
	/* we should always have a uniq ID as that gets assigned during alloc_dive(),
	 * but we want to make sure... */
	if (!dive->id)
		dive->id = dive_getUniqID(dive);

	return dive;
}

/* Don't pick a zero for MERGE_MIN() */
#define MERGE_MAX(res, a, b, n) res->n = MAX(a->n, b->n)
#define MERGE_MIN(res, a, b, n) res->n = (a->n) ? (b->n) ? MIN(a->n, b->n) : (a->n) : (b->n)
#define MERGE_TXT(res, a, b, n) res->n = merge_text(a->n, b->n)
#define MERGE_NONZERO(res, a, b, n) res->n = a->n ? a->n : b->n

struct sample *add_sample(struct sample *sample, int time, struct divecomputer *dc)
{
	struct sample *p = prepare_sample(dc);

	if (p) {
		*p = *sample;
		p->time.seconds = time;
		finish_sample(dc);
	}
	return p;
}

/*
 * This is like add_sample(), but if the distance from the last sample
 * is excessive, we add two surface samples in between.
 *
 * This is so that if you merge two non-overlapping dives, we make sure
 * that the time in between the dives is at the surface, not some "last
 * sample that happened to be at a depth of 1.2m".
 */
static void merge_one_sample(struct sample *sample, int time, struct divecomputer *dc)
{
	int last = dc->samples - 1;
	if (last >= 0) {
		static struct sample surface;
		struct sample *prev = dc->sample + last;
		int last_time = prev->time.seconds;
		int last_depth = prev->depth.mm;

		/*
		 * Only do surface events if the samples are more than
		 * a minute apart, and shallower than 5m
		 */
		if (time > last_time + 60 && last_depth < 5000) {
			add_sample(&surface, last_time + 20, dc);
			add_sample(&surface, time - 20, dc);
		}
	}
	add_sample(sample, time, dc);
}


/*
 * Merge samples. Dive 'a' is "offset" seconds before Dive 'b'
 */
static void merge_samples(struct divecomputer *res, struct divecomputer *a, struct divecomputer *b, int offset)
{
	int asamples = a->samples;
	int bsamples = b->samples;
	struct sample *as = a->sample;
	struct sample *bs = b->sample;

	/*
	 * We want a positive sample offset, so that sample
	 * times are always positive. So if the samples for
	 * 'b' are before the samples for 'a' (so the offset
	 * is negative), we switch a and b around, and use
	 * the reverse offset.
	 */
	if (offset < 0) {
		offset = -offset;
		asamples = bsamples;
		bsamples = a->samples;
		as = bs;
		bs = a->sample;
	}

	for (;;) {
		int at, bt;
		struct sample sample;

		if (!res)
			return;

		at = asamples ? as->time.seconds : -1;
		bt = bsamples ? bs->time.seconds + offset : -1;

		/* No samples? All done! */
		if (at < 0 && bt < 0)
			return;

		/* Only samples from a? */
		if (bt < 0) {
		add_sample_a:
			merge_one_sample(as, at, res);
			as++;
			asamples--;
			continue;
		}

		/* Only samples from b? */
		if (at < 0) {
		add_sample_b:
			merge_one_sample(bs, bt, res);
			bs++;
			bsamples--;
			continue;
		}

		if (at < bt)
			goto add_sample_a;
		if (at > bt)
			goto add_sample_b;

		/* same-time sample: add a merged sample. Take the non-zero ones */
		sample = *bs;
		if (as->depth.mm)
			sample.depth = as->depth;
		if (as->temperature.mkelvin)
			sample.temperature = as->temperature;
		if (as->cylinderpressure.mbar)
			sample.cylinderpressure = as->cylinderpressure;
		if (as->sensor)
			sample.sensor = as->sensor;
		if (as->cns)
			sample.cns = as->cns;
		if (as->setpoint.mbar)
			sample.setpoint = as->setpoint;
		if (as->ndl.seconds)
			sample.ndl = as->ndl;
		if (as->stoptime.seconds)
			sample.stoptime = as->stoptime;
		if (as->stopdepth.mm)
			sample.stopdepth = as->stopdepth;
		if (as->in_deco)
			sample.in_deco = true;

		merge_one_sample(&sample, at, res);

		as++;
		bs++;
		asamples--;
		bsamples--;
	}
}

static char *merge_text(const char *a, const char *b)
{
	char *res;
	if (!a && !b)
		return NULL;
	if (!a || !*a)
		return copy_string(b);
	if (!b || !*b)
		return strdup(a);
	if (!strcmp(a, b))
		return copy_string(a);
	res = malloc(strlen(a) + strlen(b) + 32);
	if (!res)
		return (char *)a;
	sprintf(res, translate("gettextFromC", "(%s) or (%s)"), a, b);
	return res;
}

#define SORT(a, b, field)         \
	if (a->field != b->field) \
		return a->field < b->field ? -1 : 1

static int sort_event(struct event *a, struct event *b)
{
	SORT(a, b, time.seconds);
	SORT(a, b, type);
	SORT(a, b, flags);
	SORT(a, b, value);
	return strcmp(a->name, b->name);
}

static int same_gas(struct event *a, struct event *b)
{
	if (a->type == b->type && a->flags == b->flags && a->value == b->value && !strcmp(a->name, b->name) &&
			a->gas.mix.o2.permille == b->gas.mix.o2.permille && a->gas.mix.he.permille == b->gas.mix.he.permille) {
		return true;
	}
	return false;
}

static void merge_events(struct divecomputer *res, struct divecomputer *src1, struct divecomputer *src2, int offset)
{
	struct event *a, *b;
	struct event **p = &res->events;

	/* Always use positive offsets */
	if (offset < 0) {
		struct divecomputer *tmp;

		offset = -offset;
		tmp = src1;
		src1 = src2;
		src2 = tmp;
	}

	a = src1->events;
	b = src2->events;
	while (b) {
		b->time.seconds += offset;
		b = b->next;
	}
	b = src2->events;

	while (a || b) {
		int s;
		if (!b) {
			*p = a;
			break;
		}
		if (!a) {
			*p = b;
			break;
		}
		s = sort_event(a, b);

		/* No gas change event when continuing with same gas */
		if (same_gas(a, b)) {
			if (s > 0) {
				p = &b->next;
				b = b->next;
			} else {
				p = &a->next;
				a = a->next;
			}
			continue;
		}

		/* Pick b */
		if (s > 0) {
			*p = b;
			p = &b->next;
			b = b->next;
			continue;
		}
		/* Pick 'a' or neither */
		if (s < 0) {
			*p = a;
			p = &a->next;
		}
		a = a->next;
		continue;
	}
}

/* Pick whichever has any info (if either). Prefer 'a' */
static void merge_cylinder_type(cylinder_type_t *src, cylinder_type_t *dst)
{
	if (!dst->size.mliter)
		dst->size.mliter = src->size.mliter;
	if (!dst->workingpressure.mbar)
		dst->workingpressure.mbar = src->workingpressure.mbar;
	if (!dst->description) {
		dst->description = src->description;
		src->description = NULL;
	}
}

static void merge_cylinder_mix(struct gasmix *src, struct gasmix *dst)
{
	if (!dst->o2.permille)
		*dst = *src;
}

static void merge_cylinder_info(cylinder_t *src, cylinder_t *dst)
{
	merge_cylinder_type(&src->type, &dst->type);
	merge_cylinder_mix(&src->gasmix, &dst->gasmix);
	MERGE_MAX(dst, dst, src, start.mbar);
	MERGE_MIN(dst, dst, src, end.mbar);
	if (!dst->cylinder_use)
		dst->cylinder_use = src->cylinder_use;
}

static void merge_weightsystem_info(weightsystem_t *res, weightsystem_t *a, weightsystem_t *b)
{
	if (!a->weight.grams)
		a = b;
	*res = *a;
}

/* get_cylinder_idx_by_use(): Find the index of the first cylinder with a particular CCR use type.
 * The index returned corresponds to that of the first cylinder with a cylinder_use that
 * equals the appropriate enum value [oxygen, diluent, bailout] given by cylinder_use_type.
 * A negative number returned indicates that a match could not be found.
 * Call parameters: dive = the dive being processed
 *                  cylinder_use_type = an enum, one of {oxygen, diluent, bailout} */
extern int get_cylinder_idx_by_use(struct dive *dive, enum cylinderuse cylinder_use_type)
{
	int cylinder_index;
	for (cylinder_index = 0; cylinder_index < MAX_CYLINDERS; cylinder_index++) {
		if (dive->cylinder[cylinder_index].cylinder_use == cylinder_use_type)
			return cylinder_index; // return the index of the cylinder with that cylinder use type
	}
	return -1; // negative number means cylinder_use_type not found in list of cylinders
}

int gasmix_distance(const struct gasmix *a, const struct gasmix *b)
{
	int a_o2 = get_o2(a), b_o2 = get_o2(b);
	int a_he = get_he(a), b_he = get_he(b);
	int delta_o2 = a_o2 - b_o2, delta_he = a_he - b_he;

	delta_he = delta_he * delta_he;
	delta_o2 = delta_o2 * delta_o2;
	return delta_he + delta_o2;
}

/* fill_pressures(): Compute partial gas pressures in bar from gasmix and ambient pressures, possibly for OC or CCR, to be
 * extended to PSCT. This function does the calculations of gas pressures applicable to a single point on the dive profile.
 * The structure "pressures" is used to return calculated gas pressures to the calling software.
 * Call parameters:	po2 = po2 value applicable to the record in calling function
 *			amb_pressure = ambient pressure applicable to the record in calling function
 *			*pressures = structure for communicating o2 sensor values from and gas pressures to the calling function.
 *			*mix = structure containing cylinder gas mixture information.
 * This function called by: calculate_gas_information_new() in profile.c; add_segment() in deco.c.
 */
extern void fill_pressures(struct gas_pressures *pressures, const double amb_pressure, const struct gasmix *mix, double po2, enum dive_comp_type divemode)
{
	if (po2) {	// This is probably a CCR dive where pressures->o2 is defined
		if (po2 >= amb_pressure) {
			pressures->o2 = amb_pressure;
			pressures->n2 = pressures->he = 0.0;
		} else {
			pressures->o2 = po2;
			if (get_o2(mix) == 1000) {
				pressures->he = pressures->n2 = 0;
			} else {
				pressures->he = (amb_pressure - pressures->o2) * (double)get_he(mix) / (1000 - get_o2(mix));
				pressures->n2 = amb_pressure - pressures->o2 - pressures->he;
			}
		}
	} else {
		if (divemode == PSCR) { /* The steady state approximation should be good enough */
			pressures->o2 = get_o2(mix) / 1000.0 * amb_pressure - (1.0 - get_o2(mix) / 1000.0) * prefs.o2consumption / (prefs.bottomsac * prefs.pscr_ratio / 1000.0);
			if (pressures->o2 < 0) // He's dead, Jim.
				pressures->o2 = 0;
			if (get_o2(mix) != 1000) {
				pressures->he = (amb_pressure - pressures->o2) * get_he(mix) / (1000.0 - get_o2(mix));
				pressures->n2 = (amb_pressure - pressures->o2) * (1000 - get_o2(mix) - get_he(mix)) / (1000.0 - get_o2(mix));
			} else {
				pressures->he = pressures->n2 = 0;
			}
		} else {
			// Open circuit dives: no gas pressure values available, they need to be calculated
			pressures->o2 = get_o2(mix) / 1000.0 * amb_pressure; // These calculations are also used if the CCR calculation above..
			pressures->he = get_he(mix) / 1000.0 * amb_pressure; // ..returned a po2 of zero (i.e. o2 sensor data not resolvable)
			pressures->n2 = (1000 - get_o2(mix) - get_he(mix)) / 1000.0 * amb_pressure;
		}
	}
}

static int find_cylinder_match(cylinder_t *cyl, cylinder_t array[], unsigned int used)
{
	if (cylinder_nodata(cyl))
		return -1;
	return find_best_gasmix_match(&cyl->gasmix, array, used);
}

/* Force an initial gaschange event to the (old) gas #0 */
static void add_initial_gaschange(struct dive *dive, struct divecomputer *dc)
{
	struct event *ev = get_next_event(dc->events, "gaschange");

	if (ev && ev->time.seconds < 30)
		return;

	/* Old starting gas mix */
	add_gas_switch_event(dive, dc, 0, 0);
}

void dc_cylinder_renumber(struct dive *dive, struct divecomputer *dc, int mapping[])
{
	int i;
	struct event *ev;

	/* Did the first gas get remapped? Add gas switch event */
	if (mapping[0] > 0)
		add_initial_gaschange(dive, dc);

	/* Remap the sensor indexes */
	for (i = 0; i < dc->samples; i++) {
		struct sample *s = dc->sample + i;
		int sensor;

		sensor = mapping[s->sensor];
		if (sensor >= 0)
			s->sensor = sensor;
	}

	/* Remap the gas change indexes */
	for (ev = dc->events; ev; ev = ev->next) {
		if (!event_is_gaschange(ev))
			continue;
		if (ev->gas.index < 0)
			continue;
		ev->gas.index = mapping[ev->gas.index];
	}
}

/*
 * If the cylinder indexes change (due to merging dives or deleting
 * cylinders in the middle), we need to change the indexes in the
 * dive computer data for this dive.
 *
 * Also note that we assume that the initial cylinder is cylinder 0,
 * so if that got renamed, we need to create a fake gas change event
 */
static void cylinder_renumber(struct dive *dive, int mapping[])
{
	struct divecomputer *dc;
	for_each_dc (dive, dc)
		dc_cylinder_renumber(dive, dc, mapping);
}

static int same_gasmix(struct gasmix *a, struct gasmix *b)
{
	if (gasmix_is_air(a) && gasmix_is_air(b))
		return 1;
	return a->o2.permille == b->o2.permille && a->he.permille == b->he.permille;
}

static int pdiff(pressure_t a, pressure_t b)
{
	return a.mbar && b.mbar && a.mbar != b.mbar;
}

static int different_manual_pressures(cylinder_t *a, cylinder_t *b)
{
	return pdiff(a->start, b->start) || pdiff(a->end, b->end);
}

/*
 * Can we find an exact match for a cylinder in another dive?
 * Take the "already matched" map into account, so that we
 * don't match multiple similar cylinders to one target.
 *
 * To match, the cylinders have to have the same gasmix and the
 * same cylinder use (ie OC/Diluent/Oxygen), and if pressures
 * have been added manually they need to match.
 */
static int match_cylinder(cylinder_t *cyl, struct dive *dive, unsigned int available)
{
	int i;

	for (i = 0; i < MAX_CYLINDERS; i++) {
		cylinder_t *target;

		if (!(available & (1u << i)))
			continue;
		target = dive->cylinder + i;
		if (!same_gasmix(&cyl->gasmix, &target->gasmix))
			continue;
		if (cyl->cylinder_use != target->cylinder_use)
			continue;
		if (different_manual_pressures(cyl, target))
			continue;

		/* FIXME! Should we check sizes too? */
		return i;
	}
	return -1;
}

/*
 * Note: we only allocate from the end, not in holes in the middle.
 * So we don't look for empty bits, we look for "no more bits set".
 * We could use some "find last bit set" math function, but let's
 * not be fancy.
 */
static int find_unused_cylinder(unsigned int used_map)
{
	int i;

	for (i = 0; i < MAX_CYLINDERS; i++) {
		if (!used_map)
			return i;
		used_map >>= 1;
	}
	return -1;
}

/*
 * We matched things up so that they have the same gasmix and
 * use, but we might want to fill in any missing cylinder details
 * in 'a' if we had it from 'b'.
 */
static void merge_one_cylinder(cylinder_t *a, cylinder_t *b)
{
	if (!a->type.size.mliter)
		a->type.size.mliter = b->type.size.mliter;
	if (!a->type.workingpressure.mbar)
		a->type.workingpressure.mbar = b->type.workingpressure.mbar;
	if (!a->type.description && b->type.description)
		a->type.description = strdup(b->type.description);
	if (!a->start.mbar)
		a->start.mbar = b->start.mbar;
	if (!a->end.mbar)
		a->end.mbar = b->end.mbar;
}

/*
 * Merging cylinder information is non-trivial, because the two dive computers
 * may have different ideas of what the different cylinder indexing is.
 *
 * Logic: take all the cylinder information from the preferred dive ('a'), and
 * then try to match each of the cylinders in the other dive by the gasmix that
 * is the best match and hasn't been used yet.
 */
static void merge_cylinders(struct dive *res, struct dive *a, struct dive *b)
{
	int i, last, renumber = 0;
	int mapping[MAX_CYLINDERS];
	unsigned int used_in_a = 0, used_in_b = 0, matched = 0;

	/* Calculate usage map of cylinders */
	for (i = 0; i < MAX_CYLINDERS; i++) {
		if (!cylinder_none(a->cylinder+i) || is_cylinder_used(a, i))
			used_in_a |= 1u << i;
		if (!cylinder_none(b->cylinder+i) || is_cylinder_used(b, i))
			used_in_b |= 1u << i;
	}

	/* For each cylinder in 'b', try to match up things */
	for (i = 0; i < MAX_CYLINDERS; i++) {
		int j;

		mapping[i] = -1;
		if (!(used_in_b & (1u << i)))
			continue;

		j = match_cylinder(b->cylinder+i, a, used_in_a & ~matched);
		if (j < 0)
			continue;

		/*
		 * If we had a successful match, we:
		 *
		 *  - try to merge individual cylinder data from both cases
		 *
		 *  - save that in the mapping table
		 *
		 *  - mark it as matched so that another cylinder in 'b'
		 *    will no longer match
		 *
		 *  - mark 'b' as needing renumbering if the index changed
		 */
		merge_one_cylinder(a->cylinder + j, b->cylinder + i);
		mapping[i] = j;
		matched |= 1u << j;
		if (j != i)
			renumber = 1;
	}

	/*
	 * Consider all the cylinders we matched as used, whether they
	 * originally were or not (either in 'a' or 'b').
	 */
	used_in_a |= matched;

	/* Now copy all the cylinder info raw from 'a' (whether used/matched or not) */
	memcpy(res->cylinder, a->cylinder, sizeof(res->cylinder));
	memset(a->cylinder, 0, sizeof(a->cylinder));

	/*
	 * Go back to 'b' and remap any remaining cylinders that didn't
	 * match completely.
	 */
	for (i = 0; i < MAX_CYLINDERS; i++) {
		int j;

		/* Already remapped, or not interesting? */
		if (mapping[i] >= 0)
			continue;
		if (!(used_in_b & (1u << i)))
			continue;

		j = find_unused_cylinder(used_in_a);
		if (j < 0)
			continue;

		res->cylinder[j] = b->cylinder[i];
		memset(b->cylinder+i, 0, sizeof(cylinder_t));
		mapping[i] = j;
		used_in_a |= 1u << j;
		if (i != j)
			renumber = 1;
	}

	if (renumber)
		cylinder_renumber(b, mapping);
}

static void merge_equipment(struct dive *res, struct dive *a, struct dive *b)
{
	int i;

	merge_cylinders(res, a, b);
	for (i = 0; i < MAX_WEIGHTSYSTEMS; i++)
		merge_weightsystem_info(res->weightsystem + i, a->weightsystem + i, b->weightsystem + i);
}

static void merge_temperatures(struct dive *res, struct dive *a, struct dive *b)
{
	un_fixup_airtemp(a);
	un_fixup_airtemp(b);
	MERGE_NONZERO(res, a, b, airtemp.mkelvin);
	MERGE_NONZERO(res, a, b, watertemp.mkelvin);
}

/*
 * When merging two dives, this picks the trip from one, and removes it
 * from the other.
 *
 * The 'next' dive is not involved in the dive merging, but is the dive
 * that will be the next dive after the merged dive.
 */
static void pick_trip(struct dive *res, struct dive *pick)
{
	tripflag_t tripflag = pick->tripflag;
	dive_trip_t *trip = pick->divetrip;

	res->tripflag = tripflag;
	add_dive_to_trip(res, trip);
}

/*
 * Pick a trip for a dive
 */
static void merge_trip(struct dive *res, struct dive *a, struct dive *b)
{
	dive_trip_t *atrip, *btrip;

	/*
	 * The larger tripflag is more relevant: we prefer
	 * take manually assigned trips over auto-generated
	 * ones.
	 */
	if (a->tripflag > b->tripflag)
		goto pick_a;

	if (a->tripflag < b->tripflag)
		goto pick_b;

	/* Otherwise, look at the trip data and pick the "better" one */
	atrip = a->divetrip;
	btrip = b->divetrip;
	if (!atrip)
		goto pick_b;
	if (!btrip)
		goto pick_a;
	if (!atrip->location)
		goto pick_b;
	if (!btrip->location)
		goto pick_a;
	if (!atrip->notes)
		goto pick_b;
	if (!btrip->notes)
		goto pick_a;

	/*
	 * Ok, so both have location and notes.
	 * Pick the earlier one.
	 */
	if (a->when < b->when)
		goto pick_a;
	goto pick_b;

pick_a:
	b = a;
pick_b:
	pick_trip(res, b);
}

#if CURRENTLY_NOT_USED
/*
 * Sample 's' is between samples 'a' and 'b'. It is 'offset' seconds before 'b'.
 *
 * If 's' and 'a' are at the same time, offset is 0, and b is NULL.
 */
static int compare_sample(struct sample *s, struct sample *a, struct sample *b, int offset)
{
	unsigned int depth = a->depth.mm;
	int diff;

	if (offset) {
		unsigned int interval = b->time.seconds - a->time.seconds;
		unsigned int depth_a = a->depth.mm;
		unsigned int depth_b = b->depth.mm;

		if (offset > interval)
			return -1;

		/* pick the average depth, scaled by the offset from 'b' */
		depth = (depth_a * offset) + (depth_b * (interval - offset));
		depth /= interval;
	}
	diff = s->depth.mm - depth;
	if (diff < 0)
		diff = -diff;
	/* cut off at one meter difference */
	if (diff > 1000)
		diff = 1000;
	return diff * diff;
}

/*
 * Calculate a "difference" in samples between the two dives, given
 * the offset in seconds between them. Use this to find the best
 * match of samples between two different dive computers.
 */
static unsigned long sample_difference(struct divecomputer *a, struct divecomputer *b, int offset)
{
	int asamples = a->samples;
	int bsamples = b->samples;
	struct sample *as = a->sample;
	struct sample *bs = b->sample;
	unsigned long error = 0;
	int start = -1;

	if (!asamples || !bsamples)
		return 0;

	/*
	 * skip the first sample - this way we know can always look at
	 * as/bs[-1] to look at the samples around it in the loop.
	 */
	as++;
	bs++;
	asamples--;
	bsamples--;

	for (;;) {
		int at, bt, diff;


		/* If we run out of samples, punt */
		if (!asamples)
			return INT_MAX;
		if (!bsamples)
			return INT_MAX;

		at = as->time.seconds;
		bt = bs->time.seconds + offset;

		/* b hasn't started yet? Ignore it */
		if (bt < 0) {
			bs++;
			bsamples--;
			continue;
		}

		if (at < bt) {
			diff = compare_sample(as, bs - 1, bs, bt - at);
			as++;
			asamples--;
		} else if (at > bt) {
			diff = compare_sample(bs, as - 1, as, at - bt);
			bs++;
			bsamples--;
		} else {
			diff = compare_sample(as, bs, NULL, 0);
			as++;
			bs++;
			asamples--;
			bsamples--;
		}

		/* Invalid comparison point? */
		if (diff < 0)
			continue;

		if (start < 0)
			start = at;

		error += diff;

		if (at - start > 120)
			break;
	}
	return error;
}

/*
 * Dive 'a' is 'offset' seconds before dive 'b'
 *
 * This is *not* because the dive computers clocks aren't in sync,
 * it is because the dive computers may "start" the dive at different
 * points in the dive, so the sample at time X in dive 'a' is the
 * same as the sample at time X+offset in dive 'b'.
 *
 * For example, some dive computers take longer to "wake up" when
 * they sense that you are under water (ie Uemis Zurich if it was off
 * when the dive started). And other dive computers have different
 * depths that they activate at, etc etc.
 *
 * If we cannot find a shared offset, don't try to merge.
 */
static int find_sample_offset(struct divecomputer *a, struct divecomputer *b)
{
	int offset, best;
	unsigned long max;

	/* No samples? Merge at any time (0 offset) */
	if (!a->samples)
		return 0;
	if (!b->samples)
		return 0;

	/*
	 * Common special-case: merging a dive that came from
	 * the same dive computer, so the samples are identical.
	 * Check this first, without wasting time trying to find
	 * some minimal offset case.
	 */
	best = 0;
	max = sample_difference(a, b, 0);
	if (!max)
		return 0;

	/*
	 * Otherwise, look if we can find anything better within
	 * a thirty second window..
	 */
	for (offset = -30; offset <= 30; offset++) {
		unsigned long diff;

		diff = sample_difference(a, b, offset);
		if (diff > max)
			continue;
		best = offset;
		max = diff;
	}

	return best;
}
#endif

/*
 * Are a and b "similar" values, when given a reasonable lower end expected
 * difference?
 *
 * So for example, we'd expect different dive computers to give different
 * max depth readings. You might have them on different arms, and they
 * have different pressure sensors and possibly different ideas about
 * water salinity etc.
 *
 * So have an expected minimum difference, but also allow a larger relative
 * error value.
 */
static int similar(unsigned long a, unsigned long b, unsigned long expected)
{
	if (!a && !b)
		return 1;

	if (a && b) {
		unsigned long min, max, diff;

		min = a;
		max = b;
		if (a > b) {
			min = b;
			max = a;
		}
		diff = max - min;

		/* Smaller than expected difference? */
		if (diff < expected)
			return 1;
		/* Error less than 10% or the maximum */
		if (diff * 10 < max)
			return 1;
	}
	return 0;
}

/*
 * Match two dive computer entries against each other, and
 * tell if it's the same dive. Return 0 if "don't know",
 * positive for "same dive" and negative for "definitely
 * not the same dive"
 */
int match_one_dc(struct divecomputer *a, struct divecomputer *b)
{
	/* Not same model? Don't know if matching.. */
	if (!a->model || !b->model)
		return 0;
	if (strcasecmp(a->model, b->model))
		return 0;

	/* Different device ID's? Don't know */
	if (a->deviceid != b->deviceid)
		return 0;

	/* Do we have dive IDs? */
	if (!a->diveid || !b->diveid)
		return 0;

	/*
	 * If they have different dive ID's on the same
	 * dive computer, that's a definite "same or not"
	 */
	return a->diveid == b->diveid ? 1 : -1;
}

/*
 * Match every dive computer against each other to see if
 * we have a matching dive.
 *
 * Return values:
 *  -1 for "is definitely *NOT* the same dive"
 *   0 for "don't know"
 *   1 for "is definitely the same dive"
 */
static int match_dc_dive(struct divecomputer *a, struct divecomputer *b)
{
	do {
		struct divecomputer *tmp = b;
		do {
			int match = match_one_dc(a, tmp);
			if (match)
				return match;
			tmp = tmp->next;
		} while (tmp);
		a = a->next;
	} while (a);
	return 0;
}

static bool new_without_trip(struct dive *a)
{
	return a->downloaded && !a->divetrip;
}

/*
 * Do we want to automatically try to merge two dives that
 * look like they are the same dive?
 *
 * This happens quite commonly because you download a dive
 * that you already had, or perhaps because you maintained
 * multiple dive logs and want to load them all together
 * (possibly one of them was imported from another dive log
 * application entirely).
 *
 * NOTE! We mainly look at the dive time, but it can differ
 * between two dives due to a few issues:
 *
 *  - rounding the dive date to the nearest minute in other dive
 *    applications
 *
 *  - dive computers with "relative datestamps" (ie the dive
 *    computer doesn't actually record an absolute date at all,
 *    but instead at download-time synchronizes its internal
 *    time with real-time on the downloading computer)
 *
 *  - using multiple dive computers with different real time on
 *    the same dive
 *
 * We do not merge dives that look radically different, and if
 * the dates are *too* far off the user will have to join two
 * dives together manually. But this tries to handle the sane
 * cases.
 */
static int likely_same_dive(struct dive *a, struct dive *b)
{
	int match, fuzz = 20 * 60;

	/* don't merge manually added dives with anything */
	if (same_string(a->dc.model, "manually added dive") ||
	    same_string(b->dc.model, "manually added dive"))
		return 0;

	/* Don't try to merge dives with different trip information */
	if (a->divetrip != b->divetrip) {
		/*
		 * Exception: if the dive is downloaded without any
		 * explicit trip information, we do want to merge it
		 * with existing old dives even if they have trips.
		 */
		if (!new_without_trip(a) && !new_without_trip(b))
			return 0;
	}

	/*
	 * Do some basic sanity testing of the values we
	 * have filled in during 'fixup_dive()'
	 */
	if (!similar(a->maxdepth.mm, b->maxdepth.mm, 1000) ||
	    (a->meandepth.mm && b->meandepth.mm && !similar(a->meandepth.mm, b->meandepth.mm, 1000)) ||
	    !similar(a->duration.seconds, b->duration.seconds, 5 * 60))
		return 0;

	/* See if we can get an exact match on the dive computer */
	match = match_dc_dive(&a->dc, &b->dc);
	if (match)
		return match > 0;

	/*
	 * Allow a time difference due to dive computer time
	 * setting etc. Check if they overlap.
	 */
	fuzz = MAX(a->duration.seconds, b->duration.seconds) / 2;
	if (fuzz < 60)
		fuzz = 60;

	return ((a->when <= b->when + fuzz) && (a->when >= b->when - fuzz));
}

/*
 * This could do a lot more merging. Right now it really only
 * merges almost exact duplicates - something that happens easily
 * with overlapping dive downloads.
 */
struct dive *try_to_merge(struct dive *a, struct dive *b, bool prefer_downloaded)
{
	if (likely_same_dive(a, b))
		return merge_dives(a, b, 0, prefer_downloaded);
	return NULL;
}

void free_events(struct event *ev)
{
	while (ev) {
		struct event *next = ev->next;
		free(ev);
		ev = next;
	}
}

static void free_dc_contents(struct divecomputer *dc)
{
	free(dc->sample);
	free((void *)dc->model);
	free_events(dc->events);
}

static void free_dc(struct divecomputer *dc)
{
	free_dc_contents(dc);
	free(dc);
}

static void free_pic(struct picture *picture)
{
	if (picture) {
		free(picture->filename);
		free(picture);
	}
}

static int same_sample(struct sample *a, struct sample *b)
{
	if (a->time.seconds != b->time.seconds)
		return 0;
	if (a->depth.mm != b->depth.mm)
		return 0;
	if (a->temperature.mkelvin != b->temperature.mkelvin)
		return 0;
	if (a->cylinderpressure.mbar != b->cylinderpressure.mbar)
		return 0;
	return a->sensor == b->sensor;
}

static int same_dc(struct divecomputer *a, struct divecomputer *b)
{
	int i;
	struct event *eva, *evb;

	i = match_one_dc(a, b);
	if (i)
		return i > 0;

	if (a->when && b->when && a->when != b->when)
		return 0;
	if (a->samples != b->samples)
		return 0;
	for (i = 0; i < a->samples; i++)
		if (!same_sample(a->sample + i, b->sample + i))
			return 0;
	eva = a->events;
	evb = b->events;
	while (eva && evb) {
		if (!same_event(eva, evb))
			return 0;
		eva = eva->next;
		evb = evb->next;
	}
	return eva == evb;
}

static int might_be_same_device(struct divecomputer *a, struct divecomputer *b)
{
	/* No dive computer model? That matches anything */
	if (!a->model || !b->model)
		return 1;

	/* Otherwise at least the model names have to match */
	if (strcasecmp(a->model, b->model))
		return 0;

	/* No device ID? Match */
	if (!a->deviceid || !b->deviceid)
		return 1;

	return a->deviceid == b->deviceid;
}

static void remove_redundant_dc(struct divecomputer *dc, int prefer_downloaded)
{
	do {
		struct divecomputer **p = &dc->next;

		/* Check this dc against all the following ones.. */
		while (*p) {
			struct divecomputer *check = *p;
			if (same_dc(dc, check) || (prefer_downloaded && might_be_same_device(dc, check))) {
				*p = check->next;
				check->next = NULL;
				free_dc(check);
				continue;
			}
			p = &check->next;
		}

		/* .. and then continue down the chain, but we */
		prefer_downloaded = 0;
		dc = dc->next;
	} while (dc);
}

static void clear_dc(struct divecomputer *dc)
{
	memset(dc, 0, sizeof(*dc));
}

static struct divecomputer *find_matching_computer(struct divecomputer *match, struct divecomputer *list)
{
	struct divecomputer *p;

	while ((p = list) != NULL) {
		list = list->next;

		if (might_be_same_device(match, p))
			break;
	}
	return p;
}


static void copy_dive_computer(struct divecomputer *res, struct divecomputer *a)
{
	*res = *a;
	res->model = copy_string(a->model);
	res->samples = res->alloc_samples = 0;
	res->sample = NULL;
	res->events = NULL;
	res->next = NULL;
}

/*
 * Join dive computers with a specific time offset between
 * them.
 *
 * Use the dive computer ID's (or names, if ID's are missing)
 * to match them up. If we find a matching dive computer, we
 * merge them. If not, we just take the data from 'a'.
 */
static void interleave_dive_computers(struct divecomputer *res,
				      struct divecomputer *a, struct divecomputer *b, int offset)
{
	do {
		struct divecomputer *match;

		copy_dive_computer(res, a);

		match = find_matching_computer(a, b);
		if (match) {
			merge_events(res, a, match, offset);
			merge_samples(res, a, match, offset);
			/* Use the diveid of the later dive! */
			if (offset > 0)
				res->diveid = match->diveid;
		} else {
			res->sample = a->sample;
			res->samples = a->samples;
			res->events = a->events;
			a->sample = NULL;
			a->samples = 0;
			a->events = NULL;
		}
		a = a->next;
		if (!a)
			break;
		res->next = calloc(1, sizeof(struct divecomputer));
		res = res->next;
	} while (res);
}


/*
 * Join dive computer information.
 *
 * If we have old-style dive computer information (no model
 * name etc), we will prefer a new-style one and just throw
 * away the old. We're assuming it's a re-download.
 *
 * Otherwise, we'll just try to keep all the information,
 * unless the user has specified that they prefer the
 * downloaded computer, in which case we'll aggressively
 * try to throw out old information that *might* be from
 * that one.
 */
static void join_dive_computers(struct divecomputer *res, struct divecomputer *a, struct divecomputer *b, int prefer_downloaded)
{
	struct divecomputer *tmp;

	if (a->model && !b->model) {
		*res = *a;
		clear_dc(a);
		return;
	}
	if (b->model && !a->model) {
		*res = *b;
		clear_dc(b);
		return;
	}

	*res = *a;
	clear_dc(a);
	tmp = res;
	while (tmp->next)
		tmp = tmp->next;

	tmp->next = calloc(1, sizeof(*tmp));
	*tmp->next = *b;
	clear_dc(b);

	remove_redundant_dc(res, prefer_downloaded);
}

static bool tag_seen_before(struct tag_entry *start, struct tag_entry *before)
{
	while (start && start != before) {
		if (same_string(start->tag->name, before->tag->name))
			return true;
		start = start->next;
	}
	return false;
}

/* remove duplicates and empty nodes */
void taglist_cleanup(struct tag_entry **tag_list)
{
	struct tag_entry **tl = tag_list;
	while (*tl) {
		/* skip tags that are empty or that we have seen before */
		if (same_string((*tl)->tag->name, "") || tag_seen_before(*tag_list, *tl)) {
			*tl = (*tl)->next;
			continue;
		}
		tl = &(*tl)->next;
	}
}

int taglist_get_tagstring(struct tag_entry *tag_list, char *buffer, int len)
{
	int i = 0;
	struct tag_entry *tmp;
	tmp = tag_list;
	memset(buffer, 0, len);
	while (tmp != NULL) {
		int newlength = strlen(tmp->tag->name);
		if (i > 0)
			newlength += 2;
		if ((i + newlength) < len) {
			if (i > 0) {
				strcpy(buffer + i, ", ");
				strcpy(buffer + i + 2, tmp->tag->name);
			} else {
				strcpy(buffer, tmp->tag->name);
			}
		} else {
			return i;
		}
		i += newlength;
		tmp = tmp->next;
	}
	return i;
}

static inline void taglist_free_divetag(struct divetag *tag)
{
	if (tag->name != NULL)
		free(tag->name);
	if (tag->source != NULL)
		free(tag->source);
	free(tag);
}

/* Add a tag to the tag_list, keep the list sorted */
static struct divetag *taglist_add_divetag(struct tag_entry **tag_list, struct divetag *tag)
{
	struct tag_entry *next, *entry;

	while ((next = *tag_list) != NULL) {
		int cmp = strcmp(next->tag->name, tag->name);

		/* Already have it? */
		if (!cmp)
			return next->tag;
		/* Is the entry larger? If so, insert here */
		if (cmp > 0)
			break;
		/* Continue traversing the list */
		tag_list = &next->next;
	}

	/* Insert in front of it */
	entry = malloc(sizeof(struct tag_entry));
	entry->next = next;
	entry->tag = tag;
	*tag_list = entry;
	return tag;
}

struct divetag *taglist_add_tag(struct tag_entry **tag_list, const char *tag)
{
	size_t i = 0;
	int is_default_tag = 0;
	struct divetag *ret_tag, *new_tag;
	const char *translation;
	new_tag = malloc(sizeof(struct divetag));

	for (i = 0; i < sizeof(default_tags) / sizeof(char *); i++) {
		if (strcmp(default_tags[i], tag) == 0) {
			is_default_tag = 1;
			break;
		}
	}
	/* Only translate default tags */
	if (is_default_tag) {
		translation = translate("gettextFromC", tag);
		new_tag->name = malloc(strlen(translation) + 1);
		memcpy(new_tag->name, translation, strlen(translation) + 1);
		new_tag->source = malloc(strlen(tag) + 1);
		memcpy(new_tag->source, tag, strlen(tag) + 1);
	} else {
		new_tag->source = NULL;
		new_tag->name = malloc(strlen(tag) + 1);
		memcpy(new_tag->name, tag, strlen(tag) + 1);
	}
	/* Try to insert new_tag into g_tag_list if we are not operating on it */
	if (tag_list != &g_tag_list) {
		ret_tag = taglist_add_divetag(&g_tag_list, new_tag);
		/* g_tag_list already contains new_tag, free the duplicate */
		if (ret_tag != new_tag)
			taglist_free_divetag(new_tag);
		ret_tag = taglist_add_divetag(tag_list, ret_tag);
	} else {
		ret_tag = taglist_add_divetag(tag_list, new_tag);
		if (ret_tag != new_tag)
			taglist_free_divetag(new_tag);
	}
	return ret_tag;
}

void taglist_free(struct tag_entry *entry)
{
	STRUCTURED_LIST_FREE(struct tag_entry, entry, free)
}

/* Merge src1 and src2, write to *dst */
static void taglist_merge(struct tag_entry **dst, struct tag_entry *src1, struct tag_entry *src2)
{
	struct tag_entry *entry;

	for (entry = src1; entry; entry = entry->next)
		taglist_add_divetag(dst, entry->tag);
	for (entry = src2; entry; entry = entry->next)
		taglist_add_divetag(dst, entry->tag);
}

void taglist_init_global()
{
	size_t i;

	for (i = 0; i < sizeof(default_tags) / sizeof(char *); i++)
		taglist_add_tag(&g_tag_list, default_tags[i]);
}

bool taglist_contains(struct tag_entry *tag_list, const char *tag)
{
	while (tag_list) {
		if (same_string(tag_list->tag->name, tag))
			return true;
		tag_list = tag_list->next;
	}
	return false;
}

// check if all tags in subtl are included in supertl (so subtl is a subset of supertl)
static bool taglist_contains_all(struct tag_entry *subtl, struct tag_entry *supertl)
{
	while (subtl) {
		if (!taglist_contains(supertl, subtl->tag->name))
			return false;
		subtl = subtl->next;
	}
	return true;
}

struct tag_entry *taglist_added(struct tag_entry *original_list, struct tag_entry *new_list)
{
	struct tag_entry *added_list = NULL;
	while (new_list) {
		if (!taglist_contains(original_list, new_list->tag->name))
			taglist_add_tag(&added_list, new_list->tag->name);
		new_list = new_list->next;
	}
	return added_list;
}

void dump_taglist(const char *intro, struct tag_entry *tl)
{
	char *comma = "";
	fprintf(stderr, "%s", intro);
	while(tl) {
		fprintf(stderr, "%s %s", comma, tl->tag->name);
		comma = ",";
		tl = tl->next;
	}
	fprintf(stderr, "\n");
}

// if tl1 is both a subset and superset of tl2 they must be the same
bool taglist_equal(struct tag_entry *tl1, struct tag_entry *tl2)
{
	return taglist_contains_all(tl1, tl2) && taglist_contains_all(tl2, tl1);
}

// count the dives where the tag list contains the given tag
int count_dives_with_tag(const char *tag)
{
	int i, counter = 0;
	struct dive *d;

	for_each_dive (i, d) {
		if (same_string(tag, "")) {
			// count dives with no tags
			if (d->tag_list == NULL)
				counter++;
		} else if (taglist_contains(d->tag_list, tag)) {
			counter++;
		}
	}
	return counter;
}

extern bool string_sequence_contains(const char *string_sequence, const char *text);

// count the dives where the person is included in the comma separated string sequences of buddies or divemasters
int count_dives_with_person(const char *person)
{
	int i, counter = 0;
	struct dive *d;

	for_each_dive (i, d) {
		if (same_string(person, "")) {
			// solo dive
			if (same_string(d->buddy, "") && same_string(d->divemaster, ""))
				counter++;
		} else if (string_sequence_contains(d->buddy, person) || string_sequence_contains(d->divemaster, person)) {
			counter++;
		}
	}
	return counter;
}

// count the dives with exactly the location
int count_dives_with_location(const char *location)
{
	int i, counter = 0;
	struct dive *d;

	for_each_dive (i, d) {
		if (same_string(get_dive_location(d), location))
			counter++;
	}
	return counter;
}

// count the dives with exactly the suit
int count_dives_with_suit(const char *suit)
{
	int i, counter = 0;
	struct dive *d;

	for_each_dive (i, d) {
		if (same_string(d->suit, suit))
			counter++;
	}
	return counter;
}

/*
 * Merging two dives can be subtle, because there's two different ways
 * of merging:
 *
 * (a) two distinctly _different_ dives that have the same dive computer
 *     are merged into one longer dive, because the user asked for it
 *     in the divelist.
 *
 *     Because this case is with the same dive computer, we *know* the
 *     two must have a different start time, and "offset" is the relative
 *     time difference between the two.
 *
 * (a) two different dive computers that we might want to merge into
 *     one single dive with multiple dive computers.
 *
 *     This is the "try_to_merge()" case, which will have offset == 0,
 *     even if the dive times might be different.
 */
struct dive *merge_dives(struct dive *a, struct dive *b, int offset, bool prefer_downloaded)
{
	struct dive *res = alloc_dive();
	struct dive *dl = NULL;

	if (offset) {
		/*
		 * If "likely_same_dive()" returns true, that means that
		 * it is *not* the same dive computer, and we do not want
		 * to try to turn it into a single longer dive. So we'd
		 * join them as two separate dive computers at zero offset.
		 */
		if (likely_same_dive(a, b))
			offset = 0;
	} else {
		/* Aim for newly downloaded dives to be 'b' (keep old dive data first) */
		if (a->downloaded && !b->downloaded) {
			struct dive *tmp = a;
			a = b;
			b = tmp;
		}
		if (prefer_downloaded && b->downloaded)
			dl = b;
	}

	if (same_string(a->dc.model, "planned dive")) {
		struct dive *tmp = a;
		a = b;
		b = tmp;
	}
	res->when = dl ? dl->when : a->when;
	res->selected = a->selected || b->selected;
	merge_trip(res, a, b);
	MERGE_TXT(res, a, b, notes);
	MERGE_TXT(res, a, b, buddy);
	MERGE_TXT(res, a, b, divemaster);
	MERGE_MAX(res, a, b, rating);
	MERGE_TXT(res, a, b, suit);
	MERGE_MAX(res, a, b, number);
	MERGE_NONZERO(res, a, b, cns);
	MERGE_NONZERO(res, a, b, visibility);
	MERGE_NONZERO(res, a, b, picture_list);
	taglist_merge(&res->tag_list, a->tag_list, b->tag_list);
	merge_equipment(res, a, b);
	merge_temperatures(res, a, b);
	if (dl) {
		/* If we prefer downloaded, do those first, and get rid of "might be same" computers */
		join_dive_computers(&res->dc, &dl->dc, &a->dc, 1);
	} else if (offset && might_be_same_device(&a->dc, &b->dc))
		interleave_dive_computers(&res->dc, &a->dc, &b->dc, offset);
	else
		join_dive_computers(&res->dc, &a->dc, &b->dc, 0);
	/* we take the first dive site, unless it's empty */
	if (a->dive_site_uuid && !dive_site_is_empty(get_dive_site_by_uuid(a->dive_site_uuid)))
		res->dive_site_uuid = a->dive_site_uuid;
	else
		res->dive_site_uuid = b->dive_site_uuid;
	fixup_dive(res);
	return res;
}

// copy_dive(), but retaining the new ID for the copied dive
static struct dive *create_new_copy(struct dive *from)
{
	struct dive *to = alloc_dive();
	int id;

	// alloc_dive() gave us a new ID, we just need to
	// make sure it's not overwritten.
	id = to->id;
	copy_dive(from, to);
	to->id = id;
	return to;
}

static void force_fixup_dive(struct dive *d)
{
	struct divecomputer *dc = &d->dc;
	int old_temp = dc->watertemp.mkelvin;
	int old_mintemp = d->mintemp.mkelvin;
	int old_maxtemp = d->maxtemp.mkelvin;
	duration_t old_duration = d->duration;

	d->maxdepth.mm = 0;
	dc->maxdepth.mm = 0;
	d->watertemp.mkelvin = 0;
	dc->watertemp.mkelvin = 0;
	d->duration.seconds = 0;
	d->maxtemp.mkelvin = 0;
	d->mintemp.mkelvin = 0;

	fixup_dive(d);

	if (!d->watertemp.mkelvin)
		d->watertemp.mkelvin = old_temp;

	if (!dc->watertemp.mkelvin)
		dc->watertemp.mkelvin = old_temp;

	if (!d->maxtemp.mkelvin)
		d->maxtemp.mkelvin = old_maxtemp;

	if (!d->mintemp.mkelvin)
		d->mintemp.mkelvin = old_mintemp;

	if (!d->duration.seconds)
		d->duration = old_duration;

}

/*
 * Split a dive that has a surface interval from samples 'a' to 'b'
 * into two dives.
 */
static int split_dive_at(struct dive *dive, int a, int b)
{
	int i, nr;
	uint32_t t;
	struct dive *d1, *d2;
	struct divecomputer *dc1, *dc2;
	struct event *event, **evp;

	/* if we can't find the dive in the dive list, don't bother */
	if ((nr = get_divenr(dive)) < 0)
		return 0;

	/* We're not trying to be efficient here.. */
	d1 = create_new_copy(dive);
	d2 = create_new_copy(dive);

	/* now unselect the first first segment so we don't keep all
	 * dives selected by mistake. But do keep the second one selected
	 * so the algorithm keeps splitting the dive further */
	d1->selected = false;

	dc1 = &d1->dc;
	dc2 = &d2->dc;
	/*
	 * Cut off the samples of d1 at the beginning
	 * of the interval.
	 */
	dc1->samples = a;

	/* And get rid of the 'b' first samples of d2 */
	dc2->samples -= b;
	memmove(dc2->sample, dc2->sample+b, dc2->samples * sizeof(struct sample));

	/*
	 * This is where we cut off events from d1,
	 * and shift everything in d2
	 */
	t = dc2->sample[0].time.seconds;
	d2->when += t;
	for (i = 0; i < dc2->samples; i++)
		dc2->sample[i].time.seconds -= t;

	/* Remove the events past 't' from d1 */
	evp = &dc1->events;
	while ((event = *evp) != NULL && event->time.seconds < t)
		evp = &event->next;
	*evp = NULL;
	while (event) {
		struct event *next = event->next;
		free(event);
		event = next;
	}

	/* Remove the events before 't' from d2, and shift the rest */
	evp = &dc2->events;
	while ((event = *evp) != NULL) {
		if (event->time.seconds < t) {
			*evp = event->next;
			free(event);
		} else {
			event->time.seconds -= t;
		}
	}

	force_fixup_dive(d1);
	force_fixup_dive(d2);

	if (dive->divetrip) {
		d1->divetrip = d2->divetrip = 0;
		add_dive_to_trip(d1, dive->divetrip);
		add_dive_to_trip(d2, dive->divetrip);
	}

	delete_single_dive(nr);
	add_single_dive(nr, d1);

	/*
	 * Was the dive numbered? If it was the last dive, then we'll
	 * increment the dive number for the tail part that we split off.
	 * Otherwise the tail is unnumbered.
	 */
	if (d2->number) {
		if (dive_table.nr == nr + 1)
			d2->number++;
		else
			d2->number = 0;
	}
	add_single_dive(nr + 1, d2);

	mark_divelist_changed(true);

	return 1;
}

/* in freedive mode we split for as little as 10 seconds on the surface,
 * otherwise we use a minute */
static bool should_split(struct divecomputer *dc, int t1, int t2)
{
	int threshold = dc->divemode == FREEDIVE ? 10 : 60;

	return t2 - t1 >= threshold;
}

/*
 * Try to split a dive into multiple dives at a surface interval point.
 *
 * NOTE! We will not split dives with multiple dive computers, and
 * only split when there is at least one surface event that has
 * non-surface events on both sides.
 *
 * In other words, this is a (simplified) reversal of the dive merging.
 */
int split_dive(struct dive *dive)
{
	int i;
	int at_surface, surface_start;
	struct divecomputer *dc;

	if (!dive || (dc = &dive->dc)->next)
		return 0;

	surface_start = 0;
	at_surface = 1;
	for (i = 1; i < dc->samples; i++) {
		struct sample *sample = dc->sample+i;
		int surface_sample = sample->depth.mm < SURFACE_THRESHOLD;

		/*
		 * We care about the transition from and to depth 0,
		 * not about the depth staying similar.
		 */
		if (at_surface == surface_sample)
			continue;
		at_surface = surface_sample;

		// Did it become surface after having been non-surface? We found the start
		if (at_surface) {
			surface_start = i;
			continue;
		}

		// Going down again? We want at least a minute from
		// the surface start.
		if (!surface_start)
			continue;
		if (!should_split(dc, dc->sample[surface_start].time.seconds, sample[i - 1].time.seconds))
			continue;

		return split_dive_at(dive, surface_start, i-1);
	}
	return 0;
}

/*
 * "dc_maxtime()" is how much total time this dive computer
 * has for this dive. Note that it can differ from "duration"
 * if there are surface events in the middle.
 *
 * Still, we do ignore all but the last surface samples from the
 * end, because some divecomputers just generate lots of them.
 */
static inline int dc_totaltime(const struct divecomputer *dc)
{
	int time = dc->duration.seconds;
	int nr = dc->samples;

	while (nr--) {
		struct sample *s = dc->sample + nr;
		time = s->time.seconds;
		if (s->depth.mm >= SURFACE_THRESHOLD)
			break;
	}
	return time;
}

/*
 * The end of a dive is actually not trivial, because "duration"
 * is not the duration until the end, but the time we spend under
 * water, which can be very different if there are surface events
 * during the dive.
 *
 * So walk the dive computers, looking for the longest actual
 * time in the samples (and just default to the dive duration if
 * there are no samples).
 */
static inline int dive_totaltime(const struct dive *dive)
{
	int time =  dive->duration.seconds;
	const struct divecomputer *dc;

	for_each_dc(dive, dc) {
		int dc_time = dc_totaltime(dc);
		if (dc_time > time)
			time = dc_time;
	}
	return time;
}

timestamp_t dive_endtime(const struct dive *dive)
{
	return dive->when + dive_totaltime(dive);
}

struct dive *find_dive_including(timestamp_t when)
{
	int i;
	struct dive *dive;

	/* binary search, anyone? Too lazy for now;
	 * also we always use the duration from the first divecomputer
	 *     could this ever be a problem? */
	for_each_dive (i, dive) {
		if (dive->when <= when && when <= dive_endtime(dive))
			return dive;
	}
	return NULL;
}

bool time_during_dive_with_offset(struct dive *dive, timestamp_t when, timestamp_t offset)
{
	timestamp_t start = dive->when;
	timestamp_t end = dive_endtime(dive);
	return start - offset <= when && when <= end + offset;
}

bool dive_within_time_range(struct dive *dive, timestamp_t when, timestamp_t offset)
{
	timestamp_t start = dive->when;
	timestamp_t end = dive_endtime(dive);
	return when - offset <= start && end <= when + offset;
}

/* find the n-th dive that is part of a group of dives within the offset around 'when'.
 *  How is that for a vague definition of what this function should do... */
struct dive *find_dive_n_near(timestamp_t when, int n, timestamp_t offset)
{
	int i, j = 0;
	struct dive *dive;

	for_each_dive (i, dive) {
		if (dive_within_time_range(dive, when, offset))
			if (++j == n)
				return dive;
	}
	return NULL;
}

void shift_times(const timestamp_t amount)
{
	int i;
	struct dive *dive;

	for_each_dive (i, dive) {
		if (!dive->selected)
			continue;
		dive->when += amount;
		invalidate_dive_cache(dive);
	}
}

timestamp_t get_times()
{
	int i;
	struct dive *dive;

	for_each_dive (i, dive) {
		if (dive->selected)
			break;
	}
	return dive->when;
}

void set_userid(char *rUserId)
{
	if (prefs.userid)
		free(prefs.userid);
	prefs.userid = strdup(rUserId);
	if (strlen(prefs.userid) > 30)
		prefs.userid[30]='\0';
}

/* this sets a usually unused copy of the preferences with the units
 * that were active the last time the dive list was saved to git storage
 * (this isn't used in XML files); storing the unit preferences in the
 * data file is usually pointless (that's a setting of the software,
 * not a property of the data), but it's a great hint of what the user
 * might expect to see when creating a backend service that visualizes
 * the dive list without Subsurface running - so this is basically a
 * functionality for the core library that Subsurface itself doesn't
 * use but that another consumer of the library (like an HTML exporter)
 * will need */
void set_informational_units(char *units)
{
	if (strstr(units, "METRIC")) {
		git_prefs.unit_system = METRIC;
	} else if (strstr(units, "IMPERIAL")) {
		git_prefs.unit_system = IMPERIAL;
	} else if (strstr(units, "PERSONALIZE")) {
		git_prefs.unit_system = PERSONALIZE;
		if (strstr(units, "METERS"))
			git_prefs.units.length = METERS;
		if (strstr(units, "FEET"))
			git_prefs.units.length = FEET;
		if (strstr(units, "LITER"))
			git_prefs.units.volume = LITER;
		if (strstr(units, "CUFT"))
			git_prefs.units.volume = CUFT;
		if (strstr(units, "BAR"))
			git_prefs.units.pressure = BAR;
		if (strstr(units, "PSI"))
			git_prefs.units.pressure = PSI;
		if (strstr(units, "PASCAL"))
			git_prefs.units.pressure = PASCAL;
		if (strstr(units, "CELSIUS"))
			git_prefs.units.temperature = CELSIUS;
		if (strstr(units, "FAHRENHEIT"))
			git_prefs.units.temperature = FAHRENHEIT;
		if (strstr(units, "KG"))
			git_prefs.units.weight = KG;
		if (strstr(units, "LBS"))
			git_prefs.units.weight = LBS;
		if (strstr(units, "SECONDS"))
			git_prefs.units.vertical_speed_time = SECONDS;
		if (strstr(units, "MINUTES"))
			git_prefs.units.vertical_speed_time = MINUTES;
	}

}

void set_git_prefs(char *prefs)
{
	if (strstr(prefs, "TANKBAR"))
		git_prefs.tankbar = 1;
	if (strstr(prefs, "DCCEILING"))
		git_prefs.dcceiling = 1;
	if (strstr(prefs, "SHOW_SETPOINT"))
		git_prefs.show_ccr_setpoint = 1;
	if (strstr(prefs, "SHOW_SENSORS"))
		git_prefs.show_ccr_sensors = 1;
	if (strstr(prefs, "PO2_GRAPH"))
		git_prefs.pp_graphs.po2 = 1;
}

void average_max_depth(struct diveplan *dive, int *avg_depth, int *max_depth)
{
	int integral = 0;
	int last_time = 0;
	int last_depth = 0;
	struct divedatapoint *dp = dive->dp;

	*max_depth = 0;

	while (dp) {
		if (dp->time) {
			/* Ignore gas indication samples */
			integral += (dp->depth + last_depth) * (dp->time - last_time) / 2;
			last_time = dp->time;
			last_depth = dp->depth;
			if (dp->depth > *max_depth)
				*max_depth = dp->depth;
		}
		dp = dp->next;
	}
	if (last_time)
		*avg_depth = integral / last_time;
	else
		*avg_depth = *max_depth = 0;
}

struct picture *alloc_picture()
{
	struct picture *pic = malloc(sizeof(struct picture));
	if (!pic)
		exit(1);
	memset(pic, 0, sizeof(struct picture));
	return pic;
}

static bool new_picture_for_dive(struct dive *d, const char *filename)
{
	FOR_EACH_PICTURE (d) {
		if (same_string(picture->filename, filename))
			return false;
	}
	return true;
}

// only add pictures that have timestamps between 30 minutes before the dive and
// 30 minutes after the dive ends
#define D30MIN (30 * 60)
bool dive_check_picture_time(struct dive *d, int shift_time, timestamp_t timestamp)
{
	offset_t offset;
	if (timestamp) {
		offset.seconds = timestamp - d->when + shift_time;
		if (offset.seconds > -D30MIN && offset.seconds < dive_totaltime(d) + D30MIN) {
			// this picture belongs to this dive
			return true;
		}
	}
	return false;
}

bool picture_check_valid(const char *filename, int shift_time)
{
	int i;
	struct dive *dive;

	timestamp_t timestamp = picture_get_timestamp(filename);
	for_each_dive (i, dive)
		if (dive->selected && dive_check_picture_time(dive, shift_time, timestamp))
			return true;
	return false;
}

void dive_create_picture(struct dive *dive, const char *filename, int shift_time, bool match_all)
{
	timestamp_t timestamp = picture_get_timestamp(filename);
	if (!new_picture_for_dive(dive, filename))
		return;
	if (!match_all && !dive_check_picture_time(dive, shift_time, timestamp))
		return;

	struct picture *picture = alloc_picture();
	picture->filename = strdup(filename);
	picture->offset.seconds = timestamp - dive->when + shift_time;
	picture_load_exif_data(picture);

	dive_add_picture(dive, picture);
	dive_set_geodata_from_picture(dive, picture);
	invalidate_dive_cache(dive);
}

void dive_add_picture(struct dive *dive, struct picture *newpic)
{
	struct picture **pic_ptr = &dive->picture_list;
	/* let's keep the list sorted by time */
	while (*pic_ptr && (*pic_ptr)->offset.seconds <= newpic->offset.seconds)
		pic_ptr = &(*pic_ptr)->next;
	newpic->next = *pic_ptr;
	*pic_ptr = newpic;
	cache_picture(newpic);
	return;
}

unsigned int dive_get_picture_count(struct dive *dive)
{
	unsigned int i = 0;
	FOR_EACH_PICTURE (dive)
		i++;
	return i;
}

void dive_set_geodata_from_picture(struct dive *dive, struct picture *picture)
{
	struct dive_site *ds = get_dive_site_by_uuid(dive->dive_site_uuid);
	if (!dive_site_has_gps_location(ds) && (picture->latitude.udeg || picture->longitude.udeg)) {
		if (ds) {
			ds->latitude = picture->latitude;
			ds->longitude = picture->longitude;
		} else {
			dive->dive_site_uuid = create_dive_site_with_gps("", picture->latitude, picture->longitude, dive->when);
			invalidate_dive_cache(dive);
		}
	}
}

void picture_free(struct picture *picture)
{
	if (!picture)
		return;
	free(picture->filename);
	free(picture->hash);
	free(picture);
}

// When handling pictures in different threads, we need to copy them so we don't
// run into problems when the main thread frees the picture.

struct picture *clone_picture(struct picture *src)
{
	struct picture *dst;

	dst = alloc_picture();
	copy_pl(src, dst);
	return dst;
}

void dive_remove_picture(char *filename)
{
	struct picture **picture = &current_dive->picture_list;
	while (picture && !same_string((*picture)->filename, filename))
		picture = &(*picture)->next;
	if (picture) {
		struct picture *temp = (*picture)->next;
		picture_free(*picture);
		*picture = temp;
		invalidate_dive_cache(current_dive);
	}
}

/* this always acts on the current divecomputer of the current dive */
void make_first_dc()
{
	struct divecomputer *dc = &current_dive->dc;
	struct divecomputer *newdc = malloc(sizeof(*newdc));
	struct divecomputer *cur_dc = current_dc; /* needs to be in a local variable so the macro isn't re-executed */

	/* skip the current DC in the linked list */
	while (dc && dc->next != cur_dc)
		dc = dc->next;
	if (!dc) {
		free(newdc);
		fprintf(stderr, "data inconsistent: can't find the current DC");
		return;
	}
	dc->next = cur_dc->next;
	*newdc = current_dive->dc;
	current_dive->dc = *cur_dc;
	current_dive->dc.next = newdc;
	free(cur_dc);
	invalidate_dive_cache(current_dive);
}

/* always acts on the current dive */
unsigned int count_divecomputers(void)
{
	int ret = 1;
	struct divecomputer *dc = current_dive->dc.next;
	while (dc) {
		ret++;
		dc = dc->next;
	}
	return ret;
}

/* always acts on the current dive */
void delete_current_divecomputer(void)
{
	struct divecomputer *dc = current_dc;

	if (dc == &current_dive->dc) {
		/* remove the first one, so copy the second one in place of the first and free the second one
		 * be careful about freeing the no longer needed structures - since we copy things around we can't use free_dc()*/
		struct divecomputer *fdc = dc->next;
		free(dc->sample);
		free((void *)dc->model);
		free_events(dc->events);
		memcpy(dc, fdc, sizeof(struct divecomputer));
		free(fdc);
	} else {
		struct divecomputer *pdc = &current_dive->dc;
		while (pdc->next != dc && pdc->next)
			pdc = pdc->next;
		if (pdc->next == dc) {
			pdc->next = dc->next;
			free_dc(dc);
		}
	}
	if (dc_number == count_divecomputers())
		dc_number--;
	invalidate_dive_cache(current_dive);
}

/* helper function to make it easier to work with our structures
 * we don't interpolate here, just use the value from the last sample up to that time */
int get_depth_at_time(struct divecomputer *dc, unsigned int time)
{
	int depth = 0;
	if (dc && dc->sample)
		for (int i = 0; i < dc->samples; i++) {
			if (dc->sample[i].time.seconds > time)
				break;
			depth = dc->sample[i].depth.mm;
		}
	return depth;
}

//Calculate O2 in best mix
fraction_t best_o2(depth_t depth, struct dive *dive)
{
	fraction_t fo2;

	fo2.permille = (prefs.bottompo2 * 100 / depth_to_mbar(depth.mm, dive)) * 10;	//use integer arithmetic to round down to nearest percent
	// Don't permit >100% O2
	if (fo2.permille > 1000)
		fo2.permille = 1000;
	return fo2;
}

//Calculate He in best mix. O2 is considered narcopic
fraction_t best_he(depth_t depth, struct dive *dive)
{
	fraction_t fhe;
	int pnarcotic, ambient;
	pnarcotic = depth_to_mbar(prefs.bestmixend.mm, dive);
	ambient = depth_to_mbar(depth.mm, dive);
	fhe.permille = (100 - 100 * pnarcotic / ambient) * 10;	//use integer arithmetic to round up to nearest percent
	if (fhe.permille < 0)
		fhe.permille = 0;
	return fhe;
}
