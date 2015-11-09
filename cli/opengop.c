/*****************************************************************************
 * opengop.c:
 *****************************************************************************
 * Copyright (C) 2010-2015 L-SMASH project
 *
 * Authors: Can Bal<canbal@gmail.com>
 *          Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *****************************************************************************/

/* This file is available under an ISC license. */

#include "cli.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#define eprintf( ... ) fprintf( stderr, __VA_ARGS__ )

static void display_version( void )
{
    eprintf( "\n"
             "Open GOP detector%s  %s\n"
             "Built on %s %s\n"
             "Copyright (C) 2010-2015 L-SMASH project\n",
             LSMASH_REV, LSMASH_GIT_HASH, __DATE__, __TIME__ );
}

static void display_help( void )
{
    display_version();
    eprintf( "\n"
             "Usage: opengop [option] input\n"
             "  options:\n"
             "    --help         Display help\n"
             "    --version      Display version information\n" );
}

static int opengop_error
(
    lsmash_root_t            *root,
    lsmash_file_parameters_t *file_param,
    const char               *message
)
{
    lsmash_close_file( file_param );
    lsmash_destroy_root( root );
    eprintf( "%s", message );
    return -1;
}

static int opengop_done
(
    lsmash_root_t            *root,
    lsmash_file_parameters_t *file_param,
    const char               *message
)
{
    lsmash_close_file( file_param );
    lsmash_destroy_root( root );
    printf( "%s", message );
    return 0;
}


#define OPENGOP_ERR( message ) opengop_error( root, &file_param, message )
#define OPENGOP_DONE( message ) opengop_done( root, &file_param, message )
#define DO_NOTHING
#define IDR 5

static int is_idr
(
     lsmash_sample_t          *sample,
     uint32_t                  nalu_length_size
)
{
    if( !sample || !sample->data || sample->length <= nalu_length_size )
    return -1;
    uint8_t *data = sample->data;
    uint32_t remaining = sample->length;
    while( remaining )
    {
    if( remaining <= nalu_length_size )
        return -1;
    uint8_t nal_type = data[nalu_length_size] & 0x1F;
    if( nal_type == IDR )
        return 1;
    fprintf( stdout, "nal_type = %d\n", nal_type );
    uint32_t nal_size = 0;
    for( uint32_t i = 0; i < nalu_length_size; i++ )
    {
        nal_size = nal_size << 8;
        nal_size += data[i];
    }
    nal_size += nalu_length_size;
    if( remaining < nal_size )
        return -1;
    remaining -= nal_size;
    data += nal_size;
    }
    return 0;
}

int main( int argc, char *argv[] )
{
    if ( argc < 2 )
    {
        display_help();
        return -1;
    }
    else if( !strcasecmp( argv[1], "-h" ) || !strcasecmp( argv[1], "--help" ) )
    {
        display_help();
        return 0;
    }
    else if( !strcasecmp( argv[1], "-v" ) || !strcasecmp( argv[1], "--version" ) )
    {
        display_version();
        return 0;
    }
    char *filename;
    lsmash_get_mainargs( &argc, &argv );
    if( argc > 2 )
    {
        display_help();
        return -1;
    }
    else
    {
        filename = argv[1];
    }
    /* Open the input file. */
    lsmash_root_t *root = lsmash_create_root();
    if( !root )
    {
        fprintf( stderr, "Failed to create a ROOT.\n" );
        return -1;
    }
    lsmash_file_parameters_t file_param = { 0 };
    if( lsmash_open_file( filename, 1, &file_param ) < 0 )
        return OPENGOP_ERR( "Failed to open an input file.\n" );
    lsmash_file_t *file = lsmash_set_file( root, &file_param );
    if( !file )
        return OPENGOP_ERR( "Failed to add a file into a ROOT.\n" );
    if( lsmash_read_file( file, &file_param ) < 0 )
        return OPENGOP_ERR( "Failed to read a file\n" );
    /* Check for Open GOP */
    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    lsmash_get_movie_parameters( root, &movie_param );

    uint32_t has_video = 0;
    uint32_t num_tracks = movie_param.number_of_tracks;
    for( uint32_t track_number = 1; track_number <= num_tracks; track_number++ )
    {
        /* Scan through tracks until first video track */
        uint32_t track_ID = lsmash_get_track_ID( root, track_number );
        if( !track_ID )
            return OPENGOP_ERR( "Failed to get track_ID.\n" );
        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        if( lsmash_get_media_parameters( root, track_ID, &media_param ) )
            return OPENGOP_ERR( "Failed to get media parameters.\n" );
        if( media_param.handler_type != ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK )
            continue;
        has_video = 1;
        
        /* Get Nal Unit Length Size */
        lsmash_summary_t *summary = lsmash_get_summary( root, track_ID, 1);
        if( !summary )
            return OPENGOP_ERR( "Failed to get video summary.\n" );
        int count = lsmash_count_codec_specific_data( summary );
        uint32_t nalu_length_size = 0;
        for ( uint32_t i = 1; i <= count; i++ )
        {
            lsmash_codec_specific_t* cs = lsmash_get_codec_specific_data( summary, i );
            if( cs &&
               cs->format == LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED &&
               cs->size > 12 &&
               cs->data.unstructured[4] == 'a' &&
               cs->data.unstructured[5] == 'v' &&
               cs->data.unstructured[6] == 'c' &&
               cs->data.unstructured[7] == 'C' )
                nalu_length_size = (cs->data.unstructured[12] & 0x03) + 1;
        }
        lsmash_cleanup_summary( summary );
        if( !nalu_length_size )
            return OPENGOP_ERR( "Failed to get nal unit length size.\n" );
        
        /* Scan video track for Open GOP(s) */
        if( lsmash_construct_timeline( root, track_ID ) )
            return OPENGOP_ERR( "Failed to construct video timeline.\n" );
        uint32_t timeline_shift;
        if( lsmash_get_composition_to_decode_shift_from_media_timeline( root, track_ID, &timeline_shift ) )
            return OPENGOP_ERR( "Failed to get frame timestamps.\n" );
        lsmash_media_ts_list_t ts_list;
        if( lsmash_get_media_timestamps( root, track_ID, &ts_list ) )
            return OPENGOP_ERR( "Failed to get frame timestamps.\n" );
        lsmash_media_ts_t *ts_array = ts_list.timestamp;
        if( !ts_array )
            return OPENGOP_ERR( "Video track does not have any frames.\n" );
        fprintf( stdout, "Started scanning %d frames for Open GOPs\n", ts_list.sample_count );
        for( uint32_t i = 1; i <= ts_list.sample_count; i++ )
        {
            lsmash_sample_property_t sample_property;
            lsmash_get_sample_property_from_media_timeline( root, track_ID, i, &sample_property );
            if( sample_property.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC )
            {
                fprintf( stdout, "Frame %d is a keyframe. Checking if IDR? ", i );
                lsmash_sample_t *sample = lsmash_get_sample_from_media_timeline( root, track_ID, i );
                int ret = is_idr( sample, nalu_length_size );
                if( ret == -1 )
                {
                    eprintf( "Failed to read frame %d.\n", i );
                }
                else if( !ret )
                {
                    fprintf( stdout, "not IDR\n" );
                    return OPENGOP_DONE( "Video contains Open GOP(s).\n" );
                }
                else
                {
                    fprintf( stdout, "IDR\n" );
                }
                lsmash_delete_sample( sample );
            }
        }
        lsmash_free( ts_array );
        break;
    }
    if( !has_video )
        return OPENGOP_ERR( "File does not contain a video track.\n" );
    fprintf( stdout, "Video does not contain an Open GOP.\n" );
    lsmash_destroy_root( root );
    return 0;
}
