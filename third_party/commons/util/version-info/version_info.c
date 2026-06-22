#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "version_info.h"

#define ALLOWED_OPERAND_CHARS "<>="

int version_info_parse(version_info_t *version, const char *value) {
    int n_segs = 0;
    int segs[3] = {-1, -1, -1};
    _Static_assert(sizeof(segs) == sizeof(*version), "Can't cast int[3] to version_info_t");
    const char *rest = value;
    do {
        char *next;
        int num = strtol(rest, &next, 10);
        if (next == rest) {
            break;
        } else if (*next == '.') {
            ++next;
        }
        segs[n_segs++] = num;
        rest = next;
    } while (n_segs < 3);
    if (n_segs == 0) {
        version->major = -1;
        version->minor = -1;
        version->patch = -1;
        return -1;
    }
    *version = *((version_info_t *) segs);
    return 0;
}

bool version_info_valid(const version_info_t *version) {
    return version->major >= 0;
}

char *version_info_str(const version_info_t *version) {
    if (version->major < 0) {
        return NULL;
    }
    char tmp[64];
    if (version->minor < 0) {
        snprintf(tmp, 64, "%d", version->major);
    } else if (version->patch < 0) {
        snprintf(tmp, 64, "%d.%d", version->major, version->minor);
    } else {
        snprintf(tmp, 64, "%d.%d.%d", version->major, version->minor, version->patch);
    }
    size_t len = strnlen(tmp, 64);
    char *result = malloc(len + 1);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, tmp, len);
    result[len] = '\0';
    return result;
}

int version_info_compare(const version_info_t *a, const version_info_t *b) {
    if (a->major < 0 || b->major < 0) {
        return 0;
    }
    int diff = a->major - b->major;
    if (diff != 0) {
        return diff;
    }
    if (a->minor < 0 || b->minor < 0) return 0;
    diff = a->minor - b->minor;
    if (diff != 0) {
        return diff;
    }
    if (a->patch < 0 || b->patch < 0) return 0;
    diff = a->patch - b->patch;
    if (diff != 0) {
        return diff;
    }
    return 0;
}

int version_constraint_parse(version_constraint_t *constraint, const char *value) {
    // Skip spaces
    while (*value == ' ') {
        value++;
    }
    const char *cur = value;
    // Find first occurrence of non-operand character
    while (1) {
        const char *find = strpbrk(cur, ALLOWED_OPERAND_CHARS);
        if (find != NULL) {
            cur = find + 1;
        } else {
            break;
        }
    }
    int op_len = cur - value;
    if (op_len > 2) {
        // If operand is too long, stop processing
        return -1;
    }
    switch (op_len) {
        case 2: {
            if (strncmp(cur - op_len, ">=", op_len) == 0) {
                constraint->operand = VERSION_GREATER_EQUALS;
                return version_info_parse(&constraint->version, cur);
            } else if (strncmp(cur - op_len, "<=", op_len) == 0) {
                constraint->operand = VERSION_LESSER_EQUALS;
                return version_info_parse(&constraint->version, cur);
            } else if (strncmp(cur - op_len, "<>", op_len) == 0) {
                constraint->operand = VERSION_NOT_EQUALS;
                return version_info_parse(&constraint->version, cur);
            }
            break;
        }
        case 1: {
            switch (*(cur - op_len)) {
                case '=':
                    constraint->operand = VERSION_EQUALS;
                    break;
                case '<':
                    constraint->operand = VERSION_LESSER_THAN;
                    break;
                case '>':
                    constraint->operand = VERSION_GREATER_THAN;
                    break;
                default:
                    return -1;
            }
            return version_info_parse(&constraint->version, cur);
        }
        default: {
            // Only possible value is 0
            constraint->operand = VERSION_EQUALS;
            return version_info_parse(&constraint->version, cur);
        }
    }
    return -1;
}

bool version_constraint_check(const version_constraint_t *constraint, const version_info_t *version) {
    switch (constraint->operand) {
        case VERSION_IGNORE:
            return true;
        case VERSION_EQUALS:
            return version_info_compare(version, &constraint->version) == 0;
        case VERSION_NOT_EQUALS:
            return version_info_compare(version, &constraint->version) != 0;
        case VERSION_GREATER_THAN:
            return version_info_compare(version, &constraint->version) > 0;
        case VERSION_GREATER_EQUALS:
            return version_info_compare(version, &constraint->version) >= 0;
        case VERSION_LESSER_THAN:
            return version_info_compare(version, &constraint->version) < 0;
        case VERSION_LESSER_EQUALS:
            return version_info_compare(version, &constraint->version) <= 0;
    }
}

int version_constraints_parse(version_constraints_t *constraints, const char *value) {
    assert(constraints->count == 0);
    assert(constraints->elements == NULL);

    const char *cur = value;
    size_t n_delim = 0;
    while ((cur = strchr(cur, ',')) != NULL) {
        n_delim++;
        cur++;
    }
    size_t max_constraints = n_delim + 1;
    constraints->elements = calloc(max_constraints, sizeof(version_constraint_t));
    char *tmp = strdup(value);
    char *rest = tmp;
    int n_items = 0;
    while ((cur = strtok_r(rest, ",", &rest)) != NULL) {
        if (version_constraint_parse(&constraints->elements[n_items], cur) != 0) {
            version_constraints_clear(constraints);
            n_items = -1;
            break;
        }
        n_items++;
    }
    free(tmp);
    if (n_items < 0) {
        return -1;
    }
    constraints->count = n_items;
    return 0;
}

bool version_constraints_check(const version_constraints_t *constraints, const version_info_t *version) {
    for (int i = 0, j = constraints->count; i < j; ++i) {
        if (!version_constraint_check(&constraints->elements[i], version)) {
            return false;
        }
    }
    return true;
}

void version_constraints_clear(version_constraints_t *constraints) {
    if (constraints->elements != NULL) {
        free(constraints->elements);
    }
    memset(constraints, 0, sizeof(version_constraints_t));
}