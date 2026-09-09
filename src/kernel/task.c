/* Cooperative scheduler. See task.h for the high-level model.
 *
 * Round-robin: yield() picks the next used slot after the current one
 * and context-switches into it. A task that returns from its entry
 * function is marked free and yields forever, so its slot can be
 * reclaimed by a later task_spawn(). */

#include "boxos.h"
#include "task.h"

#define MAX_TASKS  16
#define STACK_SIZE (64 * 1024)        /* 64 KiB per task */
#define EVQ_SIZE   32                 /* per-task event ring depth */
#define MAX_WIN_OWNERSHIP 16          /* upper bound on tracked windows */

struct task {
    int       used;
    uint64_t  rsp;        /* saved stack pointer when not currently running */
    uint64_t  cr3;        /* physical address of this task's PML4 */
    uint8_t*  stack_base;
    void    (*entry)(void);
    const char* entry_arg;            /* optional arg for task_spawn_with_arg */
    int       exit_code;              /* set by app trampoline on return */
    task_id   id;
    char      name[32];
    /* Per-task working directory cluster for fs_read_file / SYS_LIST_DIR
     * etc. Captured from the parent at spawn time; each task's own
     * fs_set_app_cwd calls only touch this field. */
    uint16_t  app_cwd;
    /* Per-task event ring. head==tail means empty; (head+1)%N==tail
     * means full and we drop incoming events on the floor. */
    struct gui_event evq[EVQ_SIZE];
    uint8_t   ev_head;
    uint8_t   ev_tail;
};

/* Window-to-task ownership table, populated by the WM via
 * task_register_window. Indexed by window handle. */
static task_id g_win_owner[MAX_WIN_OWNERSHIP];

static struct task g_tasks[MAX_TASKS];
static int         g_current;          /* index of running task */
static int         g_next_id   = 1;
static int         g_init_done = 0;

volatile uint32_t  g_task_heartbeat = 0;

/* Defined in context.asm. */
extern void context_switch(uint64_t* save_rsp_p,
                           uint64_t  load_rsp,
                           uint64_t  load_cr3);

/* Read the current CR3. */
static inline uint64_t read_cr3(void) {
    uint64_t v;
    __asm__ __volatile__ ("mov %%cr3, %0" : "=r"(v));
    return v;
}

/* Bump-style 4 KiB page allocator on top of heap_alloc. heap_alloc
 * only guarantees 16-byte alignment, so we over-allocate by a page
 * and step the pointer up to the next 4 KiB boundary. Wastes at
 * most 4 KiB per call — fine for the 16-task ceiling. */
static void* page_alloc_4k(void) {
    uint8_t* p = (uint8_t*)heap_alloc(4096 + 4095);
    if (!p) return 0;
    uint64_t a = ((uint64_t)p + 4095) & ~(uint64_t)4095;
    return (void*)a;
}

/* Clone the kernel's current PML4 into a fresh page so the new task
 * can have its own (eventually-divergent) page tables. For Phase 0
 * the content is identical to task 0's — same kernel + identity map.
 * Phase 2 will deep-clone PDPT+PD for the user half to give each app
 * its own private 0x200000+ region. */
static uint64_t* clone_pml4(void) {
    uint64_t* fresh = (uint64_t*)page_alloc_4k();
    if (!fresh) return 0;
    const uint64_t* src = (const uint64_t*)g_tasks[0].cr3;
    for (int i = 0; i < 512; i++) fresh[i] = src[i];
    return fresh;
}

static int pick_next(void) {
    for (int i = 1; i <= MAX_TASKS; i++) {
        int idx = (g_current + i) % MAX_TASKS;
        if (g_tasks[idx].used) return idx;
    }
    return g_current;
}

/* Trampoline: the first time we context-switch *into* a freshly spawned
 * task, the popped RIP lands here, which then calls the task's actual
 * entry function. When entry returns, we mark the slot free and yield
 * forever so the scheduler skips us. */
static void task_entry_trampoline(void) {
    int me = g_current;
    g_tasks[me].entry();
    g_tasks[me].used = 0;
    for (;;) yield();
}

void task_init(void) {
    if (g_init_done) return;
    for (int i = 0; i < MAX_TASKS; i++) {
        g_tasks[i].used    = 0;
        g_tasks[i].ev_head = 0;
        g_tasks[i].ev_tail = 0;
        g_tasks[i].app_cwd = 0;     /* root by default */
    }
    for (int i = 0; i < MAX_WIN_OWNERSHIP; i++) g_win_owner[i] = -1;
    g_tasks[0].used = 1;
    g_tasks[0].id   = 0;
    /* Capture stage 2's bootstrap PML4 as task 0's address space.
     * Every later task starts as a copy of this. The kernel's heap
     * is identity-mapped, so the PML4's physical address == its
     * virtual address from the kernel's point of view. */
    g_tasks[0].cr3  = read_cr3();
    const char* nm  = "kernel";
    int n = 0;
    while (nm[n] && n < 31) { g_tasks[0].name[n] = nm[n]; n++; }
    g_tasks[0].name[n] = 0;
    g_current   = 0;
    g_init_done = 1;
}

task_id task_spawn(const char* name, void (*entry)(void)) {
    if (!g_init_done) task_init();
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (!g_tasks[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1;
    struct task* t = &g_tasks[slot];

    t->stack_base = (uint8_t*)heap_alloc(STACK_SIZE);
    if (!t->stack_base) return -1;
    /* Each task gets its own PML4 — currently a copy of task 0's so
     * mappings are identical, but the slot is separate so Phase 2
     * can diverge per app. */
    uint64_t* pml4 = clone_pml4();
    if (!pml4) return -1;
    t->cr3   = (uint64_t)pml4;
    t->used  = 1;
    t->entry = entry;
    t->id    = g_next_id++;
    t->app_cwd = g_tasks[g_current].app_cwd;
    int n = 0;
    while (name && name[n] && n < 31) { t->name[n] = name[n]; n++; }
    t->name[n] = 0;

    /* Build the initial saved-stack frame so context_switch can ret
     * into the trampoline. context.asm pushes [rbp][rbx][r12-r15][flags]
     * in that order and pops in reverse; we mirror that layout.
     *
     * Stack grows down; high address → low:
     *   [trampoline RIP]   ← popped LAST by `ret`
     *   [flags = 0x202]    ← popped by `popfq` (IF set, reserved bit 1)
     *   [rbp = 0]
     *   [rbx = 0]
     *   [r12 = 0]
     *   [r13 = 0]
     *   [r14 = 0]
     *   [r15 = 0]          ← t->rsp points here (popped FIRST). */
    uint64_t* sp = (uint64_t*)(t->stack_base + STACK_SIZE);
    *--sp = (uint64_t)task_entry_trampoline;
    *--sp = 0x202;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    t->rsp = (uint64_t)sp;
    return t->id;
}

void yield(void) {
    if (!g_init_done) return;
    int prev = g_current;
    int next = pick_next();
    if (next == prev) return;
    g_current = next;
    context_switch(&g_tasks[prev].rsp, g_tasks[next].rsp, g_tasks[next].cr3);
}

task_id task_current_id(void) {
    return g_init_done ? g_tasks[g_current].id : 0;
}

const char* task_current_name(void) {
    return g_init_done ? g_tasks[g_current].name : "kernel";
}

int task_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_TASKS; i++) if (g_tasks[i].used) n++;
    return n;
}

/* ---- Per-task event queues -------------------------------------- */

/* Find the slot index for a task id. -1 if not found. */
static int slot_for_id(task_id id) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].used && g_tasks[i].id == id) return i;
    }
    return -1;
}

int task_post_event_to(task_id target, const struct gui_event* ev) {
    if (!ev) return 0;
    int slot = slot_for_id(target);
    if (slot < 0) return 0;
    struct task* t = &g_tasks[slot];
    uint8_t next_head = (uint8_t)((t->ev_head + 1) % EVQ_SIZE);
    if (next_head == t->ev_tail) return 0;     /* full, drop event */
    t->evq[t->ev_head] = *ev;
    t->ev_head = next_head;
    return 1;
}

int task_get_event(struct gui_event* out) {
    if (!g_init_done || !out) return 0;
    struct task* t = &g_tasks[g_current];
    if (t->ev_head == t->ev_tail) return 0;    /* empty */
    *out = t->evq[t->ev_tail];
    t->ev_tail = (uint8_t)((t->ev_tail + 1) % EVQ_SIZE);
    return 1;
}

/* ---- Window ownership ------------------------------------------ */

void task_register_window(task_id owner, int win_handle) {
    if (win_handle < 0 || win_handle >= MAX_WIN_OWNERSHIP) return;
    g_win_owner[win_handle] = owner;
}

void task_unregister_window(int win_handle) {
    if (win_handle < 0 || win_handle >= MAX_WIN_OWNERSHIP) return;
    g_win_owner[win_handle] = -1;
}

task_id task_lookup_owner(int win_handle) {
    if (win_handle < 0 || win_handle >= MAX_WIN_OWNERSHIP) return -1;
    return g_win_owner[win_handle];
}

/* ---- Spawn with args ------------------------------------------- */

/* Trampoline for apps: pulls the saved entry+arg from the task slot
 * and calls entry(arg). Symmetrical to task_entry_trampoline above. */
static void task_entry_trampoline_arg(void) {
    int me = g_current;
    void (*ent)(const char*) = (void(*)(const char*))g_tasks[me].entry;
    const char* arg = g_tasks[me].entry_arg;
    ent(arg);
    g_tasks[me].used = 0;
    for (;;) yield();
}

/* Deep-clone the kernel's page tables, replacing PD[1] (which covers
 * virtual 0x200000..0x3FFFFF) with a private 2 MiB huge-page mapping
 * to `user_phys`. Result: the new task sees its own private user-
 * space at virtual 0x200000, while the rest of the address space
 * (kernel low memory, BSS, heap, framebuffer regions) stays shared.
 *
 * Returns the physical address of the new PML4 (== virtual since
 * page tables live in identity-mapped heap), or 0 on failure. */
static uint64_t clone_pml4_for_app(uint64_t user_phys) {
    uint64_t* pml4 = (uint64_t*)page_alloc_4k();
    uint64_t* pdpt = (uint64_t*)page_alloc_4k();
    uint64_t* pd   = (uint64_t*)page_alloc_4k();
    if (!pml4 || !pdpt || !pd) return 0;

    /* PML4: only entry 0 is used (covers the first 512 GiB, which
     * is the whole address space the kernel + apps care about). */
    for (int i = 0; i < 512; i++) pml4[i] = 0;

    /* Copy the kernel's PDPT (at physical 0x2000) into the new PDPT.
     * Entry 3 maps 3..4 GiB which holds the BGA framebuffer; without
     * carrying it over, the app can't draw anything. We only override
     * entry 0 below to point at our private PD with the swapped PD[1]. */
    const uint64_t* kpdpt = (const uint64_t*)0x2000;
    for (int i = 0; i < 512; i++) pdpt[i] = kpdpt[i];

    /* Copy the kernel's PD (at physical 0x3000) into the new PD,
     * then override entry 1 to point at the task's private user_phys. */
    const uint64_t* kpd = (const uint64_t*)0x3000;
    for (int i = 0; i < 512; i++) pd[i] = kpd[i];
    pd[1] = (user_phys & ~(uint64_t)0x1FFFFF) | 0x83;   /* P|RW|PS */

    pdpt[0] = (uint64_t)pd   | 0x03;   /* P|RW (not a huge page) */
    pml4[0] = (uint64_t)pdpt | 0x03;
    return (uint64_t)pml4;
}

/* Trampoline for app tasks: calls the entry as a long(*)(const char*),
 * captures the return value into exit_code so task_join() can read it. */
static void task_entry_trampoline_app(void) {
    int me = g_current;
    long (*ent)(const char*) =
        (long(*)(const char*))g_tasks[me].entry;
    const char* arg = g_tasks[me].entry_arg;
    long rc = ent(arg);
    /* Defensive cleanup on task exit:
     * - silence the PC speaker so a crashed/exited app can't leave
     *   the tone gated "on" forever (port 0x61 is sticky);
     * - leave gfx mode + cursor in a sane state (already handled
     *   downstream in loader_run, but apps spawned via the async
     *   launch syscall don't go through that path). */
    sound_tone_off();
    g_tasks[me].exit_code = (int)rc;
    g_tasks[me].used      = 0;
    for (;;) yield();
}

task_id task_spawn_app(const char* name,
                       uint64_t    user_phys,
                       const char* args) {
    if (!g_init_done) task_init();
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (!g_tasks[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1;
    struct task* t = &g_tasks[slot];

    t->stack_base = (uint8_t*)heap_alloc(STACK_SIZE);
    if (!t->stack_base) return -1;
    uint64_t pml4_phys = clone_pml4_for_app(user_phys);
    if (!pml4_phys) return -1;
    t->cr3       = pml4_phys;
    t->used      = 1;
    /* The app's entry point lives at virtual 0x200000 in its private
     * user-space (== user_phys physically). The trampoline jumps to
     * that virtual address once CR3 is loaded. */
    t->entry     = (void(*)(void))0x200000ULL;
    t->entry_arg = args;
    t->exit_code = 0;
    t->id        = g_next_id++;
    t->app_cwd   = g_tasks[g_current].app_cwd;
    t->ev_head   = 0;
    t->ev_tail   = 0;
    int n = 0;
    while (name && name[n] && n < 31) { t->name[n] = name[n]; n++; }
    t->name[n] = 0;

    uint64_t* sp = (uint64_t*)(t->stack_base + STACK_SIZE);
    *--sp = (uint64_t)task_entry_trampoline_app;
    *--sp = 0x202;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    t->rsp = (uint64_t)sp;
    return t->id;
}

int task_join(task_id tid) {
    if (!g_init_done) return -1;
    /* Find the slot once; the id stays stable for this task's life. */
    for (;;) {
        int slot = -1;
        for (int i = 0; i < MAX_TASKS; i++) {
            if (g_tasks[i].id == tid) { slot = i; break; }
        }
        if (slot < 0) return -1;
        if (!g_tasks[slot].used) return g_tasks[slot].exit_code;
        yield();
    }
}

task_id task_spawn_with_arg(const char* name,
                            void (*entry)(const char* args),
                            const char* args) {
    if (!g_init_done) task_init();
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (!g_tasks[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1;
    struct task* t = &g_tasks[slot];

    t->stack_base = (uint8_t*)heap_alloc(STACK_SIZE);
    if (!t->stack_base) return -1;
    uint64_t* pml4 = clone_pml4();
    if (!pml4) return -1;
    t->cr3       = (uint64_t)pml4;
    t->used      = 1;
    t->entry     = (void(*)(void))entry;
    t->entry_arg = args;
    t->id        = g_next_id++;
    t->app_cwd   = g_tasks[g_current].app_cwd;
    t->ev_head   = 0;
    t->ev_tail   = 0;
    int n = 0;
    while (name && name[n] && n < 31) { t->name[n] = name[n]; n++; }
    t->name[n] = 0;

    uint64_t* sp = (uint64_t*)(t->stack_base + STACK_SIZE);
    *--sp = (uint64_t)task_entry_trampoline_arg;
    *--sp = 0x202;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    t->rsp = (uint64_t)sp;
    return t->id;
}

/* ---- per-task cwd accessors ----------------------------------- */

uint16_t task_get_cwd(void) {
    if (!g_init_done) return 0;
    return g_tasks[g_current].app_cwd;
}

void task_set_cwd(uint16_t cluster) {
    if (!g_init_done) return;
    g_tasks[g_current].app_cwd = cluster;
}

uint16_t task_get_cwd_for(task_id tid) {
    if (!g_init_done) return 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].used && g_tasks[i].id == tid)
            return g_tasks[i].app_cwd;
    }
    return 0;
}

void task_set_cwd_for(task_id tid, uint16_t cluster) {
    if (!g_init_done) return;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].used && g_tasks[i].id == tid) {
            g_tasks[i].app_cwd = cluster;
            return;
        }
    }
}
