/*
 * file.c
 *
 * All rights reserved. Copyright (C) 1996 by NARITA Tomio.
 * $Id: file.c,v 1.11 2004/01/05 07:30:15 nrt Exp $
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MSDOS
#include <dos.h>
#endif /* MSDOS */

#ifdef UNIX
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#endif /* UNIX */

#include <import.h>
#include <decode.h>
#include <encode.h>
#include <console.h>
#include <uty.h>
#include <guess.h>
#include <begin.h>
#include <file.h>
#ifdef USE_UTF16
#include <utf.h>
#endif

extern byte *FindResetPattern( file_t *f, i_str_t *istr );

#define LOAD_SIZE	STR_SIZE

private byte short_str[ LOAD_SIZE ];

#ifndef MSDOS /* if NOT defined */
#define LOAD_COUNT	8		/* upper logical length 8Kbytes */
#else
#define LOAD_COUNT	1
#endif /* MSDOS */

private byte *long_str = NULL;
private byte *load_array[ LOAD_COUNT ];

public void FileFreeLine( file_t *f )
{
  if( long_str ){
    free( long_str );
    long_str = NULL;
  }
}

#ifdef USE_INTERNAL_IOBUF
public INLINE int IobufGetc( iobuf_t *iobuf )
{
  if( iobuf->ungetc != EOF ){
    int ch = iobuf->ungetc;
    iobuf->ungetc = EOF;
    return ch;
  }
  if( iobuf->cur >= iobuf->last ){
    /* no stream buffer, reset and fill now */
    iobuf->cur = 0;
    iobuf->last = fread( iobuf->buf, sizeof( byte ), IOBUF_DEFAULT_SIZE, iobuf->iop );
    if( iobuf->last <= 0 ){
      return EOF;
    }
  }
  return iobuf->buf[ iobuf->cur++ ];
}

public INLINE int IobufUngetc( int ch, iobuf_t *iobuf )
{
  if( iobuf->cur == 0 ){
    /* XXX: it should be tied to fp sanely */
    if( iobuf->ungetc != EOF )
      return EOF;
    iobuf->ungetc = ch;
    return ch;
  }
  iobuf->buf[ --iobuf->cur ] = (byte)ch;
  return ch;
}

public offset_t IobufFtell( iobuf_t *iobuf )
{
  offset_t ptr;
# ifdef HAVE_FSEEKO
  ptr = ftello( iobuf->iop );
# else
  ptr = ftell( iobuf->iop );
# endif
  if( iobuf->ungetc != EOF )
    ptr--;
  if( iobuf->cur == iobuf->last ){
    return ptr;
  }
  return ptr - ( iobuf->last - iobuf->cur );
}

public int IobufFseek( iobuf_t *iobuf, offset_t off, int mode )
{
  iobuf->cur = iobuf->last = 0;  /* flush all iobuf */
  iobuf->ungetc = EOF;
# ifdef HAVE_FSEEKO
  return fseeko( iobuf->iop, off, mode );
# else
  return fseek( iobuf->iop, off, mode );
# endif
}

public int IobufFeof( iobuf_t *iobuf )
{
  if( iobuf->cur == iobuf->last ){
    return feof( iobuf->iop );
  } else {
    return 1;
  }
}
#endif


/*
 * $B8=:_$N%U%!%$%k%]%$%s%?$+$i(B 1$B9T$rFI$_9~$s$G%P%C%U%!$K3JG<$9$k(B.
 * $B%3!<%I7O$N<+F0H=JL$NBP>]$H$J$k>l9g(B, $B<+F0H=JL$r9T$J$&(B.
 * EOF $B$r8+$D$1$?;~$O(B EOF $B%U%i%0$rN)$F$k(B.
 * $BFI$_9~$s$@J8;zNs$ND9$5$r%]%$%s%?$NCf?H$KJV$9(B.
 * $B=EMW(B: $BFI$_9~$s$@D9$5$,(B 0 $B$OFI$`$Y$-%G!<%?$,L5$$$3$H$r$r0UL#$9$k(B.
 *       $B0lJ}(B, EOF $B$O%G!<%?$rFI$_9~$s$@$,(B, $B<!$KFI$`$Y$-%G!<%?$,(B
 *       $BL5$$$3$H$r0UL#$9$k(B.
 */
public byte *FileLoadLine( file_t *f, int *length, boolean_t *simple )
{
  boolean_t flagSimple = TRUE;
  boolean_t flagEightBit = FALSE, flagHz = FALSE;
  int idx, len, count, ch;
  char *str;
#ifdef USE_UTF16
  int cr_flag = 0;
#endif

  if( long_str ){
    free( long_str );
    long_str = NULL;
  }

  len = 0;
  count = 0;
  idx = 0;

#ifdef USE_UTF16
  ch = IobufGetc( &f->fp );
  if( AUTOSELECT == f->inputCodingSystem || UTF_16 == f->inputCodingSystem ){
    /* BOM check */
    if( ch != EOF ){
      int ch2 = IobufGetc( &f->fp );
      if( ch == 0xfe && ch2 == 0xff )
        f->inputCodingSystem = UTF_16BE;
      else if( ch == 0xff && ch2 == 0xfe )
        f->inputCodingSystem = UTF_16LE;
      if( ch2 != EOF )
	IobufUngetc( ch2, &f->fp );
    }
  }
  if (IsUtf16Encoding(f->inputCodingSystem))
    flagSimple = FALSE;

  for (; ch != EOF; ch = IobufGetc( &f->fp )) {
#else /* !USE_UTF16 */
  while( EOF != (ch = IobufGetc( &f->fp )) ){
#endif /* USE_UTF16 */
    len++;
    load_array[ count ][ idx++ ] = (byte)ch;
#ifdef USE_UTF16
    if( IsUtf16Encoding( f->inputCodingSystem ) ) {
      if( (idx % 2) == 0 ){
	int ch2 = load_array[ count ][ idx-2 ] & 0xff;
	if(f->inputCodingSystem != UTF_16BE && ch == 0 && ch2 == CR ){
	  load_array[ count ][ idx-1 ] = 0;
	  load_array[ count ][ idx-2 ] = LF;
	  cr_flag = UTF_16LE;
	} else if( f->inputCodingSystem != UTF_16LE && ch == CR && ch2 == 0 ){
	  load_array[ count ][ idx-1 ] = LF;
	  load_array[ count ][ idx-2 ] = 0;
	  cr_flag = UTF_16BE;
	} else if( f->inputCodingSystem != UTF_16BE && ch == 0 && ch2 == LF ){
	  if( cr_flag == UTF_16LE ){ /* MS-DOS style eol */
	    len -= 2;
	    if (idx < 2 && count > 0)
	      free( load_array[ count-- ] );
	  }
	  break;
	} else if( f->inputCodingSystem != UTF_16LE && ch == LF && ch2 == 0 ){
	  if( cr_flag == UTF_16LE ){ /* MS-DOS style eol */
	    len -= 2;
	    if( idx < 2 && count > 0 )
	      free( load_array[ count-- ] );
	  }
	  break;
	} else if( cr_flag ){ /* Mac style eol */
	  len -= 2;
	  if( idx < 2 && count > 0 )
	    free( load_array[ count-- ] );
	  if( IobufFseek( &f->fp, -2, SEEK_CUR ) )
	    perror( "FileLoadLine" ), exit( -1 );
	  break;
	} else
	  cr_flag = 0;
      }
    } else
#endif /* USE_UTF16 */
    {
      if( LF == ch ){
	/* UNIX style */
	break;
      } else if( CR == ch ){
	if( LF == (ch = IobufGetc( &f->fp )) ){
	  /* MSDOS style */
	} else if( EOF == ch ){
	  /* need to avoid EOF due to pre-load of that */
	  ch = LF;
	} else {
	  /* Mac style */
	  IobufUngetc( ch, &f->fp );
	}
	load_array[ count ][ idx - 1 ] = LF;
	break;
      }
    }
    if( LOAD_SIZE == idx ){
      count++;
      if( LOAD_COUNT == count )
	break;
      load_array[ count ] = (byte *)Malloc( LOAD_SIZE );
      idx = 0;
    }
    if( FALSE == flagEightBit ){
      if( ch > DEL ){
	flagEightBit = TRUE;
	flagSimple = FALSE;
      } else if( FALSE == flagHz ){
	if( '~' == ch && TRUE == hz_detection ){
	  flagHz = TRUE;
	  flagSimple = FALSE;
	}
      }
    }
    if( TRUE == flagSimple ){
      if( ch < SP || DEL == ch
	 || ( '+' == ch && UTF_7 == f->inputCodingSystem ) )
	flagSimple = FALSE;
    }
  }

  if( EOF == ch )
    f->eof = TRUE;
  else
    f->eof = FALSE;

  if( LOAD_SIZE >= len ){
    str = short_str;
  } else {
    long_str = (byte *)Malloc( len );
    for( ch = count = idx = 0 ; ch < len ; ch++, idx++ ){
      if( LOAD_SIZE == idx ){
	if( count > 0 )
	  free( load_array[ count ] );
	idx = 0;
	count++;
      }
      long_str[ ch ] = load_array[ count ][ idx ];
    }
    if( count > 0 )
      free( load_array[ count ] );
    str = long_str;
  }

  *length = len;
  *simple = flagSimple;

  if( AUTOSELECT == f->inputCodingSystem ){
    if( TRUE == flagEightBit ){
      f->inputCodingSystem = GuessCodingSystem( str, len,
					       f->defaultCodingSystem );
      if( NULL != f->find.pattern )
	FindResetPattern( f, f->find.pattern );
    } else if( TRUE == flagHz ){
      if( GuessHz( str, len ) ){
	f->inputCodingSystem = HZ_GB;
	if( NULL != f->find.pattern )
	  FindResetPattern( f, f->find.pattern );
      }
    }
  }

  return str;
}

/*
 * $B8=:_$N%U%!%$%k%]%$%s%?$+$i%U%!%$%k$rFI$_(B,
 * $B;XDj$5$l$?%Z!<%8$^$G%U%!%$%k%]%$%s%?!&%U%l!<%`$r?-$P$9(B.
 * $B;XDj$5$l$?%Z!<%8$^$GC)$j$D$1$J$+$C$?>l9g(B FALSE $B$rJV$9(B.
 *      1) $B%-!<%\!<%I%$%s%?%i%W%H$,$+$+$C$?>l9g$d(B
 *      2) $B%=%1%C%H$KFI$`$Y$-%G!<%?$,$J$$>l9g$r4^$`(B.
 *
 * EOF $B$KE~C#$7$?>l9g(B, $BFI$_=P$7=*N;%U%i%0$r@_Dj$9$k(B.
 *
 * $B"((B n $B%Z!<%8L\$^$G(B stretch $B$7$?;~(B, $B%U%!%$%k%]%$%s%?$O(B n+1 $B%Z!<%8$N@hF,(B.
 */

public boolean_t FileStretch( file_t *f, unsigned int target )
{
  int ch, count;
  unsigned int segment, line;
  offset_t ptr;

  if( TRUE == f->done )
    return FALSE;

  line = 0;
  count = 0;
  ptr = f->lastPtr;
  segment = f->lastSegment;

  if( IobufFseek( &f->fp, ptr, SEEK_SET ) )
    perror( "FileStretch()" ), exit( -1 );

#ifndef MSDOS /* IF NOT DEFINED */
  if( NULL != f->sp.iop ){
    while( EOF != (ch = IobufGetc( &f->sp )) ){
      IobufPutc( ch, &f->fp );
      count++;
#ifdef USE_UTF16
      if( AUTOSELECT == f->inputCodingSystem || UTF_16 == f->inputCodingSystem ){
	/* BOM check */
	int ch2;
	if( EOF != (ch2 = IobufGetc( &f->sp )) ){
	  if( ch == 0xfe && ch2 == 0xff )
	    f->inputCodingSystem = UTF_16BE;
	  else if( ch == 0xff && ch2 == 0xfe )
	    f->inputCodingSystem = UTF_16LE;
	  IobufUngetc( ch2, &f->sp );
	}
      }
      if( IsUtf16Encoding( f->inputCodingSystem ) ) {
	int ch2;
	if( EOF == (ch2 = IobufGetc( &f->sp )) )
	  break;
	IobufPutc( ch2, &f->fp );
	count++;
	if( (f->inputCodingSystem != UTF_16BE && ch == LF && ch2 == 0) ||
	    (f->inputCodingSystem != UTF_16LE && ch == 0 && ch2 == LF) ||
	    count >= (LOAD_SIZE*LOAD_COUNT) )
	  goto label1;
      } else
#endif /* USE_UTF16 */
      if( LF == ch || CR == ch || count == (LOAD_SIZE * LOAD_COUNT) ){
	if( CR == ch ){
	  if( LF != (ch = IobufGetc( &f->sp )) )
	    IobufUngetc( ch, &f->sp );
	  else
	    IobufPutc( LF, &f->fp );
	}
#ifdef USE_UTF16
  label1:
#endif /* USE_UTF16 */
	count = 0;
	if( 0 > (ptr = IobufFtell( &f->fp )) )
	  perror( "FileStretch()" ), exit( -1 );
	if( ++line == LV_PAGE_SIZE ){
	  f->totalLines += line;
	  line = 0;
	  if( 0 == Slot( ++segment ) ){
	    if( FRAME_SIZE == ++f->lastFrame
	       ||
	       NULL == (f->slot[ f->lastFrame ]
			= (offset_t *)malloc( sizeof( offset_t ) * SLOT_SIZE ))
	       ){
	      f->done = TRUE;
	      f->truncated = TRUE;
	      return FALSE;
	    }
	  }
	  f->slot[ f->lastFrame ][ Slot( segment ) ] = ptr;
	  f->lastSegment = segment;
	  f->lastPtr = ptr;
	  if( segment > target )
	    return TRUE;
	}
	if( TRUE == kb_interrupted )
	  return FALSE;
      }
    }
#ifndef _WIN32
    if( -1 != f->pid && IobufFeof( &f->sp ) ){
      int status;
      wait( &status );
    }
#endif /* _WIN32 */
  } else {
#endif /* MSDOS */
    while( EOF != (ch = IobufGetc( &f->fp )) ){
      count++;
#ifdef USE_UTF16
      if( AUTOSELECT == f->inputCodingSystem || UTF_16 == f->inputCodingSystem ){
	/* BOM check */
	int ch2;
	if( EOF != (ch2 = IobufGetc( &f->fp )) ) {
	  if( ch == 0xfe && ch2 == 0xff )
	    f->inputCodingSystem = UTF_16BE;
	  else if( ch == 0xff && ch2 == 0xfe )
	    f->inputCodingSystem = UTF_16LE;
	  IobufUngetc( ch2, &f->fp );
	}
      }
      if( IsUtf16Encoding( f->inputCodingSystem ) ) {
	int ch2;
	if( EOF == (ch2 = IobufGetc( &f->fp )) )
	  break;
	count++;
	if( f->inputCodingSystem != UTF_16BE && ch == CR && ch2 == 0 ){
	  ch = IobufGetc( &f->fp );
	  ch2 = IobufGetc( &f->fp );
	  if (ch != LF || ch2 != 0) { /* Mac style eol */
	    if (IobufFseek( &f->fp, -2, SEEK_CUR ))
	      perror("FileStretch()"), exit(-1);
	  }
	  goto label2;
	} else if( f->inputCodingSystem != UTF_16LE && ch == 0 && ch2 == CR ){
	  ch = IobufGetc( &f->fp );
	  ch2 = IobufGetc( &f->fp );
	  if( ch != 0 || ch2 != LF ){ /* Mac style eol */
	    if( IobufFseek( &f->fp, -2, SEEK_CUR ) )
	      perror("FileStretch()"), exit( -1 );
	  }
	  goto label2;
	} else if( (f->inputCodingSystem != UTF_16BE && ch == LF && ch2 == 0) ||
		   (f->inputCodingSystem != UTF_16LE && ch == 0 && ch2 == LF) ||
	    count >= (LOAD_SIZE*LOAD_COUNT) )
	  goto label2;
      } else
#endif /* USE_UTF16 */
      if( LF == ch || CR == ch || count == (LOAD_SIZE * LOAD_COUNT) ){
	if( CR == ch ){
	  if( LF != (ch = IobufGetc( &f->fp )) )
	    IobufUngetc( ch, &f->fp );
	}
#ifdef USE_UTF16
  label2:
#endif /* USE_UTF16 */
	count = 0;
	if( 0 > (ptr = IobufFtell( &f->fp )) )
	  perror( "FileStretch()" ), exit( -1 );
	if( ++line == LV_PAGE_SIZE ){
	  f->totalLines += line;
	  line = 0;
	  if( 0 == Slot( ++segment ) ){
	    if( FRAME_SIZE == ++f->lastFrame
	       || NULL == (f->slot[ f->lastFrame ]
			   = (offset_t *)malloc( sizeof( offset_t ) * SLOT_SIZE ))
	       ){
	      f->done = TRUE;
	      f->truncated = TRUE;
	      return FALSE;
	    }
	  }
	  f->slot[ f->lastFrame ][ Slot( segment ) ] = ptr;
	  f->lastSegment = segment;
	  f->lastPtr = ptr;
	  if( segment > target )
	    return TRUE;
	}
#ifdef MSDOS
	if( TRUE == allow_interrupt )
	  bdos( 0x0b, 0, 0 );
#endif /* MSDOS */
	if( TRUE == kb_interrupted )
	  return FALSE;
      }
    }
#ifndef MSDOS /* IF NOT DEFINED */
  }
#endif /* MSDOS */

  if( FALSE == kb_interrupted ){
    if( 0 < line || 0 < count ){
      segment++;
      f->totalLines += line;
      f->lastSegment = segment;
      if( 0 > (f->lastPtr = IobufFtell( &f->fp )) )
	perror( "FileStretch()" ), exit( -1 );
    }
    f->done = TRUE;
  }

  if( segment > target )
    return TRUE;
  else
    return FALSE;
}

/*
 * $B;XDj$5$l$?%Z!<%8$^$G%U%!%$%k%]%$%s%?!&%U%l!<%`$r?-$P$7(B,
 * $B%U%!%$%k%]%$%s%?$r%Z!<%8$N@hF,$K@_Dj$9$k(B.
 * $B;XDj$5$l$?%Z!<%8$^$GC)$j$D$1$J$+$C$?>l9g(B FALSE $B$rJV$9(B.
 */

public boolean_t FileSeek( file_t *f, unsigned int segment )
{
  if( segment >= f->lastSegment )
    if( FALSE == FileStretch( f, segment ) )
      return FALSE;

  if( IobufFseek( &f->fp, f->slot[ Frame( segment ) ][ Slot( segment ) ], SEEK_SET ) )
    perror( "FileSeek()" ), exit( -1 );

  return TRUE;
}

public void FileCacheInit( file_t *f )
{
  int i, j;

  for( i = 0 ; i < BLOCK_SIZE ; i++ ){
    f->used[ i ] = FALSE;
    f->page[ i ].lines = 0;
    for( j = 0 ; j < LV_PAGE_SIZE ; j++ ){
      f->page[ i ].line[ j ].heads = 0;
      f->page[ i ].line[ j ].istr = NULL;
      f->page[ i ].line[ j ].head = NULL;
    }
  }
}

public void FileRefresh( file_t *f )
{
  int i, j;

  f->fileNameI18N = NULL;

  for( i = 0 ; i < BLOCK_SIZE ; i++ ){
    for( j = 0 ; j < LV_PAGE_SIZE ; j++ ){
      if( NULL != f->page[ i ].line[ j ].head ){
	free( f->page[ i ].line[ j ].head );
	f->page[ i ].line[ j ].head = NULL;
      }
      if( NULL != f->page[ i ].line[ j ].istr ){
	IstrFree( f->page[ i ].line[ j ].istr );
	f->page[ i ].line[ j ].istr = NULL;
      }
    }
  }

  FileCacheInit( f );
}

public file_t *FileAttach( byte *fileName, stream_t *st,
			  int width, int height,
			  byte inputCodingSystem,
			  byte outputCodingSystem,
			  byte keyboardCodingSystem,
			  byte pathnameCodingSystem,
			  byte defaultCodingSystem )
{
  file_t *f;
  int i;

#ifdef MSDOS
#undef file_t
  if( NULL == (f = (file_t far *)FarMalloc( sizeof( file_t ) )) )
    NotEnoughMemory();
#define file_t file_t far
#else
  f = (file_t *)Malloc( sizeof( file_t ) );
#endif /* MSDOS */

  f->fileName		= fileName;
  f->fileNameI18N	= NULL;
  f->fileNameLength	= 0;

  f->fp.iop		= st->fp;
  f->sp.iop		= st->sp;
#ifdef USE_INTERNAL_IOBUF
  f->fp.cur		= 0;
  f->fp.last		= 0;
  f->fp.ungetc		= EOF;
  f->sp.cur		= 0;
  f->sp.last		= 0;
  f->sp.ungetc		= EOF;
#endif
  f->pid		= st->pid;
  f->lastSegment	= 0;
  f->totalLines		= 0L;
  f->lastPtr		= 0;

  f->lastFrame		= 0;

  f->done		= FALSE;
  f->eof		= FALSE;
  f->top		= TRUE;
  f->bottom		= FALSE;
  f->truncated		= FALSE;
  f->dirty		= FALSE;

  f->find.pattern	= NULL;
  f->find.displayed	= FALSE;

  f->width		= width;
  f->height		= height;

  f->inputCodingSystem		= inputCodingSystem;
  f->outputCodingSystem		= outputCodingSystem;
  f->keyboardCodingSystem	= keyboardCodingSystem;
  f->pathnameCodingSystem	= pathnameCodingSystem;
  f->defaultCodingSystem	= defaultCodingSystem;

  for (i=0; i<BLOCK_SIZE; i++) f->used[i]=FALSE;

  return f;
}

public void FilePreload( file_t *f )
{
  int i;

  for( i = 0 ; i < FRAME_SIZE ; i++ )
    f->slot[ i ] = NULL;

  f->slot[ 0 ]		= (offset_t *)Malloc( sizeof( offset_t ) * SLOT_SIZE );
  f->slot[ 0 ][ 0 ]	= 0;

  FileCacheInit( f );
  FileStretch( f, 0 );
}

/*
 * IstrFreeAll() $B$9$k$3$H$KCm0U(B.
 */
public boolean_t FileFree( file_t *f )
{
  if( NULL == f )
    return FALSE;

#ifdef MSDOS
  FarFree( f );
#else
  free( f );
#endif /* MSDOS */

  IstrFreeAll();

  return TRUE;
}

public boolean_t FileDetach( file_t *f )
{
  int i;

  if( NULL == f )
    return FALSE;

  for( i = 0 ; i < FRAME_SIZE ; i++ ){
    if( NULL != f->slot[ i ] )
      free( f->slot[ i ] );
  }

  FileRefresh( f );

  FileFree( f );

  return TRUE;
}

public byte *FileName( file_t *f )
{
  int length;

  if( NULL == f->fileNameI18N ){
    length = strlen( f->fileName );
    f->fileNameI18N = Decode( IstrAlloc( ZONE_FREE, length + 1 ),
			     f->pathnameCodingSystem,
			     f->fileName,
			     &length );
    f->fileNameLength = length;
  }

  encode_length = CODE_SIZE;
  Encode( f->fileNameI18N, 0, f->fileNameLength,
	 f->outputCodingSystem, FALSE,
	 encode_str, &encode_length );

  return EncodeStripAttribute( encode_str, encode_length );
}

private byte fileStatus[ 256 ];

public byte *FileStatus( file_t *f )
{
  sprintf( fileStatus, "%s %lu/%lu [%s|%s|%s|%s]",
	  FileName( f ),
	  (unsigned long)( 1 + f->screen.top.seg * LV_PAGE_SIZE + f->screen.top.off ),
	  f->totalLines,
	  cTable[ (int)f->inputCodingSystem ].codingSystemName,
	  cTable[ (int)f->keyboardCodingSystem ].codingSystemName,
	  cTable[ (int)f->outputCodingSystem ].codingSystemName,
	  cTable[ (int)f->pathnameCodingSystem ].codingSystemName
	  );

  return fileStatus;
}

public void FileInit()
{
  load_array[ 0 ] = short_str;
}

