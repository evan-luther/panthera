/* Direct si_user_byuid probe */
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pwd.h>

static void w(const char *s) { write(1, s, strlen(s)); }
static void wp(const void *p) { char b[24]; snprintf(b,sizeof(b),"%p",p); w(b); }

extern void *si_module_static_file(void);
extern void *si_user_byuid(void *, unsigned int);
extern void *si_user_byname(void *, const char *, const char *);

int main(void) {
    w("=== si_user direct probe ===\n");

    void *mod = si_module_static_file();
    w("module: "); wp(mod); w("\n");

    w("si_user_byuid(mod,0): ");
    void *item = si_user_byuid(mod, 0);
    wp(item); w("\n");

    w("si_user_byname(mod,root,NULL): ");
    item = si_user_byname(mod, "root", NULL);
    wp(item); w("\n");

    /* Also test getpwuid */
    w("getpwuid(0): ");
    struct passwd *pw = getpwuid(0);
    if (pw) w(pw->pw_name); else w("NULL");
    w("\n");

    w("=== done ===\n");
    return 0;
}
