/*
 *  options.c:  Command line processing routines for wype.
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

#include "wype.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "logging.h"
#include "version.h"
#include "conf.h"
#include "cpu_features.h"
#include "libconfig.h"

/* The global options struct. */
wype_options_t wype_options;

int wype_options_parse( int argc, char** argv )
{
    extern char* optarg;  // The working getopt option argument.
    extern int optind;  // The working getopt index into argv.
    extern int optopt;  // The last unhandled getopt option.
    extern int opterr;  // The last getopt error number.

    extern wype_prng_t wype_twister;
    extern wype_prng_t wype_isaac;
    extern wype_prng_t wype_isaac64;
    extern wype_prng_t wype_add_lagg_fibonacci_prng;
    extern wype_prng_t wype_xoroshiro256_prng;
    extern wype_prng_t wype_splitmix64_prng;
    extern wype_prng_t wype_aes_ctr_prng;
    extern wype_prng_t wype_chacha20_prng;

    extern config_t wype_cfg;
    config_setting_t* setting;
    const char* user_defined_tag;

    /* The getopt() result holder. */
    int wype_opt;

    /* Excluded drive indexes */
    int idx_drive_chr;
    int idx_optarg;
    int idx_drive;

    /* Array index variable. */
    int i;

    /* The list of acceptable short options. */
    char wype_options_short[] = "Vvhl:P:m:p:qr:e:";

    /* Used when reading value fron wype.conf */
    const char* read_value = NULL;

    int ret;

    /* The list of acceptable long options. */
    static struct option wype_options_long[] = {
        /* Set when user wants to allow wiping of devices that are in use. */
        { "force", no_argument, 0, 0 },

        /* Set when the user wants to wipe without a confirmation prompt. */
        { "autonuke", no_argument, 0, 0 },

        /* Set when the user wants to have the system powerdown on completion of wipe. */
        { "autopoweroff", no_argument, 0, 0 },

        /* A GNU standard option. Corresponds to the 'h' short option. */
        { "help", no_argument, 0, 0 },

        /* The wipe method. Corresponds to the 'm' short option. */
        { "method", required_argument, 0, 'm' },

        /* Log file. Corresponds to the 'l' short option. */
        { "logfile", required_argument, 0, 'l' },

        /* PDFreport path. Corresponds to the 'P' short option. */
        { "PDFreportpath", required_argument, 0, 'P' },

        /* Exclude devices, comma separated list */
        { "exclude", required_argument, 0, 'e' },

        /* The Pseudo Random Number Generator. */
        { "prng", required_argument, 0, 'p' },
        { "prng-benchmark", no_argument, 0, 0 },
        { "prng-bench-seconds", required_argument, 0, 0 },

        /* The number of times to run the method. */
        { "rounds", required_argument, 0, 'r' },

        /* Whether to blank the disk after wiping. */
        { "noblank", no_argument, 0, 0 },

        /* Whether to ignore all USB devices. */
        { "nousb", no_argument, 0, 0 },

        /* Whether to exit after wiping or wait for a keypress. */
        { "nowait", no_argument, 0, 0 },

        /* Whether to allow signals to interrupt a wipe. */
        { "nosignals", no_argument, 0, 0 },

        /* Reverse the I/O direction (end -> start). */
        { "reverse", no_argument, 0, 0 },

        /* Do NOT retry on possibly transient I/O errors. */
        { "no-retry-on-io-errors", no_argument, 0, 0 },

        /* Do NOT abort pass on block write errors. */
        { "no-abort-on-block-errors", no_argument, 0, 0 },

        /* Whether to display the gui. */
        { "nogui", no_argument, 0, 0 },

        /* Whether to anonymize the serial numbers. */
        { "quiet", no_argument, 0, 0 },

        /* A flag to indicate whether the devices would be opened in sync mode. */
        { "sync", required_argument, 0, 0 },

        /* Verify that wipe patterns are being written to the device. */
        { "verify", required_argument, 0, 0 },

        /* I/O mode selection: auto/direct/cached. */
        { "directio", no_argument, 0, 0 },
        { "cachedio", no_argument, 0, 0 },

        /* Enables a field on the PDF that holds a tag that identifies the host computer */
        { "pdftag", no_argument, 0, 0 },

        /* Display program version. */
        { "verbose", no_argument, 0, 0 },

        /* Display program version. */
        { "version", no_argument, 0, 0 },

        /* Requisite padding for getopt(). */
        { 0, 0, 0, 0 } };

    /* Set default options. */
    wype_options.force = 0;
    wype_options.autonuke = 0;
    wype_options.autopoweroff = 0;
    wype_options.method = &wype_random;
    wype_options.prng_auto = 1; /* by default the PRNG is selected through the benchmark selection */
    wype_options.prng_benchmark_only = 0;
    wype_options.prng_bench_seconds = 1.0; /* default for interactive / manual */

    /*
     * Determines and sets the default PRNG based on AES-NI support and system architecture.
     * It selects AES-CTR PRNG if AES-NI is supported, xoroshiro256 for 64-bit systems without AES-NI,
     * and add lagged Fibonacci for 32-bit systems.
     */

    if( has_aes_ni() )
    {
        wype_options.prng = &wype_aes_ctr_prng;
    }
    else if( sizeof( unsigned long int ) >= 8 )
    {
        wype_options.prng = &wype_xoroshiro256_prng;
        wype_log( WYPE_LOG_WARNING, "CPU doesn't support AES New Instructions, opting for XORoshiro-256 instead." );
    }
    else
    {
        wype_options.prng = &wype_add_lagg_fibonacci_prng;
    }

    wype_options.rounds = 1;
    wype_options.noblank = 0;
    wype_options.nousb = 0;
    wype_options.nowait = 0;
    wype_options.nosignals = 0;
    wype_options.nogui = 0;
    wype_options.quiet = 0;
    wype_options.sync = DEFAULT_SYNC_RATE;
    wype_options.verbose = 0;
    wype_options.verify = WYPE_VERIFY_LAST;
    wype_options.io_mode = WYPE_IO_MODE_AUTO; /* Default: auto-select I/O mode. */
    wype_options.io_direction = WYPE_IO_DIRECTION_FORWARD; /* Default: forward I/O direction. */
    wype_options.noretry_io_errors = 0;
    wype_options.noabort_block_errors = 0;
    wype_options.PDF_toggle_host_info = 0; /* Default: host visibility on PDF disabled */
    wype_options.PDFtag = 0;
    memset( wype_options.logfile, '\0', sizeof( wype_options.logfile ) );
    memset( wype_options.PDFreportpath, '\0', sizeof( wype_options.PDFreportpath ) );
    strncpy( wype_options.PDFreportpath, ".", 2 );

    /*
     * Read PDF Enable/Disable settings from wype.conf if available
     */
    if( ( ret = wype_conf_read_setting( "PDF_Certificate.PDF_Enable", &read_value ) ) )
    {
        /* error occurred */
        wype_log( WYPE_LOG_ERROR,
                   "wype_conf_read_setting():Error reading PDF_Certificate.PDF_Enable from wype.conf, ret code %i",
                   ret );

        /* Use default values */
        wype_options.PDF_enable = 1;
    }
    else
    {
        if( !strcmp( read_value, "ENABLED" ) )
        {
            wype_options.PDF_enable = 1;
        }
        else
        {
            if( !strcmp( read_value, "DISABLED" ) )
            {
                wype_options.PDF_enable = 0;
            }
            else
            {
                // error occurred
                wype_log(
                    WYPE_LOG_ERROR,
                    "PDF_Certificate.PDF_Enable in wype.conf returned a value that was neither ENABLED or DISABLED" );
                wype_options.PDF_enable = 1;  // Default to Enabled
            }
        }
    }

    /*
     * Read PDF host visibility settings from wype.conf if available
     */
    if( ( ret = wype_conf_read_setting( "PDF_Certificate.PDF_Host_Visibility", &read_value ) ) )
    {
        /* error occurred */
        wype_log(
            WYPE_LOG_ERROR,
            "wype_conf_read_setting():Error reading PDF_Certificate.PDF_toggle_host_info from wype.conf, ret code %i",
            ret );

        wype_options.PDF_toggle_host_info = 0; /* Disable host visibility on PDF */
    }
    else
    {
        if( !strcmp( read_value, "ENABLED" ) )
        {
            wype_options.PDF_toggle_host_info = 1;
        }
        else
        {
            if( !strcmp( read_value, "DISABLED" ) )
            {
                wype_options.PDF_toggle_host_info = 0;
            }
            else
            {
                // error occurred
                wype_log( WYPE_LOG_ERROR,
                           "PDF_Certificate.PDF_toggle_host_info in wype.conf returned a value that was neither "
                           "ENABLED or DISABLED" );
                wype_options.PDF_toggle_host_info = 0;  // Default to disabled
            }
        }
    }

    /*
     * Read PDF tag Enable/Disable settings from wype.conf if available.
     * Only show the tag on the certificate if PDF_tag is "ENABLED".
     */

    setting = config_lookup( &wype_cfg, "PDF_Certificate" );

    {
        const char* pdf_tag_enable = NULL;
        if( config_setting_lookup_string( setting, "PDF_tag", &pdf_tag_enable )
            && pdf_tag_enable != NULL
            && strcasecmp( pdf_tag_enable, "ENABLED" ) == 0 )
        {
            if( config_setting_lookup_string( setting, "User_Defined_Tag", &user_defined_tag )
                && user_defined_tag[0] != 0 )
            {
                wype_options.PDFtag = 1;
            }
            else
            {
                wype_options.PDFtag = 0;
            }
        }
        else
        {
            wype_options.PDFtag = 0;
        }
    }

    /*
     * PDF Preview enable/disable
     */
    if( ( ret = wype_conf_read_setting( "PDF_Certificate.PDF_Preview", &read_value ) ) )
    {
        /* error occurred */
        wype_log( WYPE_LOG_ERROR,
                   "wype_conf_read_setting():Error reading PDF_Certificate.PDF_Preview from wype.conf, ret code %i",
                   ret );

        /* Use default values */
        wype_options.PDF_enable = 1;
    }
    else
    {
        if( !strcmp( read_value, "ENABLED" ) )
        {
            wype_options.PDF_preview_details = 1;
        }
        else
        {
            if( !strcmp( read_value, "DISABLED" ) )
            {
                wype_options.PDF_preview_details = 0;
            }
            else
            {
                /* error occurred */
                wype_log(
                    WYPE_LOG_ERROR,
                    "PDF_Certificate.PDF_Preview in wype.conf returned a value that was neither ENABLED or DISABLED" );
                wype_options.PDF_preview_details = 1; /* Default to Enabled */
            }
        }
    }

    /*
     * Initialise each of the strings in the excluded drives array
     */
    for( i = 0; i < MAX_NUMBER_EXCLUDED_DRIVES; i++ )
    {
        wype_options.exclude[i][0] = 0;
    }

    /* Parse command line options. */
    while( 1 )
    {
        /* Get the next command line option with (3)getopt. */
        wype_opt = getopt_long( argc, argv, wype_options_short, wype_options_long, &i );

        /* Break when we have processed all of the given options. */
        if( wype_opt < 0 )
        {
            break;
        }

        switch( wype_opt )
        {
            case 0: /* Long options without short counterparts. */
                if( strcmp( wype_options_long[i].name, "force" ) == 0 )
                {
                    wype_options.force = 1;
                    break;
                }

                if( strcmp( wype_options_long[i].name, "autonuke" ) == 0 )
                {
                    /* check for the full option name, as getopt_long() allows abreviations and can lead to unintended
                     * consequences when the user makes a typo */
                    if( strcmp( argv[optind - 1], "--autonuke" ) == 0 )
                    {
                        wype_options.autonuke = 1;
                        break;
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: Strict command line options required, did you mean --autonuke?, you typed "
                                 "%s.\nType `sudo wype --help` for options \n",
                                 argv[optind - 1] );
                        exit( EINVAL );
                    }
                }

                if( strcmp( wype_options_long[i].name, "autopoweroff" ) == 0 )
                {
                    /* check for the full option name, as getopt_long() allows abreviations and can lead to unintended
                     * consequences when the user makes a typo */
                    if( strcmp( argv[optind - 1], "--autopoweroff" ) == 0 )
                    {
                        wype_options.autopoweroff = 1;
                        break;
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: Strict command line options required, did you mean --autopoweroff?, you typed "
                                 "%s.\nType `sudo wype --help` for options \n",
                                 argv[optind - 1] );
                        exit( EINVAL );
                    }
                }

                if( strcmp( wype_options_long[i].name, "help" ) == 0 )
                {
                    /* check for the full option name, as getopt_long() allows abreviations and can lead to unintended
                     * consequences when the user makes a typo */
                    if( strcmp( argv[optind - 1], "--help" ) == 0 )
                    {
                        display_help();
                        exit( EINVAL );
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: Strict command line options required, did you mean --help?, you typed "
                                 "%s.\nType `sudo wype --help` for options \n",
                                 argv[optind - 1] );
                        exit( EINVAL );
                    }
                }

                if( strcmp( wype_options_long[i].name, "noblank" ) == 0 )
                {
                    /* check for the full option name, as getopt_long() allows abreviations and can lead to unintended
                     * consequences when the user makes a typo */
                    if( strcmp( argv[optind - 1], "--noblank" ) == 0 )
                    {
                        wype_options.noblank = 1;
                        break;
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: Strict command line options required, did you mean --noblank?, you typed "
                                 "%s.\nType `sudo wype --help` for options \n",
                                 argv[optind - 1] );
                        exit( EINVAL );
                    }
                }

                if( strcmp( wype_options_long[i].name, "nousb" ) == 0 )
                {
                    /* check for the full option name, as getopt_long() allows abreviations and can lead to unintended
                     * consequences when the user makes a typo */
                    if( strcmp( argv[optind - 1], "--nousb" ) == 0 )
                    {
                        wype_options.nousb = 1;
                        break;
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: Strict command line options required, did you mean --nousb?, you typed "
                                 "%s.\nType `sudo wype --help` for options \n",
                                 argv[optind - 1] );
                        exit( EINVAL );
                    }
                }

                if( strcmp( wype_options_long[i].name, "nowait" ) == 0 )
                {
                    /* check for the full option name, as getopt_long() allows abreviations and can lead to unintended
                     * consequences when the user makes a typo */
                    if( strcmp( argv[optind - 1], "--nowait" ) == 0 )
                    {
                        wype_options.nowait = 1;
                        break;
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: Strict command line options required, did you mean --nowait?, you typed "
                                 "%s.\nType `sudo wype --help` for options \n",
                                 argv[optind - 1] );
                        exit( EINVAL );
                    }
                }

                if( strcmp( wype_options_long[i].name, "nosignals" ) == 0 )
                {
                    /* check for the full option name, as getopt_long() allows abreviations and can lead to unintended
                     * consequences when the user makes a typo */
                    if( strcmp( argv[optind - 1], "--nosignals" ) == 0 )
                    {
                        wype_options.nosignals = 1;
                        break;
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: Strict command line options required, did you mean --nosignals?, you typed "
                                 "%s.\nType `sudo wype --help` for options \n",
                                 argv[optind - 1] );
                        exit( EINVAL );
                    }
                }

                if( strcmp( wype_options_long[i].name, "reverse" ) == 0 )
                {
                    wype_options.io_direction = WYPE_IO_DIRECTION_REVERSE;
                    break;
                }

                if( strcmp( wype_options_long[i].name, "no-retry-on-io-errors" ) == 0 )
                {
                    wype_options.noretry_io_errors = 1;
                    break;
                }

                if( strcmp( wype_options_long[i].name, "no-abort-on-block-errors" ) == 0 )
                {
                    wype_options.noabort_block_errors = 1;
                    break;
                }

                if( strcmp( wype_options_long[i].name, "nogui" ) == 0 )
                {
                    /* check for the full option name, as getopt_long() allows abreviations and can lead to unintended
                     * consequences when the user makes a typo */
                    if( strcmp( argv[optind - 1], "--nogui" ) == 0 )
                    {
                        wype_options.nogui = 1;
                        wype_options.nowait = 1;
                        break;
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: Strict command line options required, did you mean --nogui?, you typed "
                                 "%s.\nType `sudo wype --help` for options \n",
                                 argv[optind - 1] );
                        exit( EINVAL );
                    }
                }

                if( strcmp( wype_options_long[i].name, "quiet" ) == 0 )
                {
                    /* check for the full option name, as getopt_long() allows abreviations and can lead to unintended
                     * consequences when the user makes a typo */
                    if( strcmp( argv[optind - 1], "--quiet" ) == 0 )
                    {
                        wype_options.quiet = 1;
                        break;
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: Strict command line options required, did you mean --quiet?, you typed "
                                 "%s.\nType `sudo wype --help` for options \n",
                                 argv[optind - 1] );
                        exit( EINVAL );
                    }
                }

                if( strcmp( wype_options_long[i].name, "verbose" ) == 0 )
                {
                    /* check for the full option name, as getopt_long() allows abreviations and can lead to unintended
                     * consequences when the user makes a typo */
                    if( strcmp( argv[optind - 1], "--verbose" ) == 0 )
                    {
                        wype_options.verbose = 1;
                        break;
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: Strict command line options required, did you mean --verbose?, you typed "
                                 "%s.\nType `sudo wype --help` for options \n",
                                 argv[optind - 1] );
                        exit( EINVAL );
                    }
                }

                if( strcmp( wype_options_long[i].name, "sync" ) == 0 )
                {
                    if( sscanf( optarg, " %i", &wype_options.sync ) != 1 || wype_options.sync < 0 )
                    {
                        fprintf( stderr, "Error: The sync argument must be a positive integer or zero.\n" );
                        exit( EINVAL );
                    }
                    break;
                }

                if( strcmp( wype_options_long[i].name, "verify" ) == 0 )
                {
                    if( strcmp( optarg, "0" ) == 0 || strcmp( optarg, "off" ) == 0 )
                    {
                        wype_options.verify = WYPE_VERIFY_NONE;
                        break;
                    }

                    if( strcmp( optarg, "1" ) == 0 || strcmp( optarg, "last" ) == 0 )
                    {
                        wype_options.verify = WYPE_VERIFY_LAST;
                        break;
                    }

                    if( strcmp( optarg, "2" ) == 0 || strcmp( optarg, "all" ) == 0 )
                    {
                        wype_options.verify = WYPE_VERIFY_ALL;
                        break;
                    }

                    /* Else we do not know this verification level. */
                    fprintf( stderr, "Error: Unknown verification level '%s'.\n", optarg );
                    exit( EINVAL );
                }

                /* I/O mode selection options. */

                if( strcmp( wype_options_long[i].name, "directio" ) == 0 )
                {
                    /* check for the full option name, as getopt_long() allows abreviations and can lead to unintended
                     * consequences when the user makes a typo */
                    if( strcmp( argv[optind - 1], "--directio" ) == 0 )
                    {
                        wype_options.io_mode = WYPE_IO_MODE_DIRECT;
                        break;
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: Strict command line options required, did you mean --directio?, you typed "
                                 "%s.\nType `sudo wype --help` for options \n",
                                 argv[optind - 1] );
                        exit( EINVAL );
                    }
                }

                if( strcmp( wype_options_long[i].name, "cachedio" ) == 0 )
                {
                    /* check for the full option name, as getopt_long() allows abreviations and can lead to unintended
                     * consequences when the user makes a typo */
                    if( strcmp( argv[optind - 1], "--cachedio" ) == 0 )
                    {
                        wype_options.io_mode = WYPE_IO_MODE_CACHED;
                        break;
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: Strict command line options required, did you mean --cachedio?, you typed "
                                 "%s.\nType `sudo wype --help` for options \n",
                                 argv[optind - 1] );
                        exit( EINVAL );
                    }
                }

                if( strcmp( wype_options_long[i].name, "pdftag" ) == 0 )
                {
                    wype_options.PDFtag = 1;
                    break;
                }

                if( strcmp( wype_options_long[i].name, "prng-benchmark" ) == 0 )
                {
                    wype_options.prng_benchmark_only = 1;
                    break;
                }
                if( strcmp( wype_options_long[i].name, "prng-bench-seconds" ) == 0 )
                {
                    wype_options.prng_bench_seconds = atof( optarg );
                    if( wype_options.prng_bench_seconds < 0.05 )
                        wype_options.prng_bench_seconds = 0.05;
                    if( wype_options.prng_bench_seconds > 10.0 )
                        wype_options.prng_bench_seconds = 10.0;
                    break;
                }

                /* getopt_long should raise on invalid option, so we should never get here. */
                exit( EINVAL );

            case 'm': /* Method option. */

                if( strcmp( optarg, "dod522022m" ) == 0 || strcmp( optarg, "dod" ) == 0 )
                {
                    wype_options.method = &wype_dod522022m;
                    break;
                }

                if( strcmp( optarg, "dodshort" ) == 0 || strcmp( optarg, "dod3pass" ) == 0 )
                {
                    wype_options.method = &wype_dodshort;
                    break;
                }

                if( strcmp( optarg, "gutmann" ) == 0 )
                {
                    wype_options.method = &wype_gutmann;
                    break;
                }

                if( strcmp( optarg, "ops2" ) == 0 )
                {
                    wype_options.method = &wype_ops2;
                    break;
                }

                if( strcmp( optarg, "random" ) == 0 || strcmp( optarg, "prng" ) == 0
                    || strcmp( optarg, "stream" ) == 0 )
                {
                    wype_options.method = &wype_random;
                    break;
                }

                if( strcmp( optarg, "zero" ) == 0 || strcmp( optarg, "quick" ) == 0 )
                {
                    wype_options.method = &wype_zero;
                    break;
                }

                if( strcmp( optarg, "one" ) == 0 )
                {
                    wype_options.method = &wype_one;
                    break;
                }

                if( strcmp( optarg, "verify_zero" ) == 0 )
                {
                    wype_options.method = &wype_verify_zero;
                    break;
                }

                if( strcmp( optarg, "verify_one" ) == 0 )
                {
                    wype_options.method = &wype_verify_one;
                    break;
                }

                if( strcmp( optarg, "is5enh" ) == 0 )
                {
                    wype_options.method = &wype_is5enh;
                    break;
                }
                if( strcmp( optarg, "bruce7" ) == 0 )
                {
                    wype_options.method = &wype_bruce7;
                    break;
                }
                if( strcmp( optarg, "bmb" ) == 0 )
                {
                    wype_options.method = &wype_bmb;
                    break;
                }
                if( strcmp( optarg, "secure_erase" ) == 0 || strcmp( optarg, "secure-erase" ) == 0 )
                {
                    wype_options.method = &wype_secure_erase;
                    break;
                }
                if( strcmp( optarg, "secure_erase_prng" ) == 0 || strcmp( optarg, "secure-erase-prng" ) == 0
                    || strcmp( optarg, "secure_erase_prng_verify" ) == 0
                    || strcmp( optarg, "secure-erase-prng-verify" ) == 0 )
                {
                    wype_options.method = &wype_secure_erase_prng_verify;
                    break;
                }
                if( strcmp( optarg, "sanitize_crypto" ) == 0 || strcmp( optarg, "sanitize-crypto" ) == 0
                    || strcmp( optarg, "sanitize_crypto_erase" ) == 0
                    || strcmp( optarg, "sanitize-crypto-erase" ) == 0 )
                {
                    wype_options.method = &wype_sanitize_crypto_erase;
                    break;
                }
                if( strcmp( optarg, "sanitize_crypto_verify" ) == 0
                    || strcmp( optarg, "sanitize-crypto-verify" ) == 0
                    || strcmp( optarg, "sanitize_crypto_erase_verify" ) == 0
                    || strcmp( optarg, "sanitize-crypto-erase-verify" ) == 0 )
                {
                    wype_options.method = &wype_sanitize_crypto_erase_verify;
                    break;
                }
                if( strcmp( optarg, "sanitize_block" ) == 0 || strcmp( optarg, "sanitize-block" ) == 0
                    || strcmp( optarg, "sanitize_block_erase" ) == 0
                    || strcmp( optarg, "sanitize-block-erase" ) == 0 )
                {
                    wype_options.method = &wype_sanitize_block_erase;
                    break;
                }
                if( strcmp( optarg, "sanitize_overwrite" ) == 0 || strcmp( optarg, "sanitize-overwrite" ) == 0 )
                {
                    wype_options.method = &wype_sanitize_overwrite;
                    break;
                }

                /* Else we do not know this wipe method. */
                fprintf( stderr, "Error: Unknown wipe method '%s'.\n", optarg );
                exit( EINVAL );

            case 'l': /* Log file option. */

                wype_options.logfile[strlen( optarg )] = '\0';
                strncpy( wype_options.logfile, optarg, sizeof( wype_options.logfile ) );
                break;

            case 'P': /* PDFreport path option. */

                wype_options.PDFreportpath[strlen( optarg )] = '\0';
                strncpy( wype_options.PDFreportpath, optarg, sizeof( wype_options.PDFreportpath ) );

                /* Command line options will override what's in wype.conf */
                if( strcmp( wype_options.PDFreportpath, "noPDF" ) == 0 )
                {
                    wype_options.PDF_enable = 0;
                    wype_conf_update_setting( "PDF_Certificate.PDF_Enable", "DISABLED" );
                }
                else
                {
                    if( strcmp( wype_options.PDFreportpath, "." ) )
                    {
                        /* and if the user has specified a PDF path then enable PDF */
                        wype_options.PDF_enable = 1;
                        wype_conf_update_setting( "PDF_Certificate.PDF_Enable", "ENABLED" );
                    }
                }

                break;

            case 'e': /* exclude drives option */

                idx_drive_chr = 0;
                idx_optarg = 0;
                idx_drive = 0;

                /* Create an array of excluded drives from the comma separated string */
                while( optarg[idx_optarg] != 0 && idx_drive < MAX_NUMBER_EXCLUDED_DRIVES )
                {
                    /* drop the leading '=' character if used */
                    if( optarg[idx_optarg] == '=' && idx_optarg == 0 )
                    {
                        idx_optarg++;
                        continue;
                    }

                    if( optarg[idx_optarg] == ',' )
                    {
                        /* terminate string and move onto next drive */
                        wype_options.exclude[idx_drive++][idx_drive_chr] = 0;
                        idx_drive_chr = 0;
                        idx_optarg++;
                    }
                    else
                    {
                        if( idx_drive_chr < MAX_DRIVE_PATH_LENGTH )
                        {
                            wype_options.exclude[idx_drive][idx_drive_chr++] = optarg[idx_optarg++];
                        }
                        else
                        {
                            /* This section deals with file names that exceed MAX_DRIVE_PATH_LENGTH */
                            wype_options.exclude[idx_drive][idx_drive_chr] = 0;
                            while( optarg[idx_optarg] != 0 && optarg[idx_optarg] != ',' )
                            {
                                idx_optarg++;
                            }
                        }
                    }
                    if( idx_drive == MAX_NUMBER_EXCLUDED_DRIVES )
                    {
                        fprintf(
                            stderr,
                            "The number of excluded drives has reached the programs configured limit, aborting\n" );
                        exit( 130 );
                    }
                }
                break;

            case 'h': /* Display help. */

                display_help();
                exit( EINVAL );
                break;

            case 'p': /* PRNG option. */

                /* Default behaviour is auto now, but allow explicit opt-out */
                if( strcmp( optarg, "auto" ) == 0 )
                {
                    wype_options.prng_auto = 1;
                    /* keep current default as fallback until autoselect runs */
                    break;
                }

                /* NEW: disable auto and keep compiled-in default selection */
                if( strcmp( optarg, "default" ) == 0 || strcmp( optarg, "manual" ) == 0 )
                {
                    wype_options.prng_auto = 0;
                    /* keep wype_options.prng as chosen by CPU heuristics above */
                    break;
                }

                /* Any explicit PRNG selection implies auto off */
                wype_options.prng_auto = 0;

                if( strcmp( optarg, "mersenne" ) == 0 || strcmp( optarg, "twister" ) == 0 )
                {
                    wype_options.prng = &wype_twister;
                    break;
                }

                if( strcmp( optarg, "isaac" ) == 0 )
                {
                    wype_options.prng = &wype_isaac;
                    break;
                }

                if( strcmp( optarg, "isaac64" ) == 0 )
                {
                    wype_options.prng = &wype_isaac64;
                    break;
                }

                if( strcmp( optarg, "add_lagg_fibonacci_prng" ) == 0 )
                {
                    wype_options.prng = &wype_add_lagg_fibonacci_prng;
                    break;
                }

                if( strcmp( optarg, "xoroshiro256_prng" ) == 0 )
                {
                    wype_options.prng = &wype_xoroshiro256_prng;
                    break;
                }

                if( strcmp( optarg, "splitmix64" ) == 0 )
                {
                    wype_options.prng = &wype_splitmix64_prng;
                    break;
                }

                if( strcmp( optarg, "aes_ctr_prng" ) == 0 )
                {
                    if( has_aes_ni() )
                    {
                        wype_options.prng = &wype_aes_ctr_prng;
                    }
                    else
                    {
                        fprintf( stderr,
                                 "Error: aes_ctr_prng requires AES-NI on this build, "
                                 "but your CPU does not support AES-NI.\n" );
                        exit( EINVAL );
                    }
                    break;
                }

                if( strcmp( optarg, "chacha20" ) == 0 )
                {
                    wype_options.prng = &wype_chacha20_prng;
                    break;
                }

                fprintf( stderr, "Error: Unknown prng '%s'.\n", optarg );
                exit( EINVAL );

            case 'q': /* Anonymize serial numbers */

                wype_options.quiet = 1;
                break;

            case 'r': /* Rounds option. */

                if( sscanf( optarg, " %i", &wype_options.rounds ) != 1 || wype_options.rounds < 1 )
                {
                    fprintf( stderr, "Error: The rounds argument must be a positive integer.\n" );
                    exit( EINVAL );
                }

                break;

            case 'v': /* verbose */

                wype_options.verbose = 1;
                break;

            case 'V': /* Version option. */

                printf( "%s version %s\n", program_name, version_string );
                exit( EXIT_SUCCESS );

            default:

                /* Bogus command line argument. */
                exit( EINVAL );

        } /* method */

    } /* command line options */

    /* Disable blanking for ops2 and verify methods */
    if( wype_options.method == &wype_ops2 || wype_options.method == &wype_verify_zero
        || wype_options.method == &wype_verify_one )
    {
        wype_options.noblank = 1;
    }

    /* Return the number of options that were processed. */
    return optind;
}

void wype_options_log( void )
{
    extern wype_prng_t wype_twister;
    extern wype_prng_t wype_isaac;
    extern wype_prng_t wype_isaac64;
    extern wype_prng_t wype_add_lagg_fibonacci_prng;
    extern wype_prng_t wype_xoroshiro256_prng;
    extern wype_prng_t wype_splitmix64_prng;
    extern wype_prng_t wype_aes_ctr_prng;
    extern wype_prng_t wype_chacha20_prng;

    /**
     *  Prints a manifest of options to the log.
     */
    wype_log( WYPE_LOG_NOTICE, "Program options are set as follows..." );

    wype_log( WYPE_LOG_NOTICE, "  force        = %i (%s)", wype_options.force, wype_options.force ? "on" : "off" );

    wype_log(
        WYPE_LOG_NOTICE, "  autonuke     = %i (%s)", wype_options.autonuke, wype_options.autonuke ? "on" : "off" );

    wype_log( WYPE_LOG_NOTICE,
               "  autopoweroff = %i (%s)",
               wype_options.autopoweroff,
               wype_options.autopoweroff ? "on" : "off" );

    if( wype_options.noblank )
        wype_log( WYPE_LOG_NOTICE, "  do not perform a final blank pass" );
    if( wype_options.nowait )
        wype_log( WYPE_LOG_NOTICE, "  do not wait for a key before exiting" );
    if( wype_options.nosignals )
        wype_log( WYPE_LOG_NOTICE, "  do not allow signals to interrupt a wipe" );
    if( wype_options.nogui )
        wype_log( WYPE_LOG_NOTICE, "  do not show GUI interface" );
    if( wype_options.noretry_io_errors )
        wype_log( WYPE_LOG_NOTICE, "  do not retry I/O errors" );
    if( wype_options.noabort_block_errors )
        wype_log( WYPE_LOG_NOTICE, "  do not abort on block errors" );

    wype_log( WYPE_LOG_NOTICE, "  banner       = %s", banner );

    if( wype_options.prng == &wype_twister )
        wype_log( WYPE_LOG_NOTICE, "  prng         = Mersenne Twister" );
    else if( wype_options.prng == &wype_add_lagg_fibonacci_prng )
        wype_log( WYPE_LOG_NOTICE, "  prng         = Lagged Fibonacci generator" );
    else if( wype_options.prng == &wype_xoroshiro256_prng )
        wype_log( WYPE_LOG_NOTICE, "  prng         = XORoshiro-256" );
    else if( wype_options.prng == &wype_splitmix64_prng )
        wype_log( WYPE_LOG_NOTICE, "  prng         = SplitMix64" );
    else if( wype_options.prng == &wype_aes_ctr_prng )
        wype_log( WYPE_LOG_NOTICE, "  prng         = AES-CTR (CSPRNG)" );
    else if( wype_options.prng == &wype_isaac )
        wype_log( WYPE_LOG_NOTICE, "  prng         = Isaac" );
    else if( wype_options.prng == &wype_isaac64 )
        wype_log( WYPE_LOG_NOTICE, "  prng         = Isaac64" );
    else if( wype_options.prng == &wype_chacha20_prng )
        wype_log( WYPE_LOG_NOTICE, "  prng         = ChaCha20 (CSPRNG)" );
    else
        wype_log( WYPE_LOG_NOTICE, "  prng         = Unknown" );

    wype_log( WYPE_LOG_NOTICE, "  method       = %s", wype_method_label( wype_options.method ) );

    wype_log( WYPE_LOG_NOTICE,
               "  direction    = %s",
               wype_options.io_direction == WYPE_IO_DIRECTION_FORWARD ? "start -> end (forward)"
                                                                        : "end -> start (reverse)" );

    wype_log( WYPE_LOG_NOTICE, "  quiet        = %i", wype_options.quiet );
    wype_log( WYPE_LOG_NOTICE, "  rounds       = %i", wype_options.rounds );
    wype_log( WYPE_LOG_NOTICE, "  sync         = %i", wype_options.sync );

    switch( wype_options.verify )
    {
        case WYPE_VERIFY_NONE:
            wype_log( WYPE_LOG_NOTICE, "  verify       = %i (off)", wype_options.verify );
            break;
        case WYPE_VERIFY_LAST:
            wype_log( WYPE_LOG_NOTICE, "  verify       = %i (last pass)", wype_options.verify );
            break;
        case WYPE_VERIFY_ALL:
            wype_log( WYPE_LOG_NOTICE, "  verify       = %i (all passes)", wype_options.verify );
            break;
        default:
            wype_log( WYPE_LOG_NOTICE, "  verify       = %i", wype_options.verify );
            break;
    }
}
