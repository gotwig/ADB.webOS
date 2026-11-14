#pragma once

#include <stdbool.h>

#include "os_info.h"
#include "version_info.h"
#include "ss4s/module.h"

#define MODULE_PREFERENCE_AUTO "auto"

typedef struct array_list_t array_list_t;

typedef struct str_list_t {
    int count;
    char **elements;
    char *raw;
} str_list_t;

typedef struct module_info_t {
    char *id;
    char *group;
    char *name;
    bool has_audio;
    bool has_video;
    int weight;
    str_list_t conflicts;

    version_constraints_t os_version;
} SS4S_ModuleInfo;

typedef struct module_preferences_t {
    const char *audio_module;
    const char *video_module;
} SS4S_ModulePreferences;

typedef struct module_selection_t {
    const SS4S_ModuleInfo *audio_module;
    const SS4S_ModuleInfo *video_module;
} SS4S_ModuleSelection;

/**
 * Load modules info into the modules pointer
 * @param modules Array modules pointer to load into
 * @param os_info Runtime system information
 * @return 0 if succeeded
 */
int SS4S_ModulesList(array_list_t *modules, const os_info_t *os_info);

/**
 * This DOES NOT free the modules pointer itself.
 * @param modules Modules modules to clear all the content
 */
void SS4S_ModulesListClear(array_list_t *modules);

bool SS4S_ModulesSelect(const array_list_t *modules, const SS4S_ModulePreferences *preferences,
                        SS4S_ModuleSelection *selection, bool checkModule);

bool SS4S_ModuleInfoConflicts(const SS4S_ModuleInfo *a, const SS4S_ModuleInfo *b);

const char *SS4S_ModuleInfoGetId(const SS4S_ModuleInfo *info);

const char *SS4S_ModuleInfoGetName(const SS4S_ModuleInfo *info);

const char *SS4S_ModuleInfoGetGroup(const SS4S_ModuleInfo *info);