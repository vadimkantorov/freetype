#include <stdio.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H


typedef struct  TestFont_
{
  const char*  filename;
  FT_UInt      glyph_index;
  FT_UInt      point_count;
  FT_Pos       first_x;
  FT_Long      glyph_count;
  FT_Bool      public_axes;

} TestFont;


static int
test_font( FT_Library       library,
           const char*      testdata_dir,
           const TestFont*  test_font )
{
  FT_Face  face = NULL;
  char     filepath[FILENAME_MAX];

  int  ret = 1;


  snprintf( filepath, sizeof ( filepath ), "%s/%s",
            testdata_dir, test_font->filename );

  if ( FT_New_Face( library, filepath, 0, &face ) )
  {
    fprintf( stderr, "Could not open file: %s\n", filepath );
    goto Exit;
  }

  if ( !!FT_HAS_MULTIPLE_MASTERS( face ) != test_font->public_axes )
  {
    fprintf( stderr, "%s exposes unexpected variation axes\n",
             test_font->filename );
    goto Exit;
  }

  if ( face->num_glyphs != test_font->glyph_count )
  {
    fprintf( stderr, "Unexpected glyph count from %s\n",
             test_font->filename );
    goto Exit;
  }

  if ( FT_Load_Glyph( face, test_font->glyph_index,
                      FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING ) )
  {
    fprintf( stderr, "Could not load VARC glyph from %s\n",
             test_font->filename );
    goto Exit;
  }

  if ( face->glyph->format != FT_GLYPH_FORMAT_OUTLINE                   ||
       (FT_UInt)face->glyph->outline.n_points != test_font->point_count ||
       face->glyph->outline.points[0].x != test_font->first_x           )
  {
    fprintf( stderr, "Unexpected VARC outline from %s\n",
             test_font->filename );
    goto Exit;
  }

  if ( !test_font->public_axes )
  {
    FT_MM_Var*  master = NULL;


    if ( !FT_Get_MM_Var( face, &master ) )
    {
      fprintf( stderr, "%s exposes private VARC axes through fvar\n",
               test_font->filename );
      FT_Done_MM_Var( library, master );
      goto Exit;
    }
  }

  ret = 0;

Exit:
  FT_Done_Face( face );
  return ret;
}


static int
test_reset_without_axes( FT_Library   library,
                         const char*  testdata_dir )
{
  FT_Face  face = NULL;
  char     filepath[FILENAME_MAX];
  FT_UInt  i;
  int      ret = 1;


  /* Outer overrides TEST to 1; inner resets without HAVE_AXES. */
  snprintf( filepath, sizeof ( filepath ), "%s/varc-reset-only.ttf",
            testdata_dir );
  if ( FT_New_Face( library, filepath, 0, &face ) )
    goto Exit;

  for ( i = 0; i < 2; i++ )
  {
    FT_Fixed  coordinate = i ? 0x4000L : 0;
    FT_BBox   box;


    if ( FT_Set_Var_Design_Coordinates( face, 1, &coordinate ) ||
         FT_Load_Glyph( face, 3, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING ) )
      goto Exit;
    FT_Outline_Get_CBox( &face->glyph->outline, &box );
    if ( box.xMin != 100 || box.xMax != ( i ? 225 : 200 ) ||
         box.yMin != 0   || box.yMax != 100 )
    {
      fprintf( stderr, "VARC reset without axes retained parent coordinates\n" );
      goto Exit;
    }
  }
  ret = 0;

Exit:
  FT_Done_Face( face );
  return ret;
}


static int
test_region_scalars( FT_Library   library,
                     const char*  testdata_dir )
{
  static const FT_Fixed  locations[] =
  {
    -0x10000L, 0, 0x4000L, 0x8000L, 0xC000L, 0x10000L
  };
  static const FT_Pos  valid_deltas[] = { 0, 0, 32, 64, 32, 0 };

  FT_Face  face = NULL;
  char     filepath[FILENAME_MAX];
  FT_UInt  i, j, k;
  int      ret = 1;


  snprintf( filepath, sizeof ( filepath ), "%s/region-scalars.ttf",
            testdata_dir );
  if ( FT_New_Face( library, filepath, 0, &face ) )
    goto Exit;

  for ( i = 0; i < sizeof ( locations ) / sizeof ( locations[0] ); i++ )
  {
    /* Keep TEST at its peak so gvar is evaluated while LEAF can be zero. */
    FT_Fixed  coords[] = { 0x10000L, locations[i] };


    if ( FT_Set_Var_Blend_Coordinates( face, 2, coords ) )
      goto Exit;

    for ( j = 0; j < 5; j++ )
      for ( k = 0; k < 2; k++ )
      {
        /* Three invalid axes, a zero peak, and a well-formed tent. */
        FT_Pos   delta = j < 4 ? 64 : valid_deltas[i];
        FT_Pos   first_x = ( k ? 500 : 100 ) + delta;
        FT_UInt  glyph = 2 + j + 5 * k;


        if ( FT_Load_Glyph( face, glyph,
                           FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING ) )
          goto Exit;
        if ( face->glyph->outline.n_points != 3 ||
             face->glyph->outline.points[0].x != first_x )
        {
          fprintf( stderr, "Unexpected region scalar for glyph %u at %ld\n",
                   glyph, (long)locations[i] );
          goto Exit;
        }
      }
  }
  ret = 0;

Exit:
  FT_Done_Face( face );
  return ret;
}


int
main( void )
{
  static const TestFont  test_fonts[] =
  {
    { "varc-static-gvar.ttf",    1,  3,  50, 3, 0 },
    { "varc-static-cff2.otf",    2, 60,  36, 9, 0 },
    { "varc-short-cff2.otf",     3,  3, 600, 4, 0 },
    { "varc-null-condition.ttf", 1, 48,  86, 8, 1 }
  };

  FT_Library  library;
  const char*  testdata_dir = getenv( "FREETYPE_TESTS_DATA_DIR" );

  int      ret = 1;
  FT_UInt  i;


  if ( !testdata_dir )
    testdata_dir = "../tests/data";

  if ( FT_Init_FreeType( &library ) )
  {
    fprintf( stderr, "Could not initialize FreeType\n" );
    return ret;
  }

  ret = 0;
  for ( i = 0;
        i < sizeof ( test_fonts ) / sizeof ( test_fonts[0] );
        i++ )
    ret |= test_font( library, testdata_dir, &test_fonts[i] );

  ret |= test_reset_without_axes( library, testdata_dir );
  ret |= test_region_scalars( library, testdata_dir );

  FT_Done_FreeType( library );
  return ret;
}


/* EOF */
