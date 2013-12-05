/* mpg123.c for ruby
 */

 // for our C code
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <mpg123.h>
#include "mpg123app.h"
#include "local.h"

#include "common.h"
#include "getlopt.h"
#include "buffer.h"
#include "term.h"
#include "playlist.h"
#include "httpget.h"
#include "metaprint.h"
#include "streamdump.h"

#include "debug.h"

#define MODE_STOPPED 0
#define MODE_PLAYING 1
#define MODE_PAUSED 2

#if 0
void generic_sendmsg (const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
}
#endif
struct parameter param = { 
  FALSE , /* aggressiv */
  FALSE , /* shuffle */
  FALSE , /* remote */
  FALSE , /* remote to stderr */
  DECODE_AUDIO , /* write samples to audio device */
  FALSE , /* silent operation */
  FALSE , /* xterm title on/off */
  0 ,     /* second level buffer size */
  0 ,     /* verbose level */
  DEFAULT_OUTPUT_MODULE,  /* output module */
  NULL,   /* output device */
  0,      /* destination (headphones, ...) */
#ifdef HAVE_TERMIOS
  FALSE , /* term control */
  MPG123_TERM_USR1,
  MPG123_TERM_USR2,
#endif
  FALSE , /* checkrange */
  0 ,   /* force_reopen, always (re)opens audio device for next song */
  /* test_cpu flag is valid for multi and 3dnow.. even if 3dnow is built alone; ensure it appears only once */
  FALSE , /* normal operation */
  FALSE,  /* try to run process in 'realtime mode' */
#ifdef HAVE_WINDOWS_H 
  0, /* win32 process priority */
#endif
  NULL,  /* wav,cdr,au Filename */
  0, /* default is to play all titles in playlist */
  NULL, /* no playlist per default */
  0 /* condensed id3 per default */
  ,0 /* list_cpu */
  ,NULL /* cpu */ 
#ifdef FIFO
  ,NULL
#endif
  ,0 /* timeout */
  ,1 /* loop */
  ,0 /* delay */
  ,0 /* index */
  /* Parameters for mpg123 handle, defaults are queried from library! */
  ,0 /* down_sample */
  ,0 /* rva */
  ,0 /* halfspeed */
  ,0 /* doublespeed */
  ,0 /* start_frame */
  ,-1 /* frame_number */
  ,0 /* outscale */
  ,0 /* flags */
  ,0 /* force_rate */
  ,1 /* ICY */
  ,1024 /* resync_limit */
  ,0 /* smooth */
  ,0.0 /* pitch */
  ,0 /* appflags */
  ,NULL /* proxyurl */
  ,0 /* keep_open */
  ,0 /* force_utf8 */
  ,INDEX_SIZE
  ,NULL /* force_encoding */
  ,1. /* preload */
  ,-1 /* preframes */
  ,-1 /* gain */
  ,NULL /* stream dump file */
  ,0 /* ICY interval */
};

int OutputDescriptor;   // an output file or I hope this is not used.
off_t framenum;
off_t frames_left;
audio_output_t *ao = NULL;
txfermem *buffermem = NULL;
char *prgName = NULL;

/* ThOr: pointers are not TRUE or FALSE */
char *equalfile = NULL;
struct httpdata htd;
int fresh = TRUE;
int have_output = FALSE; /* If we are past the output init step. */
FILE* aux_out = NULL; /* Output for interesting information, normally on stdout to be parseable. */

int buffer_fd[2];
int buffer_pid;
size_t bufferblock = 0;
static int intflag = FALSE;
static int skip_tracks = 0;
static int filept = -1;
static int network_sockets_used = 0; /* Win32 socket open/close Support */
mpg123_handle *mh = NULL;
/* Cleanup marker to know that we intiialized libmpg123 already. */
static int cleanup_mpg123 = FALSE;

static void generic_sendv1(mpg123_id3v1 *v1, const char *prefix)
{
  int i;
  char info[125] = "";
  memcpy(info,    v1->title,   30);
  memcpy(info+30, v1->artist,  30);
  memcpy(info+60, v1->album,   30);
  memcpy(info+90, v1->year,     4);
  memcpy(info+94, v1->comment, 30);

  for(i=0;i<124; ++i) if(info[i] == 0) info[i] = ' ';
  info[i] = 0;
  //generic_sendmsg("%s ID3:%s%s", prefix, info, (v1->genre<=genre_count) ? genre_table[v1->genre] : "Unknown");
  generic_sendmsg("%s ID3.genre:%i", prefix, v1->genre);
  if(v1->comment[28] == 0 && v1->comment[29] != 0)
    generic_sendmsg("%s ID3.track:%i", prefix, (unsigned char)v1->comment[29]);
}

static void generic_sendinfoid3(mpg123_handle *mh)
{
  mpg123_id3v1 *v1;
  mpg123_id3v2 *v2;
  if(MPG123_OK != mpg123_id3(mh, &v1, &v2))
  {
    generic_sendmsg("Cannot get ID3 data: %s", mpg123_strerror(mh));
    return;
  }
  if(v1 != NULL)
  {
    generic_sendv1(v1, "I");
  }
  if(v2 != NULL)
  {
    generic_sendmsg("I ID3v2.title:%s",   v2->title);
    generic_sendmsg("I ID3v2.artist:%s",  v2->artist);
    generic_sendmsg("I ID3v2.album:%s",   v2->album);
    generic_sendmsg("I ID3v2.year:%s",    v2->year);
    generic_sendmsg("I ID3v2.comment:%s", v2->comment);
    generic_sendmsg("I ID3v2.genre:%s",   v2->genre);
  }
}


/*
 *   Change the playback sample rate.
 *   Consider that changing it after starting playback is not covered by gapless code!
 */
static void reset_audio(long rate, int channels, int format)
{
#ifndef NOXFERMEM
  if (param.usebuffer) {
    /* wait until the buffer is empty,
     * then tell the buffer process to
     * change the sample rate.   [OF]
     */
    while (xfermem_get_usedspace(buffermem) > 0)
      if (xfermem_block(XF_WRITER, buffermem) == XF_CMD_TERMINATE) {
        intflag = TRUE;
        break;
      }
    buffermem->freeindex = -1;
    buffermem->readindex = 0; /* I know what I'm doing! ;-) */
    buffermem->freeindex = 0;
    if (intflag)
      return;
    buffermem->rate     = pitch_rate(rate); 
    buffermem->channels = channels; 
    buffermem->format   = format;
    buffer_reset();
  }
  else 
  {
#endif
    if(ao == NULL)
    {
      error("Audio handle should not be NULL here!");
      safe_exit(98);
    }
    ao->rate     = pitch_rate(rate); 
    ao->channels = channels; 
    ao->format   = format;
    if(reset_output(ao) < 0)
    {
      error1("failed to reset audio device: %s", strerror(errno));
      safe_exit(1);
    }
#ifndef NOXFERMEM
  }
#endif
}


// called by control_generic
static int open_track_fd (void)
{
  /* Let reader handle invalid filept */
  if(mpg123_open_fd(mh, filept) != MPG123_OK)
  {
    error2("Cannot open fd %i: %s", filept, mpg123_strerror(mh));
    return 0;
  }
  debug("Track successfully opened.");
  fresh = TRUE;
  return 1;
  /*1 for success, 0 for failure */
}

/* 1 on success, 0 on failure */
int open_track(char *fname)
{
  filept=-1;
  httpdata_reset(&htd);
  if(MPG123_OK != mpg123_param(mh, MPG123_ICY_INTERVAL, 0, 0))
  error1("Cannot (re)set ICY interval: %s", mpg123_strerror(mh));
  if(!strcmp(fname, "-"))
  {
    filept = STDIN_FILENO;
    return open_track_fd();
  }
  else if (!strncmp(fname, "http://", 7)) /* http stream */
  {
  filept = http_open(fname, &htd);

  network_sockets_used = 1;
/* utf-8 encoded URLs might not work under Win32 */
    
    /* now check if we got sth. and if we got sth. good */
    if(    (filept >= 0) && (htd.content_type.p != NULL)
        && !APPFLAG(MPG123APP_IGNORE_MIME) && !(debunk_mime(htd.content_type.p) & IS_FILE) )
    {
      error1("Unknown mpeg MIME type %s - is it perhaps a playlist (use -@)?", htd.content_type.p == NULL ? "<nil>" : htd.content_type.p);
      error("If you know the stream is mpeg1/2 audio, then please report this as "PACKAGE_NAME" bug");
      return 0;
    }
    if(filept < 0)
    {
      error1("Access to http resource %s failed.", fname);
      return 0;
    }
    if(MPG123_OK != mpg123_param(mh, MPG123_ICY_INTERVAL, htd.icy_interval, 0))
    error1("Cannot set ICY interval: %s", mpg123_strerror(mh));
    if(param.verbose > 1) fprintf(stderr, "Info: ICY interval %li\n", (long)htd.icy_interval);
  }

  if(param.icy_interval > 0)
  {
    if(MPG123_OK != mpg123_param(mh, MPG123_ICY_INTERVAL, param.icy_interval, 0))
    error1("Cannot set ICY interval: %s", mpg123_strerror(mh));
    if(param.verbose > 1) fprintf(stderr, "Info: Forced ICY interval %li\n", param.icy_interval);
  }

  debug("OK... going to finally open.");
  /* Now hook up the decoder on the opened stream or the file. */
  if(network_sockets_used) 
  {
    return open_track_fd();
  }
  else if(mpg123_open(mh, fname) != MPG123_OK)
  {
    error2("Cannot open %s: %s", fname, mpg123_strerror(mh));
    return 0;
  }
  debug("Track successfully opened.");

  fresh = TRUE;
  return 1;
}

/* for symmetry */
void close_track(void)
{
  mpg123_close(mh);
  network_sockets_used = 0;
  if(filept > -1) close(filept);
  filept = -1;
}

/* return 1 on success, 0 on failure */
int play_frame(void)
{
  unsigned char *audio;
  int mc;
  size_t bytes;
  debug("play_frame");
  /* The first call will not decode anything but return MPG123_NEW_FORMAT! */
  mc = mpg123_decode_frame(mh, &framenum, &audio, &bytes);
  /* Play what is there to play (starting with second decode_frame call!) */
  if(bytes)
  {
    if(param.frame_number > -1) --frames_left;
    if(fresh && framenum >= param.start_frame)
    {
      fresh = FALSE;
    }
    /* Normal flushing of data, includes buffer decoding. */
    if(flush_output(ao, audio, bytes) < (int)bytes && !intflag)
    {
      error("Deep trouble! Cannot flush to my output anymore!");
      safe_exit(133);   // TOTO - recover better
    }
    if(param.checkrange)
    {
      long clip = mpg123_clip(mh);
      if(clip > 0) fprintf(stderr,"%ld samples clipped\n", clip);
    }
  }
  /* Special actions and errors. */
  if(mc != MPG123_OK)
  {
    if(mc == MPG123_ERR || mc == MPG123_DONE)
    {
      if(mc == MPG123_ERR) error1("...in decoding next frame: %s", mpg123_strerror(mh));
      return 0;
    }
    if(mc == MPG123_NO_SPACE)
    {
      error("I have not enough output space? I didn't plan for this.");
      return 0;
    }
    if(mc == MPG123_NEW_FORMAT)
    {
      long rate;
      int channels, format;
      mpg123_getformat(mh, &rate, &channels, &format);
      if(param.verbose > 2) fprintf(stderr, "\nNote: New output format %liHz %ich, format %i\n", rate, channels, format);

      if(!param.quiet)
      {
        fprintf(stderr, "\n");
        if(param.verbose) print_header(mh);
        else print_header_compact(mh);
      }
      reset_audio(rate, channels, format);
    }
  }
  return 1;
}


// TODO, this should probably not exit
void safe_exit(int code)
{
  char *dummy, *dammy;

  dump_close();
#ifdef HAVE_TERMIOS
  if(param.term_ctrl)
    term_restore();
#endif
  if(have_output) exit_output(ao, intflag);

  if(mh != NULL) mpg123_delete(mh);

  if(cleanup_mpg123) mpg123_exit();

  httpdata_free(&htd);

  /* It's ugly... but let's just fix this still-reachable memory chunk of static char*. */
  split_dir_file("", &dummy, &dammy);
  exit(code);
}


