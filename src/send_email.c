/*
 *  send_email.c: Send PDF certificate via email after wipe completion (BKR)
 *
 *  Implements a minimal raw SMTP client (no auth, no TLS) for sending
 *  PDF certificates as email attachments on internal networks.
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 2.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <libconfig.h>

#include "nwipe.h"
#include "context.h"
#include "logging.h"
#include "conf.h"
#include "send_email.h"

/* Base64 encoding table */
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * Base64 encode a block of data.
 * Caller must free() the returned string.
 */
static char* base64_encode( const unsigned char* data, size_t input_length, size_t* output_length )
{
    *output_length = 4 * ( ( input_length + 2 ) / 3 );

    /* Add space for line breaks every 76 chars (MIME requirement) and null terminator */
    size_t line_breaks = *output_length / 76;
    char* encoded = malloc( *output_length + line_breaks * 2 + 1 );
    if( !encoded )
        return NULL;

    size_t i, j, col;
    for( i = 0, j = 0, col = 0; i < input_length; )
    {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        uint32_t triple = ( octet_a << 16 ) | ( octet_b << 8 ) | octet_c;

        encoded[j++] = b64_table[( triple >> 18 ) & 0x3F];
        col++;
        encoded[j++] = b64_table[( triple >> 12 ) & 0x3F];
        col++;
        encoded[j++] = b64_table[( triple >> 6 ) & 0x3F];
        col++;
        encoded[j++] = b64_table[triple & 0x3F];
        col++;

        if( col >= 76 )
        {
            encoded[j++] = '\r';
            encoded[j++] = '\n';
            col = 0;
        }
    }

    /* Add padding */
    size_t mod = input_length % 3;
    if( mod == 1 )
    {
        encoded[j - 1] = '=';
        encoded[j - 2] = '=';
    }
    else if( mod == 2 )
    {
        encoded[j - 1] = '=';
    }

    encoded[j] = '\0';
    *output_length = j;
    return encoded;
}

/**
 * Read SMTP response and check status code.
 * Returns 0 if response starts with expected code, -1 otherwise.
 */
static int smtp_check_response( int sockfd, const char* expected_code )
{
    char response[1024];
    ssize_t n = recv( sockfd, response, sizeof( response ) - 1, 0 );
    if( n <= 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Email: No response from SMTP server" );
        return -1;
    }
    response[n] = '\0';

    if( strncmp( response, expected_code, strlen( expected_code ) ) != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Email: Unexpected SMTP response: %s", response );
        return -1;
    }
    return 0;
}

/**
 * Send a string over the socket.
 */
static int smtp_send( int sockfd, const char* data, size_t len )
{
    ssize_t sent = send( sockfd, data, len, 0 );
    if( sent < 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Email: Failed to send data: %s", strerror( errno ) );
        return -1;
    }
    return 0;
}

/**
 * Send a null-terminated string.
 */
static int smtp_send_str( int sockfd, const char* str )
{
    return smtp_send( sockfd, str, strlen( str ) );
}

/**
 * Extract basename from a file path.
 */
static const char* get_basename( const char* path )
{
    const char* p = strrchr( path, '/' );
    return p ? p + 1 : path;
}

int nwipe_send_email( nwipe_context_t* c )
{
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];
    config_setting_t* setting;

    const char* email_enable = NULL;
    const char* smtp_server = NULL;
    const char* smtp_port_str = NULL;
    const char* sender = NULL;
    const char* recipient = NULL;

    /* Read email settings from nwipe.conf */
    setting = config_lookup( &nwipe_cfg, "Email_Settings" );
    if( setting == NULL )
    {
        nwipe_log( NWIPE_LOG_WARNING, "Email: Cannot locate [Email_Settings] in %s", nwipe_config_file );
        return -1;
    }

    config_setting_lookup_string( setting, "Email_Enable", &email_enable );
    if( email_enable == NULL || strcasecmp( email_enable, "ENABLED" ) != 0 )
    {
        return -1;  /* Email disabled, silently skip */
    }

    config_setting_lookup_string( setting, "SMTP_Server", &smtp_server );
    config_setting_lookup_string( setting, "SMTP_Port", &smtp_port_str );
    config_setting_lookup_string( setting, "Sender_Address", &sender );
    config_setting_lookup_string( setting, "Recipient_Address", &recipient );

    if( !smtp_server || !sender || !recipient || strlen( recipient ) == 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Email: Missing SMTP configuration (server/sender/recipient)" );
        return -1;
    }

    /* Check PDF file exists */
    if( c->PDF_filename[0] == '\0' )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Email: No PDF filename available for %s", c->device_name );
        return -1;
    }

    FILE* pdf_file = fopen( c->PDF_filename, "rb" );
    if( !pdf_file )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Email: Cannot open PDF file %s: %s", c->PDF_filename, strerror( errno ) );
        return -1;
    }

    /* Read the entire PDF file */
    fseek( pdf_file, 0, SEEK_END );
    long pdf_size = ftell( pdf_file );
    fseek( pdf_file, 0, SEEK_SET );

    unsigned char* pdf_data = malloc( pdf_size );
    if( !pdf_data )
    {
        fclose( pdf_file );
        nwipe_log( NWIPE_LOG_ERROR, "Email: Failed to allocate memory for PDF (%ld bytes)", pdf_size );
        return -1;
    }

    if( fread( pdf_data, 1, pdf_size, pdf_file ) != (size_t) pdf_size )
    {
        free( pdf_data );
        fclose( pdf_file );
        nwipe_log( NWIPE_LOG_ERROR, "Email: Failed to read PDF file %s", c->PDF_filename );
        return -1;
    }
    fclose( pdf_file );

    /* Base64 encode the PDF */
    size_t b64_len;
    char* b64_data = base64_encode( pdf_data, pdf_size, &b64_len );
    free( pdf_data );

    if( !b64_data )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Email: Failed to base64 encode PDF" );
        return -1;
    }

    /* Resolve SMTP server */
    int port = smtp_port_str ? atoi( smtp_port_str ) : 25;

    struct addrinfo hints, *res;
    memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf( port_str, sizeof( port_str ), "%d", port );

    int gai_err = getaddrinfo( smtp_server, port_str, &hints, &res );
    if( gai_err != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Email: Cannot resolve SMTP server %s: %s", smtp_server, gai_strerror( gai_err ) );
        free( b64_data );
        return -1;
    }

    /* Connect */
    int sockfd = socket( res->ai_family, res->ai_socktype, res->ai_protocol );
    if( sockfd < 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Email: Socket creation failed: %s", strerror( errno ) );
        freeaddrinfo( res );
        free( b64_data );
        return -1;
    }

    /* Set socket timeout (10 seconds) */
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt( sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof( tv ) );
    setsockopt( sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof( tv ) );

    if( connect( sockfd, res->ai_addr, res->ai_addrlen ) < 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Email: Cannot connect to %s:%d: %s", smtp_server, port, strerror( errno ) );
        close( sockfd );
        freeaddrinfo( res );
        free( b64_data );
        return -1;
    }
    freeaddrinfo( res );

    nwipe_log( NWIPE_LOG_INFO, "Email: Connected to SMTP server %s:%d", smtp_server, port );

    int result = -1;
    char cmd[512];
    const char* pdf_basename = get_basename( c->PDF_filename );

    /* SMTP conversation */
    do
    {
        /* Greeting */
        if( smtp_check_response( sockfd, "220" ) != 0 )
            break;

        /* EHLO */
        snprintf( cmd, sizeof( cmd ), "EHLO nwipe\r\n" );
        if( smtp_send_str( sockfd, cmd ) != 0 )
            break;
        if( smtp_check_response( sockfd, "250" ) != 0 )
            break;

        /* MAIL FROM */
        snprintf( cmd, sizeof( cmd ), "MAIL FROM:<%s>\r\n", sender );
        if( smtp_send_str( sockfd, cmd ) != 0 )
            break;
        if( smtp_check_response( sockfd, "250" ) != 0 )
            break;

        /* RCPT TO */
        snprintf( cmd, sizeof( cmd ), "RCPT TO:<%s>\r\n", recipient );
        if( smtp_send_str( sockfd, cmd ) != 0 )
            break;
        if( smtp_check_response( sockfd, "250" ) != 0 )
            break;

        /* DATA */
        if( smtp_send_str( sockfd, "DATA\r\n" ) != 0 )
            break;
        if( smtp_check_response( sockfd, "354" ) != 0 )
            break;

        /* Build and send MIME message */
        char header[2048];
        time_t now = time( NULL );
        struct tm* tm_info = localtime( &now );
        char date_str[64];
        strftime( date_str, sizeof( date_str ), "%a, %d %b %Y %H:%M:%S %z", tm_info );

        snprintf( header,
                  sizeof( header ),
                  "From: <%s>\r\n"
                  "To: <%s>\r\n"
                  "Date: %s\r\n"
                  "Subject: Nwipe Erasure Certificate - %s - %s [%s]\r\n"
                  "MIME-Version: 1.0\r\n"
                  "Content-Type: multipart/mixed; boundary=\"nwipe-cert-boundary\"\r\n"
                  "\r\n"
                  "--nwipe-cert-boundary\r\n"
                  "Content-Type: text/plain; charset=utf-8\r\n"
                  "\r\n"
                  "Disk erasure certificate for %s (Serial: %s).\r\n"
                  "Status: %s\r\n"
                  "\r\n"
                  "--nwipe-cert-boundary\r\n"
                  "Content-Type: application/pdf\r\n"
                  "Content-Disposition: attachment; filename=\"%s\"\r\n"
                  "Content-Transfer-Encoding: base64\r\n"
                  "\r\n",
                  sender,
                  recipient,
                  date_str,
                  c->device_model ? c->device_model : "Unknown",
                  c->device_serial_no,
                  c->wipe_status_txt,
                  c->device_model ? c->device_model : "Unknown",
                  c->device_serial_no,
                  c->wipe_status_txt,
                  pdf_basename );

        if( smtp_send_str( sockfd, header ) != 0 )
            break;

        /* Send base64 encoded PDF in chunks */
        size_t offset = 0;
        size_t chunk_size = 4096;
        while( offset < b64_len )
        {
            size_t remaining = b64_len - offset;
            size_t to_send = remaining < chunk_size ? remaining : chunk_size;
            if( smtp_send( sockfd, b64_data + offset, to_send ) != 0 )
                break;
            offset += to_send;
        }
        if( offset < b64_len )
            break;

        /* End MIME and DATA */
        if( smtp_send_str( sockfd, "\r\n--nwipe-cert-boundary--\r\n.\r\n" ) != 0 )
            break;
        if( smtp_check_response( sockfd, "250" ) != 0 )
            break;

        /* QUIT */
        smtp_send_str( sockfd, "QUIT\r\n" );

        result = 0;
        nwipe_log( NWIPE_LOG_INFO,
                   "Email: Certificate sent successfully to %s for %s (Serial: %s)",
                   recipient,
                   c->device_model ? c->device_model : "Unknown",
                   c->device_serial_no );

    } while( 0 );

    if( result != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "Email: Failed to send certificate for %s (Serial: %s)",
                   c->device_model ? c->device_model : "Unknown",
                   c->device_serial_no );
    }

    close( sockfd );
    free( b64_data );
    return result;
}
