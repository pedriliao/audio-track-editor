#include "sound_seg.h"

/* ========================================================================
 * Helper: find the node immediately before 'target' in the linked list.
 * Returns NULL if target is the head or not found.
 * ======================================================================== */
track_node* find_prev_node(sound_seg *track, track_node *target) {
    if (!track || !track->head || track->head == target)
        return NULL;
    track_node *cur = track->head;
    while (cur->next) {
        if (cur->next == target)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

/* ========================================================================
 * Debug helpers - only compiled when DEBUG is defined.
 * The spec forbids printf in submissions (it uses dynamic memory internally
 * which conflicts with the test framework's malloc/free tracking).
 * ======================================================================== */
#ifdef DEBUG
void debug_print_track(const char *name, track_node *head) {
    if (!head) {
        fprintf(stderr, "Track \"%s\" is NULL\n", name);
        return;
    }
    size_t total_length = 0;
    track_node *node = head;
    while (node) {
        total_length += node->tra_len;
        node = node->next;
    }
    fprintf(stderr, "Track \"%s\": total length = %zu\n", name, total_length);
    int idx = 0;
    node = head;
    while (node) {
        if (node->seg) {
            fprintf(stderr, "  Node %d: seg->data=%p, seg->length=%zu, ref=%d, pa_or_ch=%zu, offset=%zu, tra_len=%zu\n",
                   idx, (void*)node->seg->data, node->seg->length, node->seg->ref_count,
                   node->pa_or_ch, node->offset, node->tra_len);
        }
        idx++;
        node = node->next;
    }
}

void debug_print_parent_track(const char *name, track_node *head) {
    (void)name; (void)head;
}

void debug_print_seg(const char *name, sound_seg *seg) {
    (void)name; (void)seg;
}

void debug_print_segment(shared_block *seg) {
    (void)seg;
}
#else
/* No-op stubs when DEBUG is not defined */
void debug_print_track(const char *name, track_node *head) {
    (void)name; (void)head;
}
void debug_print_parent_track(const char *name, track_node *head) {
    (void)name; (void)head;
}
void debug_print_seg(const char *name, sound_seg *seg) {
    (void)name; (void)seg;
}
void debug_print_segment(shared_block *seg) {
    (void)seg;
}
#endif

/* ========================================================================
 * Part 1: WAV file I/O
 * ASM 0.1: PCM, 16-bit, mono, 8000Hz
 * ASM 0.2: paths always valid, IO always successful
 * ======================================================================== */

/* Load raw audio samples from a WAV file into dest buffer.
 * Skips the WAV header and non-data chunks, copies only sample data. */
void wav_load(const char* filename, int16_t* dest) {
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) return;

    /* Skip RIFF header (12 bytes: "RIFF" + size + "WAVE") */
    fseek(fp, 12, SEEK_SET);

    char chunk_name[5];
    uint32_t chunk_size;
    while (true) {
        if (fread(chunk_name, 1, 4, fp) != 4) break;
        chunk_name[4] = '\0';
        if (fread(&chunk_size, sizeof(uint32_t), 1, fp) != 1) break;

        if (strcmp(chunk_name, "data") != 0) {
            /* Skip non-data chunks (with padding for odd sizes) */
            fseek(fp, chunk_size, SEEK_CUR);
            if (chunk_size % 2 != 0) fseek(fp, 1, SEEK_CUR);
            continue;
        }
        /* Read audio samples from the data chunk */
        fread(dest, sizeof(int16_t), chunk_size / sizeof(int16_t), fp);
        break;
    }
    fclose(fp);
}

/* Save audio samples to a WAV file with proper header.
 * Constructs a valid PCM WAV: mono, 16-bit, 8000Hz sample rate. */
void wav_save(const char* fname, int16_t* src, size_t len) {
    FILE *fp = fopen(fname, "wb");
    if (fp == NULL) return;

    uint16_t wFormatTag      = 1;       /* PCM */
    uint16_t nChannels       = 1;       /* mono */
    uint32_t nSamplesPerSec  = 8000;
    uint16_t bitsPerSample   = 16;
    uint16_t nBlockAlign     = nChannels * (bitsPerSample / 8);
    uint32_t nAvgBytesPerSec = nSamplesPerSec * nBlockAlign;

    uint32_t sub_size   = (uint32_t)(len * sizeof(int16_t));
    uint32_t chunk_size = 36 + sub_size;

    /* RIFF header */
    fwrite("RIFF", 1, 4, fp);
    fwrite(&chunk_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);

    /* fmt sub-chunk */
    fwrite("fmt ", 1, 4, fp);
    uint32_t cksize = 16;
    fwrite(&cksize, 4, 1, fp);
    fwrite(&wFormatTag, 2, 1, fp);
    fwrite(&nChannels, 2, 1, fp);
    fwrite(&nSamplesPerSec, 4, 1, fp);
    fwrite(&nAvgBytesPerSec, 4, 1, fp);
    fwrite(&nBlockAlign, 2, 1, fp);
    fwrite(&bitsPerSample, 2, 1, fp);

    /* data sub-chunk */
    fwrite("data", 1, 4, fp);
    fwrite(&sub_size, 4, 1, fp);
    fwrite(src, sizeof(int16_t), len, fp);

    /* Pad byte for odd data size */
    if (sub_size % 2 == 1) {
        uint8_t pad = 0;
        fwrite(&pad, 1, 1, fp);
    }
    fclose(fp);
}

/* ========================================================================
 * Track init / destroy
 * ======================================================================== */

/* Allocate and initialise a new empty track. */
struct sound_seg* tr_init() {
    struct sound_seg* new_track = malloc(sizeof(struct sound_seg));
    if (!new_track) return NULL;
    new_track->head   = NULL;
    new_track->length = 0;
    return new_track;
}

/* Free all resources owned by a track.
 * Decrements shared_block ref_count; frees the block when it reaches 0. */
void tr_destroy(sound_seg* obj) {
    if (obj == NULL) return;

    track_node *node = obj->head;
    while (node != NULL) {
        track_node *next_node = node->next;

        if (node->seg != NULL) {
            shared_block *seg = node->seg;
            seg->ref_count--;
            if (seg->ref_count == 0) {
                if (seg->data != NULL) free(seg->data);
                free(seg);
            }
        }
        free(node);
        node = next_node;
    }
    free(obj);
}

/* ========================================================================
 * Part 1: Basic track operations (length, read, write, delete)
 * ======================================================================== */

/* Return the number of samples in the track. */
size_t tr_length(struct sound_seg* seg) {
    return seg->length;
}

/* Copy 'len' samples starting at position 'pos' from track into dest buffer.
 * Walks the linked list of nodes to gather data that may span multiple nodes. */
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    size_t data_offset = 0;       /* write cursor into dest */
    size_t pos_left    = len;     /* remaining samples to read */
    size_t add_total   = 0;       /* cumulative position in track */
    size_t crossed     = 0;       /* flag: already inside a multi-node span */
    struct track_node *now_node = track->head;

    while (now_node != NULL) {
        struct track_node *next_node = now_node->next;

        /* Skip nodes entirely before the read region */
        if (add_total + now_node->tra_len <= pos) {
            add_total += now_node->tra_len;
            now_node = next_node;
            continue;
        }

        if (!crossed) {
            /* First node that overlaps the read region */
            size_t read_start = pos - add_total;

            if (pos_left <= now_node->tra_len - read_start) {
                /* Entire read fits in this node */
                memcpy(dest + data_offset,
                       now_node->seg->data + now_node->offset + read_start,
                       pos_left * sizeof(int16_t));
                pos_left = 0;
                break;
            } else {
                /* Partial read from this node, continue to next */
                size_t chunk = now_node->tra_len - read_start;
                memcpy(dest + data_offset,
                       now_node->seg->data + now_node->offset + read_start,
                       chunk * sizeof(int16_t));
                pos_left    -= chunk;
                data_offset += chunk;
                crossed = 1;
                add_total += now_node->tra_len;
            }
        } else {
            /* Subsequent nodes in a multi-node read */
            if (pos_left <= now_node->tra_len) {
                memcpy(dest + data_offset,
                       now_node->seg->data + now_node->offset,
                       pos_left * sizeof(int16_t));
                pos_left = 0;
                break;
            } else {
                memcpy(dest + data_offset,
                       now_node->seg->data + now_node->offset,
                       now_node->tra_len * sizeof(int16_t));
                data_offset += now_node->tra_len;
                pos_left    -= now_node->tra_len;
                add_total   += now_node->tra_len;
            }
        }

        if (next_node != NULL) {
            now_node = next_node;
        } else {
            break;
        }
    }
}

/* Helper: create a new track_node with a freshly-allocated shared_block.
 * The shared_block's data pointer is left NULL (caller must allocate). */
track_node *create_track_node(size_t sample_count, size_t offset,
                              size_t tra_len, size_t pa_or_ch) {
    shared_block *new_block = malloc(sizeof(shared_block));
    if (new_block == NULL) return NULL;
    new_block->data      = NULL;
    new_block->length    = sample_count;
    new_block->ref_count = 1;

    track_node *new_node = malloc(sizeof(track_node));
    if (new_node == NULL) {
        free(new_block);
        return NULL;
    }
    new_node->seg      = new_block;
    new_node->offset   = offset;
    new_node->tra_len  = tra_len;
    new_node->pa_or_ch = pa_or_ch;
    new_node->next     = NULL;
    return new_node;
}

/* Write 'len' samples from src buffer into track at position 'pos'.
 * Overwrites existing data; extends the track if writing past the end. */
void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    /* Special case: empty track -> create the first node */
    if (track->head == NULL) {
        track_node *new_head = create_track_node(len, 0, len, 2);
        if (!new_head) return;
        new_head->seg->data = malloc(len * sizeof(int16_t));
        if (!new_head->seg->data) {
            free(new_head->seg);
            free(new_head);
            return;
        }
        memcpy(new_head->seg->data, src, len * sizeof(int16_t));
        track->head = new_head;
        track->length += len;
        return;
    }

    size_t data_offset = 0;
    size_t pos_left    = len;
    size_t add_total   = 0;
    size_t crossed     = 0;
    struct track_node *now_node = track->head;

    while (now_node != NULL) {
        struct track_node *next_node = now_node->next;

        /* If writing at exactly the track end, break to extend below */
        if (pos == track->length) break;

        /* Skip nodes entirely before the write position */
        if (add_total + now_node->tra_len <= pos) {
            add_total += now_node->tra_len;
            now_node = next_node;
            continue;
        }

        if (!crossed) {
            size_t write_start = pos - add_total;
            if (pos_left <= now_node->tra_len - write_start) {
                memcpy(now_node->seg->data + now_node->offset + write_start,
                       src + data_offset, pos_left * sizeof(int16_t));
                pos_left = 0;
                break;
            } else {
                size_t chunk = now_node->tra_len - write_start;
                memcpy(now_node->seg->data + now_node->offset + write_start,
                       src + data_offset, chunk * sizeof(int16_t));
                pos_left    -= chunk;
                data_offset += chunk;
                crossed = 1;
                add_total += now_node->tra_len;
            }
        } else {
            if (pos_left <= now_node->tra_len) {
                memcpy(now_node->seg->data + now_node->offset,
                       src + data_offset, pos_left * sizeof(int16_t));
                pos_left = 0;
                break;
            } else {
                memcpy(now_node->seg->data + now_node->offset,
                       src + data_offset, now_node->tra_len * sizeof(int16_t));
                data_offset += now_node->tra_len;
                pos_left    -= now_node->tra_len;
                crossed = 1;
                add_total += now_node->tra_len;
            }
        }

        if (next_node != NULL)
            now_node = next_node;
        else
            break;
    }

    /* Extend the track if there are remaining samples to write */
    if (pos_left != 0) {
        track_node *ext = create_track_node(pos_left, 0, pos_left, 2);
        if (!ext) return;
        ext->seg->data = malloc(pos_left * sizeof(int16_t));
        if (!ext->seg->data) {
            free(ext->seg);
            free(ext);
            return;
        }
        memcpy(ext->seg->data, src + data_offset, pos_left * sizeof(int16_t));
        track->length += pos_left;

        /* Append to end of linked list */
        struct track_node *last = track->head;
        if (last == NULL) {
            track->head = ext;
        } else {
            while (last->next != NULL) last = last->next;
            last->next = ext;
        }
    }
}

/* ========================================================================
 * Delete range
 * REQ 3.1: reads/writes over deleted portion act as if adjacent parts continuous
 * REQ 3.2: cannot delete a parent portion that still has children -> return false
 * ======================================================================== */

/* Check whether a parent node and all its immediately-following children
 * fall within [pos, end). Used for the simple case where children follow parent. */
bool check_child_in_range(track_node *parent, size_t pos, size_t end,
                          size_t parent_start) {
    size_t parent_end = parent_start + parent->tra_len;
    if (parent_start < pos || parent_end > end) return false;

    size_t cumulative = parent_end;
    track_node *node = parent->next;
    while (node != NULL && node->pa_or_ch != 0) {
        size_t child_start = cumulative;
        size_t child_end   = cumulative + node->tra_len;
        if (child_start < pos || child_end > end) return false;
        cumulative = child_end;
        node = node->next;
    }
    return true;
}

/* Check if ALL other references to the parent's block can be accounted for
 * by nodes in THIS track that are fully within [pos, end).
 * This handles the case where children appear before the parent in the linked
 * list (e.g., self-insert). Children in OTHER tracks cannot be detected here,
 * so we compare the count of in-range siblings with ref_count - 1. */
static bool all_block_refs_in_range(sound_seg *track, track_node *parent_node,
                                    size_t pos, size_t end) {
    size_t refs_in_range = 0;
    size_t total = 0;
    track_node *node = track->head;
    while (node != NULL) {
        if (node->seg == parent_node->seg && node != parent_node) {
            size_t node_start = total;
            size_t node_end   = total + node->tra_len;
            if (node_start >= pos && node_end <= end)
                refs_in_range++;
            else
                return false;  /* a sibling in this track is NOT in range */
        }
        total += node->tra_len;
        node = node->next;
    }
    /* All sibling nodes in this track are in range. But are there children
     * in other tracks? ref_count - 1 = total other references needed. */
    return refs_in_range >= (size_t)(parent_node->seg->ref_count - 1);
}

/* Delete 'len' samples from track starting at 'pos'.
 * Returns false if the range contains a parent with active children
 * that would not also be deleted. */
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    size_t end = pos + len;

    /* Phase 1: check for parent nodes that cannot be deleted.
     * A parent can be (partially) deleted if ALL children sharing its block
     * are fully within the delete range (so they'll also be removed). */
    size_t total = 0;
    track_node *node = track->head;
    while (node != NULL && total < end) {
        size_t node_start = total;
        size_t node_end   = total + node->tra_len;

        if (node_end > pos) {
            if (node->pa_or_ch == 0 && node->seg->ref_count > 1) {
                /* First try: children immediately follow parent */
                if (check_child_in_range(node, pos, end, node_start)) {
                    /* Skip parent + its immediate children */
                    size_t skip = node->tra_len;
                    track_node *temp = node->next;
                    while (temp != NULL && temp->pa_or_ch != 0) {
                        skip += temp->tra_len;
                        temp = temp->next;
                    }
                    total = node_start + skip;
                    node = temp;
                    continue;
                }
                /* Second try: children may be elsewhere in the track
                 * (e.g., self-insert puts child before parent) */
                if (!all_block_refs_in_range(track, node, pos, end))
                    return false;
            }
        }
        total = node_end;
        node = node->next;
    }

    /* Phase 2: perform the actual deletion */
    size_t pos_left  = len;
    size_t add_total = 0;
    size_t crossed   = 0;
    struct track_node *now_node = track->head;

    while (now_node != NULL) {
        struct track_node *next_node = now_node->next;

        if (add_total + now_node->tra_len <= pos) {
            add_total += now_node->tra_len;
            now_node = next_node;
            continue;
        }

        /* Note: Phase 1 already validated that all parent-child constraints
         * are satisfied. When children are deleted before the parent (as in
         * self-insert), ref_count is decremented, so the parent is treated
         * as a normal node by the time Phase 2 reaches it. */

        if (!crossed) {
            size_t read_start = pos - add_total;

            if (now_node->pa_or_ch != 2 && now_node->seg->ref_count > 1) {
                /* Child node: adjust offset/length, keep shared block */
                if (pos_left <= now_node->tra_len - read_start) {
                    if (read_start != 0) {
                        size_t right_len = now_node->tra_len - read_start - pos_left;
                        if (right_len != 0) {
                            track_node *mid = create_track_node(
                                right_len,
                                read_start + pos_left + now_node->offset,
                                right_len, now_node->pa_or_ch);
                            free(mid->seg);
                            mid->seg = now_node->seg;
                            mid->seg->ref_count += 1;
                            now_node->tra_len = read_start;
                            mid->next = now_node->next;
                            now_node->next = mid;
                        } else {
                            now_node->tra_len = read_start;
                        }
                    } else {
                        now_node->offset  += pos_left;
                        now_node->tra_len -= pos_left;
                        if (now_node->tra_len == 0) {
                            now_node->seg->ref_count -= 1;
                            track_node *prev = find_prev_node(track, now_node);
                            if (prev == NULL) track->head = now_node->next;
                            else prev->next = now_node->next;
                            free(now_node);
                        }
                    }
                    pos_left = 0;
                    break;
                } else {
                    add_total += now_node->tra_len;
                    pos_left -= (now_node->tra_len - read_start);
                    now_node->tra_len = read_start;
                    if (now_node->tra_len == 0) {
                        now_node->seg->ref_count -= 1;
                        track_node *prev = find_prev_node(track, now_node);
                        if (prev == NULL) track->head = now_node->next;
                        else prev->next = now_node->next;
                        free(now_node);
                    }
                    crossed = 1;
                }
            } else {
                /* Normal/own node: may need to split or remove */
                if (pos_left <= now_node->tra_len - read_start) {
                    if (read_start != 0) {
                        size_t right_len = now_node->tra_len - read_start - pos_left;
                        track_node *mid = create_track_node(right_len, 0, right_len, 2);
                        mid->seg->data = malloc(right_len * sizeof(int16_t));
                        if (!mid->seg->data) {
                            free(mid->seg);
                            free(mid);
                            return false;
                        }
                        memcpy(mid->seg->data,
                               now_node->seg->data + read_start + pos_left + now_node->offset,
                               right_len * sizeof(int16_t));
                        now_node->tra_len = read_start;
                        mid->next = now_node->next;
                        now_node->next = mid;
                    } else {
                        now_node->offset  += pos_left;
                        now_node->tra_len -= pos_left;
                        if (now_node->tra_len == 0) {
                            now_node->seg->ref_count -= 1;
                            track_node *prev = find_prev_node(track, now_node);
                            if (prev == NULL) track->head = now_node->next;
                            else prev->next = now_node->next;
                            if (now_node->seg->ref_count == 0) {
                                free(now_node->seg->data);
                                free(now_node->seg);
                            }
                            free(now_node);
                        }
                    }
                    pos_left = 0;
                    break;
                } else {
                    add_total += now_node->tra_len;
                    pos_left -= (now_node->tra_len - read_start);
                    now_node->tra_len = read_start;
                    crossed = 1;
                    if (now_node->tra_len == 0) {
                        now_node->seg->ref_count -= 1;
                        track_node *prev = find_prev_node(track, now_node);
                        if (prev == NULL) track->head = now_node->next;
                        else prev->next = now_node->next;
                        if (now_node->seg->ref_count == 0) {
                            free(now_node->seg->data);
                            free(now_node->seg);
                        }
                        free(now_node);
                    }
                }
            }
        } else {
            /* Multi-node deletion: subsequent nodes */
            if (now_node->pa_or_ch != 2 && now_node->seg->ref_count > 1) {
                if (pos_left <= now_node->tra_len) {
                    now_node->tra_len -= pos_left;
                    now_node->offset  += pos_left;
                    if (now_node->tra_len == 0) {
                        now_node->seg->ref_count -= 1;
                        track_node *prev = find_prev_node(track, now_node);
                        if (prev == NULL) track->head = now_node->next;
                        else prev->next = now_node->next;
                        free(now_node);
                    }
                    pos_left = 0;
                    break;
                } else {
                    add_total += now_node->tra_len;
                    pos_left -= now_node->tra_len;
                    now_node->tra_len = 0;
                    now_node->seg->ref_count -= 1;
                    track_node *prev = find_prev_node(track, now_node);
                    if (prev == NULL) track->head = now_node->next;
                    else prev->next = now_node->next;
                    free(now_node);
                    crossed = 1;
                }
            } else {
                if (pos_left <= now_node->tra_len) {
                    now_node->offset  += pos_left;
                    now_node->tra_len -= pos_left;
                    if (now_node->tra_len == 0) {
                        now_node->seg->ref_count -= 1;
                        track_node *prev = find_prev_node(track, now_node);
                        if (prev == NULL) track->head = now_node->next;
                        else prev->next = now_node->next;
                        if (now_node->seg->ref_count == 0) {
                            free(now_node->seg->data);
                            free(now_node->seg);
                        }
                        free(now_node);
                    }
                    pos_left = 0;
                    break;
                } else {
                    add_total += now_node->tra_len;
                    pos_left -= now_node->tra_len;
                    crossed = 1;
                    now_node->tra_len = 0;
                    now_node->seg->ref_count -= 1;
                    track_node *prev = find_prev_node(track, now_node);
                    if (prev == NULL) track->head = now_node->next;
                    else prev->next = now_node->next;
                    if (now_node->seg->ref_count == 0) {
                        free(now_node->seg->data);
                        free(now_node->seg);
                    }
                    free(now_node);
                }
            }
        }

        if (next_node != NULL)
            now_node = next_node;
        else
            break;
    }

    track->length -= len;
    return true;
}

/* ========================================================================
 * Part 2: Ad identification via cross-correlation
 * REQ 4.1: identify non-overlapping occurrences of ad in target
 * Uses normalised cross-correlation: match if Ryx >= 0.95 * Rxx
 * ======================================================================== */

/* Read all samples from a track into a contiguous heap buffer.
 * Caller is responsible for freeing the returned pointer. */
static int16_t* flatten_track(struct sound_seg* track, size_t *out_len) {
    *out_len = track->length;
    if (*out_len == 0) return NULL;
    int16_t *buf = malloc(*out_len * sizeof(int16_t));
    if (!buf) return NULL;
    tr_read(track, buf, 0, *out_len);
    return buf;
}

/* Identify all non-overlapping occurrences of 'ad' in 'target'.
 * Returns a dynamically-allocated string of "<start>,<end>\n..." pairs.
 * Returns empty string "" if no ads found. Caller must free the result. */
char* tr_identify(struct sound_seg* target, struct sound_seg* ad) {
    size_t target_len, ad_len;
    int16_t *target_data = flatten_track(target, &target_len);
    int16_t *ad_data     = flatten_track(ad, &ad_len);

    /* Compute ad autocorrelation (reference energy) */
    int64_t Rxx = 0;
    for (size_t i = 0; i < ad_len; i++) {
        Rxx += (int64_t)ad_data[i] * ad_data[i];
    }

    /* Edge case: zero-energy ad cannot be detected */
    if (Rxx == 0 || target_len < ad_len) {
        free(target_data);
        free(ad_data);
        char *empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    /* Dynamically-sized result buffer */
    size_t buf_cap = 256;
    size_t buf_len = 0;
    char *result = malloc(buf_cap);
    if (!result) {
        free(target_data);
        free(ad_data);
        return NULL;
    }
    result[0] = '\0';

    /* Slide window across target, compute cross-correlation at each position */
    size_t start = 0;
    double threshold = 0.95 * (double)Rxx;

    while (start <= target_len - ad_len) {
        int64_t Ryx = 0;
        for (size_t i = 0; i < ad_len; i++) {
            Ryx += (int64_t)target_data[start + i] * ad_data[i];
        }

        if ((double)Ryx >= threshold) {
            /* Format this match as "start,end" */
            char entry[64];
            int written = snprintf(entry, sizeof(entry), "%zu,%zu",
                                   start, start + ad_len - 1);
            /* Grow buffer if needed: entry + newline separator + null */
            while (buf_len + written + 2 > buf_cap) {
                buf_cap *= 2;
                char *tmp = realloc(result, buf_cap);
                if (!tmp) {
                    free(result);
                    free(target_data);
                    free(ad_data);
                    return NULL;
                }
                result = tmp;
            }
            /* Append newline separator between entries */
            if (buf_len > 0) {
                result[buf_len] = '\n';
                buf_len++;
            }
            memcpy(result + buf_len, entry, written);
            buf_len += written;
            result[buf_len] = '\0';

            /* Skip past this ad (non-overlapping) */
            start += ad_len;
        } else {
            start++;
        }
    }

    free(target_data);
    free(ad_data);
    return result;
}

/* ========================================================================
 * Part 3: Complex insertions (tr_insert)
 *
 * tr_insert creates a parent-child relationship via shared backing store.
 * Parent = source portion, Child = inserted copy in dest.
 * pa_or_ch: 0 = parent, 1 = child, 2 = normal (no relationship)
 * ======================================================================== */

/* Allocate a track_node with its own new shared_block (data=NULL, ref=1). */
track_node* init_track_node(void) {
    track_node* tn = malloc(sizeof(track_node));
    if (!tn) return NULL;
    tn->pa_or_ch = 2;
    tn->offset   = 0;
    tn->tra_len  = 0;
    tn->next     = NULL;

    tn->seg = malloc(sizeof(shared_block));
    if (!tn->seg) {
        free(tn);
        return NULL;
    }
    tn->seg->data      = NULL;
    tn->seg->length    = 0;
    tn->seg->ref_count = 1;
    return tn;
}

/* Free a track_node and its shared_block (including data). */
void free_track_node(track_node *node) {
    if (node->seg != NULL) {
        if (node->seg->data != NULL) free(node->seg->data);
        free(node->seg);
    }
    free(node);
}

/* Split a source node to extract [start, start+len) as a new parent node.
 * Three cases: extract from beginning, end, or middle of the node.
 * Returns the new parent node, or the original node if it's an exact match. */
track_node* simple_three_split(sound_seg *dest_track, track_node *ori_node,
                               size_t len, size_t start, size_t end) {
    /* Exact match: the entire node becomes the parent */
    if (start == 0 && ori_node->tra_len == len) {
        ori_node->seg->ref_count += 1;
        ori_node->pa_or_ch = 0;
        return ori_node;
    }

    track_node *first_node = init_track_node();
    if (!first_node) return NULL;

    if (start == 0) {
        /* Extract from start: [0, len) becomes parent, rest stays */
        first_node->seg->length = len;
        first_node->seg->data = malloc(sizeof(int16_t) * len);
        if (!first_node->seg->data) { free_track_node(first_node); return NULL; }
        memcpy(first_node->seg->data, ori_node->seg->data + ori_node->offset,
               len * sizeof(int16_t));
        first_node->tra_len = len;

        track_node *prev = find_prev_node(dest_track, ori_node);
        if (prev == NULL) dest_track->head = first_node;
        else prev->next = first_node;
        first_node->next = ori_node;

        ori_node->offset  += len;
        ori_node->tra_len -= len;
        first_node->seg->ref_count += 1;
        first_node->pa_or_ch = 0;
        return first_node;
    }

    if (start + len == ori_node->seg->length) {
        /* Extract from end: parent is [start, end) */
        first_node->seg->length = len;
        first_node->tra_len     = len;
        first_node->seg->data = malloc(sizeof(int16_t) * len);
        if (!first_node->seg->data) { free_track_node(first_node); return NULL; }
        memcpy(first_node->seg->data,
               ori_node->seg->data + start + ori_node->offset,
               len * sizeof(int16_t));
        first_node->next = ori_node->next;
        first_node->seg->ref_count += 1;
        first_node->pa_or_ch = 0;
        ori_node->tra_len = start;
        ori_node->next = first_node;
        return first_node;
    }

    /* Middle split: ori -> [0,start) | parent [start,end) | [end, ...) */
    track_node *middle_node = init_track_node();
    if (!middle_node) return NULL;

    first_node->seg->length = len;
    first_node->tra_len     = len;
    first_node->seg->data = malloc(sizeof(int16_t) * len);
    if (!first_node->seg->data) {
        free_track_node(first_node);
        free_track_node(middle_node);
        return NULL;
    }
    memcpy(first_node->seg->data,
           ori_node->seg->data + start + ori_node->offset,
           len * sizeof(int16_t));
    first_node->next = middle_node;
    first_node->seg->ref_count += 1;

    middle_node->next = ori_node->next;
    size_t right_len = ori_node->tra_len - end;
    free(middle_node->seg);
    middle_node->seg = ori_node->seg;
    middle_node->offset  = ori_node->offset + end;
    middle_node->tra_len = right_len;
    middle_node->seg->ref_count += 1;

    first_node->pa_or_ch = 0;
    ori_node->next    = first_node;
    ori_node->tra_len = start;
    return first_node;
}

/* Start of a multi-segment parent extraction.
 * Extracts [start, node_end) from ori_node into a new parent node. */
track_node* across_multi_segment_start(sound_seg *dest_track,
                                       track_node *ori_node,
                                       size_t start, size_t len) {
    (void)dest_track;
    track_node *first_node = init_track_node();
    if (!first_node) return NULL;

    first_node->seg->length = len;
    first_node->tra_len     = len;
    first_node->seg->data = malloc(sizeof(int16_t) * len);
    if (!first_node->seg->data) { free(first_node); return NULL; }
    memcpy(first_node->seg->data,
           ori_node->seg->data + start + ori_node->offset,
           len * sizeof(int16_t));
    first_node->next = ori_node->next;
    first_node->seg->ref_count += 1;
    first_node->pa_or_ch = 0;

    ori_node->next    = first_node;
    ori_node->tra_len = start;
    return first_node;
}

/* Continue multi-segment extraction: merge ori_node entirely into parent. */
track_node* across_multi_segment_continue(sound_seg *dest_track,
                                          track_node *ori_node,
                                          track_node *parent_node) {
    size_t old_length = parent_node->seg->length;
    size_t new_length = old_length + ori_node->tra_len;

    track_node *first_node = init_track_node();
    if (!first_node) return NULL;

    first_node->seg->length = new_length;
    first_node->tra_len     = new_length;
    first_node->seg->data = malloc(sizeof(int16_t) * new_length);
    if (!first_node->seg->data) { free(first_node); return NULL; }

    memcpy(first_node->seg->data,
           parent_node->seg->data, parent_node->seg->length * sizeof(int16_t));
    memcpy(first_node->seg->data + parent_node->seg->length,
           ori_node->seg->data + ori_node->offset,
           ori_node->tra_len * sizeof(int16_t));

    track_node *prev = find_prev_node(dest_track, parent_node);
    if (prev == NULL) dest_track->head = first_node;
    else prev->next = first_node;

    first_node->next = ori_node->next;
    first_node->seg->ref_count = parent_node->seg->ref_count;
    ori_node->tra_len = 0;
    free_track_node(parent_node);

    ori_node->seg->ref_count -= 1;
    if (ori_node->seg->ref_count == 0)
        free_track_node(ori_node);
    else
        free(ori_node);

    first_node->pa_or_ch = 0;
    return first_node;
}

/* End of multi-segment extraction: merge first 'len' samples of ori_node. */
track_node* across_multi_segment_end(sound_seg *dest_track,
                                     track_node *ori_node,
                                     track_node *parent_node, size_t len) {
    size_t old_length = parent_node->seg->length;
    size_t new_length = old_length + len;

    track_node *first_node = init_track_node();
    if (!first_node) return NULL;

    first_node->seg->length = new_length;
    first_node->tra_len     = new_length;
    first_node->seg->data = malloc(sizeof(int16_t) * new_length);
    if (!first_node->seg->data) { free(first_node); return NULL; }

    memcpy(first_node->seg->data,
           parent_node->seg->data, parent_node->seg->length * sizeof(int16_t));
    memcpy(first_node->seg->data + parent_node->seg->length,
           ori_node->seg->data + ori_node->offset, len * sizeof(int16_t));

    track_node *prev = find_prev_node(dest_track, parent_node);
    if (prev == NULL) dest_track->head = first_node;
    else prev->next = first_node;

    first_node->seg->ref_count = parent_node->seg->ref_count;

    if (len == ori_node->seg->length) {
        ori_node->tra_len = 0;
        first_node->next = ori_node->next;
        first_node->pa_or_ch = 0;
        free_track_node(parent_node);
        free_track_node(ori_node);
        return first_node;
    }

    first_node->pa_or_ch = 0;
    first_node->next = ori_node;
    ori_node->offset  += len;
    ori_node->tra_len -= len;
    free_track_node(parent_node);
    return first_node;
}

/* Insert the parent node's data into dest_track at position 'start'
 * within ori_node. Creates a child reference pointing to parent's shared block. */
void three_split_insert(sound_seg* dest_track, track_node *ori_node,
                        track_node *parent_node, size_t start) {
    track_node *middle_node = init_track_node();
    if (!middle_node) return;

    /* Insert at end of node */
    if (start == ori_node->tra_len) {
        middle_node->next    = ori_node->next;
        middle_node->tra_len = parent_node->tra_len;
        middle_node->offset  = parent_node->offset;
        free(middle_node->seg->data);
        free(middle_node->seg);
        middle_node->seg = parent_node->seg;
        ori_node->next = middle_node;
        dest_track->length += parent_node->tra_len;
        middle_node->pa_or_ch = 1;
        return;
    }

    /* Insert at start of node */
    if (start == 0) {
        track_node *prev = find_prev_node(dest_track, ori_node);
        if (prev == NULL) dest_track->head = middle_node;
        else prev->next = middle_node;
        middle_node->next = ori_node;
        free(middle_node->seg->data);
        free(middle_node->seg);
        middle_node->seg     = parent_node->seg;
        middle_node->tra_len = parent_node->tra_len;
        middle_node->offset  = parent_node->offset;
        dest_track->length  += parent_node->tra_len;
        middle_node->pa_or_ch = 1;
        return;
    }

    track_node *first_node = init_track_node();
    if (!first_node) { free_track_node(middle_node); return; }

    /* Self-insertion: split into 4 nodes with copied data */
    if (ori_node == parent_node) {
        track_node *third_node  = init_track_node();
        track_node *fourth_node = init_track_node();
        if (!third_node || !fourth_node) {
            free_track_node(first_node);
            free_track_node(middle_node);
            if (third_node) free_track_node(third_node);
            if (fourth_node) free_track_node(fourth_node);
            return;
        }

        size_t total_len = ori_node->tra_len;
        size_t left_len  = start;
        size_t right_len = total_len - start;

        /* Create left shared block (for parent left + child left) */
        shared_block *seg_left = malloc(sizeof(shared_block));
        if (!seg_left) {
            free_track_node(first_node); free_track_node(middle_node);
            free_track_node(third_node); free_track_node(fourth_node);
            return;
        }
        seg_left->length = left_len;
        seg_left->data = malloc(sizeof(int16_t) * left_len);
        if (!seg_left->data) {
            free(seg_left);
            free_track_node(first_node); free_track_node(middle_node);
            free_track_node(third_node); free_track_node(fourth_node);
            return;
        }
        memcpy(seg_left->data, ori_node->seg->data + ori_node->offset,
               left_len * sizeof(int16_t));
        seg_left->ref_count = 2;

        /* Create right shared block (for child right + parent right) */
        shared_block *seg_right = malloc(sizeof(shared_block));
        if (!seg_right) {
            free(seg_left->data); free(seg_left);
            free_track_node(first_node); free_track_node(middle_node);
            free_track_node(third_node); free_track_node(fourth_node);
            return;
        }
        seg_right->length = right_len;
        seg_right->data = malloc(sizeof(int16_t) * right_len);
        if (!seg_right->data) {
            free(seg_right); free(seg_left->data); free(seg_left);
            free_track_node(first_node); free_track_node(middle_node);
            free_track_node(third_node); free_track_node(fourth_node);
            return;
        }
        memcpy(seg_right->data,
               ori_node->seg->data + start + ori_node->offset,
               right_len * sizeof(int16_t));
        seg_right->ref_count = 2;

        /* Wire up: first(parent-left) -> middle(child-left) ->
         *          third(child-right) -> fourth(parent-right) */
        free(first_node->seg->data); free(first_node->seg);
        first_node->seg     = seg_left;
        first_node->tra_len = seg_left->length;

        free(middle_node->seg->data); free(middle_node->seg);
        middle_node->seg     = seg_left;
        middle_node->tra_len = seg_left->length;

        free(third_node->seg->data); free(third_node->seg);
        third_node->seg     = seg_right;
        third_node->tra_len = seg_right->length;

        free(fourth_node->seg->data); free(fourth_node->seg);
        fourth_node->seg     = seg_right;
        fourth_node->tra_len = seg_right->length;

        first_node->next  = middle_node;
        middle_node->next = third_node;
        third_node->next  = fourth_node;
        fourth_node->next = ori_node->next;

        track_node *prev = find_prev_node(dest_track, ori_node);
        if (prev == NULL) dest_track->head = first_node;
        else prev->next = first_node;

        dest_track->length += parent_node->tra_len;
        free_track_node(ori_node);

        first_node->pa_or_ch  = 0;
        middle_node->pa_or_ch = 1;
        third_node->pa_or_ch  = 1;
        fourth_node->pa_or_ch = 0;
        return;
    }

    /* Normal 3-way split: ori[0,start) -> child(parent data) -> ori[start,...) */
    first_node->next  = middle_node;
    middle_node->next = ori_node->next;
    ori_node->next    = first_node;

    free(first_node->seg->data);
    free(first_node->seg);
    first_node->seg     = parent_node->seg;
    first_node->tra_len = parent_node->tra_len;
    first_node->offset  = parent_node->offset;

    size_t right_len = ori_node->tra_len - start;
    free(middle_node->seg->data);
    free(middle_node->seg);
    middle_node->offset  = start + ori_node->offset;
    middle_node->tra_len = right_len;
    middle_node->seg     = ori_node->seg;
    middle_node->seg->ref_count += 1;

    dest_track->length += parent_node->tra_len;
    first_node->pa_or_ch  = 1;
    middle_node->pa_or_ch = 2;
    ori_node->tra_len = start;
}

/* Insert 'len' samples from src_track[srcpos..] into dest_track at destpos.
 * Phase 1: Extract the source portion as a parent node in src_track.
 * Phase 2: Insert a child reference into dest_track at destpos. */
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
               size_t destpos, size_t srcpos, size_t len) {
    /* Phase 1: find and extract parent portion from source */
    size_t pos_left  = len;
    size_t add_total = 0;
    size_t crossed   = 0;
    struct track_node *now_node = src_track->head;
    track_node *parent_node = NULL;

    while (now_node != NULL) {
        struct track_node *next_node = now_node->next;

        if (add_total + now_node->tra_len <= srcpos) {
            add_total += now_node->tra_len;
            now_node = next_node;
            continue;
        }

        if (!crossed) {
            size_t read_start = srcpos - add_total;
            if (pos_left <= now_node->tra_len - read_start) {
                parent_node = simple_three_split(src_track, now_node,
                    pos_left, read_start, read_start + pos_left);
                if (!parent_node) return;
                pos_left = 0;
                break;
            } else {
                add_total += now_node->tra_len;
                pos_left -= (now_node->tra_len - read_start);
                now_node = across_multi_segment_start(src_track, now_node,
                    read_start, now_node->tra_len - read_start);
                parent_node = now_node;
                crossed = 1;
            }
        } else {
            if (pos_left <= now_node->tra_len) {
                parent_node = across_multi_segment_end(src_track, now_node,
                    parent_node, pos_left);
                pos_left = 0;
                break;
            } else {
                add_total += now_node->tra_len;
                pos_left -= now_node->tra_len;
                now_node = across_multi_segment_continue(src_track, now_node,
                    parent_node);
                parent_node = now_node;
                crossed = 1;
            }
        }

        if (next_node != NULL)
            now_node = next_node;
        else
            break;
    }

    /* Phase 2: insert child reference into dest_track at destpos */
    size_t dest_add_total = 0;
    struct track_node *dest_now_node = dest_track->head;

    while (dest_now_node != NULL) {
        struct track_node *dest_next_node = dest_now_node->next;

        if (dest_add_total + dest_now_node->tra_len <= destpos) {
            dest_add_total += dest_now_node->tra_len;
            if (dest_add_total == destpos) {
                three_split_insert(dest_track, dest_now_node, parent_node,
                                   dest_now_node->tra_len);
                break;
            }
            dest_now_node = dest_next_node;
            continue;
        }

        three_split_insert(dest_track, dest_now_node, parent_node,
                           destpos - dest_add_total);
        break;
    }

    /* Dest track is empty: create a child node as the head */
    if (dest_now_node == NULL) {
        track_node *new_head = init_track_node();
        if (!new_head) return;
        free(new_head->seg->data);
        free(new_head->seg);
        new_head->seg      = parent_node->seg;
        new_head->pa_or_ch = 1;
        new_head->tra_len  = parent_node->tra_len;
        new_head->offset   = parent_node->offset;
        dest_track->length += parent_node->tra_len;
        dest_track->head = new_head;
    }
}
