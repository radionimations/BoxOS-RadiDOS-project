/* BoxOS-side entry for FastDoom.
 *
 * BoxOS apps start at app_main(args). FastDoom's logic lives in
 * D_DoomMain(); we wrap it. We also seed myargc/myargv with a
 * single fake argv[0] since FastDoom expects them. */

#include "boxos_app.h"

extern int  myargc;
extern char** myargv;
extern void D_DoomMain(void);

static char  prog_name[] = "doom";
static char* fake_argv[2] = { prog_name, 0 };

int app_main(const char* args) {
    (void)args;
    bos_puts("[DOOM] CK0 app_main entered\n");
    myargc = 1;
    myargv = fake_argv;

    bos_puts("[DOOM] CK0.5 about to call D_DoomMain\n");
    D_DoomMain();    /* never returns under normal play */
    bos_puts("[DOOM] D_DoomMain returned -- hanging\n");
    for (;;) { __asm__ __volatile__("cli; hlt"); }
    return 0;
}
