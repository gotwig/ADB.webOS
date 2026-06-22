#include "ss4s_modules.h"
#include "ss4s/modules_ini.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <ini.h>
#include "array_list.h"


typedef struct modules_parse_context {
    SS4S_ModuleInfo current;
    const os_info_t *os_info;
    array_list_t *modules;
} modules_parse_context;

static void module_info_clear(SS4S_ModuleInfo *info);

static void section_changed(modules_parse_context *context);

static void modules_parse_context_destroy(modules_parse_context *context);

static int modules_ini_handler(void *user, const char *section, const char *name, const char *value);

static int module_weight_compare_fn(const void *a, const void *b);

static const SS4S_ModuleInfo *module_by_id(const array_list_t *list, const char *id);

/* Delimited string list */

static int str_list_parse(str_list_t *list, const char *value, const char *delim);

static void str_list_clear(str_list_t *list);

static bool preference_auto(const char *value);

int SS4S_ModulesList(array_list_t *modules, const os_info_t *os_info) {
    array_list_init(modules, sizeof(SS4S_ModuleInfo), 16);
    FILE *f = SS4S_ModulesListFileOpen();
    if (f == NULL) { return errno; }
    modules_parse_context mpc = {.modules = modules, .os_info = os_info};
    module_info_clear(&mpc.current);
    ini_parse_file(f, modules_ini_handler, &mpc);
    SS4S_ModulesListFileClose(f);
    section_changed(&mpc);
    modules_parse_context_destroy(&mpc);
    array_list_qsort(modules, module_weight_compare_fn);
    return 0;
}

void SS4S_ModulesListClear(array_list_t *modules) {
    for (int i = array_list_size(modules) - 1; i >= 0; --i) {
        module_info_clear(array_list_get(modules, i));
    }
    array_list_deinit(modules);
}

bool SS4S_ModulesSelect(const array_list_t *modules, const SS4S_ModulePreferences *preferences,
                        SS4S_ModuleSelection *selection, bool checkModule) {
    const SS4S_ModuleInfo *selected_video_module = NULL, *selected_audio_module = NULL;
    if (preferences != NULL && !preference_auto(preferences->video_module)) {
        const SS4S_ModuleInfo *selected = module_by_id(modules, preferences->video_module);
        if (selected != NULL && selected->has_video) {
            SS4S_ModuleCheckFlag flags = SS4S_MODULE_CHECK_VIDEO;
            if (selected->has_audio && preference_auto(preferences->audio_module)) {
                flags |= SS4S_MODULE_CHECK_AUDIO;
            }
            if (!checkModule || (flags = SS4S_ModuleCheck(selected->id, flags)) & SS4S_MODULE_CHECK_VIDEO) {
                selected_video_module = selected;
                if (flags & SS4S_MODULE_CHECK_AUDIO) {
                    selected_audio_module = selected;
                }
            }
        }
    }
    if (selected_video_module == NULL) {
        for (int i = 0, j = array_list_size(modules); i < j; ++i) {
            const SS4S_ModuleInfo *info = array_list_get((array_list_t *) modules, i);
            if (!info->has_video) {
                continue;
            }
            SS4S_ModuleCheckFlag flags = SS4S_MODULE_CHECK_VIDEO;
            if (info->has_audio && (preferences == NULL || preference_auto(preferences->audio_module))) {
                flags |= SS4S_MODULE_CHECK_AUDIO;
            }
            if (!checkModule || (flags = SS4S_ModuleCheck(info->id, flags)) & SS4S_MODULE_CHECK_VIDEO) {
                selected_video_module = info;
                if (flags & SS4S_MODULE_CHECK_AUDIO) {
                    selected_audio_module = info;
                }
                break;
            }
        }
    }
    if (preferences != NULL && !preference_auto(preferences->audio_module)) {
        const SS4S_ModuleInfo *selected = module_by_id(modules, preferences->audio_module);
        if (selected != NULL && selected->has_audio &&
            (!checkModule ||
             (selected_video_module != NULL && !SS4S_ModuleInfoConflicts(selected_video_module, selected)) &&
             SS4S_ModuleCheck(selected->id, SS4S_MODULE_CHECK_AUDIO))) {
            selected_audio_module = selected;
        }
    }
    if (selected_audio_module == NULL) {
        for (int i = 0, j = array_list_size(modules); i < j; ++i) {
            const SS4S_ModuleInfo *info = array_list_get((array_list_t *) modules, i);
            if (!info->has_audio || checkModule && (selected_video_module != NULL &&
                                                    SS4S_ModuleInfoConflicts(selected_video_module, info))) {
                continue;
            }
            if (!checkModule || SS4S_ModuleCheck(info->id, SS4S_MODULE_CHECK_AUDIO)) {
                selected_audio_module = info;
                break;
            }
        }
    }
    selection->audio_module = selected_audio_module;
    selection->video_module = selected_video_module;
    return selected_audio_module != NULL && selected_video_module != NULL;
}

bool SS4S_ModuleInfoConflicts(const SS4S_ModuleInfo *a, const SS4S_ModuleInfo *b) {
    for (int i = 0; i < a->conflicts.count; i++) {
        const char *item = a->conflicts.elements[i];
        if (strcmp(item, b->id) == 0 || (b->group != NULL && strcmp(item, b->group) == 0)) {
            return true;
        }
    }
    for (int i = 0; i < b->conflicts.count; i++) {
        const char *item = b->conflicts.elements[i];
        if (strcmp(item, a->id) == 0 || (a->group != NULL && strcmp(item, a->group) == 0)) {
            return true;
        }
    }
    return false;
}

const char *SS4S_ModuleInfoGetId(const SS4S_ModuleInfo *info) {
    if (info == NULL) {
        return NULL;
    }
    return info->id;
}

const char *SS4S_ModuleInfoGetName(const SS4S_ModuleInfo *info) {
    if (info == NULL) {
        return NULL;
    }
    return info->name;
}

const char *SS4S_ModuleInfoGetGroup(const SS4S_ModuleInfo *info) {
    if (info == NULL) {
        return NULL;
    }
    if (info->group == NULL) {
        return info->id;
    }
    return info->group;
}

static int modules_ini_handler(void *user, const char *section, const char *name, const char *value) {
    modules_parse_context *mpc = user;
    // Only check for same pointer, ignoring content
    if (((section != NULL) != (mpc->current.id != NULL)) ||
        (section != NULL && strcmp(section, mpc->current.id) != 0)) {
        section_changed(mpc);
        mpc->current.id = strdup(section);
    }
    if (strcmp("name", name) == 0) {
        mpc->current.name = strdup(value);
    }
    if (strcmp("group", name) == 0) {
        mpc->current.group = strdup(value);
    } else if (strcmp("weight", name) == 0) {
        mpc->current.weight = (int) strtol(value, NULL, 10);
        if (mpc->current.weight < 0) {
            mpc->current.weight = 0;
        } else if (mpc->current.weight > 100) {
            mpc->current.weight = 100;
        }
    } else if (strcmp("audio", name) == 0) {
        mpc->current.has_audio = strcmp("true", value) == 0;
    } else if (strcmp("video", name) == 0) {
        mpc->current.has_video = strcmp("true", value) == 0;
    } else if (strcmp("os_version", name) == 0) {
        version_constraints_parse(&mpc->current.os_version, value);
    } else if (strcmp("conflicts", name) == 0) {
        str_list_parse(&mpc->current.conflicts, value, ",");
    }
    return 1;
}

static void section_changed(modules_parse_context *context) {
    if (context->current.id == NULL) {
        return;
    }
    if (!version_constraints_check(&context->current.os_version, &context->os_info->version)) {
        module_info_clear(&context->current);
        return;
    }
    SS4S_ModuleInfo *info = array_list_add(context->modules, -1);
    *info = context->current;
    memset(&context->current, 0, sizeof(context->current));
}

static void modules_parse_context_destroy(modules_parse_context *context) {
    (void) context;
}

static void module_info_clear(SS4S_ModuleInfo *info) {
    if (info->id == NULL) {
        return;
    }
    free(info->id);
    if (info->group != NULL) {
        free(info->group);
    }
    if (info->name != NULL) {
        free(info->name);
    }
    str_list_clear(&info->conflicts);
    version_constraints_clear(&info->os_version);
    memset(info, 0, sizeof(SS4S_ModuleInfo));
}

static int module_weight_compare_fn(const void *a, const void *b) {
    const SS4S_ModuleInfo *m1 = a, *m2 = b;
    int weight_diff = m2->weight - m1->weight;
    if (weight_diff == 0) {
        return strcmp(SS4S_ModuleInfoGetGroup(m1), SS4S_ModuleInfoGetGroup(m2));
    }
    return weight_diff;
}

static const SS4S_ModuleInfo *module_by_id(const array_list_t *list, const char *id) {
    for (int i = 0, j = array_list_size(list); i < j; ++i) {
        const SS4S_ModuleInfo *info = array_list_get((array_list_t *) list, i);
        if (strcmp(id, info->id) == 0 || (info->group != NULL && strcmp(id, info->group) == 0)) {
            return info;
        }
    }
    return NULL;
}

static int str_list_parse(str_list_t *list, const char *value, const char *delim) {
    if (value[0] == 0) {
        return 0;
    }
    int n_delim = 0;
    const char *temp = value;
    do {
        const char *find = strpbrk(temp, delim);
        if (find != NULL) {
            find++;
            n_delim++;
        }
        temp = find;
    } while (temp != NULL);
    /* This array should contain at most n_delim + 1 strings */
    char **items = calloc(n_delim + 1, sizeof(char *));
    char *value_dup = strdup(value);
    char *rest = value_dup, *cur;
    int n_items = 0;
    while ((cur = strtok_r(rest, delim, &rest))) {
        items[n_items++] = cur;
    }
    if (n_items == 0) {
        free(value_dup);
        return 0;
    }
    list->count = n_items;
    list->elements = items;
    list->raw = value_dup;
    return n_items;
}


static void str_list_clear(str_list_t *list) {
    if (list->elements != NULL) {
        free(list->elements);
    }
    if (list->raw != NULL) {
        free(list->raw);
    }
    memset(list, 0, sizeof(str_list_t));
}

static bool preference_auto(const char *value) {
    return value == NULL || strcmp(value, MODULE_PREFERENCE_AUTO) == 0;
}