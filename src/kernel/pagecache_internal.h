typedef struct pagelist {
    struct list l;
    u64 pages;
} *pagelist;

declare_closure_struct(1, 2, void, pagecache_scan_timer,
                       struct pagecache *, pc,
                       u64, expiry, u64, overruns);

declare_closure_struct(0, 2, int, pagecache_page_compare,
                       rbnode, a, rbnode, b);
declare_closure_struct(1, 1, boolean, pagecache_page_print_key,
                       struct pagecache *, pc,
                       rbnode, n);

typedef struct page_completion {
    struct list l;
    union {
        status_handler sh;
        status s;               /* queued list head has status to apply */
    };
} *page_completion;

typedef struct pagecache {
    word total_pages;
    int page_order;
    heap h;
    heap contiguous;
    heap physical;
    heap completions;

    void *zero_page;            /* for zero-fill dma */

    /* state_lock covers list access, page state changes and
       alterations to page completion vecs */
#ifdef KERNEL
    struct spinlock state_lock;
#endif
    struct pagelist free;      /* see state descriptions */
    struct pagelist new;
    struct pagelist active;
    struct pagelist writing;
    struct pagelist dirty;     /* phase 2 */
    struct list volumes;
    struct list shared_maps;

    boolean scan_in_progress;
    struct timer scan_timer;
    closure_struct(pagecache_scan_timer, do_scan_timer);
    closure_struct(pagecache_page_compare, page_compare);
    closure_struct(pagecache_page_print_key, page_print_key);
} *pagecache;

typedef struct pagecache_volume {
    struct list l;              /* volumes list */
    pagecache pc;
    struct list nodes;          /* head of pagecache_nodes */
    u64 length;                 /* end of volume */
    int block_order;
    status write_error;         /* pending error from a previous write */
} *pagecache_volume;

typedef struct pagecache_node {
    struct list l;              /* volume-wide node list */
    pagecache_volume pv;

    /* pages_lock covers traversal, insertions and removals - consider
       changing to a rw lock or semaphore */
#ifdef KERNEL
    struct spinlock pages_lock;
#endif
    struct rbtree pages;
    rangemap shared_maps;       /* shared mappings associated with this node */
    u64 length;

    sg_io cache_read;
    sg_io cache_write;
    sg_io fs_read;
    sg_io fs_write;
    pagecache_node_reserve fs_reserve;
} *pagecache_node;

typedef struct pagecache_shared_map {
    struct rmnode n;            /* pn->shared */
    struct list l;              /* pc->shared_maps */
    pagecache_node pn;
    u64 node_offset;            /* file offset of va.start */
} *pagecache_shared_map;

#define PAGECACHE_PAGESTATE_SHIFT   61

#define PAGECACHE_PAGESTATE_FREE    0 /* evicted, yet remains in search tree and retains refault data */
#define PAGECACHE_PAGESTATE_EVICTED 1 /* evicted, awaiting release by user (not on list) */
#define PAGECACHE_PAGESTATE_ALLOC   2 /* allocated, request not issued (not on list) */
#define PAGECACHE_PAGESTATE_READING 3 /* block reads issued (not on list) */
#define PAGECACHE_PAGESTATE_NEW     4 /* newly-loaded and full page writes - can be reclaimed */
#define PAGECACHE_PAGESTATE_ACTIVE  5 /* cache hit for page */
#define PAGECACHE_PAGESTATE_DIRTY   6 /* page not synced */
#define PAGECACHE_PAGESTATE_WRITING 7 /* block writes in progress; back to tail of new on completion */

typedef struct pagecache_page *pagecache_page;

declare_closure_struct(2, 0, void, pagecache_page_free,
                       pagecache, pc, pagecache_page, pp);

struct pagecache_page {
    struct rbnode rbnode;
    struct refcount refcount;   /* 24 */
    u64 state_offset;           /* 40 - state and offset in pages */
    void *kvirt;                /* 48 */
    int write_count;            /* 56 */
    int pad0;                   /* 60 */
    /* end of first cacheline */

    pagecache_node node;
    struct list l;
    u64 phys;                   /* physical address */
    struct list bh_completions; /* default for non-kernel use */

    closure_struct(pagecache_page_free, free);
    boolean evicted;
};

static inline void pagecache_release_page(pagecache_page pp)
{
    refcount_release(&pp->refcount);
}
