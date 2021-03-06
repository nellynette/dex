#include "options.h"
#include "buffer.h"
#include "completion.h"
#include "filetype.h"
#include "common.h"
#include "regexp.h"
#include "error.h"

struct global_options options = {
	.auto_indent = 1,
	.detect_indent = 0,
	.emulate_tab = 0,
	.expand_tab = 0,
	.file_history = 1,
	.indent_width = 8,
	.syntax = 1,
	.tab_width = 8,
	.text_width = 72,
	.ws_error = WSE_SPECIAL,

	.case_sensitive_search = CSS_TRUE,
	.display_special = 0,
	.esc_timeout = 100,
	.lock_files = 1,
	.newline = NEWLINE_UNIX,
	.scroll_margin = 0,
	.show_line_numbers = 0,
	.show_tab_bar = 1,
	.statusline_left = NULL,
	.statusline_right = NULL,
	.tab_bar_max_components = 0,
	.tab_bar_width = 25,
	.vertical_tab_bar = 0,
};

enum option_type {
	OPT_STR,
	OPT_INT,
	OPT_ENUM,
	OPT_FLAG,
};

struct option_desc {
	const struct option_ops *ops;
	const char *name;
	unsigned int offset;
	bool local;
	bool global;
	union {
		struct {
			// optional
			bool (*validate)(const char *value);
		} str_opt;
		struct {
			int min;
			int max;
		} int_opt;
		struct {
			const char **values;
		} enum_opt;
		struct {
			const char **values;
		} flag_opt;
	} u;
	// optional
	void (*on_change)(void);
};

union option_value {
	// OPT_STR
	char *str_val;
	// OPT_INT, OPT_ENUM, OPT_FLAG
	int int_val;
};

struct option_ops {
	union option_value (*get)(const struct option_desc *desc, void *ptr);
	void (*set)(const struct option_desc *desc, void *ptr, union option_value value);
	bool (*parse)(const struct option_desc *desc, const char *str, union option_value *value);
	char *(*string)(const struct option_desc *desc, union option_value value);
	bool (*equals)(const struct option_desc *desc, void *ptr, union option_value value);
};

#define STR_OPT(_name, OLG, _validate, _on_change) { \
	.ops = &option_ops[OPT_STR],	\
	.name = _name,			\
	OLG				\
	.u = { .str_opt = {		\
		.validate = _validate,	\
	} },				\
	.on_change = _on_change,	\
}

#define INT_OPT(_name, OLG, _min, _max, _on_change) { \
	.ops = &option_ops[OPT_INT],	\
	.name = _name,			\
	OLG				\
	.u = { .int_opt = {		\
		.min = _min,		\
		.max = _max,		\
	} },				\
	.on_change = _on_change,	\
}

#define ENUM_OPT(_name, OLG, _values, _on_change) { \
	.ops = &option_ops[OPT_ENUM],	\
	.name = _name,			\
	OLG				\
	.u = { .enum_opt = {		\
		.values = _values,	\
	} },				\
	.on_change = _on_change,	\
}

#define FLAG_OPT(_name, OLG, _values, _on_change) { \
	.ops = &option_ops[OPT_FLAG],	\
	.name = _name,			\
	OLG				\
	.u = { .flag_opt = {		\
		.values = _values,	\
	} },				\
	.on_change = _on_change,	\
}

// can't reuse ENUM_OPT() because of weird macro expansion rules
#define BOOL_OPT(_name, OLG, _on_change) { \
	.ops = &option_ops[OPT_ENUM],	\
	.name = _name,			\
	OLG				\
	.u = { .enum_opt = {		\
		.values = bool_enum,	\
	} },				\
	.on_change = _on_change,	\
}

#define OLG(_offset, _local, _global) 	\
	.offset = _offset,		\
	.local = _local,		\
	.global = _global,		\

#define L(member) OLG(offsetof(struct local_options, member), true, false)
#define G(member) OLG(offsetof(struct global_options, member), false, true)
#define C(member) OLG(offsetof(struct common_options, member), true, true)

static bool validate_statusline_format(const char *value)
{
	static const char chars[] = "fmryxXpEMnstu%";
	int i = 0;

	while (value[i]) {
		char ch = value[i++];

		if (ch == '%') {
			ch = value[i++];
			if (!ch) {
				error_msg("Format character expected after '%%'.");
				return false;
			}
			if (!strchr(chars, ch)) {
				error_msg("Invalid format character '%c'.", ch);
				return false;
			}
		}
	}
	return true;
}

static bool validate_filetype(const char *value)
{
	if (strcmp(value, "none") && !is_ft(value)) {
		error_msg("No such file type %s", value);
		return false;
	}
	return true;
}

static bool validate_regex(const char *value)
{
	if (value[0]) {
		regex_t re;
		if (!regexp_compile(&re, value, REG_NEWLINE | REG_NOSUB))
			return false;
		regfree(&re);
	}
	return true;
}

static union option_value str_get(const struct option_desc *desc, void *ptr)
{
	union option_value v;
	v.str_val = xstrdup(*(char **)ptr);
	return v;
}

static void str_set(const struct option_desc *desc, void *ptr, union option_value value)
{
	char **strp = ptr;
	free(*strp);
	*strp = xstrdup(value.str_val);
}

static bool str_parse(const struct option_desc *desc, const char *str, union option_value *value)
{
	if (desc->u.str_opt.validate && !desc->u.str_opt.validate(str)) {
		value->str_val = NULL;
		return false;
	}
	value->str_val = xstrdup(str);
	return true;
}

static char *str_string(const struct option_desc *desc, union option_value value)
{
	return xstrdup(value.str_val);
}

static bool str_equals(const struct option_desc *desc, void *ptr, union option_value value)
{
	return streq(*(char **)ptr, value.str_val);
}

static union option_value int_get(const struct option_desc *desc, void *ptr)
{
	union option_value v;
	v.int_val = *(int *)ptr;
	return v;
}

static void int_set(const struct option_desc *desc, void *ptr, union option_value value)
{
	*(int *)ptr = value.int_val;
}

static bool int_parse(const struct option_desc *desc, const char *str, union option_value *value)
{
	int val;

	if (!str_to_int(str, &val)) {
		error_msg("Integer value for %s expected.", desc->name);
		return false;
	}
	if (val < desc->u.int_opt.min || val > desc->u.int_opt.max) {
		error_msg("Value for %s must be in %d-%d range.", desc->name,
			desc->u.int_opt.min, desc->u.int_opt.max);
		return false;
	}
	value->int_val = val;
	return true;
}

static char *int_string(const struct option_desc *desc, union option_value value)
{
	return xsprintf("%d", value.int_val);
}

static bool int_equals(const struct option_desc *desc, void *ptr, union option_value value)
{
	return *(int *)ptr == value.int_val;
}

static bool enum_parse(const struct option_desc *desc, const char *str, union option_value *value)
{
	int val, i;

	for (i = 0; desc->u.enum_opt.values[i]; i++) {
		if (streq(desc->u.enum_opt.values[i], str)) {
			value->int_val = i;
			return true;
		}
	}
	if (!str_to_int(str, &val) || val < 0 || val >= i) {
		error_msg("Invalid value for %s.", desc->name);
		return false;
	}
	value->int_val = val;
	return true;
}

static char *enum_string(const struct option_desc *desc, union option_value value)
{
	return xstrdup(desc->u.enum_opt.values[value.int_val]);
}

static bool flag_parse(const struct option_desc *desc, const char *str, union option_value *value)
{
	const char **values = desc->u.flag_opt.values;
	const char *ptr = str;
	int val, flags = 0;

	// "0" is allowed for compatibility and is same as ""
	if (str_to_int(str, &val) && val == 0) {
		value->int_val = val;
		return true;
	}
	while (*ptr) {
		const char *end = strchr(ptr, ',');
		char *buf;
		int i, len;

		if (end) {
			len = end - ptr;
			end++;
		} else {
			len = strlen(ptr);
			end = ptr + len;
		}
		buf = xmemdup(ptr, len + 1);
		buf[len] = 0;
		ptr = end;

		for (i = 0; values[i]; i++) {
			if (streq(buf, values[i])) {
				flags |= 1 << i;
				break;
			}
		}
		if (!values[i]) {
			error_msg("Invalid flag '%s' for %s.", buf, desc->name);
			free(buf);
			return false;
		}
		free(buf);
	}
	value->int_val = flags;
	return true;
}

static char *flag_string(const struct option_desc *desc, union option_value value)
{
	const char **values = desc->u.flag_opt.values;
	int flags = value.int_val;
	char buf[1024];
	char *ptr = buf;
	int i;

	if (!flags)
		return xstrdup("0");

	for (i = 0; values[i]; i++) {
		if (flags & (1 << i)) {
			int len = strlen(values[i]);
			memcpy(ptr, values[i], len);
			ptr += len;
			*ptr++ = ',';
		}
	}
	ptr[-1] = 0;
	return xstrdup(buf);
}

static const struct option_ops option_ops[] = {
	{ str_get, str_set, str_parse, str_string, str_equals },
	{ int_get, int_set, int_parse, int_string, int_equals },
	{ int_get, int_set, enum_parse, enum_string, int_equals },
	{ int_get, int_set, flag_parse, flag_string, int_equals },
};

static const char *bool_enum[] = { "false", "true", NULL };
static const char *newline_enum[] = { "unix", "dos", NULL };
const char *case_sensitive_search_enum[] = { "false", "true", "auto", NULL };
static const char *ws_error_values[] = {
	"trailing",
	"space-indent",
	"space-align",
	"tab-indent",
	"tab-after-indent",
	"special",
	"auto-indent",
	NULL
};
static const char *detect_indent_values[] = { "1", "2", "3", "4", "5", "6", "7", "8", NULL };

static const struct option_desc option_desc[] = {
	BOOL_OPT("auto-indent", C(auto_indent), NULL),
	BOOL_OPT("brace-indent", L(brace_indent), NULL),
	ENUM_OPT("case-sensitive-search", G(case_sensitive_search), case_sensitive_search_enum, NULL),
	FLAG_OPT("detect-indent", C(detect_indent), detect_indent_values, NULL),
	BOOL_OPT("display-special", G(display_special), NULL),
	BOOL_OPT("emulate-tab", C(emulate_tab), NULL),
	INT_OPT("esc-timeout", G(esc_timeout), 0, 2000, NULL),
	BOOL_OPT("expand-tab", C(expand_tab), NULL),
	BOOL_OPT("file-history", C(file_history), NULL),
	STR_OPT("filetype", L(filetype), validate_filetype, filetype_changed),
	INT_OPT("indent-width", C(indent_width), 1, 8, NULL),
	STR_OPT("indent-regex", L(indent_regex), validate_regex, NULL),
	BOOL_OPT("lock-files", G(lock_files), NULL),
	ENUM_OPT("newline", G(newline), newline_enum, NULL),
	INT_OPT("scroll-margin", G(scroll_margin), 0, 100, NULL),
	BOOL_OPT("show-line-numbers", G(show_line_numbers), NULL),
	BOOL_OPT("show-tab-bar", G(show_tab_bar), NULL),
	STR_OPT("statusline-left", G(statusline_left), validate_statusline_format, NULL),
	STR_OPT("statusline-right", G(statusline_right), validate_statusline_format, NULL),
	BOOL_OPT("syntax", C(syntax), syntax_changed),
	INT_OPT("tab-bar-max-components", G(tab_bar_max_components), 0, 10, NULL),
	INT_OPT("tab-bar-width", G(tab_bar_width), TAB_BAR_MIN_WIDTH, 100, NULL),
	INT_OPT("tab-width", C(tab_width), 1, 8, NULL),
	INT_OPT("text-width", C(text_width), 1, 1000, NULL),
	BOOL_OPT("vertical-tab-bar", G(vertical_tab_bar), NULL),
	FLAG_OPT("ws-error", C(ws_error), ws_error_values, NULL),
};

static inline char *local_ptr(const struct option_desc *desc, const struct local_options *opt)
{
	return (char *)opt + desc->offset;
}

static inline char *global_ptr(const struct option_desc *desc)
{
	return (char *)&options + desc->offset;
}

static bool desc_is(const struct option_desc *desc, enum option_type type)
{
	return desc->ops == &option_ops[type];
}

static void desc_set(const struct option_desc *desc, void *ptr, union option_value value)
{
	desc->ops->set(desc, ptr, value);
	if (desc->on_change) {
		desc->on_change();
	} else {
		mark_everything_changed();
	}
}

static void free_value(const struct option_desc *desc, union option_value value)
{
	if (desc_is(desc, OPT_STR))
		free(value.str_val);
}

static const struct option_desc *find_option(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_desc *desc = &option_desc[i];

		if (streq(name, desc->name))
			return desc;
	}
	return NULL;
}

static const struct option_desc *must_find_option(const char *name)
{
	const struct option_desc *desc = find_option(name);

	if (desc == NULL)
		error_msg("No such option %s", name);
	return desc;
}

static const struct option_desc *must_find_global_option(const char *name)
{
	const struct option_desc *desc = must_find_option(name);

	if (desc && !desc->global) {
		error_msg("Option %s is not global", name);
		return NULL;
	}
	return desc;
}

static void do_set_option(const struct option_desc *desc, const char *value, bool local, bool global)
{
	union option_value val;

	if (local && !desc->local) {
		error_msg("Option %s is not local", desc->name);
		return;
	}
	if (global && !desc->global) {
		error_msg("Option %s is not global", desc->name);
		return;
	}
	if (!desc->ops->parse(desc, value, &val)) {
		return;
	}
	if (!local && !global) {
		// set both by default
		if (desc->local)
			local = true;
		if (desc->global)
			global = true;
	}

	if (local)
		desc_set(desc, local_ptr(desc, &buffer->options), val);
	if (global)
		desc_set(desc, global_ptr(desc), val);
	free_value(desc, val);
}

void set_option(const char *name, const char *value, bool local, bool global)
{
	const struct option_desc *desc = must_find_option(name);

	if (!desc)
		return;
	do_set_option(desc, value, local, global);
}

void set_bool_option(const char *name, bool local, bool global)
{
	const struct option_desc *desc = must_find_option(name);

	if (!desc)
		return;
	if (!desc_is(desc, OPT_ENUM) || desc->u.enum_opt.values != bool_enum) {
		error_msg("Option %s is not boolean.", desc->name);
		return;
	}
	do_set_option(desc, "true", local, global);
}

static const struct option_desc *find_toggle_option(const char *name, bool *global)
{
	const struct option_desc *desc;

	if (*global)
		return must_find_global_option(name);
	// toggle local value by default if option has both values
	desc = must_find_option(name);
	if (desc && !desc->local)
		*global = true;
	return desc;
}

static int toggle(int value, const char **values)
{
	if (!values[++value])
		value = 0;
	return value;
}

void toggle_option(const char *name, bool global, bool verbose)
{
	const struct option_desc *desc = find_toggle_option(name, &global);
	union option_value value;
	char *ptr = NULL;

	if (!desc)
		return;
	if (!desc_is(desc, OPT_ENUM)) {
		error_msg("Option %s is not toggleable.", name);
		return;
	}

	if (global) {
		ptr = global_ptr(desc);
	} else {
		ptr = local_ptr(desc, &buffer->options);
	}
	value.int_val = toggle(*(int *)ptr, desc->u.enum_opt.values);
	desc_set(desc, ptr, value);

	if (verbose) {
		char *str = desc->ops->string(desc, value);
		info_msg("%s = %s", desc->name, str);
		free(str);
	}
}

void toggle_option_values(const char *name, bool global, bool verbose, char **values)
{
	const struct option_desc *desc = find_toggle_option(name, &global);
	int i, count = count_strings(values);
	union option_value *parsed_values;
	int current = -1;
	bool error = false;
	char *ptr;

	if (!desc)
		return;

	if (global) {
		ptr = global_ptr(desc);
	} else {
		ptr = local_ptr(desc, &buffer->options);
	}
	parsed_values = xnew(union option_value, count);
	for (i = 0; i < count; i++) {
		if (desc->ops->parse(desc, values[i], &parsed_values[i])) {
			if (desc->ops->equals(desc, ptr, parsed_values[i]))
				current = i;
		} else {
			error = true;
		}
	}
	if (!error) {
		i = (current + 1) % count;
		desc_set(desc, ptr, parsed_values[i]);
		if (verbose) {
			char *str = desc->ops->string(desc, parsed_values[i]);
			info_msg("%s = %s", desc->name, str);
			free(str);
		}
	}
	for (i = 0; i < count; i++) {
		free_value(desc, parsed_values[i]);
	}
	free(parsed_values);
}

bool validate_local_options(char **strs)
{
	bool valid = true;
	int i;

	for (i = 0; strs[i]; i += 2) {
		const char *name = strs[i];
		const char *value = strs[i + 1];
		const struct option_desc *desc = must_find_option(name);

		if (desc == NULL) {
			valid = false;
		} else if (!desc->local) {
			error_msg("Option %s is not local", name);
			valid = false;
		} else {
			union option_value val;

			if (desc->ops->parse(desc, value, &val)) {
				free_value(desc, val);
			} else {
				valid = false;
			}
		}
	}
	return valid;
}

void collect_options(const char *prefix)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_desc *desc = &option_desc[i];
		if (str_has_prefix(desc->name, prefix))
			add_completion(xstrdup(desc->name));
	}
}

void collect_toggleable_options(const char *prefix)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_desc *desc = &option_desc[i];
		if (desc_is(desc, OPT_ENUM) && str_has_prefix(desc->name, prefix))
			add_completion(xstrdup(desc->name));
	}
}

void collect_option_values(const char *name, const char *prefix)
{
	const struct option_desc *desc = find_option(name);

	if (!desc)
		return;

	if (!*prefix) {
		// complete value
		union option_value value;
		char *ptr;

		if (desc->local) {
			ptr = local_ptr(desc, &buffer->options);
		} else {
			ptr = global_ptr(desc);
		}
		value = desc->ops->get(desc, ptr);
		add_completion(desc->ops->string(desc, value));
		free_value(desc, value);
	} else if (desc_is(desc, OPT_ENUM)) {
		// complete possible values
		int i;

		for (i = 0; desc->u.enum_opt.values[i]; i++) {
			if (str_has_prefix(desc->u.enum_opt.values[i], prefix))
				add_completion(xstrdup(desc->u.enum_opt.values[i]));
		}
	} else if (desc_is(desc, OPT_FLAG)) {
		// complete possible values
		const char *comma = strrchr(prefix, ',');
		int i, prefix_len = 0;

		if (comma)
			prefix_len = ++comma - prefix;
		for (i = 0; desc->u.flag_opt.values[i]; i++) {
			const char *str = desc->u.flag_opt.values[i];

			if (str_has_prefix(str, prefix + prefix_len)) {
				int str_len = strlen(str);
				char *completion = xmalloc(prefix_len + str_len + 1);
				memcpy(completion, prefix, prefix_len);
				memcpy(completion + prefix_len, str, str_len + 1);
				add_completion(completion);
			}
		}
	}
}

void free_local_options(struct local_options *opt)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_desc *desc = &option_desc[i];

		if (desc->local && desc_is(desc, OPT_STR)) {
			char **local = (char **)local_ptr(desc, opt);
			free(*local);
			*local = NULL;
		}
	}
}
