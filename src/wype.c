/*
 *  wype.c:  Darik's Wipe.
 *
 *  Copyright Darik Horn <dajhorn-dban@vanadac.com>.
 *
 *  Modifications to original dwipe Copyright Andy Beverley <andy@andybev.com>
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 2.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

/* Enable GNU extensions so that O_DIRECT is visible from <fcntl.h>. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>

#include "wype.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "device.h"
#include "logging.h"
#include "gui.h"
#include "temperature.h"
#include "miscellaneous.h"

#include <sys/ioctl.h> /* FIXME: Twice Included */
#include <sys/shm.h>
#include <wait.h>

#include <parted/parted.h>
#include <parted/debug.h>
#include "conf.h"
#include "version.h"
#include "hpa_dco.h"
#include "conf.h"
#include <libconfig.h>
#include <fcntl.h> /* O_DIRECT, O_RDWR, ... */

#ifdef WYPE_USE_DIRECT_IO
#ifndef O_DIRECT
/*
 * Some platforms or libcs do not define O_DIRECT at all. Defining it
 * as 0 makes the flag a no-op and keeps the code buildable.
 * On Linux/glibc, <fcntl.h> via wype.h will provide a real O_DIRECT.
 */
#define O_DIRECT 0
#endif
#endif

int terminate_signal;
int user_abort;
int global_wipe_status;

/* helper function for sorting */
int devnamecmp( const void* a, const void* b )
{
    // wype_log( WYPE_LOG_DEBUG, "a: %s, b: %s", ( *( wype_context_t** ) a)->device_name, ( *( wype_context_t** )
    // b)->device_name );

    int ldiff = strlen( ( *(wype_context_t**) a )->device_name ) - strlen( ( *(wype_context_t**) b )->device_name );
    if( ldiff != 0 )
    {
        return ldiff;
    }
    int ret = strcmp( ( *(wype_context_t**) a )->device_name, ( *(wype_context_t**) b )->device_name );
    return ( ret );
}

static int wype_prng_bench_cmp_desc( const void* a, const void* b )
{
    const wype_prng_bench_result_t* A = (const wype_prng_bench_result_t*) a;
    const wype_prng_bench_result_t* B = (const wype_prng_bench_result_t*) b;

    /* successful results first */
    if( A->rc != 0 && B->rc == 0 )
        return 1;
    if( A->rc == 0 && B->rc != 0 )
        return -1;

    /* then sort by MB/s */
    if( A->mbps < B->mbps )
        return 1;
    if( A->mbps > B->mbps )
        return -1;
    return 0;
}

#define WYPE_PDF_DIR_MODE ( S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH )
/* -> 0755: rwx for owner, r-x for group and others */

/* Helper: try to create and remove a temporary file inside the directory.
 * This catches cases where access(path, W_OK) passes (especially as root)
 * but the underlying filesystem does not allow creating regular files,
 * e.g. /proc or other pseudo/readonly filesystems.
 */
static int wype_probe_directory_writable( const char* path )
{
    const char* suffix = "/.wype_pdf_testXXXXXX";
    size_t path_len = strlen( path );
    size_t suffix_len = strlen( suffix );
    size_t total_len = path_len + suffix_len + 1; /* +1 for '\0' */

    char* tmpl = (char*) malloc( total_len );
    if( tmpl == NULL )
    {
        wype_log( WYPE_LOG_ERROR, "Failed to allocate memory to probe PDFreportpath '%s'.", path );
        return -1;
    }

    /* Build template "<path>/.wype_pdf_testXXXXXX" */
    snprintf( tmpl, total_len, "%s%s", path, suffix );

    int fd = mkstemp( tmpl );
    if( fd < 0 )
    {
        wype_log( WYPE_LOG_ERROR,
                   "PDFreportpath '%s' is not writable (cannot create test file): %s.",
                   path,
                   strerror( errno ) );
        free( tmpl );
        return -1;
    }

    /* Successfully created a temporary file, now clean it up. */
    close( fd );
    if( unlink( tmpl ) != 0 )
    {
        /* Not fatal for our check, but log it anyway. */
        wype_log( WYPE_LOG_WARNING,
                   "Failed to remove temporary test file '%s' in PDFreportpath '%s': %s.",
                   tmpl,
                   path,
                   strerror( errno ) );
    }

    free( tmpl );
    return 0;
}

static int wype_ensure_directory( const char* path )
{
    struct stat st;
    char* tmp;
    char* p;
    size_t len;

    if( path == NULL || path[0] == '\0' )
    {
        /* Empty path: nothing to do, treat as success. */
        return 0;
    }

    /* 1. First try: does the path already exist? */
    if( stat( path, &st ) == 0 )
    {
        /* Path exists; make sure it's a directory. */
        if( !S_ISDIR( st.st_mode ) )
        {
            wype_log( WYPE_LOG_ERROR, "PDFreportpath '%s' exists but is not a directory.", path );
            return -1;
        }

        /* Even if access() says it's writable (especially as root),
         * we still probe by actually creating a test file. */
        if( wype_probe_directory_writable( path ) != 0 )
        {
            /* Detailed error already logged. */
            return -1;
        }

        /* Everything is fine, directory already present and writable. */
        return 0;
    }

    /* stat() failed: if this is not "does not exist", propagate the error. */
    if( errno != ENOENT )
    {
        wype_log( WYPE_LOG_ERROR, "Failed to stat PDFreportpath '%s': %s.", path, strerror( errno ) );
        return -1;
    }

    /* 2. Directory does not exist -> create it recursively (mkdir -p style). */

    len = strlen( path );
    tmp = (char*) malloc( len + 1 );
    if( tmp == NULL )
    {
        wype_log( WYPE_LOG_ERROR, "Failed to allocate memory to create PDFreportpath '%s'.", path );
        return -1;
    }

    memcpy( tmp, path, len + 1 );

    /* Start at the beginning of the string.
     * For absolute paths ("/foo/bar") we skip the leading slash so we do not
     * try to create "/" itself. */
    p = tmp;
    if( tmp[0] == '/' )
    {
        p = tmp + 1;
    }

    for( ; *p; ++p )
    {
        if( *p == '/' )
        {
            *p = '\0';

            /* Skip empty components (can happen with leading '/' or double '//'). */
            if( tmp[0] != '\0' )
            {
                if( mkdir( tmp, WYPE_PDF_DIR_MODE ) != 0 && errno != EEXIST )
                {
                    wype_log( WYPE_LOG_ERROR,
                               "Failed to create directory '%s' for PDFreportpath '%s': %s.",
                               tmp,
                               path,
                               strerror( errno ) );
                    free( tmp );
                    return -1;
                }
            }

            *p = '/';
        }
    }

    /* Create the final directory component (the full path). */
    if( mkdir( tmp, WYPE_PDF_DIR_MODE ) != 0 && errno != EEXIST )
    {
        wype_log( WYPE_LOG_ERROR,
                   "Failed to create directory '%s' for PDFreportpath '%s': %s.",
                   tmp,
                   path,
                   strerror( errno ) );
        free( tmp );
        return -1;
    }

    free( tmp );

    /* 3. Final sanity check: ensure the path is writable by probing with a file. */
    if( wype_probe_directory_writable( path ) != 0 )
    {
        /* Detailed error already logged. */
        return -1;
    }

    return 0;
}

int main( int argc, char** argv )
{
    int wype_optind;  // The result of wype_options().
    int wype_enumerated;  // The number of contexts that have been enumerated.
    int wype_error = 0;  // An error counter.
    int wype_selected = 0;  // The number of contexts that have been selected.
    int any_threads_still_running;  // used in wipe thread cancellation wait loop
    int thread_timeout_counter;  // timeout thread cancellation after THREAD_CANCELLATION_TIMEOUT seconds
    pthread_t wype_gui_thread = 0;  // The thread ID of the GUI thread.
    pthread_t wype_temperature_thread = 0;  // The thread ID of the temperature update thread
    pthread_t wype_sigint_thread;  // The thread ID of the sigint handler.

    char modprobe_command[] = "modprobe %s";
    char modprobe_command2[] = "/sbin/modprobe %s";
    char modprobe_command3[] = "/usr/sbin/modprobe %s";
    char module_shortform[50];
    char final_cmd_modprobe[sizeof( modprobe_command ) + sizeof( module_shortform )];

    /* The generic index variables. */
    int i;
    int j;

    /* The generic result buffer. */
    int r;

    /* Initialise the termintaion signal, 1=terminate wype */
    terminate_signal = 0;

    /* Initialise the user abort signal, 1=User aborted with CNTRL-C,SIGTERM, SIGQUIT, SIGINT etc.. */
    user_abort = 0;

    /* wypes return status value, set prior to exit at the end of wype, as no other exit points allowed */
    int return_status = 0;

    /* Initialise, flag indicating whether a wipe has actually started or not 0=no, 1=yes */
    global_wipe_status = 0;

    /* Initialise flags that indicate whether a fatal or non fatal error occurred on ANY drive */
    int fatal_errors_flag = 0;
    int non_fatal_errors_flag = 0;

    /* Two arrays are used, containing pointers to the the typedef for each disk */
    /* The first array (c1) points to all devices, the second points to only     */
    /* the disks selected for wiping.                                            */

    /* The array of pointers to enumerated contexts. */
    /* Initialised and populated in device scan.     */
    wype_context_t** c1 = 0;

    if( geteuid() != 0 )
    {
        printf( "wype must run with root permissions, which is not the case.\nAborting\n" );
        exit( 99 );
    }

    int wipe_threads_started = 0;

    /** NOTE ** NOTE ** NOTE ** NOTE ** NOTE ** NOTE ** NOTE ** NOTE ** NOTE **
     * Important Note: if you want wype_log messages to go into the logfile
     * any 'wype_log()' commands must appear after the options are parsed here,
     * else they will appear in the console but not in the logfile, that is,
     * assuming you specified a log file on the command line as an wype option.
     */

    /*****************************
     * Parse command line options.
     */

    /* Initialise the libconfig code that handles wype.conf */
    wype_conf_init();

    wype_optind = wype_options_parse( argc, argv );

    /* Log wypes version */
    wype_log( WYPE_LOG_INFO, "%s", banner );

    /* Log OS info */
    wype_log_OSinfo();

    /* ------------------------------------------------------------
     * PRNG benchmark / auto-select (runs before device scan)
     * ------------------------------------------------------------ */
    if( wype_options.prng_benchmark_only || wype_options.prng_auto )
    {
        /* tune defaults */
        const size_t io_block = 4 * 1024 * 1024; /* 4 MiB RAM buffer blocks */
        double seconds = wype_options.prng_bench_seconds;

        /* If user requested auto-select and didn't override seconds, keep it short */
        if( wype_options.prng_auto )
        {
            if( seconds <= 0.0 )
                seconds = 0.25;
            /* Optional: if you consider "1.0" to be the default and want auto shorter:
             * if( seconds == 1.0 ) seconds = 0.25;
             */
        }
        else
        {
            if( seconds <= 0.0 )
                seconds = 1.0;
        }

        wype_prng_bench_result_t results[16];
        memset( results, 0, sizeof( results ) );

        /* ------------------------------------------------------------
         * --prng-benchmark-only path
         * (keep output clean: no live "Testing..." lines by default)
         * ------------------------------------------------------------ */
        if( wype_options.prng_benchmark_only )
        {
            const int live_print = 0; /* set to 1 if you also want live here */

            int n = wype_prng_benchmark_all_live(
                seconds, io_block, results, (int) ( sizeof( results ) / sizeof( results[0] ) ), live_print );

            if( n <= 0 )
            {
                wype_log( WYPE_LOG_ERROR, "PRNG benchmark failed (no results)." );
                printf( "PRNG benchmark failed (no results).\n" );
                cleanup();
                exit( 3 );
            }

            qsort( results, (size_t) n, sizeof( results[0] ), wype_prng_bench_cmp_desc );

            /* Print to console + log */
            printf( "\nPRNG Benchmark (RAM-only) ~%.2fs each, block=%zu MiB\n", seconds, io_block / ( 1024 * 1024 ) );
            printf( "---------------------------------------------------\n" );

            wype_log( WYPE_LOG_INFO,
                       "PRNG Benchmark (RAM-only) ~%.2fs each, block=%zu MiB",
                       seconds,
                       io_block / ( 1024 * 1024 ) );

            for( int i = 0; i < n; i++ )
            {
                if( results[i].rc == 0 )
                {
                    printf( "%2d) %-40s %10.1f MB/s\n", i + 1, results[i].prng->label, results[i].mbps );
                    wype_log( WYPE_LOG_INFO,
                               "PRNG bench %2d) %-40s %10.1f MB/s",
                               i + 1,
                               results[i].prng->label,
                               results[i].mbps );
                }
                else
                {
                    printf( "%2d) %-40s (failed: rc=%d)\n", i + 1, results[i].prng->label, results[i].rc );
                    wype_log( WYPE_LOG_WARNING,
                               "PRNG bench %2d) %-40s (failed: rc=%d)",
                               i + 1,
                               results[i].prng->label,
                               results[i].rc );
                }
            }

            printf( "\n" );
            cleanup();
            exit( 0 );
        }

        /* ------------------------------------------------------------
         * --prng=auto path
         * (THIS is the path where live output matters for “GUI delay”)
         * ------------------------------------------------------------ */
        if( wype_options.prng_auto )
        {
            /* live_print=1: prints:
             *  - "Analysing PRNG performance:" immediately (with spinner)
             *  - "Testing <PRNG> performance..." per PRNG
             *  - "<PRNG> -> xx.x MB/s" immediately after each PRNG
             */
            const int live_print = 1;

            /* Option A (preferred): make select_fastest call the live benchmark internally.
             *   best = wype_prng_select_fastest(seconds, io_block, results, count);
             * and inside select_fastest use wype_prng_benchmark_all_live(..., 1)
             *
             * Option B: benchmark here (live), then choose best locally.
             * Since you already have wype_prng_select_fastest(), stick with Option A.
             */

            const wype_prng_t* best = wype_prng_select_fastest(
                seconds, io_block, results, (int) ( sizeof( results ) / sizeof( results[0] ) ) /* results_count */
                /* ensure select_fastest uses wype_prng_benchmark_all_live(..., live_print) */
            );

            if( best != NULL )
            {
                /* Apply selection */
                wype_options.prng = (wype_prng_t*) best;

                wype_log( WYPE_LOG_INFO, "Auto-selected fastest PRNG: %s", best->label );
                printf( "Auto-selected fastest PRNG: %s\n", best->label );
            }
            else
            {
                wype_log( WYPE_LOG_WARNING,
                           "Auto PRNG selection: no working PRNG found, keeping configured default." );
                printf( "Auto PRNG selection: no working PRNG found, keeping configured default.\n" );
            }
        }
    }

    /* Check that hdparm exists, we use hdparm for some HPA/DCO detection etc, if not
     * exit wype. These checks are required if the PATH environment is not setup !
     * Example: Debian sid 'su' as opposed to 'su -'
     */
    if( system( "which hdparm > /dev/null 2>&1" ) )
    {
        if( system( "which /sbin/hdparm > /dev/null 2>&1" ) )
        {
            if( system( "which /usr/bin/hdparm > /dev/null 2>&1" ) )
            {
                if( system( "which /usr/sbin/hdparm > /dev/null 2>&1" ) )
                {
                    wype_log( WYPE_LOG_WARNING, "hdparm command not found." );
                    wype_log( WYPE_LOG_WARNING,
                               "Required by wype for HPA/DCO detection & correction and ATA secure erase." );
                    wype_log( WYPE_LOG_WARNING, "** Please install hdparm **\n" );
                    cleanup();
                    exit( 1 );
                }
            }
        }
    }

    /* Check if the given path for PDF reports is a writeable directory.
     * If it does not exist, try to create it (mkdir -p style).
     */
    if( strcmp( wype_options.PDFreportpath, "noPDF" ) != 0 )
    {
        if( wype_ensure_directory( wype_options.PDFreportpath ) != 0 )
        {
            /* wype_ensure_directory already logged a detailed error message. */
            cleanup();
            exit( 2 );
        }
    }

    if( wype_optind == argc )
    {
        /* File names were not given by the user.  Scan for devices. */
        wype_enumerated = wype_device_scan( &c1 );

        if( terminate_signal == 1 )
        {
            cleanup();
            exit( 1 );
        }

        if( wype_enumerated == 0 )
        {
            wype_log( WYPE_LOG_INFO,
                       "Storage devices not found. Wype should be run as root or sudo/su, i.e sudo wype etc" );
            cleanup();
            return -1;
        }
        else
        {
            wype_log( WYPE_LOG_INFO, "Automatically enumerated %i devices.", wype_enumerated );
        }
    }
    else
    {
        argv += wype_optind;
        argc -= wype_optind;

        wype_enumerated = wype_device_get( &c1, argv, argc );
        if( wype_enumerated == 0 )
        {
            wype_log( WYPE_LOG_ERROR, "Devices not found. Check you're not excluding drives unnecessarily," );
            wype_log( WYPE_LOG_ERROR, "and you are running wype as sudo or as root." );
            printf( "Devices not found, check you're not excluding drives unnecessarily \n and you are running wype "
                    "as sudo or as root." );
            cleanup();
            exit( 1 );
        }
    }

    /* sort list of devices here */
    qsort( (void*) c1, (size_t) wype_enumerated, sizeof( wype_context_t* ), devnamecmp );

    if( terminate_signal == 1 )
    {
        cleanup();
        exit( 1 );
    }

    /* Log the System information */
    wype_log_sysinfo();

    /* The array of pointers to contexts that will actually be wiped. */
    wype_context_t** c2 = (wype_context_t**) malloc( wype_enumerated * sizeof( wype_context_t* ) );
    if( c2 == NULL )
    {
        wype_log( WYPE_LOG_ERROR, "memory allocation for c2 failed" );
        cleanup();
        exit( 1 );
    }

    /* Block relevant signals in main thread. Any other threads that are     */
    /*        created after this will also block those signals.              */
    sigset_t sigset;
    sigemptyset( &sigset );
    sigaddset( &sigset, SIGHUP );
    sigaddset( &sigset, SIGTERM );
    sigaddset( &sigset, SIGQUIT );
    sigaddset( &sigset, SIGINT );
    sigaddset( &sigset, SIGUSR1 );
    pthread_sigmask( SIG_SETMASK, &sigset, NULL );

    /* Create a signal handler thread.  This thread will catch all           */
    /*      signals and decide what to do with them.  This will only         */
    /*      catch nondirected signals.  (I.e., if a thread causes a SIGFPE   */
    /*      then that thread will get that signal.                           */

    /* Pass a pointer to a struct containing all data to the signal handler. */
    wype_misc_thread_data_t wype_misc_thread_data;
    wype_thread_data_ptr_t wype_thread_data_ptr;

    wype_thread_data_ptr.c = c2;
    wype_misc_thread_data.wype_enumerated = wype_enumerated;
    wype_misc_thread_data.wype_selected = 0;
    if( !wype_options.nogui )
        wype_misc_thread_data.gui_thread = &wype_gui_thread;
    wype_thread_data_ptr.wype_misc_thread_data = &wype_misc_thread_data;

    if( !wype_options.nosignals )
    {
        pthread_attr_t pthread_attr;
        pthread_attr_init( &pthread_attr );
        pthread_attr_setdetachstate( &pthread_attr, PTHREAD_CREATE_DETACHED );

        pthread_create( &wype_sigint_thread, &pthread_attr, signal_hand, &wype_thread_data_ptr );
    }

    /* Makesure the drivetemp module is loaded, else drives hwmon entries won't appear in /sys/class/hwmon */
    final_cmd_modprobe[0] = 0;

    /* The kernel module we are going to load */
    strcpy( module_shortform, "drivetemp" );

    /* Determine whether we can access modprobe, required if the PATH environment is not setup ! (Debian sid 'su' as
     * opposed to 'su -' */

    if( system( "which modprobe > /dev/null 2>&1" ) )
    {
        if( system( "which /sbin/modprobe > /dev/null 2>&1" ) )
        {
            if( system( "which /usr/sbin/modprobe > /dev/null 2>&1" ) )
            {
                wype_log( WYPE_LOG_WARNING, "modprobe command not found. Install kmod package (modprobe)) !" );
                wype_log( WYPE_LOG_WARNING, "Most temperature monitoring may be unavailable as module drivetemp" );
                wype_log( WYPE_LOG_WARNING, "could not be loaded. drivetemp is not available on kernels < v5.5" );
            }
            else
            {
                sprintf( final_cmd_modprobe, modprobe_command3, module_shortform );
            }
        }
        else
        {
            sprintf( final_cmd_modprobe, modprobe_command2, module_shortform );
        }
    }
    else
    {
        sprintf( final_cmd_modprobe, modprobe_command, module_shortform );
    }

    /* load the drivetemp module */
    if( system( final_cmd_modprobe ) != 0 )
    {
        wype_log( WYPE_LOG_WARNING, "hwmon: Unable to load module drivetemp, temperatures may be unavailable." );
        wype_log( WYPE_LOG_WARNING, "hwmon: It's possible the drivetemp software isn't modular but built-in" );
        wype_log( WYPE_LOG_WARNING, "hwmon: to the kernel, as is the case with wypeOS in which case" );
        wype_log( WYPE_LOG_WARNING, "hwmon: the temperatures will actually be available despite this issue." );
    }
    else
    {
        wype_log( WYPE_LOG_NOTICE, "hwmon: Module drivetemp loaded, drive temperatures available" );
    }

    /* A context struct for each device has already been created. */
    /* Now set specific wype options */
    for( i = 0; i < wype_enumerated; i++ )
    {
        if( c1[i]->device_busy && !wype_options.force )
        {
            /* Do not allow to wipe in-use devices if --force is not set. */
            c1[i]->select = WYPE_SELECT_DISABLED_BUSY;
        }
        else if( wype_options.autonuke == 1 )
        {
            /* When the autonuke option is set, select all disks. */
            // TODO - partitions
            // if( c1[i].device_part == 0 ) { c1[i].select = WYPE_SELECT_TRUE;        }
            // else                         { c1[i].select = WYPE_SELECT_TRUE_PARENT; }
            c1[i]->select = WYPE_SELECT_TRUE;
        }
        else
        {
            /* The user must manually select devices. */
            c1[i]->select = WYPE_SELECT_FALSE;
        }

        /* Initialise temperature variables for device */
        wype_init_temperature( c1[i] );
        if( wype_options.verbose )
        {
            wype_log( WYPE_LOG_NOTICE, "hwmon: Device %s hwmon path = %s", c1[i]->device_name, c1[i]->temp1_path );
        }

        // wype_update_temperature( c1[i] );

        /* Log the temperature crtical, highest, lowest and lowest critical temperature
         * limits to wypes log file using the INFO catagory
         */

        wype_log_drives_temperature_limits( c1[i] );
    }

    /* Check for initialization errors. */
    if( wype_error )
    {
        wype_log( WYPE_LOG_ERROR, "Initialization error %i\n", wype_error );
        cleanup();
        return -1;
    }

    /* Set up the data structures to pass the temperature thread the data it needs */
    wype_thread_data_ptr_t wype_temperature_thread_data;
    wype_temperature_thread_data.c = c1;
    wype_temperature_thread_data.wype_misc_thread_data = &wype_misc_thread_data;

    /* Fork the temperature thread */
    errno = pthread_create(
        &wype_temperature_thread, NULL, wype_update_temperature_thread, &wype_temperature_thread_data );

    /* Start the ncurses interface. */
    switch( wype_options.nogui )
    {
        case 0:
            wype_gui_init();
            break;

        case 1:
            break;

        default:
            printf( "system error: wype_options.nogui (should be 0 or 1) is invalid, wype_options.nogui=%i !? \n",
                    wype_options.nogui );
    }

    switch( wype_options.autonuke )
    {
        case 0:
            /* The user can't specify the nogui option without also using the autonuke option */
            if( wype_options.nogui == 1 )
            {
                printf( "--nogui option must be used with autonuke option\n" );
                cleanup();
                exit( 1 );
            }
            else
            {
                /* Show startup overview with org/customer info and customer selection (if preview enabled) */
                if( wype_options.PDF_preview_details )
                {
                    wype_gui_startup_info();
                }

                /* Get device selections from the user. */
                wype_gui_select( &wype_enumerated, &c1 );
            }
            break;

        case 1:
            /* Print the options window. */
            if( !wype_options.nogui )
                wype_gui_options();

            break;

        default:
            printf(
                "system error: wype_options.autonuke (should be 0 or 1) is invalid, wype_options.autonuke=%i !? \n",
                wype_options.autonuke );
    }

    /* Rescan may have added devices — reallocate c2 to match current count */
    c2 = (wype_context_t**) realloc( c2, wype_enumerated * sizeof( wype_context_t* ) );
    if( c2 == NULL )
    {
        wype_log( WYPE_LOG_ERROR, "memory reallocation for c2 failed" );
        cleanup();
        exit( 1 );
    }
    wype_misc_thread_data.wype_enumerated = wype_enumerated;

    /* Initialise some of the variables in the drive contexts
     */
    for( i = 0; i < wype_enumerated; i++ )
    {
        /* Set the PRNG implementation, which must always come after the function wype_gui_select ! */
        c1[i]->prng = wype_options.prng;
        c1[i]->prng_seed.length = 0;
        c1[i]->prng_seed.s = 0;
        c1[i]->prng_state = 0;

        /* Count the number of selected contexts. */
        if( c1[i]->select == WYPE_SELECT_TRUE )
        {
            wype_selected += 1;
        }

        /* Initialise the wipe result value */
        c1[i]->result = 0;

        /* Initialise the variable that tracks how much of the drive has been erased */
        c1[i]->bytes_erased = 0;
    }

    /* Pass the number selected to the struct for other threads */
    wype_misc_thread_data.wype_selected = wype_selected;

    /* Populate the array of selected contexts. */
    for( i = 0, j = 0; i < wype_enumerated; i++ )
    {
        if( c1[i]->select == WYPE_SELECT_TRUE )
        {
            /* Copy the context. */
            c2[j++] = c1[i];
        }
    }

    /* TODO: free c1 and c2 memory. */
    if( user_abort == 0 )
    {
        /* Log the wipe options that have been selected immediately prior to the start of the wipe */
        wype_options_log();

        /* The wipe has been initiated */
        global_wipe_status = 1;

        for( i = 0; i < wype_selected; i++ )
        {
            /* A result buffer for the BLKGETSIZE64 ioctl. */
            u64 size64;

            /* Should be filtered out earlier, but keep it as a last-minute seatbelt. */
            if( c2[i]->device_busy && !wype_options.force )
            {
                wype_log( WYPE_LOG_FATAL,
                           "Device '%s' is IN USE but --force is not set, not wiping it.",
                           c2[i]->device_name );
                c2[i]->select = WYPE_SELECT_DISABLED_BUSY;
                continue;
            }

            /* Initialise the spinner character index */
            c2[i]->spinner_idx = 0;

            /* Initialise the start and end time of the wipe */
            c2[i]->start_time = 0;
            c2[i]->end_time = 0;

            /* Initialise the wipe_status flag, -1 = wipe not yet started */
            c2[i]->wipe_status = -1;

            /* Initialise the I/O direction */
            c2[i]->io_direction = wype_options.io_direction;

            /* Open the file for reads and writes, honoring the configured I/O mode. */
            int open_flags = O_RDWR;

#ifdef WYPE_USE_DIRECT_IO
            /*
             * Decide whether to request O_DIRECT based on the runtime I/O mode:
             *   auto   -> try O_DIRECT, fall back to cached I/O if needed
             *   direct -> force O_DIRECT, fail hard if not supported
             *   cached -> do not request O_DIRECT at all
             */
            if( wype_options.io_mode == WYPE_IO_MODE_DIRECT || wype_options.io_mode == WYPE_IO_MODE_AUTO )
            {
                open_flags |= O_DIRECT;
            }
#endif

            c2[i]->device_fd = open( c2[i]->device_name, open_flags );

#ifdef WYPE_USE_DIRECT_IO
            if( c2[i]->device_fd < 0 && ( errno == EINVAL || errno == EOPNOTSUPP ) )
            {
                if( wype_options.io_mode == WYPE_IO_MODE_DIRECT )
                {
                    /*
                     * User explicitly requested direct I/O: do not silently
                     * fall back. Mark the device as unusable and continue.
                     */
                    wype_perror( errno, __FUNCTION__, "open" );
                    wype_log( WYPE_LOG_FATAL,
                               "O_DIRECT requested via --directio but not supported on '%s'.",
                               c2[i]->device_name );
                    c2[i]->select = WYPE_SELECT_DISABLED;
                    continue;
                }
                else if( wype_options.io_mode == WYPE_IO_MODE_AUTO )
                {
                    /*
                     * Auto mode: transparently fall back to cached I/O and
                     * log a warning.
                     */
                    wype_log( WYPE_LOG_WARNING,
                               "O_DIRECT not supported on '%s', falling back to cached I/O.",
                               c2[i]->device_name );

                    open_flags &= ~O_DIRECT;
                    c2[i]->device_fd = open( c2[i]->device_name, open_flags );
                }
            }

            if( c2[i]->device_fd >= 0 )
            {
                const char* io_desc;

                if( open_flags & O_DIRECT )
                {
                    io_desc = "direct I/O (O_DIRECT)";
                    c2[i]->io_mode = WYPE_IO_MODE_DIRECT;
                }
                else
                {
                    io_desc = "cached I/O";
                    c2[i]->io_mode = WYPE_IO_MODE_CACHED;
                }

                wype_log( WYPE_LOG_NOTICE, "Using %s on device '%s'.", io_desc, c2[i]->device_name );
            }
#endif /* WYPE_USE_DIRECT_IO */

            /* Check the open() result (after any fallback logic). */
            if( c2[i]->device_fd < 0 )
            {
                wype_perror( errno, __FUNCTION__, "open" );
                wype_log( WYPE_LOG_WARNING, "Unable to open device '%s'.", c2[i]->device_name );
                c2[i]->select = WYPE_SELECT_DISABLED;
                continue;
            }

            /* Check the open() result. */
            if( c2[i]->device_fd < 0 )
            {
                wype_perror( errno, __FUNCTION__, "open" );
                wype_log( WYPE_LOG_WARNING, "Unable to open device '%s'.", c2[i]->device_name );
                c2[i]->select = WYPE_SELECT_DISABLED;
                continue;
            }

            /* Stat the file. */
            if( fstat( c2[i]->device_fd, &c2[i]->device_stat ) != 0 )
            {
                wype_perror( errno, __FUNCTION__, "fstat" );
                wype_log( WYPE_LOG_ERROR, "Unable to stat file '%s'.", c2[i]->device_name );
                wype_error++;
                continue;
            }

            /* Check that the file is a block device. */
            if( !S_ISBLK( c2[i]->device_stat.st_mode ) )
            {
                wype_log( WYPE_LOG_ERROR, "'%s' is not a block device.", c2[i]->device_name );
                wype_error++;
                continue;
            }

            /* TODO: Lock the file for exclusive access. */
            /*
            if( flock( c2[i]->device_fd, LOCK_EX | LOCK_NB ) != 0 )
            {
                    wype_perror( errno, __FUNCTION__, "flock" );
                    wype_log( WYPE_LOG_ERROR, "Unable to lock the '%s' file.", c2[i]->device_name );
                    wype_error++;
                    continue;
            }
            */

            /* Print serial number of device if it exists. */
            if( strlen( (const char*) c2[i]->device_serial_no ) )
            {
                wype_log( WYPE_LOG_NOTICE, "%s has serial number %s", c2[i]->device_name, c2[i]->device_serial_no );
            }

            /* Do sector size and block size checking. I don't think this does anything useful as logical/Physical
             * sector sizes are obtained by libparted in check.c */
            if( ioctl( c2[i]->device_fd, BLKSSZGET, &c2[i]->device_sector_size ) == 0 )
            {

                if( ioctl( c2[i]->device_fd, BLKBSZGET, &c2[i]->device_block_size ) != 0 )
                {
                    wype_log( WYPE_LOG_WARNING, "Device '%s' failed BLKBSZGET ioctl.", c2[i]->device_name );
                    c2[i]->device_block_size = 0;
                }
            }
            else
            {
                wype_log( WYPE_LOG_WARNING, "Device '%s' failed BLKSSZGET ioctl.", c2[i]->device_name );
                c2[i]->device_sector_size = 0;
                c2[i]->device_block_size = 0;
            }

            /* The st_size field is zero for block devices. */
            /* ioctl( c2[i]->device_fd, BLKGETSIZE64, &c2[i]->device_size ); */

            /* Seek to the end of the device to determine its size. */
            c2[i]->device_size = lseek( c2[i]->device_fd, 0, SEEK_END );

            /* Also ask the driver for the device size. */
            /* if( ioctl( c2[i]->device_fd, BLKGETSIZE64, &size64 ) ) */
            if( ioctl( c2[i]->device_fd, _IOR( 0x12, 114, size_t ), &size64 ) )
            {
                /* The ioctl failed. */
                fprintf( stderr, "Error: BLKGETSIZE64 failed  on '%s'.\n", c2[i]->device_name );
                wype_log( WYPE_LOG_ERROR, "BLKGETSIZE64 failed  on '%s'.\n", c2[i]->device_name );
                wype_error++;
            }
            c2[i]->device_size = size64;

            /* Check whether the two size values agree. */
            if( c2[i]->device_size != size64 )
            {
                /* This could be caused by the linux last-odd-block problem. */
                fprintf( stderr, "Error: Last-odd-block detected on '%s'.\n", c2[i]->device_name );
                wype_log( WYPE_LOG_ERROR, "Last-odd-block detected on '%s'.", c2[i]->device_name );
                wype_error++;
            }

            if( c2[i]->device_size == (long long) -1 )
            {
                /* We cannot determine the size of this device. */
                wype_perror( errno, __FUNCTION__, "lseek" );
                wype_log( WYPE_LOG_ERROR, "Unable to determine the size of '%s'.", c2[i]->device_name );
                wype_error++;
            }
            else
            {
                /* Reset the file pointer. */
                r = lseek( c2[i]->device_fd, 0, SEEK_SET );

                if( r == (off64_t) -1 )
                {
                    wype_perror( errno, __FUNCTION__, "lseek" );
                    wype_log( WYPE_LOG_ERROR, "Unable to reset the '%s' file offset.", c2[i]->device_name );
                    wype_error++;
                }
            }

            if( c2[i]->device_size == 0 )
            {
                wype_log( WYPE_LOG_ERROR,
                           "%s, sect/blk/dev %i/%i/%llu",
                           c2[i]->device_name,
                           c2[i]->device_sector_size,
                           c2[i]->device_block_size,
                           c2[i]->device_size );
                wype_error++;
                continue;
            }
            else
            {
                wype_log( WYPE_LOG_NOTICE,
                           "%s, sect/blk/dev %i/%i/%llu",
                           c2[i]->device_name,
                           c2[i]->device_sector_size,
                           c2[i]->device_block_size,
                           c2[i]->device_size );
            }

            /* Fork a child process. */
            errno = pthread_create( &c2[i]->thread, NULL, wype_options.method, (void*) c2[i] );
            if( errno )
            {
                wype_perror( errno, __FUNCTION__, "pthread_create" );
                if( !wype_options.nogui )
                    wype_gui_free();
                return errno;
            }
            else
            {
                wipe_threads_started = 1;
            }
        }
    }

    /* Change the terminal mode to non-blocking input. */
    nodelay( stdscr, 0 );

    /* Set getch to delay in order to slow screen updates. */
    halfdelay( WYPE_KNOB_SLEEP * 10 );

    /* Set up data structs to pass the GUI thread the data it needs. */
    wype_thread_data_ptr_t wype_gui_data;
    if( !wype_options.nogui )
    {
        wype_gui_data.c = c2;
        wype_gui_data.wype_misc_thread_data = &wype_misc_thread_data;
        /* Fork the GUI thread. */
        errno = pthread_create( &wype_gui_thread, NULL, wype_gui_status, &wype_gui_data );
    }

    /* Wait for all the wiping threads to finish, but don't wait if we receive the terminate signal */

    /* set getch delay to 2/10th second. */
    halfdelay( 10 );

    i = 0;
    while( i < wype_selected && terminate_signal == 0 )
    {
        if( i == wype_selected )
        {
            break;
        }

        if( c2[i]->wipe_status != 0 )
        {
            i = 0;
        }
        else
        {
            i++;
            continue;
        }
        sleep( 1 ); /* DO NOT REMOVE ! Stops the routine hogging CPU cycles */
    }

    if( terminate_signal != 1 )
    {
        if( !wype_options.nowait && !wype_options.autopoweroff )
        {
            do
            {
                sleep( 1 );

            } while( terminate_signal != 1 );
        }
    }
    if( wype_options.verbose )
    {
        wype_log( WYPE_LOG_INFO, "Exit in progress" );
    }
    /* Send a REQUEST for the wipe threads to be cancelled */
    for( i = 0; i < wype_selected; i++ )
    {

        if( c2[i]->thread )
        {
            if( wype_options.verbose )
            {
                wype_log( WYPE_LOG_INFO, "Requesting wipe thread cancellation for %s", c2[i]->device_name );
            }
            pthread_cancel( c2[i]->thread );
        }
    }

    /* Kill the GUI thread */
    if( wype_gui_thread )
    {
        if( wype_options.verbose )
        {
            wype_log( WYPE_LOG_INFO, "Cancelling the GUI thread." );
        }

        /* We don't want to use pthread_cancel as our GUI thread is aware of the control-c
         *  signal and will exit itself we just join the GUI thread and wait for confirmation
         */
        r = pthread_join( wype_gui_thread, NULL );
        if( r != 0 )
        {
            wype_log( WYPE_LOG_WARNING, "main()>pthread_join():Error when waiting for GUI thread to cancel." );
        }
        else
        {
            if( wype_options.verbose )
            {
                wype_log( WYPE_LOG_INFO, "GUI compute_stats thread has been cancelled" );
            }
        }
    }

    /* Release the gui. */
    if( !wype_options.nogui )
    {
        wype_gui_free();
    }

    /* Now join the wipe threads and wait until they have terminated */
    any_threads_still_running = 1;
    thread_timeout_counter = THREAD_CANCELLATION_TIMEOUT;
    while( any_threads_still_running )
    {
        /* quit waiting if we've tried 'thread_timeout_counter' times */
        if( thread_timeout_counter == 0 )
        {
            break;
        }

        any_threads_still_running = 0;
        for( i = 0; i < wype_selected; i++ )
        {
            if( c2[i]->thread )
            {
                printf( "\nWaiting for wipe thread to cancel for %s\n", c2[i]->device_name );

                /* Joins the thread and waits for completion before continuing */
                r = pthread_join( c2[i]->thread, NULL );
                if( r != 0 )
                {
                    wype_log( WYPE_LOG_ERROR,
                               "Error joining the wipe thread when waiting for thread to cancel.",
                               c2[i]->device_name );

                    if( r == EDEADLK )
                    {
                        wype_log( WYPE_LOG_ERROR,
                                   "Error joining the wipe thread: EDEADLK: Deadlock detected.",
                                   c2[i]->device_name );
                    }
                    else
                    {
                        if( r == EINVAL )
                        {
                            wype_log( WYPE_LOG_ERROR,
                                       "Error joining the wipe thread: %s EINVAL: thread is not joinable.",
                                       c2[i]->device_name );
                        }
                        else
                        {
                            if( r == ESRCH )
                            {
                                wype_log( WYPE_LOG_ERROR,
                                           "Error joining the wipe thread: %s ESRCH: no matching thread found",
                                           c2[i]->device_name );
                            }
                        }
                    }

                    any_threads_still_running = 1;
                }
                else
                {
                    c2[i]->thread = 0; /* Zero the thread so we know it's been cancelled */

                    if( wype_options.verbose )
                    {
                        wype_log( WYPE_LOG_INFO, "Wipe thread for device %s has terminated", c2[i]->device_name );
                    }

                    /* Close the device file descriptor. */
                    close( c2[i]->device_fd );
                }
            }
        }
        thread_timeout_counter--;
        sleep( 1 );
    }

    /* Now all the wipe threads have finished, we can issue a terminate_signal = 1
     * which will cause the temperature update thread to terminate, this is necessary
     * because in gui mode the terminate_signal is set when the user presses a key to
     * exit on completion of all the wipes, however in non gui mode that code isn't
     * active (being in the gui section) so here we need to set the terminate signal
     * specifically for a completed wipes/s just for non gui mode.
     */
    terminate_signal = 1;

    /* Kill the temperature update thread */
    if( wype_temperature_thread )
    {
        if( wype_options.verbose )
        {
            wype_log( WYPE_LOG_INFO, "Cancelling the temperature thread." );
        }

        /* We don't want to use pthread_cancel as our temperature thread is aware of the control-c
         *  signal and will exit itself we just join the temperature thread and wait for confirmation
         */
        r = pthread_join( wype_temperature_thread, NULL );
        if( r != 0 )
        {
            wype_log( WYPE_LOG_WARNING,
                       "main()>pthread_join():Error when waiting for temperature thread to cancel." );
        }
        else
        {
            if( wype_options.verbose )
            {
                wype_log( WYPE_LOG_INFO, "temperature thread has been cancelled" );
            }
        }
    }

    if( wype_options.verbose )
    {
        for( i = 0; i < wype_selected; i++ )
        {
            wype_log( WYPE_LOG_DEBUG,
                       "Status: %s, result=%d, pass_errors=%llu, verify_errors=%llu, fsync_errors=%llu",
                       c2[i]->device_name,
                       c2[i]->result,
                       c2[i]->pass_errors,
                       c2[i]->verify_errors,
                       c2[i]->fsyncdata_errors );
        }
    }

    /* if no wipe threads started then zero each selected drive result flag,
     * as we don't need to report fatal/non fatal errors if no wipes were ever started ! */
    if( wipe_threads_started == 0 )
    {
        for( i = 0; i < wype_selected; i++ )
        {
            c2[i]->result = 0;
        }
    }
    else
    {
        for( i = 0; i < wype_selected; i++ )
        {
            /* Check for errors. */
            if( c2[i]->result != 0 || c2[i]->pass_errors != 0 || c2[i]->verify_errors != 0
                || c2[i]->fsyncdata_errors != 0 )
            {
                /*
                 * If the wipe finished with non-zero but did not internally increase
                 * any error count, set at least one pass error for consistency between
                 * the shown FAILURE/IOERROR status and the error count (also done in GUI).
                 */
                if( c2[i]->pass_errors == 0 && c2[i]->verify_errors == 0 && c2[i]->fsyncdata_errors == 0 )
                {
                    c2[i]->pass_errors = 1;
                }

                wype_log( WYPE_LOG_FATAL,
                           "Wype exited with errors on device = %s, see log for specific error\n",
                           c2[i]->device_name );
                wype_log( WYPE_LOG_DEBUG,
                           "Status: %s, result=%d, pass_errors=%llu, verify_errors=%llu, fsync_errors=%llu",
                           c2[i]->device_name,
                           c2[i]->result,
                           c2[i]->pass_errors,
                           c2[i]->verify_errors,
                           c2[i]->fsyncdata_errors );
                non_fatal_errors_flag = 1;
                return_status = 1;
            }
        }
    }

    /* Generate and send the drive status summary to the log */
    wype_log_summary( &wype_thread_data_ptr, c2, wype_selected );

    /* Print a one line status message for the user */
    if( return_status == 0 || return_status == 1 )
    {
        if( user_abort == 1 )
        {
            if( global_wipe_status == 1 )
            {
                wype_log( WYPE_LOG_INFO,
                           "Wype was aborted by the user. Check the summary table for the drive status." );
            }
            else
            {
                wype_log( WYPE_LOG_INFO, "Wype was aborted by the user prior to the wipe starting." );
            }
        }
        else
        {
            if( fatal_errors_flag == 1 || non_fatal_errors_flag == 1 )
            {
                wype_log( WYPE_LOG_INFO,
                           "Wype exited with errors, check the log & summary table for individual drive status." );
            }
            else
            {
                wype_log( WYPE_LOG_INFO, "Wype successfully completed. See summary table for details." );
            }
        }
    }

    cleanup();

    check_for_autopoweroff();

    /* Exit. */
    return return_status;
}

void* signal_hand( void* ptr )
{
    int sig;
    int hours;
    int minutes;
    int seconds;

    hours = 0;
    minutes = 0;
    seconds = 0;

    // Define signals that this handler should react to
    sigset_t sigset;
    sigemptyset( &sigset );
    sigaddset( &sigset, SIGHUP );
    sigaddset( &sigset, SIGTERM );
    sigaddset( &sigset, SIGQUIT );
    sigaddset( &sigset, SIGINT );
    sigaddset( &sigset, SIGUSR1 );

    int i;
    char eta[9];

    /* Set up the structs we will use for the data required. */
    wype_thread_data_ptr_t* wype_thread_data_ptr;
    wype_context_t** c;
    wype_misc_thread_data_t* wype_misc_thread_data;

    /* Retrieve from the pointer passed to the function. */
    wype_thread_data_ptr = (wype_thread_data_ptr_t*) ptr;
    c = wype_thread_data_ptr->c;
    wype_misc_thread_data = wype_thread_data_ptr->wype_misc_thread_data;

    while( 1 )
    {
        /* wait for a signal to arrive */
        sigwait( &sigset, &sig );

        switch( sig )
        {

            // Log current status. All values are automatically updated by the GUI
            case SIGUSR1:
                compute_stats( ptr );

                for( i = 0; i < wype_misc_thread_data->wype_selected; i++ )
                {
                    if( c[i]->thread )
                    {
                        char* status = "";
                        const char* op_prefix = c[i]->io_direction == WYPE_IO_DIRECTION_FORWARD ? "" : "<";
                        const char* op_suffix = c[i]->io_direction == WYPE_IO_DIRECTION_FORWARD ? ">" : "";

                        switch( c[i]->pass_type )
                        {
                            case WYPE_PASS_FINAL_BLANK:
                                status = "[blanking]";
                                break;

                            case WYPE_PASS_FINAL_OPS2:
                                status = "[OPS-II final]";
                                break;

                            case WYPE_PASS_WRITE:
                                status = "[writing]";
                                break;

                            case WYPE_PASS_VERIFY:
                                status = "[verifying]";
                                break;

                            case WYPE_PASS_NONE:
                                break;
                        }
                        if( c[i]->sync_status )
                        {
                            status = "[syncing]";
                        }
                        if( c[i]->retry_status )
                        {
                            status = "[retrying]";
                        }

                        convert_seconds_to_hours_minutes_seconds( c[i]->eta, &hours, &minutes, &seconds );

                        wype_log( WYPE_LOG_INFO,
                                   "%s: %05.2f%%, round %i of %i, pass %i of %i, eta %02i:%02i:%02i, %s%s%s",
                                   c[i]->device_name,
                                   c[i]->round_percent,
                                   c[i]->round_working,
                                   c[i]->round_count,
                                   c[i]->pass_working,
                                   c[i]->pass_count,
                                   hours,
                                   minutes,
                                   seconds,
                                   status[0] != '\0' ? op_prefix : "",
                                   status,
                                   status[0] != '\0' ? op_suffix : "" );
                    }
                    else
                    {
                        if( c[i]->result == 0 )
                        {
                            wype_log( WYPE_LOG_INFO, "%s: Success", c[i]->device_name );
                        }
                        else if( c[i]->signal )
                        {
                            wype_log(
                                WYPE_LOG_INFO, "%s: >>> FAILURE! <<<: signal %i", c[i]->device_name, c[i]->signal );
                        }
                        else
                        {
                            wype_log(
                                WYPE_LOG_INFO, "%s: >>> FAILURE! <<<: code %i", c[i]->device_name, c[i]->result );
                        }
                    }
                }

                break;

            case SIGHUP:
            case SIGINT:
            case SIGQUIT:
            case SIGTERM:
                /* Set termination flag for main() which will do housekeeping prior to exit */
                terminate_signal = 1;

                /* Set the user abort flag */
                user_abort = 1;

                /* Return control to the main thread, returning the signal received */
                return ( (void*) 0 );

                break;
        }
    }

    return ( 0 );
}

int cleanup()
{
    int i;
    extern int log_elements_displayed;  // initialised and found in logging.c
    extern int log_elements_allocated;  // initialised and found in logging.c
    extern char** log_lines;
    extern config_t wype_cfg;

    /* Print the logs held in memory to the console */
    for( i = log_elements_displayed; i < log_elements_allocated; i++ )
    {
        printf( "%s\n", log_lines[i] );
    }
    fflush( stdout );

    /* Deallocate memory used by logging */
    if( log_elements_allocated != 0 )
    {
        for( i = 0; i < log_elements_allocated; i++ )
        {
            free( log_lines[i] );
        }
        log_elements_allocated = 0;  // zeroed just in case cleanup is called twice.
        free( log_lines );
    }

    /* Deallocate libconfig resources */
    config_destroy( &wype_cfg );

    /* TODO: Any other cleanup required ? */

    return 0;
}
void check_for_autopoweroff( void )
{
    char cmd[] = "shutdown -Ph +1 \"System going down in one minute\"";
    FILE* fp;
    int r;  // A result buffer.

    /* User request auto power down ? */
    if( wype_options.autopoweroff == 1 )
    {
        fp = popen( cmd, "r" );
        if( fp == NULL )
        {
            wype_log( WYPE_LOG_INFO, "Failed to autopoweroff to %s", cmd );
            return;
        }
        r = pclose( fp );
    }
}
