/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_wine_heap.h"
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

enum {
    HEADER=16,MIN_SHIFT=4,MAX_SHIFT=16,CLASSES=MAX_SHIFT-MIN_SHIFT+1,
    SPAN_BYTES=1u<<20,MAX_SPANS=4096,MAX_LARGE=65536,
    PAGE=16384      /* mapping granularity on the console */
};
#define MAGIC_SMALL UINT64_C(0x70775f68656170c5)
#define MAGIC_LARGE UINT64_C(0x70775f6865617071)
/* In front of an aligned block's pointer, inside the block that holds it:
 * class_index is how far back that block's pointer is. */
#define MAGIC_ALIGNED UINT64_C(0x70775f68656170a1)

/* Every block starts with this header; the caller's pointer follows it. */
typedef struct Header { uint64_t magic; uint32_t class_index; uint32_t requested; } Header;
typedef struct FreeBlock { struct FreeBlock *next; } FreeBlock;
/* requested's top bit says the block came from the backing, not from mmap. */
typedef struct LargeHeader { uint64_t mapped; uint64_t requested; } LargeHeader;
#define FROM_BACKING (UINT64_C(1)<<63)
_Static_assert(sizeof(Header)==HEADER,"header size");

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static FreeBlock *free_lists[CLASSES];
static uint8_t *spans[MAX_SPANS];
static uint32_t span_count;
static uint8_t *carve,*carve_end;
/* Live large blocks by caller pointer, sorted: a header is only ever read
 * behind a pointer this heap has handed out. */
static void *larges[MAX_LARGE];
static uint32_t large_count;
static PwWineHeapStats stats;

/* The guard around the mutex (pw_wine_heap_set_guard); none until set. */
static void (*guard_enter)(void),(*guard_leave)(void);

void pw_wine_heap_set_guard(void (*enter)(void),void (*leave)(void))
{
    guard_leave=leave;guard_enter=enter;
}

/* The heap's one mutex, counting the calls that had to wait for it. */
static void lock_heap(void)
{
    if(guard_enter)guard_enter();
    if(pthread_mutex_trylock(&lock)) {
        pthread_mutex_lock(&lock);
        stats.contended++;
    }
}

static void unlock_heap(void)
{
    pthread_mutex_unlock(&lock);
    if(guard_leave)guard_leave();
}

static void *(*backing_map)(size_t bytes);
static void (*backing_unmap)(void *address,size_t bytes);

void pw_wine_heap_set_backing(void *(*map_bytes)(size_t bytes),void (*unmap_bytes)(void *address,size_t bytes))
{
    lock_heap();backing_map=map_bytes;backing_unmap=unmap_bytes;unlock_heap();
}
/* *from_backing says where the memory came from, for its release. Once a backing is set there is no mapping of the
 * heap's own: one the kernel places lands in any hole it finds, inside a Windows view too, and the program reads what
 * the heap keeps there as its own memory (Wine's host-protection leaves, 2026-10-13; record removed from the tree; in git history). */
static void *map(size_t bytes,int *from_backing)
{
    void *p;
    *from_backing=backing_map && backing_unmap;
    if(*from_backing)return backing_map(bytes);
    p=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    return p==MAP_FAILED?NULL:p;
}
static void note_mapped(int64_t delta)
{
    stats.mapped_bytes+=(uint64_t)delta;
    if(stats.mapped_bytes>stats.peak_mapped_bytes)stats.peak_mapped_bytes=stats.mapped_bytes;
}
static void note_live(int64_t delta)
{
    stats.live_bytes+=(uint64_t)delta;
    if(stats.live_bytes>stats.peak_live_bytes)stats.peak_live_bytes=stats.live_bytes;
}
static unsigned class_for(size_t bytes)
{
    unsigned index=0;size_t block=(size_t)1<<MIN_SHIFT;
    while(block<bytes+HEADER){block<<=1;index++;}
    return index;
}
static size_t class_bytes(unsigned index){return (size_t)1<<(MIN_SHIFT+index);}
/* Spans are kept sorted so ownership is a binary search. */
static int owned_span(const void *pointer)
{
    uint32_t low=0,high=span_count;
    while(low<high) {
        uint32_t mid=(low+high)/2;
        if((const uint8_t *)pointer<spans[mid])high=mid;
        else if((const uint8_t *)pointer>=spans[mid]+SPAN_BYTES)low=mid+1;
        else return 1;
    }
    return 0;
}
static int add_span(void)
{
    if(span_count==MAX_SPANS)return 0;
    int from_backing;
    uint8_t *span=map(SPAN_BYTES,&from_backing);if(!span)return 0;
    uint32_t at=span_count;
    while(at && spans[at-1]>span){spans[at]=spans[at-1];at--;}
    spans[at]=span;span_count++;note_mapped(SPAN_BYTES);
    carve=span;carve_end=span+SPAN_BYTES;return 1;
}
static Header *small_alloc(unsigned index)
{
    FreeBlock *block=free_lists[index];
    if(block){free_lists[index]=block->next;return (Header *)block;}
    size_t bytes=class_bytes(index);
    if((size_t)(carve_end-carve)<bytes && !add_span())return NULL;
    Header *header=(Header *)carve;carve+=bytes;return header;
}
static Header *small_header(const void *pointer)
{
    if(!pointer || ((uintptr_t)pointer&(HEADER-1)) || !owned_span(pointer))return NULL;
    Header *header=(Header *)((uint8_t *)pointer-HEADER);
    return header->magic==MAGIC_SMALL && header->class_index<CLASSES?header:NULL;
}
/* Position of pointer in the sorted large registry, or where it would go. */
static uint32_t large_slot(const void *pointer)
{
    uint32_t low=0,high=large_count;
    while(low<high) {
        uint32_t mid=(low+high)/2;
        if((const uint8_t *)larges[mid]<(const uint8_t *)pointer)low=mid+1;else high=mid;
    }
    return low;
}
/* A large block: [LargeHeader][Header][data], mapping starts at the first. */
static Header *large_header(const void *pointer)
{
    uint32_t slot=large_slot(pointer);
    if(!pointer || slot==large_count || larges[slot]!=pointer)return NULL;
    return (Header *)((uint8_t *)pointer-HEADER);
}
static void *allocate(size_t bytes)
{
    if(bytes>UINT32_MAX-PAGE){stats.failures++;return NULL;}
    Header *header;
    if(bytes+HEADER<=class_bytes(CLASSES-1)) {
        unsigned index=class_for(bytes);
        if(!(header=small_alloc(index))){stats.failures++;return NULL;}
        *header=(Header){MAGIC_SMALL,index,(uint32_t)bytes};
    } else {
        size_t mapped=(bytes+HEADER+sizeof(LargeHeader)+PAGE-1)/PAGE*PAGE;
        int from_backing=0;
        LargeHeader *large=large_count<MAX_LARGE?map(mapped,&from_backing):NULL;
        if(!large){stats.failures++;return NULL;}
        *large=(LargeHeader){mapped,bytes|(from_backing?FROM_BACKING:0)};note_mapped((int64_t)mapped);stats.large_live++;
        header=(Header *)(large+1);*header=(Header){MAGIC_LARGE,CLASSES,(uint32_t)bytes};
        uint32_t slot=large_slot(header+1);
        memmove(&larges[slot+1],&larges[slot],(large_count-slot)*sizeof(larges[0]));
        larges[slot]=header+1;large_count++;
    }
    stats.allocations++;note_live((int64_t)bytes);
    return header+1;
}
static void release(Header *header)
{
    note_live(-(int64_t)header->requested);stats.frees++;
    if(header->magic==MAGIC_SMALL) {
        unsigned index=header->class_index;header->magic=0;
        FreeBlock *block=(FreeBlock *)header;block->next=free_lists[index];
        free_lists[index]=block;
    } else {
        LargeHeader *large=(LargeHeader *)header-1;uint64_t mapped=large->mapped;
        int from_backing=(large->requested&FROM_BACKING)!=0;
        uint32_t slot=large_slot(header+1);
        memmove(&larges[slot],&larges[slot+1],(large_count-slot-1)*sizeof(larges[0]));
        large_count--;header->magic=0;
        if(from_backing && backing_unmap)backing_unmap(large,mapped);else (void)munmap(large,mapped);
        note_mapped(-(int64_t)mapped);stats.large_live--;
    }
}
static Header *owned(const void *pointer)
{
    Header *header=small_header(pointer);
    return header?header:large_header(pointer);
}
/* The block holding an aligned pointer, read only once the pointer is known
 * to lie inside memory this heap handed out. */
static Header *owned_aligned(const void *pointer,void **base)
{
    const uint8_t *p=pointer;
    if(!p || ((uintptr_t)p&(HEADER-1)))return NULL;
    if(!owned_span(p)) {
        uint32_t slot=large_slot(p);
        if(!slot)return NULL;
        const uint8_t *start=larges[slot-1];
        if(p<=start || p>=start+((Header *)(start-HEADER))->requested)return NULL;
    }
    Header *mark=(Header *)(p-HEADER);
    if(mark->magic!=MAGIC_ALIGNED || mark->class_index<HEADER || mark->class_index>(uintptr_t)p)return NULL;
    Header *header=owned(p-mark->class_index);
    if(!header)return NULL;
    *base=(void *)(p-mark->class_index);
    return header;
}

void *pw_wine_heap_malloc(size_t bytes)
{
    lock_heap();void *result=allocate(bytes);unlock_heap();
    return result;
}
void *pw_wine_heap_calloc(size_t count,size_t bytes)
{
    if(bytes && count>SIZE_MAX/bytes) {
        lock_heap();stats.failures++;unlock_heap();return NULL;
    }
    void *result=pw_wine_heap_malloc(count*bytes);
    if(result)memset(result,0,count*bytes);
    return result;
}
void *pw_wine_heap_memalign(size_t alignment,size_t bytes)
{
    if(!alignment || (alignment&(alignment-1)))return NULL;
    if(alignment<=HEADER)return pw_wine_heap_malloc(bytes);
    if(bytes>SIZE_MAX-alignment || alignment>UINT32_MAX) {
        lock_heap();stats.failures++;unlock_heap();return NULL;
    }
    lock_heap();
    uint8_t *base=allocate(bytes+alignment),*result=NULL;
    if(base) {
        /* base is 16-byte aligned, so the gap is 16..alignment bytes and the
         * mark in front of result stays inside the block. */
        result=(uint8_t *)(((uintptr_t)base+HEADER+alignment-1)&~(uintptr_t)(alignment-1));
        *(Header *)(result-HEADER)=(Header){MAGIC_ALIGNED,(uint32_t)(result-base),(uint32_t)bytes};
    }
    unlock_heap();
    return result;
}
void pw_wine_heap_free(void *pointer)
{
    if(!pointer)return;
    lock_heap();
    void *base;
    Header *header=owned(pointer);
    if(header)release(header);
    else if((header=owned_aligned(pointer,&base))) {
        ((Header *)((uint8_t *)pointer-HEADER))->magic=0;release(header);
    }
    else stats.foreign_frees++;
    unlock_heap();
}
void *pw_wine_heap_realloc(void *pointer,size_t bytes)
{
    if(!pointer)return pw_wine_heap_malloc(bytes);
    if(!bytes){pw_wine_heap_free(pointer);return NULL;}
    lock_heap();
    void *base;
    Header *header=owned(pointer);
    if(!header && (header=owned_aligned(pointer,&base))) {
        /* An aligned block moves to an ordinary one, as realloc allows. */
        size_t old=((Header *)((uint8_t *)pointer-HEADER))->requested;
        void *moved=allocate(bytes);
        if(moved){memcpy(moved,pointer,old<bytes?old:bytes);
                  ((Header *)((uint8_t *)pointer-HEADER))->magic=0;release(header);}
        unlock_heap();
        return moved;
    }
    if(!header) {
        /* Not ours: its size is unknown, so it cannot be moved safely. */
        stats.failures++;unlock_heap();return NULL;
    }
    size_t old=header->requested;
    if(header->magic==MAGIC_SMALL && bytes+HEADER<=class_bytes(header->class_index) &&
       bytes<=UINT32_MAX) {
        note_live((int64_t)bytes-(int64_t)old);header->requested=(uint32_t)bytes;
        unlock_heap();return pointer;
    }
    void *moved=allocate(bytes);
    if(moved){memcpy(moved,pointer,old<bytes?old:bytes);release(header);}
    unlock_heap();
    return moved;
}
size_t pw_wine_heap_usable_size(const void *pointer)
{
    lock_heap();
    void *base;
    Header *header=owned(pointer);size_t usable=0;
    if(!header && owned_aligned(pointer,&base))
        usable=((Header *)((const uint8_t *)pointer-HEADER))->requested;
    else if(header)usable=header->magic==MAGIC_SMALL?class_bytes(header->class_index)-HEADER:
        (size_t)(((LargeHeader *)header-1)->mapped-HEADER-sizeof(LargeHeader));
    unlock_heap();
    return usable;
}
void pw_wine_heap_stats(PwWineHeapStats *out)
{
    if(!out)return;
    lock_heap();*out=stats;unlock_heap();
}
