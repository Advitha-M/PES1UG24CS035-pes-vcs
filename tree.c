// tree.c — Tree object serialization and construction

#include "pes.h"
#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// Forward declaration
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

#define MODE_FILE 0100644
#define MODE_EXEC 0100755
#define MODE_DIR  0040000

// ─── PROVIDED ───────────────────────────────────────────────────────────────

uint32_t get_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode))   return MODE_DIR;
    if (st.st_mode & S_IXUSR)  return MODE_EXEC;
    return MODE_FILE;
}

int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;

    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *entry = &tree_out->entries[tree_out->count];

        const uint8_t *space = memchr(ptr, ' ', end - ptr);
        if (!space) return -1;

        char mode_str[16] = {0};
        size_t mode_len = space - ptr;
        if (mode_len >= sizeof(mode_str)) return -1;
        memcpy(mode_str, ptr, mode_len);
        entry->mode = (uint32_t)strtol(mode_str, NULL, 8);
        ptr = space + 1;

        const uint8_t *null_byte = memchr(ptr, '\0', end - ptr);
        if (!null_byte) return -1;

        size_t name_len = null_byte - ptr;
        if (name_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, ptr, name_len);
        entry->name[name_len] = '\0';
        ptr = null_byte + 1;

        if (ptr + HASH_SIZE > end) return -1;
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;

        tree_out->count++;
    }
    return 0;
}

static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    size_t max_size = (size_t)tree->count * 296;
    uint8_t *buffer = malloc(max_size);
    if (!buffer) return -1;

    Tree sorted_tree = *tree;
    qsort(sorted_tree.entries, (size_t)sorted_tree.count,
          sizeof(TreeEntry), compare_tree_entries);

    size_t offset = 0;
    for (int i = 0; i < sorted_tree.count; i++) {
        const TreeEntry *entry = &sorted_tree.entries[i];
        int written = sprintf((char *)buffer + offset, "%o %s",
                              entry->mode, entry->name);
        offset += (size_t)written + 1;
        memcpy(buffer + offset, entry->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }

    *data_out = buffer;
    *len_out  = offset;
    return 0;
}

// ─── TODO: tree_from_index ──────────────────────────────────────────────────

static int build_tree_recursive(IndexEntry *entries, int count,
                                 const char *prefix, ObjectID *id_out) {
    Tree tree;
    tree.count = 0;
    size_t prefix_len = strlen(prefix);

    int i = 0;
    while (i < count) {
        const char *rel   = entries[i].path + prefix_len;
        const char *slash = strchr(rel, '/');

        if (!slash) {
            if (tree.count >= MAX_TREE_ENTRIES) return -1;
            TreeEntry *e = &tree.entries[tree.count++];
            e->mode = entries[i].mode;
            size_t nlen = strlen(rel);
            if (nlen >= sizeof(e->name)) nlen = sizeof(e->name) - 1;
            memcpy(e->name, rel, nlen);
            e->name[nlen] = '\0';
            e->hash = entries[i].hash;
            i++;
        } else {
            size_t dlen = (size_t)(slash - rel);
            char subdir_name[256];
            if (dlen >= sizeof(subdir_name)) return -1;
            memcpy(subdir_name, rel, dlen);
            subdir_name[dlen] = '\0';

            char new_prefix[512];
            snprintf(new_prefix, sizeof(new_prefix),
                     "%s%s/", prefix, subdir_name);
            size_t new_prefix_len = strlen(new_prefix);

            int j = i;
            while (j < count &&
                   strncmp(entries[j].path, new_prefix, new_prefix_len) == 0) {
                j++;
            }

            ObjectID sub_id;
            if (build_tree_recursive(entries + i, j - i,
                                     new_prefix, &sub_id) < 0)
                return -1;

            if (tree.count >= MAX_TREE_ENTRIES) return -1;
            TreeEntry *e = &tree.entries[tree.count++];
            e->mode = MODE_DIR;
            memcpy(e->name, subdir_name, dlen);
            e->name[dlen] = '\0';
            e->hash = sub_id;
            i = j;
        }
    }

    void *tdata; size_t tlen;
    if (tree_serialize(&tree, &tdata, &tlen) < 0) return -1;
    int ret = object_write(OBJ_TREE, tdata, tlen, id_out);
    free(tdata);
    return ret;
}

int tree_from_index(ObjectID *id_out) {
    (void)id_out;
    return -1;
}
