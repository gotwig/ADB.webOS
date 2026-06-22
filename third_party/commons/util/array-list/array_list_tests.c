#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "array_list.h"

static void test_add_get_remove(void) {
    array_list_t *list = array_list_create(sizeof(int), 4);
    for (int i = 0; i < 4; i++) {
        int *slot = array_list_add(list, -1);
        *slot = i * 10;
    }
    assert(array_list_size(list) == 4);
    for (int i = 0; i < 4; i++) {
        assert(*(int *) array_list_get(list, i) == i * 10);
    }

    array_list_remove(list, 1);
    assert(array_list_size(list) == 3);
    assert(*(int *) array_list_get(list, 0) == 0);
    assert(*(int *) array_list_get(list, 1) == 20);
    assert(*(int *) array_list_get(list, 2) == 30);

    array_list_destroy(list);
}

static void test_remove_last(void) {
    array_list_t *list = array_list_create(sizeof(int), 4);
    for (int i = 0; i < 3; i++) {
        *(int *) array_list_add(list, -1) = i;
    }
    array_list_remove(list, 2);
    assert(array_list_size(list) == 2);
    assert(*(int *) array_list_get(list, 0) == 0);
    assert(*(int *) array_list_get(list, 1) == 1);
    array_list_destroy(list);
}

static void test_remove_out_of_bounds(void) {
    array_list_t *list = array_list_create(sizeof(int), 4);
    *(int *) array_list_add(list, -1) = 42;
    array_list_remove(list, 5);
    assert(array_list_size(list) == 1);
    array_list_remove(list, -1);
    assert(array_list_size(list) == 1);
    array_list_remove(list, 1);
    assert(array_list_size(list) == 1);
    array_list_destroy(list);
}

static void test_remove_does_not_read_past_end(void) {
    /* Regression: array_list_remove used to memmove (size - index) items
     * instead of (size - index - 1), reading one element past the buffer.
     * Fill the list exactly to capacity so the slot at index `size`
     * (just past valid data) is also past the allocation. */
    array_list_t *list = array_list_create(sizeof(int), 4);
    for (int i = 0; i < 4; i++) {
        *(int *) array_list_add(list, -1) = i + 1;
    }
    array_list_remove(list, 0);
    assert(array_list_size(list) == 3);
    assert(*(int *) array_list_get(list, 0) == 2);
    assert(*(int *) array_list_get(list, 1) == 3);
    assert(*(int *) array_list_get(list, 2) == 4);
    /* The slot at the former last index should still hold its
     * original value; the buggy code clobbered it with an OOB read. */
    const int *raw = (const int *) list->data;
    assert(raw[3] == 4);
    array_list_destroy(list);
}

static void test_grow_beyond_initial_capacity(void) {
    /* Regression: ensure_capacity used to clobber list->data on realloc
     * failure and doubled capacity even when starting from zero. */
    array_list_t *list = array_list_create(sizeof(int), 2);
    for (int i = 0; i < 100; i++) {
        int *slot = array_list_add(list, -1);
        *slot = i;
    }
    assert(array_list_size(list) == 100);
    for (int i = 0; i < 100; i++) {
        assert(*(int *) array_list_get(list, i) == i);
    }
    assert(list->capacity >= 100);
    array_list_destroy(list);
}

static void test_insert_at_index(void) {
    array_list_t *list = array_list_create(sizeof(int), 4);
    for (int i = 0; i < 3; i++) {
        *(int *) array_list_add(list, -1) = (i + 1) * 10;
    }
    int *slot = array_list_add(list, 1);
    *slot = 99;
    assert(array_list_size(list) == 4);
    assert(*(int *) array_list_get(list, 0) == 10);
    assert(*(int *) array_list_get(list, 1) == 99);
    assert(*(int *) array_list_get(list, 2) == 20);
    assert(*(int *) array_list_get(list, 3) == 30);
    array_list_destroy(list);
}

int main(void) {
    test_add_get_remove();
    test_remove_last();
    test_remove_out_of_bounds();
    test_remove_does_not_read_past_end();
    test_grow_beyond_initial_capacity();
    test_insert_at_index();
    return 0;
}
