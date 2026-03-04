#ifndef SOUND_SEG_H
#define SOUND_SEG_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* Shared block: the underlying sample data with reference counting.
 * Multiple track_nodes can point to the same shared_block to avoid
 * duplicating data when performing insert operations. */
typedef struct shared_block {
    int16_t *data;        /* raw sample data */
    size_t   length;      /* number of samples in data */
    uint8_t  ref_count;   /* number of track_nodes referencing this block */
} shared_block;

/* Track node: one segment of a track's linked list.
 * Each node references a contiguous range [offset, offset+tra_len)
 * within its shared_block's data array.
 * pa_or_ch: 0 = parent, 1 = child, 2 = normal (no relationship) */
typedef struct track_node {
    shared_block *seg;        /* pointer to shared sample data */
    size_t offset;            /* start index within seg->data */
    size_t tra_len;           /* number of samples this node covers */
    size_t pa_or_ch;          /* relationship type: 0=parent, 1=child, 2=normal */
    struct track_node *next;  /* next node in the linked list */
} track_node;

/* Track: a complete audio track represented as a linked list of nodes. */
typedef struct sound_seg {
    track_node *head;     /* first node in the linked list */
    size_t length;        /* total number of samples across all nodes */
} sound_seg;

/* Debug helpers (no-ops unless compiled with -DDEBUG) */
void debug_print_track(const char *name, track_node *head);
void debug_print_parent_track(const char *name, track_node *head);
void debug_print_seg(const char *name, sound_seg *seg);
void debug_print_segment(shared_block *seg);

/* WAV file I/O */
void wav_load(const char* filename, int16_t* dest);
void wav_save(const char* fname, int16_t* src, size_t len);

/* Track lifecycle */
struct sound_seg* tr_init(void);
void tr_destroy(sound_seg* obj);

/* Basic track operations */
size_t tr_length(struct sound_seg* seg);
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len);
void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len);
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len);

/* Ad identification */
char* tr_identify(struct sound_seg* target, struct sound_seg* ad);

/* Complex insertions */
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
               size_t destpos, size_t srcpos, size_t len);

/* Internal helpers */
track_node *create_track_node(size_t sample_count, size_t offset,
                              size_t tra_len, size_t pa_or_ch);
track_node* init_track_node(void);
void free_track_node(track_node *node);
track_node* find_prev_node(sound_seg *track, track_node *target);
bool check_child_in_range(track_node *parent, size_t pos, size_t end,
                          size_t parent_start);
track_node* simple_three_split(sound_seg *dest_track, track_node *ori_node,
                               size_t len, size_t start, size_t end);
track_node* across_multi_segment_start(sound_seg *dest_track,
                                       track_node *ori_node,
                                       size_t start, size_t len);
track_node* across_multi_segment_continue(sound_seg *dest_track,
                                          track_node *ori_node,
                                          track_node *parent_node);
track_node* across_multi_segment_end(sound_seg *dest_track,
                                     track_node *ori_node,
                                     track_node *parent_node, size_t len);
void three_split_insert(sound_seg* dest_track, track_node *ori_node,
                        track_node *parent_node, size_t start);

#endif /* SOUND_SEG_H */
