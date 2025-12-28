# Dynamic Memory Allocator

A custom implementation of a dynamic memory allocator in C, providing `malloc`, `free`, and `realloc` functionality with efficient memory management, block coalescing, and splitting.

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Building the Project](#building-the-project)
- [API Reference](#api-reference)
- [Architecture](#architecture)
- [Testing](#testing)
- [Debugging](#debugging)
- [Project Structure](#project-structure)

## Overview

This project implements a dynamic memory allocator that manages heap memory efficiently. The allocator uses multiple free lists organized by size, enabling fast allocation and deallocation of memory blocks. It supports block coalescing to reduce fragmentation and block splitting to minimize wasted space.

## Features

- **Efficient Memory Management**: Uses multiple free lists organized by size for fast allocation
- **Block Coalescing**: Automatically merges adjacent free blocks to reduce fragmentation
- **Block Splitting**: Splits large blocks when allocating smaller requests to minimize waste
- **Memory Alignment**: All allocated memory is properly aligned
- **Heap Expansion**: Automatically grows the heap when more memory is needed
- **Error Handling**: Proper error reporting via `sf_errno`
- **Pointer Validation**: Validates pointers before freeing to prevent crashes

## Building the Project

### Prerequisites

- GCC compiler
- Make
- Criterion testing framework (for running tests)

### Build Commands

**Standard Build:**
```bash
make
```

This creates:
- `bin/sfmm` - The main executable
- `bin/sfmm_tests` - The test suite executable

**Debug Build:**
```bash
make debug
```

The debug build includes:
- Debug symbols (`-g`)
- Debug macros enabled (`-DDEBUG`)
- Color output (`-DCOLOR`)
- All logging levels (`-DERROR -DSUCCESS -DWARN -DINFO`)

**Clean Build Artifacts:**
```bash
make clean
```

This removes the `build/` and `bin/` directories.

## API Reference

### `void *sf_malloc(size_t size)`

Allocates a block of memory of at least `size` bytes.

**Parameters:**
- `size`: Number of bytes to allocate

**Returns:**
- On success: Pointer to the allocated memory (properly aligned)
- On failure: `NULL` and sets `sf_errno` to `ENOMEM`
- If `size` is 0: Returns `NULL` without setting `sf_errno`

**Example:**
```c
int *x = sf_malloc(sizeof(int));
if (x == NULL) {
    // Allocation failed
}
```

### `void sf_free(void *ptr)`

Frees a previously allocated block of memory.

**Parameters:**
- `ptr`: Pointer to memory previously returned by `sf_malloc` or `sf_realloc`

**Behavior:**
- If `ptr` is `NULL`, the function calls `abort()`
- If `ptr` is invalid, the function calls `abort()`
- Automatically coalesces with adjacent free blocks

**Example:**
```c
int *x = sf_malloc(sizeof(int));
// ... use x ...
sf_free(x);
```

### `void *sf_realloc(void *ptr, size_t size)`

Resizes a previously allocated block of memory.

**Parameters:**
- `ptr`: Pointer to memory previously returned by `sf_malloc` or `sf_realloc`
- `size`: New size in bytes

**Returns:**
- On success: Pointer to the resized memory (may be different from `ptr`)
- On failure: `NULL` and sets `sf_errno` appropriately
- If `size` is 0: Frees the block and returns `NULL`
- If `ptr` is invalid: Returns `NULL` and sets `sf_errno` to `EINVAL`

**Behavior:**
- If `size` is larger: Allocates new block, copies data, frees old block
- If `size` is smaller: May split the block if remainder is large enough
- If `size` is the same: Returns `ptr` unchanged

**Example:**
```c
int *arr = sf_malloc(10 * sizeof(int));
arr = sf_realloc(arr, 20 * sizeof(int));
```

### Error Handling

The allocator uses `sf_errno` to report errors:

- `ENOMEM`: Out of memory (allocation failed)
- `EINVAL`: Invalid pointer passed to `sf_realloc`

**Example:**
```c
void *ptr = sf_malloc(1000000);
if (ptr == NULL && sf_errno == ENOMEM) {
    fprintf(stderr, "Out of memory!\n");
}
```

### Debugging Functions

The following functions are available for debugging and visualization:

- `void sf_show_heap()` - Display the entire heap structure
- `void sf_show_blocks()` - Show all blocks in the heap
- `void sf_show_block(sf_block *bp)` - Show details of a specific block
- `void sf_show_free_lists()` - Display all free lists
- `void sf_show_free_list(int index)` - Display a specific free list

**Example:**
```c
sf_malloc(100);
sf_show_heap();  // Visualize heap state
```

## Architecture

### Memory Block Format

Each memory block contains:
- **Header**: Stores block size and allocation status
- **Payload**: The actual data for allocated blocks
- **Links**: Pointers to next/previous blocks for free blocks
- **Footer**: Duplicate header information for free blocks (used for efficient coalescing)

### Free List Organization

The allocator maintains multiple free lists organized by size classes, allowing it to quickly find blocks of appropriate size. Each list is a circular, doubly-linked list that efficiently manages free blocks of similar sizes.

### Heap Structure

The heap is organized with:
- **Prologue block**: A special allocated block at the start that helps with boundary checking
- **Data blocks**: The actual allocated and free blocks where user data is stored
- **Epilogue block**: A special marker block at the end that indicates heap boundaries

### Key Algorithms

#### Allocation (`sf_malloc`)
1. Align requested size to proper boundary
2. Calculate total block size including header
3. Find appropriate size class
4. Search free lists starting from the appropriate size class
5. Use first-fit strategy to find a block
6. If no block found, expand the heap
7. Split the block if remainder is large enough
8. Mark block as allocated and return payload pointer

#### Deallocation (`sf_free`)
1. Validate the pointer
2. Mark block as free
3. Update next block's `prev_alloc` bit
4. Coalesce with adjacent free blocks
5. Insert coalesced block into appropriate free list

#### Coalescing
When a block is freed, the allocator checks:
- **Previous block**: If free, merge with current block
- **Next block**: If free, merge with current block

This reduces fragmentation by combining adjacent free blocks.

#### Splitting
When allocating from a larger block:
- If the remainder is large enough, split the block
- The remainder becomes a new free block inserted into the appropriate free list

#### Reallocation (`sf_realloc`)
1. If new size is 0: Free the block and return NULL
2. If new size equals current size: Return pointer unchanged
3. If new size is larger: Allocate new block, copy data, free old block
4. If new size is smaller: Split block if remainder is large enough, otherwise keep block

## Testing

The project includes a comprehensive test suite using the Criterion testing framework.

### Running Tests

```bash
# Build and run tests
make
./bin/sfmm_tests
```

### Test Coverage

The test suite includes:

- **Basic Allocation**: Allocating small objects (e.g., `int`)
- **Large Allocation**: Allocating multiple pages
- **Allocation Failure**: Handling requests that are too large
- **Free Operations**: Freeing blocks without coalescing
- **Coalescing**: Freeing adjacent blocks that merge
- **Free List Management**: Verifying blocks are in correct free lists
- **Reallocation**: Growing and shrinking blocks

### Writing Custom Tests

You can add your own tests in `tests/sfmm_tests.c`:

```c
Test(sfmm_student_suite, my_custom_test, .timeout = TEST_TIMEOUT) {
    sf_errno = 0;
    void *ptr = sf_malloc(100);
    cr_assert_not_null(ptr, "Allocation failed!");
    sf_free(ptr);
    cr_assert(sf_errno == 0, "Error occurred!");
}
```

## Debugging

### Debug Build

Build with debug symbols and logging:

```bash
make debug
```

### Debug Macros

The project includes several debug macros (defined in `include/debug.h`):

- `debug()` - Debug messages
- `info()` - Informational messages
- `warn()` - Warning messages
- `error()` - Error messages
- `success()` - Success messages

These are enabled in debug builds or when specific flags are defined.

### Visualization Functions

Use the built-in visualization functions to inspect heap state:

```c
#include "sfmm.h"

int main() {
    void *ptr1 = sf_malloc(100);
    void *ptr2 = sf_malloc(200);
    
    sf_show_heap();        // Show entire heap
    sf_show_free_lists();  // Show all free lists
    
    sf_free(ptr1);
    sf_show_blocks();      // Show all blocks
    
    return 0;
}
```

## Project Structure

```
dynamic-memory-allocator/
├── include/
│   ├── sfmm.h          # Public API and data structures
│   └── debug.h         # Debug macros and utilities
├── src/
│   ├── sfmm.c          # Implementation of allocator
│   └── main.c          # Example usage
├── tests/
│   └── sfmm_tests.c    # Test suite
├── lib/
│   └── sfutil.o        # Helper functions (heap management)
├── build/              # Object files (generated)
├── bin/                # Executables (generated)
├── Makefile            # Build configuration
└── README.md           # This file
```