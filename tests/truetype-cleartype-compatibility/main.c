/* Public-API regression tests for the legacy ClearType rules in v40.
 *
 * Copyright (C) 2026 by David Turner, Robert Wilhelm, and Werner Lemberg.
 * This file is part of the FreeType project, and may only be used,
 * modified, and distributed under the terms of the FreeType project
 * license, LICENSE.TXT.
 *
 * All fonts are constructed in memory.  Bytecode assertions expose VM
 * results even when MINIMAL deliberately suppresses the corresponding
 * point movement.  An assertion failure executes DIV by zero; every
 * ordinary case must load successfully with FT_LOAD_PEDANTIC.
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_CONFIG_OPTIONS_H
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_DRIVER_H
#include FT_SYSTEM_H

typedef struct  Program_
{
  unsigned char  bytes[2048];
  unsigned       length;
} Program;

static unsigned char  font[16384];
static unsigned       font_size;
static unsigned       table_count;
static int            failures;
static unsigned       cases;
static const char*    current_case;
static const char*    export_directory;
static FILE*          export_manifest;
static int            reference_case;
static int            first_glyph_error;
static unsigned       export_version = 40;
static FT_Int32       export_mode = FT_LOAD_TARGET_LCD;
static int            export_tricky;

static void
check( const char* what, long actual, long expected )
{
  if ( actual != expected )
  {
    fprintf( stderr, "%s: %s: got %ld, expected %ld\n",
             current_case, what, actual, expected );
    failures++;
  }
}

static void
op( Program* p, unsigned byte )
{
  if ( p->length >= sizeof ( p->bytes ) )
  {
    fputs( "test bytecode buffer exhausted\n", stderr );
    exit( 1 );
  }
  p->bytes[p->length++] = (unsigned char)byte;
}

static void
push( Program* p, int value )
{
  if ( value >= 0 && value <= 255 )
  {
    op( p, 0xB0 );
    op( p, (unsigned)value );
  }
  else
  {
    op( p, 0xB8 );
    op( p, (unsigned)value >> 8 );
    op( p, (unsigned)value );
  }
}

/* Assert the top stack value without depending on outline movement. */
static void
expect( Program* p, int value )
{
  push( p, value );
  op( p, 0x55 );                 /* NEQ */
  op( p, 0x58 );                 /* IF */
  push( p, 1 );
  push( p, 0 );
  op( p, 0x62 );                 /* DIV: intentional assertion failure */
  op( p, 0x21 );
  op( p, 0x59 );                 /* EIF */
}

static void
write_store( Program* p, unsigned slot, int value )
{
  push( p, (int)slot );
  push( p, value );
  op( p, 0x42 );
}

static void
expect_store( Program* p, unsigned slot, int value )
{
  push( p, (int)slot );
  op( p, 0x43 );
  expect( p, value );
}

static void
begin_function( Program* p, unsigned id )
{
  push( p, (int)id );
  op( p, 0x2C );
}

static void
call( Program* p, unsigned id )
{
  push( p, (int)id );
  op( p, 0x2B );
}

static void
loopcall( Program* p, unsigned id, unsigned count )
{
  push( p, (int)count );
  push( p, (int)id );
  op( p, 0x2A );
}

static void
point_y( Program* p, unsigned point, int value )
{
  op( p, 0x00 );                 /* SVTCA[y] */
  push( p, (int)point );
  push( p, value );
  op( p, 0x48 );                 /* SCFS */
}

static void
expect_point( Program* p, unsigned point, int value, int x_axis )
{
  op( p, x_axis ? 0x01 : 0x00 );
  push( p, (int)point );
  op( p, 0x46 );                 /* GC[current] */
  expect( p, value );
}

static void
negative_y( Program* p )
{
  push( p, 0 );
  push( p, -16384 );
  op( p, 0x0A );                 /* SPVFS */
  op( p, 0x0E );                 /* SFVTPV */
}

static void
stroke_prefix( Program* p, unsigned id, unsigned slot, int spacing_round )
{
  begin_function( p, id );
  if ( slot == 24 )
  {
    op( p, 0x01 );
    if ( spacing_round )
      op( p, 0x18 );
  }
  push( p, (int)slot );
  op( p, 0x43 );
  op( p, 0x58 );
  op( p, 0x59 );                 /* empty guard; continue after it */
}

static void
delta_definition( Program* p, unsigned id, int range )
{
  static const unsigned char single[] =
    { 0x4B, 0x54, 0x58, 0x38, 0x1B, 0x21, 0x21, 0x59, 0x2D };
  static const unsigned char interval[] =
    { 0x4B, 0x53, 0x23, 0x4B, 0x51, 0x5A, 0x58,
      0x38, 0x1B, 0x21, 0x21, 0x59, 0x2D };
  const unsigned char* signature = range ? interval : single;
  unsigned length = range ? sizeof ( interval ) : sizeof ( single );
  unsigned i;

  begin_function( p, id );
  for ( i = 0; i < length; i++ )
    op( p, signature[i] );
}

/* MINIMAL allows this Y shift; legacy delta filtering rejects its +X PV. */
static void
delta_witness( Program* p )
{
  op( p, 0x00 );
  push( p, 0 );
  op( p, 0x2E );                 /* MDAP[unrounded]: touch Y */
  op( p, 0x03 );                 /* SPVTCA[x], retaining freedom +Y */
}

static void
shift( Program* p )
{
  push( p, 0 );
  push( p, 8 );
  op( p, 0x38 );
}

static void
vacu_definition( Program* p )
{
  static const unsigned char signature[] =
    { 0x45, 0x23, 0x46, 0x60, 0x20, 0xB0, 0x26, 0x2D };
  unsigned i;
  begin_function( p, 0 );
  for ( i = 0; i < sizeof ( signature ); i++ )
    op( p, signature[i] );
}

static void
vacu_geometry( Program* p )
{
  point_y( p, 0, 13 );
  point_y( p, 2, 77 );
}

static void
expect_distance( Program* p, int distance )
{
  op( p, 0x00 );
  push( p, 2 );
  push( p, 0 );
  op( p, 0x49 );                 /* MD[current] */
  expect( p, distance );
}

static void
put16( unsigned char* p, unsigned value )
{
  p[0] = (unsigned char)( value >> 8 );
  p[1] = (unsigned char)value;
}

static void
put32( unsigned char* p, unsigned value )
{
  put16( p, value >> 16 );
  put16( p + 2, value );
}

static unsigned char*
table( const char* tag, unsigned size )
{
  unsigned char* entry = font + 12 + 16 * table_count++;
  unsigned char* data = font + font_size;
  if ( font_size + size + 3 > sizeof ( font ) )
    exit( 1 );
  memcpy( entry, tag, 4 );
  put32( entry + 8, font_size );
  put32( entry + 12, size );
  font_size += ( size + 3 ) & ~3U;
  return data;
}

static unsigned
glyph_size( const Program* p )
{
  return ( 29 + p->length + 1 ) & ~1U;
}

static void
write_glyph( unsigned char* p, const Program* program )
{
  put16( p, 1 );
  put16( p + 2, 13 );
  put16( p + 4, 13 );
  put16( p + 6, 113 );
  put16( p + 8, 113 );
  put16( p + 10, 2 );
  put16( p + 12, program->length );
  memcpy( p + 14, program->bytes, program->length );
  p += 14 + program->length;
  p[0] = p[1] = p[2] = 1;
  put16( p + 3, 13 );
  put16( p + 5, 100 );
  put16( p + 7, (unsigned)-100 );
  put16( p + 9, 13 );
  put16( p + 11, 0 );
  put16( p + 13, 100 );
}

static void
export_font( void )
{
  char path[4096];
  char filename[256];
  unsigned n, i;
  FILE* file;
  if ( !export_directory ) return;
  n = (unsigned)snprintf( filename, sizeof ( filename ), "%03u-", cases );
  for ( i = 0; current_case[i] && n + 5 < sizeof ( filename ); i++ )
  {
    unsigned char c = (unsigned char)current_case[i];
    filename[n++] = (char)( ( c >= 'a' && c <= 'z' ) ||
                            ( c >= 'A' && c <= 'Z' ) ||
                            ( c >= '0' && c <= '9' ) ? c : '-' );
  }
  memcpy( filename + n, ".ttf", 5 );
  if ( snprintf( path, sizeof ( path ), "%s/%s", export_directory, filename ) >=
       (int)sizeof ( path ) ) exit( 1 );
  file = fopen( path, "wb" );
  if ( !file || fwrite( font, 1, font_size, file ) != font_size )
  {
    fprintf( stderr, "Cannot export fixture %s\n", path );
    if ( file ) fclose( file );
    exit( 1 );
  }
  fclose( file );
  fprintf( export_manifest, "%s\t%d\t%u\t%ld\t%d\t%d\t0\t%s\n",
           filename, reference_case, export_version,
           (long)export_mode, export_tricky, first_glyph_error, current_case );
}

static void
make_font( const Program* first, const Program* second,
           const Program* prep, const Program* fpgm )
{
  unsigned char* p;
  unsigned first_size = glyph_size( first );
  unsigned second_size = glyph_size( second );
  unsigned longest = first->length > second->length
                       ? first->length : second->length;

  memset( font, 0, sizeof ( font ) );
  table_count = 0;
  font_size = 12 + 11 * 16;
  put32( font, 0x10000 );
  put16( font + 4, 11 );
  put16( font + 6, 128 );
  put16( font + 8, 3 );
  put16( font + 10, 48 );
  p = table( "head", 54 );
  put32( p, 0x10000 );
  put32( p + 12, 0x5F0F3CF5 );
  put16( p + 18, 1024 );
  put16( p + 36, 13 );
  put16( p + 38, 13 );
  put16( p + 40, 113 );
  put16( p + 42, 113 );
  put16( p + 46, 1 );
  p = table( "hhea", 36 );
  put32( p, 0x10000 );
  put16( p + 4, 800 );
  put16( p + 6, (unsigned)-200 );
  put16( p + 10, 333 );
  put16( p + 34, 1 );
  p = table( "maxp", 32 );
  put32( p, 0x10000 );
  put16( p + 4, 2 );
  put16( p + 6, 3 );
  put16( p + 8, 1 );
  put16( p + 14, 2 );
  put16( p + 16, 4 );
  put16( p + 18, 128 );
  put16( p + 20, 128 );
  put16( p + 24, 128 );
  put16( p + 26, longest );
  p = table( "hmtx", 6 );
  put16( p, 333 );
  put16( p + 2, 13 );
  put16( p + 4, 13 );
  p = table( "loca", 6 );
  put16( p + 2, first_size / 2 );
  put16( p + 4, ( first_size + second_size ) / 2 );
  p = table( "glyf", first_size + second_size );
  write_glyph( p, first );
  write_glyph( p + first_size, second );
  p = table( "cvt ", 2 );
  put16( p, 65 );
  p = table( "prep", prep->length );
  memcpy( p, prep->bytes, prep->length );
  p = table( "fpgm", fpgm->length );
  memcpy( p, fpgm->bytes, fpgm->length );
  p = table( "gasp", 8 );
  put16( p, 1 );
  put16( p + 2, 1 );
  put16( p + 4, 0xFFFF );
  put16( p + 6, 4 );
  p = table( "hdmx", 12 );
  put16( p + 2, 1 );
  put32( p + 4, 4 );
  p[8] = 16;
  p[9] = p[10] = p[11] = 99;
  export_font();
}

static FT_Face
open_font( FT_Library library, unsigned version, int tricky )
{
  FT_Face face = NULL;
  FT_Error error;
  check( "select interpreter", FT_Property_Set( library, "truetype",
                                                "interpreter-version",
                                                &version ), 0 );
  error = FT_New_Memory_Face( library, font, font_size, 0, &face );
  check( "open font", error, 0 );
  if ( !error )
  {
    check( "set size", FT_Set_Pixel_Sizes( face, 0, 16 ), 0 );
    if ( tricky )
      face->face_flags |= FT_FACE_FLAG_TRICKY;
  }
  return face;
}

static FT_Error
load( FT_Face face, unsigned glyph, FT_Int32 mode )
{
  return FT_Load_Glyph( face, glyph, mode | FT_LOAD_NO_BITMAP |
                       FT_LOAD_NO_AUTOHINT | FT_LOAD_PEDANTIC );
}

static void
run_mode( FT_Library library, const char* name, const Program* glyph,
          const Program* prep, const Program* fpgm,
          unsigned version, FT_Int32 mode, int tricky )
{
  FT_Face face;
  current_case = name;
  cases++;
  export_version = version;
  export_mode = mode;
  export_tricky = tricky;
  make_font( glyph, glyph, prep, fpgm );
  face = open_font( library, version, tricky );
  if ( face )
  {
    check( "load/assert bytecode", load( face, 0, mode ), 0 );
    FT_Done_Face( face );
  }
  export_version = 40;
  export_mode = FT_LOAD_TARGET_LCD;
  export_tricky = 0;
}

static void
run( FT_Library library, const char* name, const Program* glyph,
     const Program* prep, const Program* fpgm )
{
  run_mode( library, name, glyph, prep, fpgm,
            40, FT_LOAD_TARGET_LCD, 0 );
}

static void
test_minimal( FT_Library library )
{
  Program p = { { 0 }, 0 }, empty = { { 0 }, 0 }, native = { { 0 }, 0 };
  op( &p, 0x01 );
  push( &p, 0 ); push( &p, 80 ); op( &p, 0x48 );
  expect_point( &p, 0, 13, 1 );
  shift( &p );
  expect_point( &p, 0, 13, 1 );
  op( &p, 0x30 ); op( &p, 0x31 );
  point_y( &p, 0, 80 );
  expect_point( &p, 0, 13, 0 );
  run( library, "MINIMAL suppresses X and post-IUP movement", &p, &empty, &empty );

  p.length = 0;
  op( &p, 0x00 ); shift( &p );
  expect_point( &p, 0, 13, 0 );
  delta_witness( &p ); shift( &p );
  expect_point( &p, 0, 21, 0 );
  run( library, "ordinary SHPIX keeps MINIMAL untouched/touched policy", &p, &empty, &empty );

  p.length = 0;
  op( &p, 0x01 ); push( &p, 13 ); op( &p, 0x68 ); expect( &p, 0 );
  run( library, "ordinary compatible ROUND stays physical", &p, &empty, &empty );
  p.length = 0;
  push( &native, 4 ); push( &native, 3 ); op( &native, 0x8E );
  op( &p, 0x01 ); push( &p, 13 ); op( &p, 0x68 ); expect( &p, 13 );
  push( &p, 1 ); push( &p, 0 ); op( &p, 0x3F );
  expect_point( &p, 1, 113, 1 );
  run( library, "native ROUND and MIAP retain upstream behavior", &p, &native, &empty );

  p.length = 0;
  op( &p, 0x01 ); push( &p, -1 ); op( &p, 0x1A ); op( &p, 0x4E );
  push( &p, 0 ); push( &p, -1 ); op( &p, 0x44 );
  push( &p, 1 ); push( &p, 0 ); op( &p, 0xE8 );
  expect_point( &p, 1, 12, 1 );
  run( library, "native signed minimum distance is unchanged", &p, &native, &empty );
}

static void
test_storage( FT_Library library )
{
  static const unsigned ids[] = { 64, 65, 66, 0, 1, 2, 4, 7, 8 };
  unsigned i;
  for ( i = 0; i < sizeof ( ids ) / sizeof ( ids[0] ); i++ )
  {
    unsigned slot = ids[i] >= 64 ? 22 : 24;
    Program p = { { 0 }, 0 }, prep = { { 0 }, 0 }, f = { { 0 }, 0 };
    write_store( &prep, slot, 5 );
    stroke_prefix( &f, ids[i], slot, 0 );
    expect_store( &f, slot, 0 );
    write_store( &f, slot, 9 );
    expect_store( &f, slot, 9 );
    op( &f, 0x2D );
    call( &p, ids[i] );
    expect_store( &p, slot, 5 );
    run( library, "support CALL saves/zeros/restores real storage", &p, &prep, &f );
  }
  for ( i = 0; i < 2; i++ )
  {
    unsigned slot = i ? 24 : 22, outer = i ? 1 : 64, inner = i ? 2 : 65;
    Program p = { { 0 }, 0 }, prep = { { 0 }, 0 }, f = { { 0 }, 0 };
    write_store( &prep, slot, 5 );
    stroke_prefix( &f, outer, slot, i );
    write_store( &f, slot, 9 );
    call( &f, inner );
    expect_store( &f, slot, 9 );
    op( &f, 0x2D );
    /* Only the outer definition has a signature: family recognition is shared. */
    begin_function( &f, inner );
    expect_store( &f, slot, 0 );
    write_store( &f, slot, 11 );
    op( &f, 0x2D );
    call( &p, outer );
    expect_store( &p, slot, 5 );
    run( library, "nested shared-family CALL restores each caller", &p, &prep, &f );
  }
  {
    Program p = { { 0 }, 0 }, prep = { { 0 }, 0 }, f = { { 0 }, 0 };
    write_store( &prep, 22, 5 );
    stroke_prefix( &f, 64, 22, 0 ); op( &f, 0x2D );
    begin_function( &f, 64 );
    expect_store( &f, 22, 0 );
    op( &f, 0x2D );
    call( &p, 64 ); expect_store( &p, 22, 5 );
    run( library, "family classification survives nonmatching redefinition", &p, &prep, &f );
  }
  {
    Program p = { { 0 }, 0 }, prep = { { 0 }, 0 }, f = { { 0 }, 0 };
    write_store( &prep, 22, 5 );
    stroke_prefix( &f, 64, 22, 0 );
    push( &f, 22 ); push( &f, 22 ); op( &f, 0x43 );
    push( &f, 1 ); op( &f, 0x60 ); op( &f, 0x42 ); op( &f, 0x2D );
    loopcall( &p, 64, 2 ); expect_store( &p, 22, 7 );
    call( &p, 64 ); expect_store( &p, 22, 7 );
    run( library, "LOOPCALL bypasses support entry on every repetition", &p, &prep, &f );
  }
}

static void
test_delta( FT_Library library )
{
  unsigned i, variant;
  for ( variant = 0; variant < 4; variant++ )
  {
    Program p = { { 0 }, 0 }, empty = { { 0 }, 0 }, f = { { 0 }, 0 };
    int range = variant & 1;
    delta_definition( &f, 10, range );
    delta_witness( &p );
    push( &p, 0 ); push( &p, 8 );
    if ( range ) { push( &p, 10 ); push( &p, 20 ); }
    else push( &p, 16 );
    if ( variant & 2 ) loopcall( &p, 10, 1 );
    else call( &p, 10 );
    expect_point( &p, 0, !( variant & 2 ) ? 13 : 21, 0 );
    run( library, "both delta signatures distinguish CALL from LOOPCALL", &p, &empty, &f );
  }
  for ( variant = 0; variant < 3; variant++ )
  {
    Program p = { { 0 }, 0 }, empty = { { 0 }, 0 }, f = { { 0 }, 0 };
    unsigned target = variant == 0 ? 11 : variant == 1 ? 14 : 11;
    if ( variant == 2 )
      for ( i = 0; i < 4; i++ ) delta_definition( &f, 10, 0 );
    else
      for ( i = 10; i < target; i++ ) delta_definition( &f, i, 0 );
    delta_definition( &f, target, 0 );
    begin_function( &f, target ); shift( &f ); op( &f, 0x2D );
    delta_witness( &p ); call( &p, target );
    expect_point( &p, 0, variant == 0 ? 13 : 21, 0 );
    reference_case = variant != 2;
    run( library, variant == 0 ? "delta classification survives replacement" :
                  variant == 1 ? "fifth delta definition is outside capacity" :
                                 "duplicate delta definitions consume capacity",
         &p, &empty, &f );
  }
  reference_case = 1;
  for ( variant = 0; variant < 2; variant++ )
  {
    Program p = { { 0 }, 0 }, empty = { { 0 }, 0 }, f = { { 0 }, 0 };
    delta_definition( &f, 10, 0 );
    if ( variant ) delta_definition( &f, 11, 0 );
    begin_function( &f, 11 ); op( &f, 0x2D );
    begin_function( &f, 10 );
    shift( &f ); call( &f, 11 ); shift( &f ); op( &f, 0x2D );
    delta_witness( &p ); call( &p, 10 );
    expect_point( &p, 0, variant ? 21 : 13, 0 );
    run( library, variant ? "inner recognized return clears delta boolean" :
                           "unrecognized inner return preserves delta boolean",
         &p, &empty, &f );
  }
}

static void
test_delta_boundaries( FT_Library library )
{
  unsigned variant;
  for ( variant = 0; variant < 5; variant++ )
  {
    Program p = { { 0 }, 0 }, empty = { { 0 }, 0 };
    int expected = 21;
    op( &p, 0x00 ); push( &p, 0 ); op( &p, 0x2E );
    if ( variant == 1 )
    {
      negative_y( &p );
      expected = 13;
    }
    else if ( variant == 2 )
    {
      op( &p, 0x03 );            /* +X projection, +Y freedom */
      /* Orthogonal P/F suppresses ordinary DELTAP movement in MINIMAL. */
      expected = 13;
    }
    else if ( variant == 3 )
    {
      op( &p, 0x30 );
      expected = 13;
    }
    else if ( variant == 4 )
      op( &p, 0x31 );
    push( &p, 0x78 ); push( &p, 0 ); push( &p, 1 ); op( &p, 0x5D );
    expect_point( &p, 0, expected, 0 );
    run( library, variant == 0 ? "DELTAP retains touched +Y movement" :
                  variant == 1 ? "DELTAP excludes negative-Y projection" :
                  variant == 2 ? "DELTAP preserves orthogonal movement suppression" :
                  variant == 3 ? "DELTAP skips after IUP[y]" :
                                 "IUP[x] does not suppress vertical DELTAP",
         &p, &empty, &empty );
  }
  for ( variant = 0; variant < 2; variant++ )
  {
    Program p = { { 0 }, 0 }, empty = { { 0 }, 0 }, f = { { 0 }, 0 };
    delta_definition( &f, 10, 0 );
    push( &p, 0 ); op( &p, 0x16 ); /* SZPS[twilight] */
    op( &p, 0x00 );
    if ( variant )
    {
      push( &p, 0 ); push( &p, 8 ); push( &p, 16 ); call( &p, 10 );
    }
    else shift( &p );
    expect_point( &p, 0, variant ? 0 : 8, 0 );
    run( library, variant ? "recognized delta adds restriction in twilight" :
                           "ordinary SHPIX retains MINIMAL twilight exception",
         &p, &empty, &f );
  }
}

static void
test_recognition_boundaries( FT_Library library )
{
  unsigned variant;
  for ( variant = 0; variant < 3; variant++ )
  {
    Program p = { { 0 }, 0 }, prep = { { 0 }, 0 }, f = { { 0 }, 0 };
    unsigned id = variant == 0 ? 63 : 64;
    write_store( &prep, 22, 5 );
    if ( variant < 2 )
    {
      /* Correct signature outside candidate range, or a near-match. */
      stroke_prefix( &f, id, variant == 1 ? 21 : 22, 0 );
      expect_store( &f, 22, 5 ); op( &f, 0x2D );
      call( &p, id );
    }
    else
    {
      /* A complete short FDEF is not a prefix match spanning ENDF. */
      begin_function( &f, id ); push( &f, 22 ); op( &f, 0x43 ); op( &f, 0x2D );
      call( &p, id ); expect( &p, 5 );
    }
    run( library, "signature and candidate-number boundaries", &p, &prep, &f );
  }
  {
    Program p = { { 0 }, 0 }, prep = { { 0 }, 0 }, empty = { { 0 }, 0 };
    write_store( &prep, 8, 5 ); expect_store( &p, 8, 5 );
    run( library, "storage 8 is ordinary without VacuForm recognition", &p, &prep, &empty );
  }
}

static void
test_vacuform( FT_Library library )
{
  Program p = { { 0 }, 0 }, second = { { 0 }, 0 };
  Program prep = { { 0 }, 0 }, f = { { 0 }, 0 };
  FT_Face face;
  write_store( &prep, 8, 5 ); vacu_definition( &f );
  begin_function( &f, 9 );
  expect_store( &f, 8, 0 ); op( &f, 0x2D );
  vacu_geometry( &p );
  expect_distance( &p, 64 );
  call( &p, 9 );
  expect_distance( &p, 64 );
  op( &p, 0x30 ); expect_distance( &p, 64 );
  op( &p, 0x31 ); expect_distance( &p, 65 );
  call( &p, 9 ); expect_distance( &p, 65 );
  expect_distance( &p, 65 );
  vacu_geometry( &second ); op( &second, 0x30 ); op( &second, 0x31 );
  expect_distance( &second, 64 );
  current_case = "VacuForm survives CALL return and resets for next glyph";
  cases++;
  make_font( &p, &second, &prep, &f );
  face = open_font( library, 40, 0 );
  if ( face )
  {
    check( "first glyph assertions", load( face, 0, FT_LOAD_TARGET_LCD ), 0 );
    check( "next glyph assertions", load( face, 1, FT_LOAD_TARGET_LCD ), 0 );
    FT_Done_Face( face );
  }
}

static void
test_cutin( FT_Library library )
{
  Program p = { { 0 }, 0 }, empty = { { 0 }, 0 }, f = { { 0 }, 0 };
  unsigned i;
  for ( i = 0; i < 2; i++ )
  {
    p.length = 0; negative_y( &p ); push( &p, 2 );
    push( &p, i ? -65 : 0 ); op( &p, i ? 0x3A : 0xE0 );
    expect_point( &p, 2, 113, 0 );
    run( library, i ? "legacy MSIRP cut-in is instruction-local" :
                     "unrounded MIRP receives legacy cut-in",
         &p, &empty, &empty );
  }
  for ( i = 0; i < 2; i++ )
  {
    Program prep = { { 0 }, 0 };
    push( &prep, 0 ); push( &prep, i ? 95 : 96 ); op( &prep, 0x44 );
    p.length = 0; negative_y( &p ); push( &p, 2 ); push( &p, 0 ); op( &p, 0xE0 );
    expect_point( &p, 2, i ? 113 : 109, 0 );
    run( library, i ? "relative cut-in rejects difference above boundary" :
                     "relative cut-in accepts exact boundary",
         &p, &prep, &empty );
  }
  p.length = 0; negative_y( &p ); push( &p, 1 ); push( &p, -65 ); op( &p, 0x3A );
  expect_point( &p, 1, 78, 0 );
  run( library, "MSIRP preserves zero-original-distance placement", &p, &empty, &empty );
  { Program prep = { { 0 }, 0 };
    push( &prep, 4 ); push( &prep, 3 ); op( &prep, 0x8E );
    p.length = 0; negative_y( &p ); push( &p, 2 ); push( &p, 0 ); op( &p, 0xE0 );
    expect_point( &p, 2, 78, 0 );
    run( library, "native opt-out bypasses legacy relative cut-in", &p, &prep, &empty ); }
  begin_function( &f, 58 );
  { static const unsigned char prefix[] =
      { 0x20, 0x20, 0xB0, 1, 0x60, 0x46, 0xB0, 64, 0x23, 0x42 };
    for ( i = 0; i < sizeof ( prefix ); i++ ) op( &f, prefix[i] ); }
  op( &f, 0x21 ); op( &f, 0x21 );
  push( &f, 2 ); push( &f, 0 ); op( &f, 0xE0 ); op( &f, 0x2D );
  p.length = 0; negative_y( &p ); push( &p, 0 ); call( &p, 58 );
  expect_point( &p, 2, 78, 0 ); point_y( &p, 2, 113 );
  negative_y( &p ); push( &p, 2 ); push( &p, 0 ); op( &p, 0xE0 );
  expect_point( &p, 2, 113, 0 );
  run( library, "TomsDiag restores caller cut-in", &p, &empty, &f );

  p.length = 0; negative_y( &p ); push( &p, 0 ); loopcall( &p, 58, 1 );
  expect_point( &p, 2, 113, 0 );
  run( library, "TomsDiag LOOPCALL keeps ordinary cut-in", &p, &empty, &f );

  /* Neither absolute MIAP nor ordinary minimum distance belongs to this layer. */
  p.length = 0; negative_y( &p ); push( &p, 2 ); push( &p, 0 ); op( &p, 0x3F );
  expect_point( &p, 2, 128, 0 );
  /* This deliberately preserves MINIMAL's physical MIAP rounding, whereas
   * the independent Microsoft/oracle interpreter selects its virtual grid.
   */
  reference_case = 0;
  run( library, "legacy MIAP retains full physical cut-in/rounding", &p, &empty, &empty );
  reference_case = 1;
  p.length = 0; op( &p, 0x00 ); push( &p, 0 ); push( &p, 13 );
  op( &p, 0x68 ); op( &p, 0x44 );
  { Program glyph = { { 0 }, 0 };
    push( &glyph, 0 ); op( &glyph, 0x45 ); expect( &glyph, 0 );
    run( library, "ROUND remains physical in prep", &glyph, &p, &empty ); }
  p.length = 0; negative_y( &p ); op( &p, 0x7D );
  push( &p, 77 ); op( &p, 0x68 ); expect( &p, 64 );
  run( library, "RDTG stays physical under general projection", &p, &empty, &empty );
}

static void
test_metrics( FT_Library library )
{
  Program p = { { 0 }, 0 }, prep = { { 0 }, 0 }, empty = { { 0 }, 0 };
  FT_Face face;
  unsigned native;
  for ( native = 0; native < 2; native++ )
  {
    p.length = prep.length = 0;
    if ( native )
    {
      push( &prep, 4 ); push( &prep, 3 ); op( &prep, 0x8E );
    }
    else
    {
      expect_point( &p, 4, 320, 1 );
      op( &p, 0x01 ); push( &p, 4 ); push( &p, 512 ); op( &p, 0x48 );
      expect_point( &p, 4, 320, 1 );
    }
    current_case = native ? "native hdmx selection is unchanged" :
                            "phantom initialization and compatible advance are unchanged";
    cases++;
    make_font( &p, &p, &prep, &empty ); face = open_font( library, 40, 0 );
    if ( face )
    {
      FT_Error error = load( face, 0, FT_LOAD_TARGET_LCD );
      check( "load", error, 0 );
      if ( !error ) check( "advance", face->glyph->advance.x, native ? 99 * 64 : 320 );
      FT_Done_Face( face );
    }
  }
}

static void
test_boundaries( FT_Library library )
{
  Program p = { { 0 }, 0 }, prep = { { 0 }, 0 }, f = { { 0 }, 0 };
  unsigned i;
  write_store( &prep, 22, 5 );
  stroke_prefix( &f, 64, 22, 0 ); expect_store( &f, 22, 5 ); op( &f, 0x2D );
  call( &p, 64 ); expect_store( &p, 22, 5 );
  run_mode( library, "v35 ignores legacy rules", &p, &prep, &f,
            35, FT_LOAD_TARGET_LCD, 0 );
  run_mode( library, "monochrome ignores legacy rules", &p, &prep, &f,
            40, FT_LOAD_TARGET_MONO, 0 );
  run_mode( library, "tricky fonts ignore legacy rules", &p, &prep, &f,
            40, FT_LOAD_TARGET_LCD, 1 );
  push( &prep, 4 ); push( &prep, 3 ); op( &prep, 0x8E );
  run( library, "prep native opt-out ignores legacy rules", &p, &prep, &f );
  prep.length -= 5;
  p.length = 0; push( &p, 4 ); push( &p, 3 ); op( &p, 0x8E ); call( &p, 64 );
  run( library, "glyph native opt-out keeps upstream support", &p, &prep, &f );

  /* Do not bake expected values into the function; observe them afterward. */
  f.length = 0; stroke_prefix( &f, 64, 22, 0 );
  push( &f, 22 ); op( &f, 0x43 ); op( &f, 0x2D );
  p.length = 0; call( &p, 64 ); op( &p, 0x00 ); push( &p, 0 );
  op( &p, 0x23 ); op( &p, 0x48 );
  current_case = "recognition survives mode and interpreter changes"; cases++;
  make_font( &p, &p, &prep, &f );
  { FT_Face face = open_font( library, 40, 0 );
    static const unsigned versions[] = { 40, 40, 35, 40, 40 };
    static const FT_Int32 modes[] = { FT_LOAD_TARGET_MONO, FT_LOAD_TARGET_LCD,
      FT_LOAD_TARGET_LCD, FT_LOAD_TARGET_LCD, FT_LOAD_TARGET_MONO };
    if ( face )
    {
      for ( i = 0; i < sizeof ( versions ) / sizeof ( versions[0] ); i++ )
      {
        unsigned version = versions[i];
        FT_Error error;
        check( "change interpreter", FT_Property_Set( library, "truetype",
                            "interpreter-version", &version ), 0 );
        error = load( face, 0, modes[i] ); check( "load", error, 0 );
        if ( !error ) check( "storage result", face->glyph->outline.points[0].y,
                              ( i == 1 || i == 3 ) ? 0 : 5 );
      }
      FT_Done_Face( face );
    }
  }
}

static void
test_abort( FT_Library library )
{
  Program p = { { 0 }, 0 }, clean = { { 0 }, 0 };
  Program prep = { { 0 }, 0 }, f = { { 0 }, 0 };
  FT_Face face;
  write_store( &prep, 22, 5 );
  stroke_prefix( &f, 64, 22, 0 ); write_store( &f, 22, 9 );
  call( &f, 65 ); op( &f, 0x2D );
  begin_function( &f, 65 ); write_store( &f, 22, 11 );
  push( &f, 1 ); push( &f, 0 ); op( &f, 0x62 ); op( &f, 0x2D );
  call( &p, 64 ); expect_store( &clean, 22, 5 );
  current_case = "aborted nested CALL keeps prep storage for next glyph"; cases++;
  first_glyph_error = FT_Err_Divide_By_Zero;
  make_font( &p, &clean, &prep, &f ); face = open_font( library, 40, 0 );
  first_glyph_error = 0;
  if ( face )
  {
    check( "intentional DIV failure", load( face, 0, FT_LOAD_TARGET_LCD ),
           FT_Err_Divide_By_Zero );
    check( "load after failure", load( face, 1, FT_LOAD_TARGET_LCD ), 0 );
    FT_Done_Face( face );
  }
}

typedef struct  AllocationState_
{
  int armed;
  int failed;
} AllocationState;

static void*
test_alloc( FT_Memory memory, long size )
{
  AllocationState* state = (AllocationState*)memory->user;
  if ( state->armed && !state->failed && size == 128 * (long)sizeof ( FT_Long ) )
  {
    state->failed = 1;
    return NULL;
  }
  return malloc( (size_t)size );
}

static void
test_free( FT_Memory memory, void* block )
{
  (void)memory;
  free( block );
}

static void*
test_realloc( FT_Memory memory, long old_size, long new_size, void* block )
{
  AllocationState* state = (AllocationState*)memory->user;
  (void)old_size;
  if ( state->armed && !state->failed && new_size == 128 * (long)sizeof ( FT_Long ) )
  {
    state->failed = 1;
    return NULL;
  }
  return realloc( block, (size_t)new_size );
}

static void
test_allocation_failure( void )
{
  AllocationState state = { 0, 0 };
  struct FT_MemoryRec_ memory;
  FT_Library library;
  FT_Face face;
  Program p = { { 0 }, 0 }, clean = { { 0 }, 0 };
  Program prep = { { 0 }, 0 }, f = { { 0 }, 0 };
  current_case = "CALL storage allocation failure is recoverable"; cases++;
  memory.user = &state; memory.alloc = test_alloc;
  memory.free = test_free; memory.realloc = test_realloc;
  if ( FT_New_Library( &memory, &library ) ) { failures++; return; }
  FT_Add_Default_Modules( library );
  write_store( &prep, 22, 5 );
  stroke_prefix( &f, 64, 22, 0 ); op( &f, 0x2D );
  call( &p, 64 ); expect_store( &p, 22, 5 );
  expect_store( &clean, 22, 5 );
  make_font( &clean, &p, &prep, &f ); face = open_font( library, 40, 0 );
  if ( face )
  {
    check( "warm up without glyph storage writes", load( face, 0, FT_LOAD_TARGET_LCD ), 0 );
    state.armed = 1;
    check( "allocation failure", load( face, 1, FT_LOAD_TARGET_LCD ), FT_Err_Out_Of_Memory );
    state.armed = 0;
    check( "injected glyph-storage allocation failure", state.failed, 1 );
    check( "retry CALL", load( face, 1, FT_LOAD_TARGET_LCD ), 0 );
    check( "prep storage unchanged", load( face, 0, FT_LOAD_TARGET_LCD ), 0 );
    FT_Done_Face( face );
  }
  FT_Done_Library( library );
}

static void
test_nested_loopcall( FT_Library library )
{
  Program p = { { 0 }, 0 }, prep = { { 0 }, 0 }, f = { { 0 }, 0 };
  write_store( &prep, 22, 5 );
  stroke_prefix( &f, 64, 22, 0 );
  write_store( &f, 22, 9 ); loopcall( &f, 65, 1 );
  expect_store( &f, 22, 11 ); op( &f, 0x2D );
  begin_function( &f, 65 );
  expect_store( &f, 22, 9 ); write_store( &f, 22, 11 ); op( &f, 0x2D );
  call( &p, 64 ); expect_store( &p, 22, 5 );
  run( library, "LOOPCALL within active support CALL does not enter another scope",
       &p, &prep, &f );
}

static void
test_size_changes( FT_Library library )
{
  Program p = { { 0 }, 0 }, prep = { { 0 }, 0 }, f = { { 0 }, 0 };
  FT_Face face;
  unsigned i;
  static const unsigned sizes[] = { 16, 17, 12, 16 };
  write_store( &prep, 22, 5 );
  stroke_prefix( &f, 64, 22, 0 );
  expect_store( &f, 22, 0 ); op( &f, 0x2D );
  call( &p, 64 ); expect_store( &p, 22, 5 );
  current_case = "historical recognition survives repeated size changes"; cases++;
  make_font( &p, &p, &prep, &f ); face = open_font( library, 40, 0 );
  if ( face )
  {
    for ( i = 0; i < sizeof ( sizes ) / sizeof ( sizes[0] ); i++ )
    {
      check( "change size", FT_Set_Pixel_Sizes( face, 0, sizes[i] ), 0 );
      check( "load after prep rerun", load( face, 0, FT_LOAD_TARGET_LCD ), 0 );
    }
    FT_Done_Face( face );
  }
}

int
main( int argc, char** argv )
{
  FT_Library library;
  FT_Error error;
  unsigned version = 40;
  if ( argc == 3 && strcmp( argv[1], "--export-dir" ) == 0 )
  {
    char path[4096];
    export_directory = argv[2];
    if ( snprintf( path, sizeof ( path ), "%s/manifest.tsv", export_directory ) >=
         (int)sizeof ( path ) ) return 1;
    export_manifest = fopen( path, "w" );
    if ( !export_manifest )
    {
      fprintf( stderr, "Cannot open %s; the export directory must exist\n", path );
      return 1;
    }
    fputs( "file\toracle_shared\tversion\tload_target\ttricky\tglyph0_error\tglyph1_error\tcase\n",
           export_manifest );
  }
  else if ( argc != 1 )
  {
    fprintf( stderr, "Usage: %s [--export-dir existing-directory]\n", argv[0] );
    return 1;
  }
  current_case = "initialization";
  if ( FT_Init_FreeType( &library ) ) return 1;
  error = FT_Property_Set( library, "truetype", "interpreter-version", &version );
  if ( error == FT_Err_Unimplemented_Feature )
  {
    FT_Done_FreeType( library );
    return 77;
  }
  check( "select v40", error, 0 );
  test_minimal( library );
  reference_case = 1;
  test_storage( library );
  test_delta( library );
  reference_case = 0;
  test_delta_boundaries( library );
  reference_case = 1;
  test_recognition_boundaries( library );
  test_vacuform( library );
  test_cutin( library );
  reference_case = 0;
  test_metrics( library );
  test_boundaries( library );
  reference_case = 1;
  test_abort( library );
  test_nested_loopcall( library );
  test_size_changes( library );
  FT_Done_FreeType( library );
  test_allocation_failure();
  if ( export_manifest && fclose( export_manifest ) ) failures++;
  if ( !failures )
    printf( "%u v40 ClearType compatibility cases passed\n", cases );
  return failures != 0;
}
