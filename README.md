# Backend Audio Track Editor

A backend audio track editor implemented in C, designed for efficient audio clip management using shared memory and hierarchical data structures.

## Overview

This project implements a backend audio editing engine that supports:
- **WAV file I/O** – Loading and saving 16-bit PCM mono audio
- **Track operations** – Read, write, and delete audio samples with linked-list-based track segments
- **Ad identification** – Cross-correlation-based detection of advertisement patterns in audio tracks
- **Complex insertions** – Logical insertion with shared backing store, maintaining parent-child relationships between audio clips to minimize memory usage
- **Inter-process shared memory model** – Reference-counted shared blocks enable efficient multi-track editing without data duplication

## Architecture

### Data Structures

```
sound_seg (Track)
 └── track_node (linked list) ──→ track_node ──→ track_node ──→ ...
      └── shared_block              └── shared_block (may be shared)
           └── int16_t[] data            └── ref_count tracks ownership
```

- **`sound_seg`** – Represents a complete audio track as a linked list of nodes
- **`track_node`** – A segment referencing a contiguous range within a shared block. Tracks its relationship type: parent (0), child (1), or normal (2)
- **`shared_block`** – Reference-counted sample data. Multiple nodes can share the same block, enabling copy-on-reference semantics for `tr_insert()`

### Key Design Decisions

1. **Linked list of segments** – Allows efficient insertion and deletion without shifting data
2. **Shared backing store** – `tr_insert()` creates parent-child relationships via shared `shared_block` pointers, so inserted data exists only once in memory
3. **Reference counting** – `shared_block.ref_count` ensures memory is freed only when no nodes reference it
4. **Write-through sharing** – Writes to a child portion are immediately visible in the parent (and vice versa) through the shared block

## API Reference

### Track Lifecycle
| Function | Description |
|----------|-------------|
| `tr_init()` | Allocate an empty track |
| `tr_destroy(track)` | Free all memory (respects ref-counting) |

### Basic Operations
| Function | Description |
|----------|-------------|
| `tr_length(track)` | Return sample count |
| `tr_read(track, dest, pos, len)` | Copy samples to buffer |
| `tr_write(track, src, pos, len)` | Write samples (auto-extends) |
| `tr_delete_range(track, pos, len)` | Delete samples; returns `false` if parent has active children |

### Ad Identification
| Function | Description |
|----------|-------------|
| `tr_identify(target, ad)` | Find non-overlapping ad occurrences via cross-correlation (95% threshold) |

### Complex Insertions
| Function | Description |
|----------|-------------|
| `tr_insert(src, dest, destpos, srcpos, len)` | Logical insertion with shared backing store |

## Building

```bash
make              # Build sound_seg.o
make clean        # Remove build artifacts
```

The Makefile compiles with `-Wall -Werror -fPIC -Wvla -g`. The output is an object file (`sound_seg.o`) designed to be linked as a shared library.

## Technical Highlights

- **Zero-copy insertions**: `tr_insert()` shares the underlying data block between source and destination, reducing memory usage from O(n) to O(1) per insertion
- **Cross-correlation detection**: `tr_identify()` uses normalized cross-correlation to detect ad patterns with >= 95% similarity threshold
- **Correct parent-child lifecycle**: Deletion properly handles cascading reference count updates, including self-insertion edge cases where children precede parents in the linked list
- **No forbidden functions**: Avoids `printf` (which uses dynamic memory internally), VLAs, and restricted system calls per assignment constraints
