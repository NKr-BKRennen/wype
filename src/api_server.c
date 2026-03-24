/*
 *  api_server.c: Embedded HTTP/JSON API for wype dashboard integration.
 *
 *  Uses libmicrohttpd for the HTTP server and cJSON for JSON
 *  serialisation.  When HAVE_API_SERVER is not defined (libraries
 *  absent at build time) the public functions are compiled as stubs.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "api_server.h"

#ifdef HAVE_API_SERVER

#include <microhttpd.h>
#include <cjson/cJSON.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "context.h"
#include "options.h"
#include "method.h"
#include "version.h"
#include "logging.h"

#define API_DEFAULT_PORT 5000

/* ------------------------------------------------------------------ */
/*  Module-level state (set once by _start, read by the HTTP handler) */
/* ------------------------------------------------------------------ */

static struct MHD_Daemon* api_daemon = NULL;
static wype_context_t*** api_ctx_ptr = NULL;
static int* api_count_ptr = NULL;
static wype_misc_thread_data_t* api_misc = NULL;
static char api_password[256] = "";
static int api_has_password = 0;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static int check_auth( struct MHD_Connection* connection )
{
    if( !api_has_password )
        return 1;

    const char* key = MHD_lookup_connection_value( connection, MHD_HEADER_KIND, "X-API-Key" );
    if( key != NULL && strcmp( key, api_password ) == 0 )
        return 1;

    return 0;
}

static enum MHD_Result send_json( struct MHD_Connection* connection,
                                   unsigned int status_code,
                                   cJSON* json )
{
    char* body = cJSON_PrintUnformatted( json );
    cJSON_Delete( json );

    struct MHD_Response* response =
        MHD_create_response_from_buffer( strlen( body ), body, MHD_RESPMEM_MUST_FREE );

    MHD_add_response_header( response, "Content-Type", "application/json" );
    MHD_add_response_header( response, "Access-Control-Allow-Origin", "*" );
    MHD_add_response_header( response, "Access-Control-Allow-Headers", "X-API-Key, Content-Type" );
    MHD_add_response_header( response, "Access-Control-Allow-Methods", "GET, OPTIONS" );

    enum MHD_Result ret = MHD_queue_response( connection, status_code, response );
    MHD_destroy_response( response );
    return ret;
}

static enum MHD_Result send_unauthorized( struct MHD_Connection* connection )
{
    cJSON* j = cJSON_CreateObject();
    cJSON_AddStringToObject( j, "error", "Unauthorized" );
    return send_json( connection, MHD_HTTP_UNAUTHORIZED, j );
}

/* ------------------------------------------------------------------ */
/*  Enum → string helpers                                              */
/* ------------------------------------------------------------------ */

static const char* device_type_str( wype_device_t t )
{
    switch( t )
    {
        case WYPE_DEVICE_IDE: return "IDE";
        case WYPE_DEVICE_SCSI: return "SCSI";
        case WYPE_DEVICE_USB: return "USB";
        case WYPE_DEVICE_ATA: return "ATA";
        case WYPE_DEVICE_NVME: return "NVMe";
        case WYPE_DEVICE_VIRT: return "VIRT";
        case WYPE_DEVICE_SAS: return "SAS";
        case WYPE_DEVICE_MMC: return "MMC";
        default: return "Unknown";
    }
}

static const char* pass_type_str( wype_pass_t t )
{
    switch( t )
    {
        case WYPE_PASS_WRITE: return "write";
        case WYPE_PASS_VERIFY: return "verify";
        case WYPE_PASS_FINAL_BLANK: return "blank";
        case WYPE_PASS_FINAL_OPS2: return "final_ops2";
        default: return "none";
    }
}

static const char* verify_str( wype_verify_t v )
{
    switch( v )
    {
        case WYPE_VERIFY_LAST: return "last";
        case WYPE_VERIFY_ALL: return "all";
        default: return "none";
    }
}

static const char* wipe_status_str( int status, const char* txt )
{
    if( status == -1 )
        return "pending";
    if( status == 1 )
        return "wiping";
    if( txt[0] != '\0' )
        return txt;
    return "idle";
}

/* ------------------------------------------------------------------ */
/*  JSON builders                                                      */
/* ------------------------------------------------------------------ */

static cJSON* build_status_json( void )
{
    extern int global_wipe_status;

    cJSON* root = cJSON_CreateObject();

    /* ---- node ---- */
    cJSON* node = cJSON_CreateObject();
    char hostname[256] = "";
    gethostname( hostname, sizeof( hostname ) - 1 );
    hostname[sizeof( hostname ) - 1] = '\0';

    cJSON_AddStringToObject( node, "hostname", hostname );
    cJSON_AddStringToObject( node, "version", version_string );
    cJSON_AddBoolToObject( node, "wipe_active", global_wipe_status == 1 );

    const char* method_name = wype_method_label( wype_options.method );
    cJSON_AddStringToObject( node, "method", method_name ? method_name : "Unknown" );
    cJSON_AddStringToObject( node, "prng", wype_options.prng ? wype_options.prng->label : "N/A" );
    cJSON_AddNumberToObject( node, "rounds", wype_options.rounds );
    cJSON_AddStringToObject( node, "verify", verify_str( wype_options.verify ) );

    if( api_misc )
    {
        cJSON_AddNumberToObject( node, "total_throughput", (double) api_misc->throughput );
        cJSON_AddNumberToObject( node, "max_eta_seconds", (double) api_misc->maxeta );
        cJSON_AddNumberToObject( node, "total_errors", (double) api_misc->errors );
        cJSON_AddNumberToObject( node, "total_io_retries", (double) api_misc->io_retries );
        cJSON_AddNumberToObject( node, "devices_enumerated", api_misc->wype_enumerated );
        cJSON_AddNumberToObject( node, "devices_selected", api_misc->wype_selected );
    }

    /* system info (Linux /proc – absent fields are simply omitted) */
    {
        FILE* f;
        double up = -1;
        f = fopen( "/proc/uptime", "r" );
        if( f )
        {
            if( fscanf( f, "%lf", &up ) != 1 )
                up = -1;
            fclose( f );
        }
        if( up >= 0 )
            cJSON_AddNumberToObject( node, "uptime_seconds", up );

        double load1 = -1;
        f = fopen( "/proc/loadavg", "r" );
        if( f )
        {
            if( fscanf( f, "%lf", &load1 ) != 1 )
                load1 = -1;
            fclose( f );
        }
        if( load1 >= 0 )
            cJSON_AddNumberToObject( node, "load_average_1m", load1 );

        long ncpu = sysconf( _SC_NPROCESSORS_ONLN );
        if( ncpu > 0 )
            cJSON_AddNumberToObject( node, "cpu_count", (double) ncpu );

        long mt = 0, ma = 0;
        f = fopen( "/proc/meminfo", "r" );
        if( f )
        {
            char ln[256];
            while( fgets( ln, sizeof( ln ), f ) )
            {
                sscanf( ln, "MemTotal: %ld kB", &mt );
                sscanf( ln, "MemAvailable: %ld kB", &ma );
            }
            fclose( f );
        }
        if( mt > 0 )
        {
            cJSON_AddNumberToObject( node, "memory_total_mb", (double) ( mt / 1024 ) );
            cJSON_AddNumberToObject( node, "memory_used_mb", (double) ( ( mt - ma ) / 1024 ) );
        }

        /* CPU temperature from thermal zones (millidegrees → °C) */
        {
            int cpu_temp_mdeg = -1;
            char tz_path[128];
            char tz_type[64];
            int zone;
            for( zone = 0; zone < 20 && cpu_temp_mdeg < 0; zone++ )
            {
                snprintf( tz_path, sizeof( tz_path ), "/sys/class/thermal/thermal_zone%d/type", zone );
                FILE* tf = fopen( tz_path, "r" );
                if( !tf )
                    break; /* no more zones */
                tz_type[0] = '\0';
                if( fscanf( tf, "%63s", tz_type ) != 1 )
                    tz_type[0] = '\0';
                fclose( tf );

                /* Accept common CPU-related zone types */
                if( strstr( tz_type, "x86_pkg" ) || strstr( tz_type, "coretemp" )
                    || strstr( tz_type, "cpu" ) || strstr( tz_type, "CPU" )
                    || strstr( tz_type, "acpitz" ) || strstr( tz_type, "soc" ) )
                {
                    snprintf( tz_path, sizeof( tz_path ), "/sys/class/thermal/thermal_zone%d/temp", zone );
                    FILE* vf = fopen( tz_path, "r" );
                    if( vf )
                    {
                        if( fscanf( vf, "%d", &cpu_temp_mdeg ) != 1 )
                            cpu_temp_mdeg = -1;
                        fclose( vf );
                    }
                }
            }
            if( cpu_temp_mdeg >= 0 )
                cJSON_AddNumberToObject( node, "cpu_temp_celsius", cpu_temp_mdeg / 1000 );
        }
    }

    cJSON_AddItemToObject( root, "node", node );

    /* ---- disks ---- */
    cJSON* disks = cJSON_CreateArray();
    int count = api_count_ptr ? *api_count_ptr : 0;
    wype_context_t** contexts = ( api_ctx_ptr && *api_ctx_ptr ) ? *api_ctx_ptr : NULL;

    for( int i = 0; i < count && contexts; i++ )
    {
        wype_context_t* c = contexts[i];
        if( !c )
            continue;

        /* Skip busy/boot devices — they cannot be wiped */
        if( c->device_busy )
            continue;

        cJSON* d = cJSON_CreateObject();

        /* identification */
        cJSON_AddStringToObject( d, "device_name", c->device_name ? c->device_name : "" );
        cJSON_AddStringToObject( d, "device_type", device_type_str( c->device_type ) );
        cJSON_AddStringToObject( d, "device_model", c->device_model ? c->device_model : "" );
        cJSON_AddStringToObject( d, "serial_number", c->device_serial_no );
        cJSON_AddBoolToObject( d, "is_ssd", c->device_is_ssd );
        cJSON_AddNumberToObject( d, "size_bytes", (double) c->device_size );
        cJSON_AddStringToObject( d, "size_text", c->device_size_txt );

        /* user metadata */
        cJSON_AddStringToObject( d, "hostname", c->device_hostname );
        cJSON_AddStringToObject( d, "inventory_number", c->inventory_number );

        /* selection */
        cJSON_AddBoolToObject( d, "selected",
                                c->select == WYPE_SELECT_TRUE || c->select == WYPE_SELECT_TRUE_PARENT );

        /* wipe status */
        cJSON_AddStringToObject( d, "wipe_status", wipe_status_str( c->wipe_status, c->wipe_status_txt ) );

        /* progress */
        cJSON_AddNumberToObject( d, "round_percent", c->round_percent );
        cJSON_AddNumberToObject( d, "round_working", c->round_working );
        cJSON_AddNumberToObject( d, "round_count", c->round_count );
        cJSON_AddNumberToObject( d, "pass_working", c->pass_working );
        cJSON_AddNumberToObject( d, "pass_count", c->pass_count );
        cJSON_AddStringToObject( d, "pass_type", pass_type_str( c->pass_type ) );

        /* throughput & ETA */
        cJSON_AddNumberToObject( d, "throughput", (double) c->throughput );
        cJSON_AddStringToObject( d, "throughput_text", c->throughput_txt );
        cJSON_AddNumberToObject( d, "eta_seconds", (double) c->eta );

        /* errors */
        cJSON* err = cJSON_CreateObject();
        cJSON_AddNumberToObject( err, "pass", (double) c->pass_errors );
        cJSON_AddNumberToObject( err, "verify", (double) c->verify_errors );
        cJSON_AddNumberToObject( err, "fsync", (double) c->fsyncdata_errors );
        cJSON_AddNumberToObject( err, "io_retries", (double) c->io_retries );
        cJSON_AddItemToObject( d, "errors", err );

        /* temperature – current reading (already °C, see temperature.c) */
        if( c->temp1_input != 1000000 && c->temp1_input != -1 )
            cJSON_AddNumberToObject( d, "temperature_celsius", c->temp1_input );
        else
            cJSON_AddNullToObject( d, "temperature_celsius" );

        /* temperature thresholds & history (all in °C, sentinel 1000000 = no data) */
        {
            cJSON* temps = cJSON_CreateObject();
            if( c->temp1_crit != 1000000 ) cJSON_AddNumberToObject( temps, "crit", c->temp1_crit );
            if( c->temp1_max != 1000000 ) cJSON_AddNumberToObject( temps, "max", c->temp1_max );
            if( c->temp1_min != 1000000 ) cJSON_AddNumberToObject( temps, "min", c->temp1_min );
            if( c->temp1_lcrit != 1000000 ) cJSON_AddNumberToObject( temps, "lcrit", c->temp1_lcrit );
            if( c->temp1_highest != 1000000 ) cJSON_AddNumberToObject( temps, "highest", c->temp1_highest );
            if( c->temp1_lowest != 1000000 ) cJSON_AddNumberToObject( temps, "lowest", c->temp1_lowest );
            if( c->temp1_monitored_wipe_max != 1000000 )
                cJSON_AddNumberToObject( temps, "wipe_max", c->temp1_monitored_wipe_max );
            if( c->temp1_monitored_wipe_min != 1000000 )
                cJSON_AddNumberToObject( temps, "wipe_min", c->temp1_monitored_wipe_min );
            if( c->temp1_monitored_wipe_avg != 1000000 )
                cJSON_AddNumberToObject( temps, "wipe_avg", c->temp1_monitored_wipe_avg );
            cJSON_AddItemToObject( d, "temperature_thresholds", temps );
        }

        /* HPA (Host Protected Area) */
        {
            cJSON* hpa = cJSON_CreateObject();
            cJSON_AddNumberToObject( hpa, "reported_set", (double) c->HPA_reported_set );
            cJSON_AddNumberToObject( hpa, "reported_real", (double) c->HPA_reported_real );
            if( c->HPA_sectors > 0 )
            {
                cJSON_AddNumberToObject( hpa, "sectors", (double) c->HPA_sectors );
                cJSON_AddStringToObject( hpa, "size_text", c->HPA_size_text );
            }
            cJSON_AddItemToObject( d, "hpa", hpa );
        }

        /* DCO (Device Configuration Overlay) */
        {
            cJSON* dco = cJSON_CreateObject();
            cJSON_AddNumberToObject( dco, "real_max_sectors", (double) c->DCO_reported_real_max_sectors );
            cJSON_AddNumberToObject( dco, "real_max_size", (double) c->DCO_reported_real_max_size );
            if( c->DCO_reported_real_max_size_text[0] != '\0' )
                cJSON_AddStringToObject( dco, "real_max_size_text", c->DCO_reported_real_max_size_text );
            if( c->Calculated_real_max_size_in_bytes > 0 )
            {
                cJSON_AddNumberToObject( dco, "calculated_real_max", (double) c->Calculated_real_max_size_in_bytes );
                cJSON_AddStringToObject( dco, "calculated_real_max_text", c->Calculated_real_max_size_in_bytes_text );
            }
            cJSON_AddItemToObject( d, "dco", dco );
        }

        /* I/O mode & direction */
        {
            const char* mode = "auto";
            if( c->io_mode == WYPE_IO_MODE_DIRECT ) mode = "direct";
            else if( c->io_mode == WYPE_IO_MODE_CACHED ) mode = "cached";
            cJSON_AddStringToObject( d, "io_mode", mode );

            cJSON_AddStringToObject( d, "io_direction",
                c->io_direction == WYPE_IO_DIRECTION_FORWARD ? "forward" : "reverse" );
        }

        /* sector sizes */
        cJSON_AddNumberToObject( d, "block_size", c->device_block_size );
        cJSON_AddNumberToObject( d, "physical_sector_size", c->device_phys_sector_size );

        /* bytes erased / duration */
        cJSON_AddNumberToObject( d, "bytes_erased", (double) c->bytes_erased );
        cJSON_AddNumberToObject( d, "duration_seconds", c->duration );

        /* result */
        cJSON_AddNumberToObject( d, "result", c->result );
        cJSON_AddStringToObject( d, "result_message", c->result_message );

        /* transient flags */
        cJSON_AddBoolToObject( d, "syncing", c->sync_status != 0 );
        cJSON_AddBoolToObject( d, "retrying", c->retry_status != 0 );

        cJSON_AddItemToArray( disks, d );
    }

    cJSON_AddItemToObject( root, "disks", disks );
    return root;
}

/* ------------------------------------------------------------------ */
/*  MHD request handler                                                */
/* ------------------------------------------------------------------ */

static enum MHD_Result handle_request( void* cls,
                                        struct MHD_Connection* connection,
                                        const char* url,
                                        const char* method,
                                        const char* http_version,
                                        const char* upload_data,
                                        size_t* upload_data_size,
                                        void** con_cls )
{
    (void) cls;
    (void) http_version;
    (void) upload_data;
    (void) upload_data_size;
    (void) con_cls;

    /* CORS preflight */
    if( strcmp( method, "OPTIONS" ) == 0 )
    {
        struct MHD_Response* r = MHD_create_response_from_buffer( 0, "", MHD_RESPMEM_PERSISTENT );
        MHD_add_response_header( r, "Access-Control-Allow-Origin", "*" );
        MHD_add_response_header( r, "Access-Control-Allow-Headers", "X-API-Key, Content-Type" );
        MHD_add_response_header( r, "Access-Control-Allow-Methods", "GET, OPTIONS" );
        MHD_add_response_header( r, "Access-Control-Max-Age", "86400" );
        enum MHD_Result ret = MHD_queue_response( connection, MHD_HTTP_NO_CONTENT, r );
        MHD_destroy_response( r );
        return ret;
    }

    /* Only GET */
    if( strcmp( method, "GET" ) != 0 )
    {
        cJSON* j = cJSON_CreateObject();
        cJSON_AddStringToObject( j, "error", "Method not allowed" );
        return send_json( connection, MHD_HTTP_METHOD_NOT_ALLOWED, j );
    }

    /* Auth */
    if( !check_auth( connection ) )
        return send_unauthorized( connection );

    /* Routes */
    if( strcmp( url, "/api/v1/health" ) == 0 )
    {
        cJSON* j = cJSON_CreateObject();
        cJSON_AddBoolToObject( j, "ok", 1 );
        cJSON_AddStringToObject( j, "version", version_string );
        return send_json( connection, MHD_HTTP_OK, j );
    }

    if( strcmp( url, "/api/v1/status" ) == 0 )
        return send_json( connection, MHD_HTTP_OK, build_status_json() );

    /* 404 */
    cJSON* j = cJSON_CreateObject();
    cJSON_AddStringToObject( j, "error", "Not found" );
    return send_json( connection, MHD_HTTP_NOT_FOUND, j );
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int wype_api_server_start( wype_context_t*** contexts_ptr,
                            int* count_ptr,
                            wype_misc_thread_data_t* misc,
                            int port,
                            const char* password )
{
    if( api_daemon )
    {
        wype_log( WYPE_LOG_WARNING, "API server already running" );
        return 0;
    }

    /* A password is mandatory */
    if( password == NULL || password[0] == '\0' )
    {
        wype_log( WYPE_LOG_INFO, "Dashboard API password not configured — API server disabled" );
        return -1;
    }

    api_ctx_ptr = contexts_ptr;
    api_count_ptr = count_ptr;
    api_misc = misc;

    strncpy( api_password, password, sizeof( api_password ) - 1 );
    api_password[sizeof( api_password ) - 1] = '\0';
    api_has_password = 1;

    if( port <= 0 )
        port = API_DEFAULT_PORT;

    api_daemon = MHD_start_daemon( MHD_USE_SELECT_INTERNALLY,
                                    (uint16_t) port,
                                    NULL,
                                    NULL,
                                    &handle_request,
                                    NULL,
                                    MHD_OPTION_CONNECTION_LIMIT,
                                    (unsigned int) 8,
                                    MHD_OPTION_END );
    if( !api_daemon )
    {
        wype_log( WYPE_LOG_ERROR, "Failed to start API server on port %d", port );
        return -1;
    }

    wype_log( WYPE_LOG_NOTICE, "Dashboard API server listening on port %d", port );
    return 0;
}

void wype_api_server_stop( void )
{
    if( api_daemon )
    {
        MHD_stop_daemon( api_daemon );
        api_daemon = NULL;
        wype_log( WYPE_LOG_NOTICE, "Dashboard API server stopped" );
    }
}

int wype_api_server_is_running( void )
{
    return api_daemon != NULL ? 1 : 0;
}

#else /* !HAVE_API_SERVER — stub implementations */

int wype_api_server_start( wype_context_t*** contexts_ptr,
                            int* count_ptr,
                            wype_misc_thread_data_t* misc,
                            int port,
                            const char* password )
{
    (void) contexts_ptr;
    (void) count_ptr;
    (void) misc;
    (void) port;
    (void) password;
    return -1;
}

void wype_api_server_stop( void )
{
}

int wype_api_server_is_running( void )
{
    return 0;
}

#endif /* HAVE_API_SERVER */
