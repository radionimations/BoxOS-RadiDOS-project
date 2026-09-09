/* Cooperative-multitasking core for BoxOS 2.0.
 *
 * Each task is a separate kernel-mode thread of execution with its
 * own stack. They share one address space for now — per-task page
 * tables are the next step. Tasks yield voluntarily; no preemption.
 *
 * Boot sequence: kernel_main calls task_init() to register itself as
 * task 0, then task_spawn() for any kernel tasks (clock ticker,
 * background widgets), then enters its normal loop. Every call to
 * yield() (and timer_sleep_ms, which yields under the hood) gives the
 * scheduler a chance to pick another runnable task. */

#ifndef BOXOS_TASK_H
#define BOXOS_TASK_H

#include "boxos.h"

typedef int task_id;

/* Register the current execution as task 0 ("kernel"). Must be called
 * exactly once, before any task_spawn() or yield(). */
void task_init(void);

/* Spawn a new task with its own stack. Returns the task id, or -1 if
 * we're out of slots / heap. The new task starts running on the first
 * yield() the caller makes. */
task_id task_spawn(const char* name, void (*entry)(void));

/* Voluntary scheduler entry point. Picks the next runnable task and
 * context-switches to it. No-op if there's only one task. */
void    yield(void);

/* Introspection — handy for debug HUDs. */
task_id     task_current_id(void);
const char* task_current_name(void);
int         task_count(void);

/* Demo heartbeat: bumped once per second by the bundled ticker task
 * spawned from kernel_main. Visible in the menu bar to prove that
 * cooperative multitasking is actually switching. */
extern volatile uint32_t g_task_heartbeat;

/* ---- Per-task event queues -------------------------------------
 *
 * Each task owns a ring buffer of GUI events. The WM's drain step
 * routes hardware events to the right task's queue based on window
 * ownership and focus. The task's gui_poll_event syscall pulls
 * from its own queue, so multiple tasks (= multiple apps) can each
 * have their own input stream without stepping on each other.
 *
 * Returns 0 if the queue is full / target task doesn't exist.
 * task_get_event reads from the *current* task's queue. */
struct gui_event;
int task_post_event_to(task_id target, const struct gui_event* ev);
int task_get_event(struct gui_event* out);

/* Find the task that "owns" a given window. Returns -1 if no owner
 * was registered (shouldn't happen for properly-opened windows). */
task_id task_lookup_owner(int win_handle);

/* Register / unregister a window with a task. Called by the WM
 * from wm_open_window / wm_close_window — apps don't touch these. */
void task_register_window(task_id owner, int win_handle);
void task_unregister_window(int win_handle);

/* Spawn a task whose initial entry is an app `main(args)` style
 * call. The args pointer is copied into the new task's saved
 * register r12 so its trampoline can pass it through. */
task_id task_spawn_with_arg(const char* name,
                            void (*entry)(const char* args),
                            const char* args);

/* Phase 2: spawn an app task with a private user-space half.
 *
 * `user_phys` is a 2 MiB-aligned physical address (also identity-
 * mapped, so the kernel can write the binary into it). The new
 * task's PML4 is deep-cloned so virtual 0x200000 maps to user_phys
 * for this task only. The app's binary should already be loaded
 * into user_phys when this is called. Returns the task id. */
task_id task_spawn_app(const char* name,
                       uint64_t    user_phys,
                       const char* args);

/* Block the current task until the named task exits, then return
 * its exit code. Implemented as a cooperative yield loop. */
int task_join(task_id tid);

/* Per-task working directory (FAT12 cluster). 0 == root. Lets two
 * apps run side-by-side without one launcher's fs_set_app_cwd
 * clobbering the other task's view. fat12.c's app-facing read/write
 * helpers go through these. */
uint16_t task_get_cwd(void);
void     task_set_cwd(uint16_t cluster);
uint16_t task_get_cwd_for(task_id tid);
void     task_set_cwd_for(task_id tid, uint16_t cluster);

#endif
