/**
 *  TODO:
 *  config.h should be auto-generated.
 *
 **/
// for a ruby gem
#include <ruby.h>

// for our C code
#include <stdio.h>
#include <strings.h>
#include <mpg123.h>
#include "mpg123app.h"
#include "mpg123.h"
#include "local.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "getlopt.h"
#include "buffer.h"
#include "term.h"
#include "playlist.h"
#include "httpget.h"
#include "metaprint.h"
#include "streamdump.h"

#include "debug.h"

#include "pthread.h"

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

// the ruby class that we live in.
static VALUE rubyClassMpg123;

typedef struct mpg123_globals {
  mpg123_handle   *mh;
  int              mode;  //stopped, playing, paused
  pthread_t       playerThread;
} sMPG123Globals;


/* this function is run by the second thread */
void *PlayerThread(void *gPtr)
{
  int             result;
  sMPG123Globals   *p123Globals = (sMPG123Globals *) gPtr;

  // wait for an event from the parent when we have a handle (or create one here)

  result = mpg123_init();
  if(result == MPG123_OK)
  {
    //if(init_output(&pMPG123Globals->ao) >= 0)

      //mh = mpg123_parnew(mp, param.cpu, &result);
      p123Globals->mh = mpg123_parnew(NULL, NULL, &result);
      if(p123Globals->mh != NULL)
      {
          printf("PlayerThread passing control to generic loop\n");
          control_generic(&p123Globals->mh);
      }
      else
      {
        printf("Crap! Cannot get a mpg123 handle: %s", mpg123_plain_strerror(result));
        //safe_exit(77);
      }

  }
  else
  {
    printf("Cannot initialize mpg123 library: %s", mpg123_plain_strerror(result));
  }


  /* increment x to 100 */
  int i = 0;
  for (i = 0; i < 100; i++) {
    sleep(1);
    p123Globals->mode++;
    if (0 == (i % 10)) {
      printf("PlayerThread count %d\n", p123Globals->mode);
      printf("PT:p123Globals = 0x%08x\n", (unsigned int) p123Globals);
    }
  }
  printf("PlayerThread finished\n");

  /* the function must return something - NULL will do */
  return NULL;

}

static void mpg123Ruby_free( void *p)
{
  sMPG123Globals   *p123Globals = (sMPG123Globals *) p;
  printf("mpg123Ruby_free\n");
  // close and free the handle?  I think this is sMPG123Globals *ptr
  // kill thread
  if ( p123Globals != NULL) {
    if (p123Globals->playerThread != 0) {

    }
  }
}

VALUE mpg123Ruby_alloc(VALUE klass) {
  sMPG123Globals  *p123Globals = NULL;
  VALUE           obj;

	printf("new!\n");
  // wrap the C structure in Ruby
  obj = Data_Make_Struct(klass, sMPG123Globals, 0, mpg123Ruby_free, p123Globals);

  // start a thread
  /* create a second thread which executes inc_x(&x) */
  if(pthread_create(&p123Globals->playerThread, NULL, PlayerThread, p123Globals)) {
    fprintf(stderr, "Error creating thread\n");
  }

  return obj;
}


VALUE mpg123Ruby_load(VALUE self, VALUE url)
{
  sMPG123Globals  *p123Globals = NULL;
  int             returnCode = 0;

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  printf("call mpg123_load %s\n", (char*) RSTRING_PTR(url));
  Check_Type(url, T_STRING);
  //INT2FIX(mpg123_load(DATA_PTR(self), (char*) RSTRING_PTR(url)));
  if (p123Globals == NULL)
    return Qnil;

  //
  if(p123Globals->mode != MODE_STOPPED)
  {
    mpg123_close(p123Globals->mh);
    p123Globals->mode = MODE_STOPPED;
  }
  if(mpg123_open(p123Globals->mh, (char*) RSTRING_PTR(url)) != MPG123_OK)
  {
    generic_sendmsg("E Error opening stream: %s", (char*) RSTRING_PTR(url));
    generic_sendmsg("P 0");
    return Qnil;
  }
  mpg123_seek(p123Globals->mh, 0, SEEK_SET); /* This finds ID3v2 at beginning. */
  if(mpg123_meta_check(p123Globals->mh) & MPG123_NEW_ID3)
  {
    generic_sendinfoid3(p123Globals->mh);
  }
  //else generic_sendinfo(arg);

  //if(htd.icy_name.fill) generic_sendmsg("I ICY-NAME: %s", htd.icy_name.p);
  //if(htd.icy_url.fill)  generic_sendmsg("I ICY-URL: %s", htd.icy_url.p);

  p123Globals->mode = MODE_PLAYING;
  //init = 1;
  //generic_sendmsg(mode == MODE_PAUSED ? "P 1" : "P 2");

  return INT2NUM(p123Globals->mode);    // what should success look like?
}

VALUE mpg123Ruby_pause(VALUE self, VALUE url)
{
  sMPG123Globals  *p123Globals = NULL;
  int             returnCode = 0;

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL)
    return Qnil;

  printf("mpg123_pause before %d\n", p123Globals->mode);
  printf("p123Globals = 0x%08x\n", (unsigned int) p123Globals);

  // if not playing, start
  if (p123Globals->mode != MODE_PLAYING) {

  }
  else {
    
  }

  return INT2NUM(p123Globals->mode);
}
void Init_mpg123_ruby(void) {

  // init local things

  // connect to ruby class
  rubyClassMpg123 = rb_define_class("Mpg123_ruby", rb_cObject);

  rb_define_alloc_func(rubyClassMpg123, mpg123Ruby_alloc);
  rb_define_method(rubyClassMpg123, "load", mpg123Ruby_load, 1);
  rb_define_method(rubyClassMpg123, "pause", mpg123Ruby_pause, 0);

}
