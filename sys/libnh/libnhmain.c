/* NetHack 3.7  libnhmain.c $NHDT-Date: 1693359589 2023/08/30 01:39:49 $  $NHDT-Branch: keni-crashweb2 $:$NHDT-Revision: 1.106 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Robert Patrick Rankin, 2011. */
/* NetHack may be freely redistributed.  See license for details. */

/* main.c - Unix NetHack */

#include "hack.h"
#include "dlb.h"

#include <sys/stat.h>
#include <signal.h>
#include <pwd.h>
#include <func_tab.h>
#ifndef O_RDONLY
#include <fcntl.h>
#endif

/* for cross-compiling to WebAssembly (WASM) */
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
void js_helpers_init();
void js_constants_init();
void js_globals_init();
#endif

#if !defined(_BULL_SOURCE) && !defined(__sgi) && !defined(_M_UNIX)
#if !defined(SUNOS4) && !(defined(ULTRIX) && defined(__GNUC__))
#if defined(POSIX_TYPES) || defined(SVR4) || defined(HPUX)
extern struct passwd *getpwuid(uid_t);
#else
extern struct passwd *getpwuid, (int);
#endif
#endif
#endif
extern struct passwd *getpwnam(const char *);
#ifdef CHDIR
static void chdirx(const char *, boolean);
#endif /* CHDIR */
static boolean whoami(void);
static void process_options(int, char **);

#ifdef _M_UNIX
extern void check_sco_console(void);
extern void init_sco_cons(void);
#endif
#ifdef __linux__
extern void check_linux_console(void);
extern void init_linux_cons(void);
#endif

static void wd_message(void);
static struct passwd *get_unix_pw(void);
ATTRNORETURN static void opt_terminate(void) NORETURN;

#ifdef __EMSCRIPTEN__
/* if WebAssembly, export this API and don't optimize it out */
EMSCRIPTEN_KEEPALIVE
int
main(int argc, char *argv[])

#else /* !__EMSCRIPTEN__ */
int nhmain(int argc, char *argv[]);

int
nhmain(int argc, char *argv[])

#endif /* __EMSCRIPTEN__ */
{
#ifdef CHDIR
    char *dir;
#endif
    NHFILE *nhfp;
    boolean exact_username;
    boolean resuming = FALSE; /* assume new game */
    boolean plsel_once = FALSE;
    // int i;
    // for (i = 0; i < argc; i++) {
    //     printf ("argv[%d]: %s\n", i, argv[i]);
    // }

    early_init(argc, argv);

    gh.hname = argv[0];
    svh.hackpid = getpid();
    (void) umask(0777 & ~FCMASK);

    choose_windows(DEFAULT_WINDOW_SYS);

#ifdef CHDIR /* otherwise no chdir() */
    /*
     * See if we must change directory to the playground.
     * (Perhaps hack runs suid and playground is inaccessible
     *  for the player.)
     * The environment variable HACKDIR is overridden by a
     *  -d command line option (must be the first option given).
     */
    dir = nh_getenv("NETHACKDIR");
    if (!dir)
        dir = nh_getenv("HACKDIR");

    if (argc > 1) {
        if (argcheck(argc, argv, ARG_VERSION) == 2)
            exit(EXIT_SUCCESS);

#ifdef CRASHREPORT
        if (argcheck(argc, argv, ARG_BIDSHOW))
            exit(EXIT_SUCCESS);
#endif

        if (argcheck(argc, argv, ARG_SHOWPATHS) == 2) {
            gd.deferred_showpaths = TRUE;
            return;
        }
        if (argcheck(argc, argv, ARG_DEBUG) == 1) {
            argc--;
            argv++;
        }
        if (argc > 1 && !strncmp(argv[1], "-d", 2) && argv[1][2] != 'e') {
            /* avoid matching "-dec" for DECgraphics; since the man page
             * says -d directory, hope nobody's using -desomething_else
             */
            argc--;
            argv++;
            dir = argv[0] + 2;
            if (*dir == '=' || *dir == ':')
                dir++;
            if (!*dir && argc > 1) {
                argc--;
                argv++;
                dir = argv[0];
            }
            if (!*dir)
                error("Flag -d must be followed by a directory name.");
        }
    }
#endif /* CHDIR */

    if (argc > 1) {
        /*
         * Now we know the directory containing 'record' and
         * may do a prscore().  Exclude `-style' - it's a Qt option.
         */
        if (!strncmp(argv[1], "-s", 2) && strncmp(argv[1], "-style", 6)) {
#ifdef CHDIR
            chdirx(dir, 0);
#endif
#ifdef SYSCF
            initoptions();
#endif
#ifdef PANICTRACE
            ARGV0 = gh.hname; /* save for possible stack trace */
#ifndef NO_SIGNAL
            panictrace_setsignals(TRUE);
#endif
#endif
            prscore(argc, argv);
            /* FIXME: shouldn't this be using nh_terminate() to free
               up any memory allocated by initoptions() */
            exit(EXIT_SUCCESS);
        }
    } /* argc > 1 */

/*
 * Change directories before we initialize the window system so
 * we can find the tile file.
 */
#ifdef CHDIR
    chdirx(dir, 1);
#endif

#ifdef __EMSCRIPTEN__
    js_helpers_init();
    js_constants_init();
    js_globals_init();
#endif

#ifdef _M_UNIX
    check_sco_console();
#endif
#ifdef __linux__
    check_linux_console();
#endif
    initoptions();
#ifdef PANICTRACE
    ARGV0 = gh.hname; /* save for possible stack trace */
#ifndef NO_SIGNAL
    panictrace_setsignals(TRUE);
#endif
#endif
    exact_username = whoami();

    /*
     * It seems you really want to play.
     */
    u.uhp = 1; /* prevent RIP on early quits */
    program_state.preserve_locks = 1;
#ifndef NO_SIGNAL
    sethanguphandler((SIG_RET_TYPE) hangup);
#endif

    process_options(argc, argv); /* command line options */
#ifdef WINCHAIN
    commit_windowchain();
#endif
    init_nhwindows(&argc, argv); /* now we can set up window system */
#ifdef _M_UNIX
    init_sco_cons();
#endif
#ifdef __linux__
    init_linux_cons();
#endif

#ifdef DEF_PAGER
    if (!(gc.catmore = nh_getenv("HACKPAGER"))
        && !(gc.catmore = nh_getenv("PAGER")))
        gc.catmore = DEF_PAGER;
#endif
#ifdef MAIL
    getmailstatus();
#endif

    /* wizard mode access is deferred until here */
    set_playmode(); /* sets plname to "wizard" for wizard mode */
    /* hide any hyphens from plnamesuffix() */
    gp.plnamelen = exact_username ? (int) strlen(svp.plname) : 0;
    /* strip role,race,&c suffix; calls askname() if plname[] is empty
       or holds a generic user name like "player" or "games" */
    plnamesuffix();

    if (wizard) {
        /* use character name rather than lock letter for file names */
        gl.locknum = 0;
    } else {
#ifndef NO_SIGNAL
        /* suppress interrupts while processing lock file */
        (void) signal(SIGQUIT, SIG_IGN);
        (void) signal(SIGINT, SIG_IGN);
#endif
    }

    dlb_init(); /* must be before newgame() */

    /*
     * Initialize the vision system.  This must be before mklev() on a
     * new game or before a level restore on a saved game.
     */
    vision_init();

    init_sound_disp_gamewindows();

    /*
     * First, try to find and restore a save file for specified character.
     * We'll return here if new game player_selection() renames the hero.
     */
 attempt_restore:

    /*
     * getlock() complains and quits if there is already a game
     * in progress for current character name (when gl.locknum == 0)
     * or if there are too many active games (when gl.locknum > 0).
     * When proceeding, it creates an empty <lockname>.0 file to
     * designate the current game.
     * getlock() constructs <lockname> based on the character
     * name (for !gl.locknum) or on first available of alock, block,
     * clock, &c not currently in use in the playground directory
     * (for gl.locknum > 0).
     */
    if (*svp.plname) {
        getlock();
        program_state.preserve_locks = 0; /* after getlock() */
    }

    if (*svp.plname && (nhfp = restore_saved_game()) != 0) {
        const char *fq_save = fqname(gs.SAVEF, SAVEPREFIX, 1);

        (void) chmod(fq_save, 0); /* disallow parallel restores */
#ifndef NO_SIGNAL
        (void) signal(SIGINT, (SIG_RET_TYPE) done1);
#endif
#ifdef NEWS
        if (iflags.news) {
            display_file(NEWS, FALSE);
            iflags.news = FALSE; /* in case dorecover() fails */
        }
#endif
        pline("Restoring save file...");
        mark_synch(); /* flush output */
        if (dorecover(nhfp)) {
            resuming = TRUE; /* not starting new game */
            wd_message();
            if (discover || wizard) {
                /* this seems like a candidate for paranoid_confirmation... */
                if (y_n("Do you want to keep the save file?") == 'n') {
                    (void) delete_savefile();
                } else {
                    (void) chmod(fq_save, FCMASK); /* back to readable */
                    nh_compress(fq_save);
                }
            }
        }
    }

    if (!resuming) {
        boolean neednewlock = (!*svp.plname);
        /* new game:  start by choosing role, race, etc;
           player might change the hero's name while doing that,
           in which case we try to restore under the new name
           and skip selection this time if that didn't succeed */
        if (!iflags.renameinprogress || iflags.defer_plname || neednewlock) {
            if (!plsel_once)
                player_selection();
            plsel_once = TRUE;
            if (neednewlock && *svp.plname)
                goto attempt_restore;
            if (iflags.renameinprogress) {
                /* player has renamed the hero while selecting role;
                   if locking alphabetically, the existing lock file
                   can still be used; otherwise, discard current one
                   and create another for the new character name */
                if (!gl.locknum) {
                    delete_levelfile(0); /* remove empty lock file */
                    getlock();
                }
                goto attempt_restore;
            }
        }
        newgame();
        wd_message();
    }

    /* moveloop() never returns but isn't flagged NORETURN */
    moveloop(resuming);

    exit(EXIT_SUCCESS);
    /*NOTREACHED*/
    return 0;
}

/* caveat: argv elements might be arbitrary long */
static void
process_options(int argc, char *argv[])
{
    int i, l;

    /*
     * Process options.
     */
    while (argc > 1 && argv[1][0] == '-') {
        argv++;
        argc--;
        l = (int) strlen(*argv);
        /* must supply at least 4 chars to match "-XXXgraphics" */
        if (l < 4)
            l = 4;

        switch (argv[0][1]) {
        case 'D':
        case 'd':
            if ((argv[0][1] == 'D' && !argv[0][2])
                || !strcmpi(*argv, "-debug")) {
                wizard = TRUE, discover = FALSE;
            } else if (!strncmpi(*argv, "-DECgraphics", l)) {
                load_symset("DECGraphics", PRIMARYSET);
                switch_symbols(TRUE);
            } else {
                raw_printf("Unknown option: %.60s", *argv);
            }
            break;
        case 'X':
            discover = TRUE, wizard = FALSE;
            break;
#ifdef NEWS
        case 'n':
            iflags.news = FALSE;
            break;
#endif
        case 'u':
            if (argv[0][2]) {
                (void) strncpy(svp.plname, argv[0] + 2, sizeof svp.plname - 1);
                gp.plnamelen = 0; /* plname[] might have -role-race attached */
            } else if (argc > 1) {
                argc--;
                argv++;
                (void) strncpy(svp.plname, argv[0], sizeof svp.plname - 1);
                gp.plnamelen = 0;
            } else {
                raw_print("Player name expected after -u");
            }
            break;
        case 'I':
        case 'i':
            if (!strncmpi(*argv, "-IBMgraphics", l)) {
                load_symset("IBMGraphics", PRIMARYSET);
                load_symset("RogueIBM", ROGUESET);
                switch_symbols(TRUE);
            } else {
                raw_printf("Unknown option: %.60s", *argv);
            }
            break;
        case 'p': /* profession (role) */
            if (argv[0][2]) {
                if ((i = str2role(&argv[0][2])) >= 0)
                    flags.initrole = i;
            } else if (argc > 1) {
                argc--;
                argv++;
                if ((i = str2role(argv[0])) >= 0)
                    flags.initrole = i;
            }
            break;
        case 'r': /* race */
            if (argv[0][2]) {
                if ((i = str2race(&argv[0][2])) >= 0)
                    flags.initrace = i;
            } else if (argc > 1) {
                argc--;
                argv++;
                if ((i = str2race(argv[0])) >= 0)
                    flags.initrace = i;
            }
            break;
        case 'w': /* windowtype */
            config_error_init(FALSE, "command line", FALSE);
            choose_windows(&argv[0][2]);
            config_error_done();
            break;
        case '@':
            flags.randomall = 1;
            break;
        default:
            if ((i = str2role(&argv[0][1])) >= 0) {
                flags.initrole = i;
                break;
            }
            /* else raw_printf("Unknown option: %.60s", *argv); */
        }
    }

#ifdef SYSCF
    if (argc > 1)
        raw_printf("MAXPLAYERS are set in sysconf file.\n");
#else
    /* XXX This is deprecated in favor of SYSCF with MAXPLAYERS */
    if (argc > 1)
        gl.locknum = atoi(argv[1]);
#endif
#ifdef MAX_NR_OF_PLAYERS
    /* limit to compile-time limit */
    if (!gl.locknum || gl.locknum > MAX_NR_OF_PLAYERS)
        gl.locknum = MAX_NR_OF_PLAYERS;
#endif
#ifdef SYSCF
    /* let syscf override compile-time limit */
    if (!gl.locknum || (sysopt.maxplayers && gl.locknum > sysopt.maxplayers))
        gl.locknum = sysopt.maxplayers;
#endif
}

#ifdef CHDIR
static void
chdirx(const char *dir, boolean wr)
{
    if (dir /* User specified directory? */
#ifdef HACKDIR
        && strcmp(dir, HACKDIR) /* and not the default? */
#endif
        ) {
#ifdef SECURE
        (void) setgid(getgid());
        (void) setuid(getuid()); /* Ron Wessels */
#endif
    } else {
        /* non-default data files is a sign that scores may not be
         * compatible, or perhaps that a binary not fitting this
         * system's layout is being used.
         */
#ifdef VAR_PLAYGROUND
        int len = strlen(VAR_PLAYGROUND);

        gf.fqn_prefix[SCOREPREFIX] = (char *) alloc(len + 2);
        Strcpy(gf.fqn_prefix[SCOREPREFIX], VAR_PLAYGROUND);
        if (gf.fqn_prefix[SCOREPREFIX][len - 1] != '/') {
            gf.fqn_prefix[SCOREPREFIX][len] = '/';
            gf.fqn_prefix[SCOREPREFIX][len + 1] = '\0';
        }

#endif
    }

#ifdef HACKDIR
    if (dir == (const char *) 0)
        dir = HACKDIR;
#endif

    if (dir && chdir(dir) < 0) {
        perror(dir);
        error("Cannot chdir to %s.", dir);
    }

    /* warn the player if we can't write the record file
     * perhaps we should also test whether . is writable
     * unfortunately the access system-call is worthless.
     */
    if (wr) {
#ifdef VAR_PLAYGROUND
        gf.fqn_prefix[LEVELPREFIX] = gf.fqn_prefix[SCOREPREFIX];
        gf.fqn_prefix[SAVEPREFIX] = gf.fqn_prefix[SCOREPREFIX];
        gf.fqn_prefix[BONESPREFIX] = gf.fqn_prefix[SCOREPREFIX];
        gf.fqn_prefix[LOCKPREFIX] = gf.fqn_prefix[SCOREPREFIX];
        gf.fqn_prefix[TROUBLEPREFIX] = gf.fqn_prefix[SCOREPREFIX];
#endif
        check_recordfile(dir);
    }
}
#endif /* CHDIR */

/* returns True iff we set plname[] to username which contains a hyphen */
static boolean
whoami(void)
{
    /*
     * Who am i? Algorithm: 1. Use name as specified in NETHACKOPTIONS
     *                      2. Use $USER or $LOGNAME    (if 1. fails)
     *                      3. Use getlogin()           (if 2. fails)
     * The resulting name is overridden by command line options.
     * If everything fails, or if the resulting name is some generic
     * account like "games", "play", "player", "hack" then eventually
     * we'll ask him.
     * Note that we trust the user here; it is possible to play under
     * somebody else's name.
     */
    if (!*svp.plname) {
        const char *s;

        s = nh_getenv("USER");
        if (!s || !*s)
            s = nh_getenv("LOGNAME");
        if (!s || !*s)
            s = getlogin();

        if (s && *s) {
            (void) strncpy(svp.plname, s, sizeof svp.plname - 1);
            if (strchr(svp.plname, '-'))
                return TRUE;
        }
    }
    return FALSE;
}

#ifndef NO_SIGNAL
void
sethanguphandler(void (*handler)(int))
{
#ifdef SA_RESTART
    /* don't want reads to restart.  If SA_RESTART is defined, we know
     * sigaction exists and can be used to ensure reads won't restart.
     * If it's not defined, assume reads do not restart.  If reads restart
     * and a signal occurs, the game won't do anything until the read
     * succeeds (or the stream returns EOF, which might not happen if
     * reading from, say, a window manager). */
    struct sigaction sact;

    (void) memset((genericptr_t) &sact, 0, sizeof sact);
    sact.sa_handler = (SIG_RET_TYPE) handler;
    (void) sigaction(SIGHUP, &sact, (struct sigaction *) 0);
#ifdef SIGXCPU
    (void) sigaction(SIGXCPU, &sact, (struct sigaction *) 0);
#endif
#else /* !SA_RESTART */
    (void) signal(SIGHUP, (SIG_RET_TYPE) handler);
#ifdef SIGXCPU
    (void) signal(SIGXCPU, (SIG_RET_TYPE) handler);
#endif
#endif /* ?SA_RESTART */
}
#endif /* !NO_SIGNAL */

#ifdef PORT_HELP
void
port_help(void)
{
    /*
     * Display unix-specific help.   Just show contents of the helpfile
     * named by PORT_HELP.
     */
    display_file(PORT_HELP, TRUE);
}
#endif

/* validate wizard mode if player has requested access to it */
boolean
authorize_wizard_mode(void)
{
    if (sysopt.wizards && sysopt.wizards[0]) {
        if (check_user_string(sysopt.wizards))
            return TRUE;
    }
    iflags.wiz_error_flag = TRUE; /* not being allowed into wizard mode */
    return FALSE;
}

/* similar to above, validate explore mode access */
boolean
authorize_explore_mode(void)
{
#ifdef SYSCF
    if (sysopt.explorers && sysopt.explorers[0]) {
        if (check_user_string(sysopt.explorers))
            return TRUE;
    }
    iflags.explore_error_flag = TRUE; /* not allowed into explore mode */
    return FALSE;
#else
    return TRUE; /* if sysconf disabled, no restrictions on explore mode */
#endif
}

static void
wd_message(void)
{
    if (iflags.wiz_error_flag) {
        if (sysopt.wizards && sysopt.wizards[0]) {
            char *tmp = build_english_list(sysopt.wizards);
            pline("Only user%s %s may access debug (wizard) mode.",
                  strchr(sysopt.wizards, ' ') ? "s" : "", tmp);
            free(tmp);
        } else {
            You("cannot access debug (wizard) mode.");
        }
        wizard = FALSE; /* (paranoia) */
        if (!iflags.explore_error_flag)
            pline("Entering explore/discovery mode instead.");
    } else if (iflags.explore_error_flag) {
        You("cannot access explore mode."); /* same as enter_explore_mode */
        discover = iflags.deferred_X = FALSE; /* (more paranoia) */
    } else if (discover)
        You("are in non-scoring explore/discovery mode.");
}

/*
 * Add a slash to any name not ending in /. There must
 * be room for the /
 */
void
append_slash(char *name)
{
    char *ptr;

    if (!*name)
        return;
    ptr = name + (strlen(name) - 1);
    if (*ptr != '/') {
        *++ptr = '/';
        *++ptr = '\0';
    }
    return;
}

boolean
check_user_string(const char *optstr)
{
    struct passwd *pw;
    int pwlen;
    const char *eop, *w;
    char *pwname = 0;

    if (optstr[0] == '*')
        return TRUE; /* allow any user */
    if (sysopt.check_plname)
        pwname = svp.plname;
    else if ((pw = get_unix_pw()) != 0)
        pwname = pw->pw_name;
    if (!pwname || !*pwname)
        return FALSE;
    pwlen = (int) strlen(pwname);
    eop = eos((char *)optstr);
    w = optstr;
    while (w + pwlen <= eop) {
        if (!*w)
            break;
        if (isspace(*w)) {
            w++;
            continue;
        }
        if (!strncmp(w, pwname, pwlen)) {
            if (!w[pwlen] || isspace(w[pwlen]))
                return TRUE;
        }
        while (*w && !isspace(*w))
            w++;
    }
    return FALSE;
}

static struct passwd *
get_unix_pw(void)
{
    char *user;
    unsigned uid;
    static struct passwd *pw = (struct passwd *) 0;

    if (pw)
        return pw; /* cache answer */

    uid = (unsigned) getuid();
    user = getlogin();
    if (user) {
        pw = getpwnam(user);
        if (pw && ((unsigned) pw->pw_uid != uid))
            pw = 0;
    }
    if (pw == 0) {
        user = nh_getenv("USER");
        if (user) {
            pw = getpwnam(user);
            if (pw && ((unsigned) pw->pw_uid != uid))
                pw = 0;
        }
        if (pw == 0) {
            pw = getpwuid(uid);
        }
    }
    return pw;
}

char *
get_login_name(void)
{
    static char buf[BUFSZ];
    struct passwd *pw = get_unix_pw();

    buf[0] = '\0';
    if (pw)
        (void)strcpy(buf, pw->pw_name);

    return buf;
}

unsigned long
sys_random_seed(void)
{
    unsigned long seed = 0L;
    unsigned long pid = (unsigned long) getpid();
    boolean no_seed = TRUE;
#ifdef DEV_RANDOM
    FILE *fptr;

    fptr = fopen(DEV_RANDOM, "r");
    if (fptr) {
        fread(&seed, sizeof (long), 1, fptr);
        has_strong_rngseed = TRUE;  /* decl.c */
        no_seed = FALSE;
        (void) fclose(fptr);
    } else {
        /* leaves clue, doesn't exit */
        paniclog("sys_random_seed", "falling back to weak seed");
    }
#endif
    if (no_seed) {
        seed = (unsigned long) getnow(); /* time((TIME_type) 0) */
        /* Quick dirty band-aid to prevent PRNG prediction */
        if (pid) {
            if (!(pid & 3L))
                pid -= 1L;
            seed *= pid;
        }
    }
    return seed;
}

/* for command-line options that perform some immediate action and then
   terminate the program without starting play, like 'nethack --version'
   or 'nethack -s Zelda'; do some cleanup before that termination */
ATTRNORETURN static void
opt_terminate(void)
{
    config_error_done(); /* free memory allocated by config_error_init() */

    nh_terminate(EXIT_SUCCESS);
    /*NOTREACHED*/
}

/* show the sysconf file name, playground directory, run-time configuration
   file name, dumplog file name if applicable, and some other things */
ATTRNORETURN void
after_opt_showpaths(const char *dir)
{
#ifdef CHDIR
    chdirx(dir, FALSE);
#else
    nhUse(dir);
#endif
    opt_terminate();
    /*NOTREACHED*/
}

#ifdef __EMSCRIPTEN__
/***
 * Helpers
 ***/
EM_JS(void, js_helpers_init, (), {
    globalThis.nethackGlobal = globalThis.nethackGlobal || {};
    globalThis.nethackGlobal.helpers = globalThis.nethackGlobal.helpers || {};

    installHelper(displayInventory, "displayInventory");
    installHelper(getPointerValue, "getPointerValue");
    installHelper(setPointerValue, "setPointerValue");

    // used by update_inventory
    function displayInventory() {
        // Asyncify.handleAsync(async () => {
            return _display_inventory(0, 0);
        // });
    }

    // convert 'ptr' to the type indicated by 'type'
    function getPointerValue(name, ptr, type) {
        // console.log("getPointerValue", name, "0x" + ptr.toString(16), type);
        switch(type) {
        case "s": // string
            // var value = UTF8ToString(getValue(ptr, "*"));
            return UTF8ToString(ptr);
        case "p": // pointer
            if(!ptr) return 0; // null pointer
            return getValue(ptr, "*");
        case "c": // char
            return String.fromCharCode(getValue(ptr, "i8"));
        case "b":
            return getValue(ptr, "i8") == 1;
        case "0": /* 2^0 = 1 byte */
            return getValue(ptr, "i8");
        case "1": /* 2^1 = 2 bytes */
            return getValue(ptr, "i16");
        case "2": /* 2^2 = 4 bytes */
        case "i": // integer
        case "n": // number
            return getValue(ptr, "i32");
        case "f": // float
            return getValue(ptr, "float");
        case "d": // double
            return getValue(ptr, "double");
        case "v": // void
            return undefined;
        default:
            throw new TypeError ("unknown type:" + type);
        }
    }

    // sets the return value of the function to the type expected
    function setPointerValue(name, ptr, type, value = 0) {
        // console.log("setPointerValue", name, "0x" + ptr.toString(16), type, value);
        switch (type) {
        case "p":
            setValue(ptr, value, "*");
            break;
        case "s":
            if(typeof value !== "string")
                throw new TypeError(`expected ${name} return type to be string`);
            // value=value?value:"(no value)";
            // var strPtr = getValue(ptr, "i32");
            stringToUTF8(value, ptr, 1024); // TODO: uhh... danger will robinson
            break;
        case "i":
            if(typeof value !== "number" || !Number.isInteger(value))
                throw new TypeError(`expected ${name} return type to be integer`);
            setValue(ptr, value, "i32");
            break;
        case "1":
            if(typeof value !== "number" || !Number.isInteger(value))
                throw new TypeError(`expected ${name} return type to be integer`);
            setValue(ptr, value, "i16");
            break;
        case "c":
            if(typeof value !== "number" || value < 0 || value > 128)
                throw new TypeError(`expected ${name} return type to be integer representing an ASCII character`);
            setValue(ptr, value, "i8");
            break;
        case "f":
            if(typeof value !== "number" || isFloat(value))
                throw new TypeError(`expected ${name} return type to be float`);
            // XXX: I'm not sure why 'double' works and 'float' doesn't
            setValue(ptr, value, "double");
            break;
        case "d":
            if(typeof value !== "number" || isFloat(value))
                throw new TypeError(`expected ${name} return type to be double`);
            setValue(ptr, value, "double");
            break;
        case "b":
            if (typeof value !== "boolean")
                throw new TypeError(`expected ${name} return type to be boolean`);
            setValue(ptr, value ? 1 : 0, "i8");
        case "v":
            break;
        default:
            throw new Error("unknown type");
        }

        function isFloat(n){
            return n === +n && n !== (n|0) && !Number.isInteger(n);
        }
    }


    function installHelper(fn, name) {
        name = name || fn.name;
        globalThis.nethackGlobal.helpers[name] = fn;
    }
})

/***
 * Constants
 ***/
#define SET_CONSTANT(scope, name) set_const(scope, #name, name);
EM_JS(void, set_const, (char *scope_str, char *name_str, int num), {
    let scope = UTF8ToString(scope_str);
    let name = UTF8ToString(name_str);

    globalThis.nethackGlobal.constants[scope] = globalThis.nethackGlobal.constants[scope] || {};
    globalThis.nethackGlobal.constants[scope][name] = num;
    globalThis.nethackGlobal.constants[scope][num] = name;
});
#define SET_CONSTANT_STRING(scope, name) set_const_str(scope, #name, name);
EM_JS(void, set_const_str, (char *scope_str, char *name_str, char *input_str), {
    let scope = UTF8ToString(scope_str);
    let name = UTF8ToString(name_str);
    let str = UTF8ToString(input_str);

    globalThis.nethackGlobal.constants[scope] = globalThis.nethackGlobal.constants[scope] || {};
    globalThis.nethackGlobal.constants[scope][name] = str;
});
#define SET_POINTER(name) set_const_ptr(#name, (void *)&name);
EM_JS(void, set_const_ptr, (char *name_str, void* ptr), {
    let name = UTF8ToString(name_str);

    globalThis.nethackGlobal.pointers = globalThis.nethackGlobal.pointers || {};
    globalThis.nethackGlobal.pointers[name] = ptr;
});

void js_constants_init() {
    EM_ASM({
        globalThis.nethackGlobal = globalThis.nethackGlobal || {};
        globalThis.nethackGlobal.constants = globalThis.nethackGlobal.constants || {};
        globalThis.nethackGlobal.pointers = globalThis.nethackGlobal.pointers || {};
    });

    // create_nhwindow
    SET_CONSTANT("WIN_TYPE", NHW_MESSAGE)
    SET_CONSTANT("WIN_TYPE", NHW_STATUS)
    SET_CONSTANT("WIN_TYPE", NHW_MAP)
    SET_CONSTANT("WIN_TYPE", NHW_MENU)
    SET_CONSTANT("WIN_TYPE", NHW_TEXT)

    // status_update
    SET_CONSTANT("STATUS_FIELD", BL_CHARACTERISTICS)
    SET_CONSTANT("STATUS_FIELD", BL_RESET)
    SET_CONSTANT("STATUS_FIELD", BL_FLUSH)
    SET_CONSTANT("STATUS_FIELD", BL_TITLE)
    SET_CONSTANT("STATUS_FIELD", BL_STR)
    SET_CONSTANT("STATUS_FIELD", BL_DX)
    SET_CONSTANT("STATUS_FIELD", BL_CO)
    SET_CONSTANT("STATUS_FIELD", BL_IN)
    SET_CONSTANT("STATUS_FIELD", BL_WI)
    SET_CONSTANT("STATUS_FIELD", BL_CH)
    SET_CONSTANT("STATUS_FIELD", BL_ALIGN)
    SET_CONSTANT("STATUS_FIELD", BL_SCORE)
    SET_CONSTANT("STATUS_FIELD", BL_CAP)
    SET_CONSTANT("STATUS_FIELD", BL_GOLD)
    SET_CONSTANT("STATUS_FIELD", BL_ENE)
    SET_CONSTANT("STATUS_FIELD", BL_ENEMAX)
    SET_CONSTANT("STATUS_FIELD", BL_XP)
    SET_CONSTANT("STATUS_FIELD", BL_AC)
    SET_CONSTANT("STATUS_FILED", BL_MC)
    SET_CONSTANT("STATUS_FIELD", BL_HD)
    SET_CONSTANT("STATUS_FIELD", BL_TIME)
    SET_CONSTANT("STATUS_FIELD", BL_HUNGER)
    SET_CONSTANT("STATUS_FIELD", BL_HP)
    SET_CONSTANT("STATUS_FIELD", BL_HPMAX)
    SET_CONSTANT("STATUS_FIELD", BL_LEVELDESC)
    SET_CONSTANT("STATUS_FIELD", BL_EXP)
    SET_CONSTANT("STATUS_FIELD", BL_CONDITION)
    SET_CONSTANT("STATUS_FIELD", BL_TOD)
    SET_CONSTANT("STATUS_FIELD", BL_BOOST)
    SET_CONSTANT("STATUS_FIELD", MAXBLSTATS)

    // text attributes
    SET_CONSTANT("ATTR", ATR_NONE);
    SET_CONSTANT("ATTR", ATR_BOLD);
    SET_CONSTANT("ATTR", ATR_DIM);
    SET_CONSTANT("ATTR", ATR_ULINE);
    SET_CONSTANT("ATTR", ATR_BLINK);
    SET_CONSTANT("ATTR", ATR_INVERSE);
    SET_CONSTANT("ATTR", ATR_URGENT);
    SET_CONSTANT("ATTR", ATR_NOHISTORY);

    // conditions
    SET_CONSTANT("CONDITION", BL_MASK_BAREH);
    SET_CONSTANT("CONDITION", BL_MASK_BLIND);
    SET_CONSTANT("CONDITION", BL_MASK_BUSY);
    SET_CONSTANT("CONDITION", BL_MASK_CONF);
    SET_CONSTANT("CONDITION", BL_MASK_DEAF);
    SET_CONSTANT("CONDITION", BL_MASK_ELF_IRON);
    SET_CONSTANT("CONDITION", BL_MASK_FLY);
    SET_CONSTANT("CONDITION", BL_MASK_FOODPOIS);
    SET_CONSTANT("CONDITION", BL_MASK_GLOWHANDS);
    SET_CONSTANT("CONDITION", BL_MASK_GRAB);
    SET_CONSTANT("CONDITION", BL_MASK_HALLU);
    SET_CONSTANT("CONDITION", BL_MASK_HELD);
    SET_CONSTANT("CONDITION", BL_MASK_ICY);
    SET_CONSTANT("CONDITION", BL_MASK_INLAVA);
    SET_CONSTANT("CONDITION", BL_MASK_LEV);
    SET_CONSTANT("CONDITION", BL_MASK_PARLYZ);
    SET_CONSTANT("CONDITION", BL_MASK_RIDE);
    SET_CONSTANT("CONDITION", BL_MASK_SLEEPING);
    SET_CONSTANT("CONDITION", BL_MASK_SLIME);
    SET_CONSTANT("CONDITION", BL_MASK_SLIPPERY);
    SET_CONSTANT("CONDITION", BL_MASK_STONE);
    SET_CONSTANT("CONDITION", BL_MASK_STRNGL);
    SET_CONSTANT("CONDITION", BL_MASK_STUN);
    SET_CONSTANT("CONDITION", BL_MASK_SUBMERGED);
    SET_CONSTANT("CONDITION", BL_MASK_TERMILL);
    SET_CONSTANT("CONDITION", BL_MASK_TETHERED);
    SET_CONSTANT("CONDITION", BL_MASK_TRAPPED);
    SET_CONSTANT("CONDITION", BL_MASK_UNCONSC);
    SET_CONSTANT("CONDITION", BL_MASK_WOUNDEDL);
    SET_CONSTANT("CONDITION", BL_MASK_HOLDING);
    SET_CONSTANT("CONDITION", BL_MASK_PRONE);

    // menu
    SET_CONSTANT("MENU_SELECT", PICK_NONE);
    SET_CONSTANT("MENU_SELECT", PICK_ONE);
    SET_CONSTANT("MENU_SELECT", PICK_ANY);

    // copyright
    SET_CONSTANT_STRING("COPYRIGHT", COPYRIGHT_BANNER_A);
    SET_CONSTANT_STRING("COPYRIGHT", COPYRIGHT_BANNER_B);
    set_const_str("COPYRIGHT", "COPYRIGHT_BANNER_C", (char*) COPYRIGHT_BANNER_C);
    SET_CONSTANT_STRING("COPYRIGHT", COPYRIGHT_BANNER_D);

    // glyphs
    SET_CONSTANT("GLYPH", GLYPH_MON_OFF);
    SET_CONSTANT("GLYPH", GLYPH_PET_OFF);
    SET_CONSTANT("GLYPH", GLYPH_BOOSTED_OFF);
    SET_CONSTANT("GLYPH", GLYPH_INVIS_OFF);
    SET_CONSTANT("GLYPH", GLYPH_DETECT_OFF);
    SET_CONSTANT("GLYPH", GLYPH_BODY_OFF);
    SET_CONSTANT("GLYPH", GLYPH_RIDDEN_OFF);
    SET_CONSTANT("GLYPH", GLYPH_OBJ_OFF);
    SET_CONSTANT("GLYPH", GLYPH_CMAP_OFF);
    SET_CONSTANT("GLYPH", GLYPH_EXPLODE_OFF);
    SET_CONSTANT("GLYPH", GLYPH_ZAP_OFF);
    SET_CONSTANT("GLYPH", GLYPH_SWALLOW_OFF);
    SET_CONSTANT("GLYPH", GLYPH_WARNING_OFF);
    SET_CONSTANT("GLYPH", GLYPH_STATUE_OFF);
    SET_CONSTANT("GLYPH", GLYPH_UNEXPLORED_OFF);
    SET_CONSTANT("GLYPH", GLYPH_NOTHING_OFF);
    SET_CONSTANT("GLYPH", MAX_GLYPH);
    SET_CONSTANT("GLYPH", NO_GLYPH);
    SET_CONSTANT("GLYPH", GLYPH_INVISIBLE);
    SET_CONSTANT("GLYPH", GLYPH_UNEXPLORED);
    SET_CONSTANT("GLYPH", GLYPH_NOTHING);

    // colors
    SET_CONSTANT("COLORS", CLR_BLACK);
    SET_CONSTANT("COLORS", CLR_RED);
    SET_CONSTANT("COLORS", CLR_GREEN);
    SET_CONSTANT("COLORS", CLR_BROWN);
    SET_CONSTANT("COLORS", CLR_BLUE);
    SET_CONSTANT("COLORS", CLR_MAGENTA);
    SET_CONSTANT("COLORS", CLR_CYAN);
    SET_CONSTANT("COLORS", CLR_GRAY);
    SET_CONSTANT("COLORS", NO_COLOR);
    SET_CONSTANT("COLORS", CLR_ORANGE);
    SET_CONSTANT("COLORS", CLR_BRIGHT_GREEN);
    SET_CONSTANT("COLORS", CLR_YELLOW);
    SET_CONSTANT("COLORS", CLR_BRIGHT_BLUE);
    SET_CONSTANT("COLORS", CLR_BRIGHT_MAGENTA);
    SET_CONSTANT("COLORS", CLR_BRIGHT_CYAN);
    SET_CONSTANT("COLORS", CLR_WHITE);
    SET_CONSTANT("COLORS", CLR_MAX);

    // color attributes (?)
    SET_CONSTANT("COLOR_ATTR", HL_ATTCLR_BOLD);
    SET_CONSTANT("COLOR_ATTR", HL_ATTCLR_DIM);
    SET_CONSTANT("COLOR_ATTR", HL_ATTCLR_ITALIC);
    SET_CONSTANT("COLOR_ATTR", HL_ATTCLR_ULINE);
    SET_CONSTANT("COLOR_ATTR", HL_ATTCLR_BLINK);
    SET_CONSTANT("COLOR_ATTR", HL_ATTCLR_INVERSE);
    SET_CONSTANT("COLOR_ATTR", BL_ATTCLR_MAX);

    SET_CONSTANT("BL_MASK", BL_MASK_BAREH);
    SET_CONSTANT("BL_MASK", BL_MASK_BLIND);
    SET_CONSTANT("BL_MASK", BL_MASK_BUSY);
    SET_CONSTANT("BL_MASK", BL_MASK_CONF);
    SET_CONSTANT("BL_MASK", BL_MASK_DEAF);
    SET_CONSTANT("BL_MASK", BL_MASK_ELF_IRON);
    SET_CONSTANT("BL_MASK", BL_MASK_FLY);
    SET_CONSTANT("BL_MASK", BL_MASK_FOODPOIS);
    SET_CONSTANT("BL_MASK", BL_MASK_GLOWHANDS);
    SET_CONSTANT("BL_MASK", BL_MASK_GRAB);
    SET_CONSTANT("BL_MASK", BL_MASK_HALLU);
    SET_CONSTANT("BL_MASK", BL_MASK_HELD);
    SET_CONSTANT("BL_MASK", BL_MASK_ICY);
    SET_CONSTANT("BL_MASK", BL_MASK_INLAVA);
    SET_CONSTANT("BL_MASK", BL_MASK_LEV);
    SET_CONSTANT("BL_MASK", BL_MASK_PARLYZ);
    SET_CONSTANT("BL_MASK", BL_MASK_RIDE);
    SET_CONSTANT("BL_MASK", BL_MASK_SLEEPING);
    SET_CONSTANT("BL_MASK", BL_MASK_SLIME);
    SET_CONSTANT("BL_MASK", BL_MASK_SLIPPERY);
    SET_CONSTANT("BL_MASK", BL_MASK_STONE);
    SET_CONSTANT("BL_MASK", BL_MASK_STRNGL);
    SET_CONSTANT("BL_MASK", BL_MASK_STUN);
    SET_CONSTANT("BL_MASK", BL_MASK_SUBMERGED);
    SET_CONSTANT("BL_MASK", BL_MASK_TERMILL);
    SET_CONSTANT("BL_MASK", BL_MASK_TETHERED);
    SET_CONSTANT("BL_MASK", BL_MASK_TRAPPED);
    SET_CONSTANT("BL_MASK", BL_MASK_UNCONSC);
    SET_CONSTANT("BL_MASK", BL_MASK_WOUNDEDL);
    SET_CONSTANT("BL_MASK", BL_MASK_HOLDING);

    SET_CONSTANT("ROLE_RACEMASK", MH_HUMAN);
    SET_CONSTANT("ROLE_RACEMASK", MH_ELF);
    SET_CONSTANT("ROLE_RACEMASK", MH_DWARF);
    SET_CONSTANT("ROLE_RACEMASK", MH_GNOME);
    SET_CONSTANT("ROLE_RACEMASK", MH_ORC);

    SET_CONSTANT("ROLE_GENDMASK", ROLE_MALE);
    SET_CONSTANT("ROLE_GENDMASK", ROLE_FEMALE);
    SET_CONSTANT("ROLE_GENDMASK", ROLE_NEUTER);

    SET_CONSTANT("ROLE_ALIGNMASK", ROLE_LAWFUL);
    SET_CONSTANT("ROLE_ALIGNMASK", ROLE_NEUTRAL);
    SET_CONSTANT("ROLE_ALIGNMASK", ROLE_CHAOTIC);

    SET_CONSTANT("blconditions", bl_bareh);
    SET_CONSTANT("blconditions", bl_blind);
    SET_CONSTANT("blconditions", bl_busy);
    SET_CONSTANT("blconditions", bl_conf);
    SET_CONSTANT("blconditions", bl_deaf);
    SET_CONSTANT("blconditions", bl_elf_iron);
    SET_CONSTANT("blconditions", bl_fly);
    SET_CONSTANT("blconditions", bl_foodpois);
    SET_CONSTANT("blconditions", bl_glowhands);
    SET_CONSTANT("blconditions", bl_grab);
    SET_CONSTANT("blconditions", bl_hallu);
    SET_CONSTANT("blconditions", bl_held);
    SET_CONSTANT("blconditions", bl_icy);
    SET_CONSTANT("blconditions", bl_inlava);
    SET_CONSTANT("blconditions", bl_lev);
    SET_CONSTANT("blconditions", bl_parlyz);
    SET_CONSTANT("blconditions", bl_ride);
    SET_CONSTANT("blconditions", bl_sleeping);
    SET_CONSTANT("blconditions", bl_slime);
    SET_CONSTANT("blconditions", bl_slippery);
    SET_CONSTANT("blconditions", bl_stone);
    SET_CONSTANT("blconditions", bl_strngl);
    SET_CONSTANT("blconditions", bl_stun);
    SET_CONSTANT("blconditions", bl_submerged);
    SET_CONSTANT("blconditions", bl_termill);
    SET_CONSTANT("blconditions", bl_tethered);
    SET_CONSTANT("blconditions", bl_trapped);
    SET_CONSTANT("blconditions", bl_unconsc);
    SET_CONSTANT("blconditions", bl_woundedl);
    SET_CONSTANT("blconditions", bl_holding);
    SET_CONSTANT("blconditions", CONDITION_COUNT);

    SET_CONSTANT("HL", HL_UNDEF);
    SET_CONSTANT("HL", HL_NONE);
    SET_CONSTANT("HL", HL_BOLD);
    SET_CONSTANT("HL", HL_DIM);
    SET_CONSTANT("HL", HL_ITALIC);
    SET_CONSTANT("HL", HL_ULINE);
    SET_CONSTANT("HL", HL_BLINK);
    SET_CONSTANT("HL", HL_INVERSE);

    SET_CONSTANT("MG", MG_HERO);
    SET_CONSTANT("MG", MG_CORPSE);
    SET_CONSTANT("MG", MG_INVIS);
    SET_CONSTANT("MG", MG_DETECT);
    SET_CONSTANT("MG", MG_PET);
    SET_CONSTANT("MG", MG_BOOST);
    SET_CONSTANT("MG", MG_RIDDEN);
    SET_CONSTANT("MG", MG_STATUE);
    SET_CONSTANT("MG", MG_OBJPILE);
    SET_CONSTANT("MG", MG_BW_LAVA);
    SET_CONSTANT("MG", MG_BW_ICE);
    SET_CONSTANT("MG", MG_BW_SINK);
    SET_CONSTANT("MG", MG_BW_ENGR);
    SET_CONSTANT("MG", MG_NOTHING);
    SET_CONSTANT("MG", MG_UNEXPL);
    SET_CONSTANT("MG", MG_MALE);
    SET_CONSTANT("MG", MG_FEMALE);

    SET_POINTER(extcmdlist);
    SET_POINTER(conditions);
    SET_POINTER(condtests);

    /* roles/races/genders/alignments */
    SET_POINTER(roles);
    SET_POINTER(races);
    SET_POINTER(genders);
    SET_POINTER(aligns);
}

/***
 * Globals
 ***/
#define CREATE_GLOBAL(var, type) create_global(#var, (void *)&var, type);
#define CREATE_GLOBAL_FROM_ARRAY(base, iter, path, end_expr, type) \
    for(iter = 0; end_expr; iter++) { \
        snprintf(buf, BUFSZ, #base ".%d." #path, iter); \
        create_global(buf, (void *)(&(base[iter].path)), type); \
    }

void create_global (char *name, void *ptr, char *type);

void js_globals_init() {
    // int i;
    // char buf[BUFSZ];

    EM_ASM({
        globalThis.nethackGlobal = globalThis.nethackGlobal || {};
        globalThis.nethackGlobal.globals = globalThis.nethackGlobal.globals || {};
    });

    /* globals */
    CREATE_GLOBAL(svp.plname, "s");

    /* window globals */
    CREATE_GLOBAL(WIN_MAP, "i");
    CREATE_GLOBAL(WIN_MESSAGE, "i");
    CREATE_GLOBAL(WIN_INVEN, "i");
    CREATE_GLOBAL(WIN_STATUS, "i");

    /* instance flags */
    CREATE_GLOBAL(iflags.window_inited, "b");
    CREATE_GLOBAL(iflags.wc2_hitpointbar, "b");
    CREATE_GLOBAL(iflags.wc_hilite_pet, "b");
    CREATE_GLOBAL(iflags.hilite_pile, "b");

    /* flags */
    CREATE_GLOBAL(flags.initrole, "i");
    CREATE_GLOBAL(flags.initrace, "i");
    CREATE_GLOBAL(flags.initgend, "i");
    CREATE_GLOBAL(flags.initalign, "i");
    CREATE_GLOBAL(flags.showexp, "b");
    CREATE_GLOBAL(flags.time, "b");
}

EM_JS(void, create_global, (char *name_str, void *ptr, char *type_str), {
    let name = UTF8ToString(name_str);
    let type = UTF8ToString(type_str);

    // get helpers
    let getPointerValue = globalThis.nethackGlobal.helpers.getPointerValue;
    let setPointerValue = globalThis.nethackGlobal.helpers.setPointerValue;

    let { obj, prop } = createPath(globalThis.nethackGlobal.globals, name);

    // setters / getters with bound pointers
    Object.defineProperty(obj, prop, {
        get: getPointerValue.bind(null, name, ptr, type),
        set: setPointerValue.bind(null, name, ptr, type),
        configurable: true,
        enumerable: true
    });

    function createPath(obj, path) {
        path = path.split(".");
        let i;
        for (i = 0; i < path.length - 1; i++) {
            // obj[path[i]] = obj[path[i]] || {};
            if (obj[path[i]] === undefined) {
                obj[path[i]] = {};
            }
            obj = obj[path[i]];
        }

        return { obj, prop: path[i] };
    }
})

#endif

/*libnhmain.c*/
