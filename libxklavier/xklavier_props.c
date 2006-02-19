#include <errno.h>
#include <string.h>
#include <locale.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <libxml/xpath.h>

#include "config.h"

#include "xklavier.h"
#include "xklavier_config.h"
#include "xklavier_private.h"

void
xkl_config_rec_init(XklConfigRec * data)
{
	/* clear the structure VarDefsPtr... */
	memset((void *) data, 0, sizeof(XklConfigRec));
}

static gboolean
xkl_strings_equal(gchar * p1, gchar * p2)
{
	if (p1 == p2)
		return TRUE;
	if ((p1 == NULL && p2 != NULL) || (p1 != NULL && p2 == NULL))
		return FALSE;
	return !g_ascii_strcasecmp(p1, p2);
}

static gboolean
xkl_lists_equal(gchar ** items1, gchar ** items2)
{
	if (items1 == items2)
		return TRUE;

	if ((items1 == NULL && items2 != NULL) ||
	    (items1 != NULL && items2 == NULL))
		return FALSE;

	while (*items1 != NULL && *items2 != NULL)
		if (!xkl_strings_equal(*items1++, *items2++))
			return FALSE;

	return (*items1 == NULL && *items2 == NULL);
}

static gboolean
xkl_get_default_names_prop(char **rules_file_out, XklConfigRec * data)
{
	if (rules_file_out != NULL)
		*rules_file_out = strdup(XKB_DEFAULT_RULESET);
	data->model = g_strdup(xkl_vtable->default_model);
/* keeping Nvariants = Nlayouts */
	data->layouts = g_new0(char *, 2);
	data->layouts[0] = g_strdup(xkl_vtable->default_layout);
	data->variants = g_new0(char *, 2);
	data->variants[0] = g_strdup("");
	data->options = NULL;
	return TRUE;
}

gboolean
xkl_config_get_full_from_server(char **rules_file_out, XklConfigRec * data)
{
	gboolean rv =
	    xkl_get_names_prop(xkl_vtable->base_config_atom,
			       rules_file_out, data);

	if (!rv)
		rv = xkl_get_default_names_prop(rules_file_out, data);

	return rv;
}

gboolean
xkl_config_rec_equals(XklConfigRec * data1, XklConfigRec * data2)
{
	if (data1 == data2)
		return TRUE;
	if (!xkl_strings_equal(data1->model, data2->model))
		return FALSE;
	if (!xkl_lists_equal(data1->layouts, data2->layouts))
		return FALSE;
	if (!xkl_lists_equal(data1->variants, data2->variants))
		return FALSE;
	return xkl_lists_equal(data1->options, data2->options);
}

void
xkl_config_rec_destroy(XklConfigRec * data)
{
	if (data->model != NULL)
		g_free(data->model);

	g_strfreev(data->layouts);
	g_strfreev(data->variants);
	g_strfreev(data->options);
	data->layouts = data->variants = data->options = NULL;
}

void
xkl_config_rec_reset(XklConfigRec * data)
{
	xkl_config_rec_destroy(data);
	xkl_config_rec_init(data);
}

gboolean
xkl_config_get_from_server(XklConfigRec * data)
{
	return xkl_config_get_full_from_server(NULL, data);
}

gboolean
xkl_config_get_from_backup(XklConfigRec * data)
{
	return xkl_get_names_prop(xkl_vtable->backup_config_atom, NULL,
				  data);
}

gboolean
xkl_backup_names_prop(void)
{
	gboolean rv = TRUE;
	gchar *rf = NULL;
	XklConfigRec data;
	gboolean cgp = FALSE;

	xkl_config_rec_init(&data);
	if (xkl_get_names_prop
	    (xkl_vtable->backup_config_atom, NULL, &data)) {
		xkl_config_rec_destroy(&data);
		return TRUE;
	}
	/* "backup" property is not defined */
	xkl_config_rec_reset(&data);
	cgp = xkl_config_get_full_from_server(&rf, &data);

	if (cgp) {
		if (!xkl_set_names_prop
		    (xkl_vtable->backup_config_atom, rf, &data)) {
			xkl_debug(150,
				  "Could not backup the configuration");
			rv = FALSE;
		}
		if (rf != NULL)
			g_free(rf);
	} else {
		xkl_debug(150,
			  "Could not get the configuration for backup");
		rv = FALSE;
	}
	xkl_config_rec_destroy(&data);
	return rv;
}

gboolean
xkl_restore_names_prop(void)
{
	gboolean rv = TRUE;
	gchar *rf = NULL;
	XklConfigRec data;

	xkl_config_rec_init(&data);
	if (!xkl_get_names_prop
	    (xkl_vtable->backup_config_atom, NULL, &data)) {
		xkl_config_rec_destroy(&data);
		return FALSE;
	}

	if (!xkl_set_names_prop(xkl_vtable->base_config_atom, rf, &data)) {
		xkl_debug(150, "Could not backup the configuration");
		rv = FALSE;
	}
	xkl_config_rec_destroy(&data);
	return rv;
}

gboolean
xkl_get_names_prop(Atom rules_atom,
		   gchar ** rules_file_out, XklConfigRec * data)
{
	Atom real_prop_type;
	int fmt;
	unsigned long nitems, extra_bytes;
	char *prop_data = NULL, *out;
	Status rtrn;

	/* no such atom! */
	if (rules_atom == None) {	/* property cannot exist */
		xkl_last_error_message = "Could not find the atom";
		return FALSE;
	}

	rtrn =
	    XGetWindowProperty(xkl_display, xkl_root_window, rules_atom,
			       0L, XKB_RF_NAMES_PROP_MAXLEN, False,
			       XA_STRING, &real_prop_type, &fmt, &nitems,
			       &extra_bytes,
			       (unsigned char **) (void *) &prop_data);
	/* property not found! */
	if (rtrn != Success) {
		xkl_last_error_message = "Could not get the property";
		return FALSE;
	}
	/* set rules file to "" */
	if (rules_file_out)
		*rules_file_out = NULL;

	/* has to be array of strings */
	if ((extra_bytes > 0) || (real_prop_type != XA_STRING)
	    || (fmt != 8)) {
		if (prop_data)
			XFree(prop_data);
		xkl_last_error_message = "Wrong property format";
		return FALSE;
	}

	if (!prop_data) {
		xkl_last_error_message = "No properties returned";
		return FALSE;
	}

	/* rules file */
	out = prop_data;
	if (out && (*out) && rules_file_out)
		*rules_file_out = g_strdup(out);
	out += strlen(out) + 1;

	/* if user is interested in rules only - don't waste the time */
	if (!data) {
		XFree(prop_data);
		return TRUE;
	}

	if ((out - prop_data) < nitems) {
		if (*out)
			data->model = g_strdup(out);
		out += strlen(out) + 1;
	}

	if ((out - prop_data) < nitems) {
		xkl_config_rec_split_layouts(data, out);
		out += strlen(out) + 1;
	}

	if ((out - prop_data) < nitems) {
		gint nv, nl;
		gchar **layout, **variant;
		xkl_config_rec_split_variants(data, out);
		/*
		   Now have to ensure that number of variants matches the number of layouts
		   The 'remainder' is filled with NULLs (not ""s!)
		 */

		nv = g_strv_length(data->variants);
		nl = g_strv_length(data->layouts);
		if (nv < nl) {
			data->variants = g_realloc(data->variants,
						   (nl +
						    1) * sizeof(char *));
			memset(data->variants + nv + 1, 0,
			       (nl - nv) * sizeof(char *));
		}
		/* take variants from layouts like ru(winkeys) */
		layout = data->layouts;
		variant = data->variants;
		while (*layout != NULL && *variant != NULL) {
			gchar *varstart = g_strstr_len(*layout, -1, "(");
			if (varstart != NULL) {
				gchar *varend =
				    g_strstr_len(varstart, -1, ")");
				if (varend != NULL) {
					gint varlen = varend - varstart;
					gint laylen = varstart - *layout;
					/* I am not sure - but I assume variants in layout have priority */
					gchar *var = *variant =
					    (*variant !=
					     NULL) ? g_realloc(*variant,
							       varlen) :
					    g_new(gchar, varlen);
					memcpy(var, varstart + 1,
					       --varlen);
					var[varlen] = '\0';
					/* Resize the original layout */
					((char *)
					 g_realloc(*layout,
						   laylen + 1))[laylen] =
				   '\0';
				}
			}
			layout++;
			variant++;
		}
		out += strlen(out) + 1;
	}

	if ((out - prop_data) < nitems) {
		xkl_config_rec_split_options(data, out);
	}
	XFree(prop_data);
	return TRUE;
}

/* taken from XFree86 maprules.c */
gboolean
xkl_set_names_prop(Atom rules_atom,
		   gchar * rules_file, const XklConfigRec * data)
{
	gint len, rv;
	gchar *pval;
	gchar *next;
	gchar *all_layouts = xkl_config_rec_merge_layouts(data);
	gchar *all_variants = xkl_config_rec_merge_variants(data);
	gchar *all_options = xkl_config_rec_merge_options(data);

	len = (rules_file ? strlen(rules_file) : 0);
	len += (data->model ? strlen(data->model) : 0);
	len += (all_layouts ? strlen(all_layouts) : 0);
	len += (all_variants ? strlen(all_variants) : 0);
	len += (all_options ? strlen(all_options) : 0);
	if (len < 1)
		return TRUE;

	len += 5;		/* trailing NULs */

	pval = next = g_new(char, len + 1);
	if (!pval) {
		xkl_last_error_message = "Could not allocate buffer";
		if (all_layouts != NULL)
			g_free(all_layouts);
		if (all_variants != NULL)
			g_free(all_variants);
		if (all_options != NULL)
			g_free(all_options);
		return FALSE;
	}
	if (rules_file) {
		strcpy(next, rules_file);
		next += strlen(rules_file);
	}
	*next++ = '\0';
	if (data->model) {
		strcpy(next, data->model);
		next += strlen(data->model);
	}
	*next++ = '\0';
	if (data->layouts) {
		strcpy(next, all_layouts);
		next += strlen(all_layouts);
	}
	*next++ = '\0';
	if (data->variants) {
		strcpy(next, all_variants);
		next += strlen(all_variants);
	}
	*next++ = '\0';
	if (data->options) {
		strcpy(next, all_options);
		next += strlen(all_options);
	}
	*next++ = '\0';
	if ((next - pval) != len) {
		xkl_debug(150, "Illegal final position: %d/%d\n",
			  (next - pval), len);
		if (all_layouts != NULL)
			g_free(all_layouts);
		if (all_variants != NULL)
			g_free(all_variants);
		if (all_options != NULL)
			g_free(all_options);
		g_free(pval);
		xkl_last_error_message = "Internal property parsing error";
		return FALSE;
	}

	rv = XChangeProperty(xkl_display, xkl_root_window, rules_atom,
			     XA_STRING, 8, PropModeReplace,
			     (unsigned char *) pval, len);
	XSync(xkl_display, False);
#if 0
	for (i = len - 1; --i >= 0;)
		if (pval[i] == '\0')
			pval[i] = '?';
	XklDebug(150, "Stored [%s] of length %d to [%s] of %X: %d\n", pval,
		 len, propName, _xklRootWindow, rv);
#endif
	if (all_layouts != NULL)
		g_free(all_layouts);
	if (all_variants != NULL)
		g_free(all_variants);
	if (all_options != NULL)
		g_free(all_options);
	g_free(pval);
	return TRUE;
}
