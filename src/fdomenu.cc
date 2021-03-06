/*
 *  FDOmenu - Menu code generator for icewm
 *  Copyright (C) 2015 Eduard Bloch
 *
 *  Inspired by icewm-menu-gnome2 and Freedesktop.org specifications
 *  Using pure glib/gio code and a built-in menu structure instead
 *  the XML based external definition (as suggested by FD.o specs)
 *
 *  Release under terms of the GNU Library General Public License
 *  (version 2.0)
 *
 *  2015/02/05: Eduard Bloch <edi@gmx.de>
 *  - initial version
 */

#include "config.h"
#include "base.h"
#include "sysdep.h"
#include "intl.h"
#include "appnames.h" // for QUOTE macro

char const *ApplicationName;

#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <gio/gdesktopappinfo.h>
#include <string>
#include <list>

typedef GTree* tMenuContainer;

template<class T>
struct auto_gfree
{
        T *m_p;
        auto_gfree() : m_p(NULL) {}
        auto_gfree(T *xp) : m_p(xp) {}
        ~auto_gfree() { g_free(m_p); }
};
struct auto_gunref
{
        GObject *m_p;
        auto_gunref(GObject *xp): m_p(xp) {}
        ~auto_gunref() { g_object_unref(m_p); }
};

bool find_in_zArray(const char * const *arrToScan, const char *keyword)
{
        for (const gchar * const * p=arrToScan;*p;++p)
                if (!strcmp(keyword, *p))
                        return true;
        return false;
}

// for optional bits that are not consuming much and
// it's OK to leak them, OS will clean up soon enough
//#define FREEASAP
#ifdef FREEASAP
#define opt_g_free(x) g_free(x);
#else
#define opt_g_free(x)
#endif

tMenuContainer msettings=0, mscreensavers=0, maccessories=0, mdevelopment=0, meducation=0,
                mgames=0, mgraphics=0, mmultimedia=0, mnetwork=0, moffice=0, msystem=0,
                mother=0, mwine=0, meditors=0, maccessibility=0;

struct tListMeta
{
        const char *title;
        tMenuContainer* store;
};

char *themedIonToPath(char *icon_path);

tListMeta menuinfo[] =
{
{ N_("Accessibility"), &maccessibility },
{ N_("Settings"), &msettings },
{ N_("Screensavers"), &mscreensavers },
{ N_("Accessories"), &maccessories },
{ N_("Development"), &mdevelopment },
{ N_("Education"), &meducation },
{ N_("Games"), &mgames },
{ N_("Graphics"), &mgraphics },
{ N_("Multimedia"), &mmultimedia },
{ N_("Network"), &mnetwork },
{ N_("Office"), &moffice },
{ N_("System"), &msystem },
{ N_("WINE"), &mwine },
{ N_("Editors"), &meditors },
{ N_("Other"), &mother }
};

void proc_dir(const char *path, unsigned depth=0)
{
        GDir *pdir = g_dir_open (path, 0, NULL);
        if (!pdir)
                return;
        struct tdircloser {
                GDir *m_p;
                tdircloser(GDir *p) : m_p(p) {}
                ~tdircloser() { g_dir_close(m_p);}
        } dircloser(pdir);

        const gchar *szFilename(NULL);
        while (NULL != (szFilename = g_dir_read_name (pdir)))
        {
                if (!szFilename)
                        continue;
                gchar *szFullName = g_strjoin("/", path, szFilename, NULL);
                auto_gfree<gchar> xxfree(szFullName);
                static GStatBuf buf;
                if (g_stat(szFullName, &buf))
                        return;
                if (S_ISDIR(buf.st_mode))
                {
                        static ino_t reclog[6];
                        for (unsigned i=0; i<depth; ++i)
                        {
                                if (reclog[i] == buf.st_ino)
                                        goto dir_visited_before;
                        }
                        if (depth<ACOUNT(reclog))
                        {
                                reclog[++depth] = buf.st_ino;
                                proc_dir(szFullName, depth);
                                --depth;
                        }
                        dir_visited_before:;
                }

                if (!S_ISREG(buf.st_mode))
                        continue;

                GDesktopAppInfo *pInfo = g_desktop_app_info_new_from_filename (szFullName);
                if (!pInfo)
                        continue;
                auto_gunref ___pinfo_releaser((GObject*)pInfo);

                if (!g_app_info_should_show((GAppInfo*) pInfo))
                        continue;

                const char *cmdraw = g_app_info_get_commandline ((GAppInfo*) pInfo);
                if (!cmdraw || !*cmdraw)
                        continue;

                // if the strings contains the exe and then only file/url tags that we wouldn't
                // set anyway, THEN create a simplified version and use it later (if bSimpleCmd is true)
                // OR use the original command through a wrapper (if bSimpleCmd is false)
                bool bUseSimplifiedCmd = true;
                gchar * cmdMod = g_strdup(cmdraw);
                auto_gfree<gchar> cmdfree(cmdMod);
                gchar *pcut = strpbrk(cmdMod, " \f\n\r\t\v");

                if (pcut)
                {
                        bool bExpectXchar=false;
                        for (gchar *p=pcut; *p && bUseSimplifiedCmd; ++p)
                        {
                                int c = (unsigned) *p;
                                if (bExpectXchar)
                                {
                                        if (strchr("FfuU", c))
                                                bExpectXchar = false;
                                        else
                                                bUseSimplifiedCmd = false;
                                        continue;
                                }
                                else if (c == '%')
                                {
                                        bExpectXchar = true;
                                        continue;
                                }
                                else if (isspace(unsigned(c)))
                                        continue;
                                else if (! strchr(p, '%'))
                                        goto cmdMod_is_good_as_is;
                                else
                                        bUseSimplifiedCmd = false;
                        }

                        if (bExpectXchar)
                                bUseSimplifiedCmd = false;
                        if (bUseSimplifiedCmd)
                                *pcut = '\0';
                        cmdMod_is_good_as_is:;
                }

                const char *pName=g_app_info_get_name( (GAppInfo*) pInfo);
                if (!pName)
                        continue;
                const char *pCats=g_desktop_app_info_get_categories(pInfo);
                if (!pCats)
                        pCats="Other";
                if (0 == strncmp(pCats, "X-", 2))
                        continue;

                const char *sicon = "-";
                GIcon *pIcon=g_app_info_get_icon( (GAppInfo*) pInfo);
                auto_gfree<char> iconstringrelease;
                if (pIcon)
                {
                        char *icon_path = g_icon_to_string(pIcon);
                        if (G_IS_THEMED_ICON(pIcon)) {
                                char * realIconPath = themedIonToPath(icon_path);
                                g_free(icon_path);
                                icon_path = realIconPath;
                        }
                        iconstringrelease.m_p=icon_path;
                        sicon=icon_path;
                }

                gchar *menuLine;
                bool bForTerminal = false;
#if GLIB_VERSION_CUR_STABLE >= G_ENCODE_VERSION(2, 36)
                bForTerminal = g_desktop_app_info_get_boolean(pInfo, "Terminal");
#else
                // cannot check terminal property, callback is as safe bet
                bUseSimplifiedCmd = false;
#endif

                if (bUseSimplifiedCmd && !bForTerminal) // best case
                        menuLine = g_strjoin(" ", sicon, cmdMod, NULL);
#ifdef XTERMCMD
                else if (bForTerminal && bUseSimplifiedCmd)
                        menuLine = g_strjoin(" ", sicon, QUOTE(XTERMCMD), "-e", cmdMod, NULL);
#endif
                else // not simple command or needs a terminal started via launcher callback, or both
                        menuLine = g_strdup_printf("%s %s \"%s\"", sicon, ApplicationName, szFullName);

                // Pigeonholing roughly by guessed menu structure
#define add2menu(x) { g_tree_replace(x, g_strdup(pName), menuLine); }
                gchar **ppCats = g_strsplit(pCats, ";", -1);
                if (find_in_zArray(ppCats, "Screensaver"))
                        add2menu(mscreensavers)
                else if (find_in_zArray(ppCats, "Settings"))
                        add2menu(msettings)
                else if (find_in_zArray(ppCats, "Accessories"))
                        add2menu(maccessories)
                else if (find_in_zArray(ppCats, "Development"))
                        add2menu(mdevelopment)
                else if (find_in_zArray(ppCats, "Education"))
                        add2menu(meducation)
                else if (find_in_zArray(ppCats, "Game"))
                        add2menu(mgames)
                else if (find_in_zArray(ppCats, "Graphics"))
                        add2menu(mgraphics)
                else if (find_in_zArray(ppCats, "AudioVideo") || find_in_zArray(ppCats, "Audio")
                                || find_in_zArray(ppCats, "Video"))
                {
                        add2menu(mmultimedia)
                }
                else if (find_in_zArray(ppCats, "Network"))
                        add2menu(mnetwork)
                else if (find_in_zArray(ppCats, "Office"))
                        add2menu(moffice)
                else if (find_in_zArray(ppCats, "System") || find_in_zArray(ppCats, "Emulator"))
                        add2menu(msystem)
                else if (strstr(pCats, "Editor"))
                        add2menu(meditors)
                else if (strstr(pCats, "Accessibility"))
                        add2menu(maccessibility)
                else
                {
#if GLIB_VERSION_CUR_STABLE >= G_ENCODE_VERSION(2, 34)
                        const char *pwmclass = g_desktop_app_info_get_startup_wm_class(pInfo);
                        if (pwmclass && strstr(pwmclass, "Wine"))
                                add2menu(mwine)
                        else
#endif
                        if (strstr(cmdraw, " wine "))
                                add2menu(mwine)
                        else
                                add2menu(mother)
                }
                g_strfreev(ppCats);
        }
}

char *themedIonToPath(char *icon_theme_name) {
        std::list<std::string> iconSearchOrder;
        iconSearchOrder.push_back("/usr/share/icons/hicolor/48x48/apps/%s.png");
        iconSearchOrder.push_back("/usr/share/pixmaps/%s.png");
        iconSearchOrder.push_back("/usr/share/pixmaps/%s.xpm");

        for (std::list<std::string>::iterator oneSearchPath = iconSearchOrder.begin() ; oneSearchPath != iconSearchOrder.end(); ++oneSearchPath) {
                char *pathToFile = g_strdup_printf(oneSearchPath->c_str(), icon_theme_name);
                if (g_file_test(pathToFile, G_FILE_TEST_EXISTS))
                {
                        return pathToFile;
                } else
                {
                        g_free((pathToFile));
                }
        }
        return g_strdup("noicon.png");
}

static gboolean printKey(const char *key, const char *value, void*)
{
        printf("prog \"%s\" %s\n", key, value);
        return FALSE;
}

void print_submenu(const char *title, tMenuContainer data)
{
        if (!data || !g_tree_nnodes(data))
                return;
        printf("menu \"%s\" folder {\n", title);
        g_tree_foreach(data, (GTraverseFunc) printKey, NULL);
        puts("}");
}

void dump_menu()
{
        for (tListMeta *p=menuinfo; p < menuinfo+ACOUNT(menuinfo)-1; ++p)
                print_submenu(p->title, * p->store);
        puts("separator");
        print_submenu(menuinfo[ACOUNT(menuinfo)-1].title, * menuinfo[ACOUNT(menuinfo)-1].store);
}

bool launch(const char *dfile, const char **argv, int argc)
{
        GDesktopAppInfo *pInfo = g_desktop_app_info_new_from_filename (dfile);
        if (!pInfo)
                return false;
#if 0 // g_file_get_uri crashes, no idea why, even enforcing file prefix doesn't help
        if (argc>0)
        {
                GList* parms=NULL;
                for (int i=0; i<argc; ++i)
                        parms=g_list_append(parms,
                                        g_strdup_printf("%s%s", strstr(argv[i], "://") ? "" : "file://",
                                                        argv[i]));
                return g_app_info_launch ((GAppInfo *)pInfo,
                                   parms, NULL, NULL);
        }
        else
#else
        (void) argv;
        (void) argc;
#endif
        return g_app_info_launch ((GAppInfo *)pInfo,
                   NULL, NULL, NULL);
}
static int
cmpstringp(const void *p1, const void *p2)
{
    return g_utf8_collate(* (char * const *) p1, * (char * const *) p2);
}

static void init()
{
#ifdef CONFIG_I18N
        setlocale (LC_ALL, "");
#endif

    bindtextdomain(PACKAGE, LOCDIR);
    textdomain(PACKAGE);

    for (tListMeta *p=menuinfo; p < menuinfo+ACOUNT(menuinfo); ++p)
    {
#ifdef ENABLE_NLS
        p->title = gettext(p->title);
#endif
        *(p->store) = g_tree_new((GCompareFunc) g_utf8_collate);
    }

    qsort(menuinfo, ACOUNT(menuinfo)-1, sizeof(menuinfo[0]), cmpstringp);
}

static void help(const char *home, const char *dirs, FILE* out, int xit)
{
    g_fprintf(out,
            "This program doesn't use command line options. It only listens to\n"
            "environment variables defined by XDG Base Directory Specification.\n"
            "XDG_DATA_HOME=%s\n"
            "XDG_DATA_DIRS=%s\n"
            , home, dirs);
    exit(xit);
}

int main(int argc, const char **argv)
{
        ApplicationName = my_basename(argv[0]);

        init();

        const char * usershare=getenv("XDG_DATA_HOME"),
                        *sysshare=getenv("XDG_DATA_DIRS");

        if (!usershare || !*usershare)
                usershare=g_strjoin(NULL, getenv("HOME"), "/.local/share", NULL);

        if (!sysshare || !*sysshare)
                sysshare="/usr/local/share:/usr/share";

        if (argc>1)
        {
                if (is_version_switch(argv[1]))
                        print_version_exit(VERSION);
                if (is_help_switch(argv[1]))
                        help(usershare, sysshare, stdout, EXIT_SUCCESS);

                if (strstr(argv[1], ".desktop") && launch(argv[1], argv+2, argc-2))
                        return EXIT_SUCCESS;

                help(usershare, sysshare, stderr, EXIT_FAILURE);
        }
        gchar **ppDirs = g_strsplit (sysshare, ":", -1);
#ifdef FREEASAP
        g_strfreev(ppDirs);
#endif
        for (const gchar * const * p = ppDirs; *p; ++p)
        {
                gchar *pmdir = g_strjoin(0, *p, "/applications", NULL);
                proc_dir(pmdir);
                opt_g_free(pmdir);
        }
        // user's stuff might replace the system links
        gchar *usershare_full = g_strjoin(NULL, usershare, "/applications", NULL);
        proc_dir(usershare_full);
        opt_g_free(usershare_full);

        dump_menu();

        return EXIT_SUCCESS;
}

// vim: set sw=4 ts=4 et:
