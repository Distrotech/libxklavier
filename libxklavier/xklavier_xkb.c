#include <time.h>
#include <stdlib.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlibint.h>

#include "xklavier_private.h"
#include "xklavier_private_xkb.h"

#ifdef XKB_HEADERS_PRESENT
XkbDescPtr xkl_xkb_desc;

static XkbDescPtr precached_xkb = NULL;

gchar *xkl_indicator_names[XkbNumIndicators];

gint xkl_xkb_event_type, xkl_xkb_error_code;

static gchar *group_names[XkbNumKbdGroups];

const gchar **
xkl_xkb_groups_get_names(void)
{
	return (const gchar **) group_names;
}

gint
xkl_xkb_listen_pause(void)
{
	XkbSelectEvents(xkl_display, XkbUseCoreKbd, XkbAllEventsMask, 0);
/*  XkbSelectEventDetails( xkl_display,
                         XkbUseCoreKbd,
                       XkbStateNotify,
                     0,
                   0 );

  !!_XklSelectInput( _xklRootWindow, 0 );
*/
	return 0;
}

gint
xkl_xkb_listen_resume(void)
{
	/* What events we want */
#define XKB_EVT_MASK \
         (XkbStateNotifyMask| \
          XkbNamesNotifyMask| \
          XkbControlsNotifyMask| \
          XkbIndicatorStateNotifyMask| \
          XkbIndicatorMapNotifyMask| \
          XkbNewKeyboardNotifyMask)

	XkbSelectEvents(xkl_display, XkbUseCoreKbd, XKB_EVT_MASK,
			XKB_EVT_MASK);

#define XKB_STATE_EVT_DTL_MASK \
         (XkbGroupStateMask)

	XkbSelectEventDetails(xkl_display,
			      XkbUseCoreKbd,
			      XkbStateNotify,
			      XKB_STATE_EVT_DTL_MASK,
			      XKB_STATE_EVT_DTL_MASK);

#define XKB_NAMES_EVT_DTL_MASK \
         (XkbGroupNamesMask|XkbIndicatorNamesMask)

	XkbSelectEventDetails(xkl_display,
			      XkbUseCoreKbd,
			      XkbNamesNotify,
			      XKB_NAMES_EVT_DTL_MASK,
			      XKB_NAMES_EVT_DTL_MASK);
	return 0;
}

guint
xkl_xkb_groups_get_max_num(void)
{
	return xkl_vtable->features & XKLF_MULTIPLE_LAYOUTS_SUPPORTED ?
	    XkbNumKbdGroups : 1;
}

guint
xkl_xkb_groups_get_num(void)
{
	return xkl_xkb_desc->ctrls->num_groups;
}

#define KBD_MASK \
    ( 0 )
#define CTRLS_MASK \
  ( XkbSlowKeysMask )
#define NAMES_MASK \
  ( XkbGroupNamesMask | XkbIndicatorNamesMask )

void
xkl_xkb_free_all_info(void)
{
	gint i;
	gchar **pi = xkl_indicator_names;
	for (i = 0; i < XkbNumIndicators; i++, pi++) {
		/* only free non-empty ones */
		if (*pi && **pi)
			XFree(*pi);
	}
	if (xkl_xkb_desc != NULL) {
		int i;
		char **group_name = group_names;
		for (i = xkl_xkb_desc->ctrls->num_groups; --i >= 0;
		     group_name++)
			if (*group_name) {
				XFree(*group_name);
				*group_name = NULL;
			}
		XkbFreeKeyboard(xkl_xkb_desc, XkbAllComponentsMask, True);
		xkl_xkb_desc = NULL;
	}

	/* just in case - never actually happens... */
	if (precached_xkb != NULL) {
		XkbFreeKeyboard(precached_xkb, XkbAllComponentsMask, True);
		precached_xkb = NULL;
	}
}

static gboolean
xkl_xkb_load_precached_xkb(void)
{
	gboolean rv = FALSE;
	Status status;

	precached_xkb = XkbGetMap(xkl_display, KBD_MASK, XkbUseCoreKbd);
	if (precached_xkb != NULL) {
		rv = Success == (status = XkbGetControls(xkl_display,
							 CTRLS_MASK,
							 precached_xkb)) &&
		    Success == (status = XkbGetNames(xkl_display,
						     NAMES_MASK,
						     precached_xkb)) &&
		    Success == (status = XkbGetIndicatorMap(xkl_display,
							    XkbAllIndicatorsMask,
							    precached_xkb));
		if (!rv) {
			xkl_last_error_message =
			    "Could not load controls/names/indicators";
			xkl_debug(0, "%s: %d\n", xkl_last_error_message,
				  status);
			XkbFreeKeyboard(precached_xkb,
					XkbAllComponentsMask, True);
		}
	}
	return rv;
}

gboolean
xkl_xkb_if_cached_info_equals_actual(void)
{
	gint i;
	Atom *pa1, *pa2;
	gboolean rv = FALSE;

	if (xkl_xkb_load_precached_xkb()) {
		/* First, compare the number of groups */
		if (xkl_xkb_desc->ctrls->num_groups ==
		    precached_xkb->ctrls->num_groups) {
			/* Then, compare group names, just atoms */
			pa1 = xkl_xkb_desc->names->groups;
			pa2 = precached_xkb->names->groups;
			for (i = xkl_xkb_desc->ctrls->num_groups; --i >= 0;
			     pa1++, pa2++)
				if (*pa1 != *pa2)
					break;

			/* Then, compare indicator names, just atoms */
			if (i < 0) {
				pa1 = xkl_xkb_desc->names->indicators;
				pa2 = precached_xkb->names->indicators;
				for (i = XkbNumIndicators; --i >= 0;
				     pa1++, pa2++)
					if (*pa1 != *pa2)
						break;
				rv = i < 0;
			}
		}
    /** 
     * in case of failure, reuse in _XklXkbLoadAllInfo
     * in case of success - free it
     */
		if (rv) {
			XkbFreeKeyboard(precached_xkb,
					XkbAllComponentsMask, True);
			precached_xkb = NULL;
		}
	} else {
		xkl_debug(0,
			  "Could not load the XkbDescPtr for comparison\n");
	}
	return rv;
}

/**
 * Load some XKB parameters
 */
gboolean
xkl_xkb_load_all_info(void)
{
	gint i;
	Atom *pa;
	gchar **group_name;
	gchar **pi = xkl_indicator_names;

	if (precached_xkb == NULL)
		if (!xkl_xkb_load_precached_xkb()) {
			xkl_last_error_message = "Could not load keyboard";
			return FALSE;
		}

	/* take it from the cache (in most cases LoadAll is called from ResetAll which in turn ...) */
	xkl_xkb_desc = precached_xkb;
	precached_xkb = NULL;

	/* First, output the number of the groups */
	xkl_debug(200, "found %d groups\n",
		  xkl_xkb_desc->ctrls->num_groups);

	/* Then, cache (and output) the names of the groups */
	pa = xkl_xkb_desc->names->groups;
	group_name = group_names;
	for (i = xkl_xkb_desc->ctrls->num_groups; --i >= 0;
	     pa++, group_name++) {
		*group_name =
		    XGetAtomName(xkl_display,
				 *pa == None ? XInternAtom(xkl_display,
							   "-",
							   False) : *pa);
		xkl_debug(200, "Group %d has name [%s]\n", i, *group_name);
	}

	xkl_last_error_code =
	    XkbGetIndicatorMap(xkl_display, XkbAllIndicatorsMask,
			       xkl_xkb_desc);

	if (xkl_last_error_code != Success) {
		xkl_last_error_message = "Could not load indicator map";
		return FALSE;
	}

	/* Then, cache (and output) the names of the indicators */
	pa = xkl_xkb_desc->names->indicators;
	for (i = XkbNumIndicators; --i >= 0; pi++, pa++) {
		Atom a = *pa;
		if (a != None)
			*pi = XGetAtomName(xkl_display, a);
		else
			*pi = "";

		xkl_debug(200, "Indicator[%d] is %s\n", i, *pi);
	}

	xkl_debug(200, "Real indicators are %X\n",
		  xkl_xkb_desc->indicators->phys_indicators);

	if (xkl_config_callback != NULL)
		(*xkl_config_callback) (xkl_config_callback_data);
	return TRUE;
}

void
xkl_xkb_group_lock(int group)
{
	xkl_debug(100, "Posted request for change the group to %d ##\n",
		  group);
	XkbLockGroup(xkl_display, XkbUseCoreKbd, group);
	XSync(xkl_display, False);
}

/**
 * Updates current internal state from X state
 */
void
xkl_xkb_get_server_state(XklState * current_state_out)
{
	XkbStateRec state;

	current_state_out->group = 0;
	if (Success == XkbGetState(xkl_display, XkbUseCoreKbd, &state))
		current_state_out->group = state.locked_group;

	if (Success ==
	    XkbGetIndicatorState(xkl_display, XkbUseCoreKbd,
				 &current_state_out->indicators))
		current_state_out->indicators &=
		    xkl_xkb_desc->indicators->phys_indicators;
	else
		current_state_out->indicators = 0;
}

/*
 * Actually taken from mxkbledpanel, valueChangedProc
 */
gboolean
xkl_indicator_set(gint indicator_num, gboolean set)
{
	XkbIndicatorMapPtr map;

	map = xkl_xkb_desc->indicators->maps + indicator_num;

	/* The 'flags' field tells whether this indicator is automatic
	 * (XkbIM_NoExplicit - 0x80), explicit (XkbIM_NoAutomatic - 0x40),
	 * or neither (both - 0xC0).
	 *
	 * If NoAutomatic is set, the server ignores the rest of the 
	 * fields in the indicator map (i.e. it disables automatic control 
	 * of the LED).   If NoExplicit is set, the server prevents clients 
	 * from explicitly changing the value of the LED (using the core 
	 * protocol *or* XKB).   If NoAutomatic *and* NoExplicit are set, 
	 * the LED cannot be changed (unless you change the map first).   
	 * If neither NoAutomatic nor NoExplicit are set, the server will 
	 * change the LED according to the indicator map, but clients can 
	 * override that (until the next automatic change) using the core 
	 * protocol or XKB.
	 */
	switch (map->flags & (XkbIM_NoExplicit | XkbIM_NoAutomatic)) {
	case XkbIM_NoExplicit | XkbIM_NoAutomatic:
		{
			/* Can do nothing. Just ignore the indicator */
			return TRUE;
		}

	case XkbIM_NoAutomatic:
		{
			if (xkl_xkb_desc->names->
			    indicators[indicator_num] != None)
				XkbSetNamedIndicator(xkl_display,
						     XkbUseCoreKbd,
						     xkl_xkb_desc->names->
						     indicators
						     [indicator_num], set,
						     False, NULL);
			else {
				XKeyboardControl xkc;
				xkc.led = indicator_num;
				xkc.led_mode =
				    set ? LedModeOn : LedModeOff;
				XChangeKeyboardControl(xkl_display,
						       KBLed | KBLedMode,
						       &xkc);
				XSync(xkl_display, False);
			}

			return TRUE;
		}

	case XkbIM_NoExplicit:
		break;
	}

	/* The 'ctrls' field tells what controls tell this indicator to
	 * to turn on:  RepeatKeys (0x1), SlowKeys (0x2), BounceKeys (0x4),
	 *              StickyKeys (0x8), MouseKeys (0x10), AccessXKeys (0x20),
	 *              TimeOut (0x40), Feedback (0x80), ToggleKeys (0x100),
	 *              Overlay1 (0x200), Overlay2 (0x400), GroupsWrap (0x800),
	 *              InternalMods (0x1000), IgnoreLockMods (0x2000),
	 *              PerKeyRepeat (0x3000), or ControlsEnabled (0x4000)
	 */
	if (map->ctrls) {
		gulong which = map->ctrls;

		XkbGetControls(xkl_display, XkbAllControlsMask,
			       xkl_xkb_desc);
		if (set)
			xkl_xkb_desc->ctrls->enabled_ctrls |= which;
		else
			xkl_xkb_desc->ctrls->enabled_ctrls &= ~which;
		XkbSetControls(xkl_display, which | XkbControlsEnabledMask,
			       xkl_xkb_desc);
	}

	/* The 'which_groups' field tells when this indicator turns on
	 * for the 'groups' field:  base (0x1), latched (0x2), locked (0x4),
	 * or effective (0x8).
	 */
	if (map->groups) {
		gint i;
		guint group = 1;

		/* Turning on a group indicator is kind of tricky.  For
		 * now, we will just Latch or Lock the first group we find
		 * if that is what this indicator does.  Otherwise, we're
		 * just going to punt and get out of here.
		 */
		if (set) {
			for (i = XkbNumKbdGroups; --i >= 0;)
				if ((1 << i) & map->groups) {
					group = i;
					break;
				}
			if (map->
			    which_groups & (XkbIM_UseLocked |
					    XkbIM_UseEffective)) {
				/* Important: Groups should be ignored here - because they are handled separately! */
				/* XklLockGroup( group ); */
			} else if (map->which_groups & XkbIM_UseLatched)
				XkbLatchGroup(xkl_display, XkbUseCoreKbd,
					      group);
			else {
				/* Can do nothing. Just ignore the indicator */
				return TRUE;
			}
		} else
			/* Turning off a group indicator will mean that we just
			 * Lock the first group that this indicator doesn't watch.
			 */
		{
			for (i = XkbNumKbdGroups; --i >= 0;)
				if (!((1 << i) & map->groups)) {
					group = i;
					break;
				}
			xkl_group_lock(group);
		}
	}

	/* The 'which_mods' field tells when this indicator turns on
	 * for the modifiers:  base (0x1), latched (0x2), locked (0x4),
	 *                     or effective (0x8).
	 *
	 * The 'real_mods' field tells whether this turns on when one of 
	 * the real X modifiers is set:  Shift (0x1), Lock (0x2), Control (0x4),
	 * Mod1 (0x8), Mod2 (0x10), Mod3 (0x20), Mod4 (0x40), or Mod5 (0x80). 
	 *
	 * The 'virtual_mods' field tells whether this turns on when one of
	 * the virtual modifiers is set.
	 *
	 * The 'mask' field tells what real X modifiers the virtual_modifiers
	 * map to?
	 */
	if (map->mods.real_mods || map->mods.mask) {
		guint affect, mods;

		affect = (map->mods.real_mods | map->mods.mask);

		mods = set ? affect : 0;

		if (map->
		    which_mods & (XkbIM_UseLocked | XkbIM_UseEffective))
			XkbLockModifiers(xkl_display, XkbUseCoreKbd,
					 affect, mods);
		else if (map->which_mods & XkbIM_UseLatched)
			XkbLatchModifiers(xkl_display, XkbUseCoreKbd,
					  affect, mods);
		else {
			return TRUE;
		}
	}
	return TRUE;
}

#endif

gint
xkl_xkb_init(void)
{
#ifdef XKB_HEADERS_PRESENT
	gint opcode;
	gboolean xkl_xkb_ext_present;
	static XklVTable xkl_xkb_vtable = {
		"XKB",
		XKLF_CAN_TOGGLE_INDICATORS |
		    XKLF_CAN_OUTPUT_CONFIG_AS_ASCII |
		    XKLF_CAN_OUTPUT_CONFIG_AS_BINARY,
		xkl_xkb_config_activate,
		xkl_xkb_config_init,
		xkl_xkb_config_registry_load,
		xkl_xkb_config_write_file,

		xkl_xkb_groups_get_names,
		xkl_xkb_groups_get_max_num,
		xkl_xkb_groups_get_num,
		xkl_xkb_group_lock,

		xkl_xkb_process_x_event,
		xkl_xkb_free_all_info,
		xkl_xkb_if_cached_info_equals_actual,
		xkl_xkb_load_all_info,
		xkl_xkb_get_server_state,
		xkl_xkb_listen_pause,
		xkl_xkb_listen_resume,
		xkl_xkb_indicators_set,
	};

	if (getenv("XKL_XKB_DISABLE") != NULL)
		return -1;

	xkl_xkb_ext_present = XkbQueryExtension(xkl_display,
						&opcode,
						&xkl_xkb_event_type,
						&xkl_xkb_error_code, NULL,
						NULL);
	if (!xkl_xkb_ext_present) {
		XSetErrorHandler((XErrorHandler)
				 xkl_default_error_handler);
		return -1;
	}

	xkl_debug(160,
		  "xkbEvenType: %X, xkbError: %X, display: %p, root: "
		  WINID_FORMAT "\n", xkl_xkb_event_type,
		  xkl_xkb_error_code, xkl_display, xkl_root_window);

	xkl_xkb_vtable.base_config_atom =
	    XInternAtom(xkl_display, _XKB_RF_NAMES_PROP_ATOM, False);
	xkl_xkb_vtable.backup_config_atom =
	    XInternAtom(xkl_display, "_XKB_RULES_NAMES_BACKUP", False);

	xkl_xkb_vtable.default_model = "pc101";
	xkl_xkb_vtable.default_layout = "us";

	xkl_vtable = &xkl_xkb_vtable;

	/* First, we have to assign xkl_vtable - 
	   because this function uses it */

	if (xkl_xkb_config_multiple_layouts_supported())
		xkl_xkb_vtable.features |= XKLF_MULTIPLE_LAYOUTS_SUPPORTED;

	return 0;
#else
	xkl_debug(160,
		  "NO XKB LIBS, display: %p, root: " WINID_FORMAT
		  "\n", xkl_display, xkl_root_window);
	return -1;
#endif
}

#ifdef XKB_HEADERS_PRESENT
const gchar *
xkl_xkb_event_get_name(int xkb_type)
{
	/* Not really good to use the fact of consecutivity
	   but XKB protocol extension is already standartized so... */
	static const char *evt_names[] = {
		"XkbNewKeyboardNotify",
		"XkbMapNotify",
		"XkbStateNotify",
		"XkbControlsNotify",
		"XkbIndicatorStateNotify",
		"XkbIndicatorMapNotify",
		"XkbNamesNotify",
		"XkbCompatMapNotify",
		"XkbBellNotify",
		"XkbActionMessage",
		"XkbAccessXNotify",
		"XkbExtensionDeviceNotify",
		"LASTEvent"
	};
	xkb_type -= XkbNewKeyboardNotify;
	if (xkb_type < 0 ||
	    xkb_type >= (sizeof(evt_names) / sizeof(evt_names[0])))
		return "UNKNOWN/OOR";
	return evt_names[xkb_type];
}
#endif
