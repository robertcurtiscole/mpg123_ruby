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

#include "pthread.h"

#define MODE_STOPPED 0
#define MODE_PLAYING 1
#define MODE_PAUSED 2
extern int playerMode;
extern audio_output_t *ao;
extern mpg123_handle *mh;
static char cwd[1024];
extern char newurl[1024];
char *binpath; /* Path to myself. */
extern int have_output; /* If we are past the output init step. */
// the ruby class that we live in.
static VALUE rubyClassMpg123;

extern pthread_mutex_t new_cmd_ready;
extern char new_cmd;
typedef struct mpg123_globals {
  mpg123_handle   *mh;
  int             *mode;  //stopped, playing, paused
  pthread_t       playerThread;
  pthread_mutex_t *pnew_cmd_ready;
  char            *title;
  double          saved_volume;
  char            muted;
} sMPG123Globals;

//!
//! @brief     This is thethread that plays the media
//! @param[in] sMPG123Globals * pointer to existing struct
//! @param[out] none
//! @return 
//!     NULL          Function must return something
//! @details
//!     This function is essentially the background thread
//!     It manages the MPG123 player.
//!     It is the audio loop that decodes and plays media.
//!     The audio loop is in control_generic()
//!
void *PlayerThread(void *gPtr)
{
  int             result;
  sMPG123Globals   *p123Globals = (sMPG123Globals *) gPtr;

  if (gPtr == NULL) 
  {
    return (void*)NULL;
  }

  p123Globals->muted = 0;

  // wait for an event from the parent when we have a handle (or create one here)
  printf("PT: PlayerThread started");
  result = mpg123_init();
  if(result == MPG123_OK)
  {
      printf("PT: after mpg123_init");
      //if(init_output(&pMPG123Globals->ao) >= 0)
      getcwd(cwd, sizeof(cwd));
      binpath = cwd;

      //mh = mpg123_parnew(mp, param.cpu, &result);
      p123Globals->mh = mpg123_parnew(NULL, NULL, &result);
      p123Globals->mode = &player_mode;
      p123Globals->pnew_cmd_ready = &new_cmd_ready;
      pthread_mutex_trylock(&new_cmd_ready);   // init in locked state if it's not there already. (should do this outside.)

      printf("PT: after locking mutex");
      if(p123Globals->mh != NULL)
      {
          mh = p123Globals->mh;   // gross global

          /* Init audio as early as possible.
             If there is the buffer process to be spawned, it shouldn't carry the mpg123_handle with it. */
          bufferblock = mpg123_safe_buffer(); /* Can call that before mpg123_init(), it's stateless. */
          printf("PT: after getting safe_buffer");
          if(init_output(&ao) < 0)
          {
            error("Failed to initialize output, goodbye.");
            // mpg123_delete_pars(mp);
            return (void*)NULL; /* It's safe here... nothing nasty happened yet. */  // TODO don't return
          }
          have_output = TRUE;

          /* ========================================================================================================= */
          /* Enterning the leaking zone... we start messing with stuff here that should be taken care of when leaving. */
          /* Don't just exit() or return out...                                                                        */
          /* ========================================================================================================= */
          printf("PT: after init_output");
          httpdata_init(&htd);

          /* Now either check caps myself or query buffer for that. */
          audio_capabilities(ao, mh);

          load_equalizer(mh);

          printf("PlayerThread passing control to generic loop\n");
          control_generic(mh);        // this returns on exit
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

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  printf("call mpg123_load %s\n", (char*) RSTRING_PTR(url));
  Check_Type(url, T_STRING);
  //INT2FIX(mpg123_load(DATA_PTR(self), (char*) RSTRING_PTR(url)));
  if (p123Globals == NULL)
    return Qnil;

  // get access, put in new load command and release the new_cmd mutex/lock
  strcpy(newurl, (char*) RSTRING_PTR(url));
  pthread_mutex_trylock(&new_cmd_ready);
  new_cmd = 'L';
  pthread_mutex_unlock(&new_cmd_ready);

  return INT2NUM(*p123Globals->mode);    // what should success look like?
}

VALUE mpg123Ruby_pause(VALUE self)
{
  sMPG123Globals  *p123Globals = NULL;

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL)
    return Qnil;

  printf("mpg123_pause before %d\n", *p123Globals->mode);
  //printf("p123Globals = 0x%08x\n", (unsigned int) p123Globals);

  // if not playing, start
  if(*p123Globals->mode != MODE_STOPPED)
  { 
    if (*p123Globals->mode == MODE_PLAYING) {
      *p123Globals->mode = MODE_PAUSED;
      if(param.usebuffer) buffer_stop();
      //generic_sendmsg("P 1");
    } else {
      *p123Globals->mode = MODE_PLAYING;
      if(param.usebuffer) buffer_start();
      // set simple command to run
      pthread_mutex_trylock(&new_cmd_ready);
      new_cmd = (char) NULL;   // just wake up
      pthread_mutex_unlock(&new_cmd_ready);

      //generic_sendmsg("P 2");
    }
  }

  return INT2NUM(*p123Globals->mode);
}

double GetOutputVolume(sMPG123Globals *p123Globals)
{
  double v;
  // get current volume
  if (p123Globals->muted)
    v = p123Globals->saved_volume;
  else
    mpg123_getvolume(p123Globals->mh, &v, NULL, NULL); /* Necessary? */
  return v;  
}

// set/get volume;  input/output volume is 0-100.
// internally, volume is 0-1.0
VALUE mpg123Ruby_volume(int argc, VALUE *argv, VALUE self)
{
  double  v;
  VALUE   newvol;
  sMPG123Globals  *p123Globals = NULL;

  rb_scan_args(argc, argv, "01", &newvol);    // throws exception if wrong # ags (0 req, 1 optional)

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL)
    return Qnil;
  // set new, return new
  if (newvol != Qnil) {
    v = NUM2DBL(newvol);
    if (p123Globals->muted)
      p123Globals->saved_volume = v/100.0;
    else
      mpg123_volume(p123Globals->mh, v/100);
  }
  return DBL2NUM(GetOutputVolume(p123Globals)*100);
}

// jump to frame num,
// return frame - new current position
VALUE mpg123Ruby_frame(int argc, VALUE *argv, VALUE self)
{
  VALUE   frame_num;
  sMPG123Globals  *p123Globals = NULL;
  off_t   offset = 0;
  off_t oldpos;

  rb_scan_args(argc, argv, "01", &frame_num);    // throws exception if wrong # ags (0 req, 1 optional)

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL)
    return Qnil;
  // set new, return new
  if (frame_num != Qnil) {
    offset = NUM2INT(frame_num);
  }

  if(playerMode == MODE_STOPPED)
  {
    return Qnil;
  }
  oldpos = framenum;

  if(0 > (framenum = mpg123_seek_frame(p123Globals->mh, offset, SEEK_SET)))
  {
    //generic_sendmsg("E Error while seeking");
    mpg123_seek_frame(p123Globals->mh, 0, SEEK_SET);
  }
  if(param.usebuffer) buffer_resync();

  if(framenum <= oldpos) mpg123_meta_free(p123Globals->mh);

  return INT2NUM(framenum);
}

VALUE mpg123Ruby_mute(VALUE self)
{
  sMPG123Globals  *p123Globals = NULL;
  double          current_volume;

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL) return Qnil;
  mpg123_getvolume(p123Globals->mh, &current_volume, NULL, NULL); /* Necessary? */

  // if muted, unmute, etc
  if (p123Globals->muted)
  {
    p123Globals->muted = 0;
    mpg123_volume(p123Globals->mh, p123Globals->saved_volume);
  }
  else 
  {
    p123Globals->muted = 1;
    p123Globals->saved_volume = current_volume;
    mpg123_volume(p123Globals->mh, (double) 0.0);  
  }

  return INT2NUM(p123Globals->muted);
}

VALUE mpg123Ruby_shuffle(VALUE self)
{
  sMPG123Globals  *p123Globals = NULL;

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL)
    return Qnil;

  return INT2NUM(*p123Globals->mode);
}

 VALUE mpg123Ruby_loopsong(VALUE self)
{
  sMPG123Globals  *p123Globals = NULL;

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL)
    return Qnil;

  return INT2NUM(*p123Globals->mode);
}

 VALUE mpg123Ruby_looplist(VALUE self)
{
  sMPG123Globals  *p123Globals = NULL;

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL)
    return Qnil;

  return INT2NUM(*p123Globals->mode);
}

 
/*
 *  This can be a ruby function
 */
VALUE mpg123Ruby_state(VALUE self)
{
  sMPG123Globals  *p123Globals = NULL;

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL)
    return Qnil;

  return INT2NUM(p123Globals->mode);
}

//
// info return a hash of
// mode:
// source-type: 
// source:
// volume:
// mute:
// shuffle:
// loopsong:
// looplist:
// position {...}
//  frame:
//  num-frames:
//  seconds:
//  num-seconds:
// position stuff:
//@F 5808 2361 151.72 61.68
//@F 5809 2360 151.75 61.65
//@F 5810 2359 151.77 61.62
// song {....}
/***
    track-name:   song:
    artist-name:  artist:
    album-name:   album:
    track-num:    track:
***/
VALUE mpg123Ruby_info(VALUE self)
{
  sMPG123Globals  *p123Globals = NULL;
  VALUE           rbHash;
  VALUE           songHash;
  off_t           pos;
  off_t           len;
  mpg123_id3v1 *v1;
  mpg123_id3v2 *v2;
  off_t           current_frame, frames_left;
  double          current_seconds, seconds_left;

// get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL)
    return Qnil;

  // get state of the player and return a hash
  rbHash = rb_hash_new();
  // player state
  rb_hash_aset(rbHash, rb_str_new2("mode"),         INT2NUM(*p123Globals->mode));
  rb_hash_aset(rbHash, rb_str_new2("source-type"),  INT2NUM(0));
  rb_hash_aset(rbHash, rb_str_new2("source"),       rb_str_new2(newurl));
#if 0
  pos = mpg123_tell(p123Globals->mh);
  len = mpg123_length(p123Globals->mh);
  rb_hash_aset(rbHash, rb_str_new2("frame"),        INT2NUM(pos));
  rb_hash_aset(rbHash, rb_str_new2("num-frames"),   INT2NUM(len));
#endif
  rb_hash_aset(rbHash, rb_str_new2("volume"),       DBL2NUM(GetOutputVolume(p123Globals)));
  rb_hash_aset(rbHash, rb_str_new2("mute"),         INT2NUM(p123Globals->muted));
  
  rb_hash_aset(rbHash, rb_str_new2("shuffle"),  INT2NUM(0));
  rb_hash_aset(rbHash, rb_str_new2("loopsong"), INT2NUM(0));
  rb_hash_aset(rbHash, rb_str_new2("looplist"), INT2NUM(0));

  // position info

  if(!mpg123_position(p123Globals->mh, 0, xfermem_get_usedspace(buffermem), &current_frame, &frames_left, &current_seconds, &seconds_left)) 
  {
    VALUE positionHash = rb_hash_new();
    //generic_sendmsg("F %"OFF_P" %"OFF_P" %3.2f %3.2f", (off_p)current_frame, (off_p)frames_left, current_seconds, seconds_left);
    rb_hash_aset(positionHash, rb_str_new2("seconds"),      DBL2NUM(current_seconds));
    rb_hash_aset(positionHash, rb_str_new2("num-seconds"),  DBL2NUM(current_seconds+seconds_left));
    rb_hash_aset(positionHash, rb_str_new2("frame"),        INT2NUM(current_frame));
    rb_hash_aset(positionHash, rb_str_new2("num-frames"),   INT2NUM(current_frame+frames_left));
    rb_hash_aset(rbHash, rb_str_new2("position"), positionHash);
  }
 
  // song info
  songHash = rb_hash_new();
  if(MPG123_OK == mpg123_id3(p123Globals->mh, &v1, &v2))
  {
    if(v1 != NULL && v2 == NULL)
    {
      rb_hash_aset(songHash, rb_str_new2("track-name"),   rb_str_new2(v1->title));
      rb_hash_aset(songHash, rb_str_new2("artist-name"),  rb_str_new2(v1->artist));
      rb_hash_aset(songHash, rb_str_new2("album-name"),   rb_str_new2(v1->album));
    }
    else if(v2 != NULL)
    {
      //generic_sendmsg("I ID3v2.year:%s",    v2->year);
      //generic_sendmsg("I ID3v2.comment:%s", v2->comment);
      //generic_sendmsg("I ID3v2.genre:%s",   v2->genre);
      if (v2->title != NULL)
        rb_hash_aset(songHash, rb_str_new2("track-name"),   rb_str_new2(v2->title->p));
      else if (v1 != NULL)
        rb_hash_aset(songHash, rb_str_new2("track-name"),   rb_str_new2(v1->title));
      if (v2->artist != NULL)
        rb_hash_aset(songHash, rb_str_new2("artist-name"),  rb_str_new2(v2->artist->p));
      else if (v1 != NULL)
        rb_hash_aset(songHash, rb_str_new2("artist-name"),  rb_str_new2(v1->artist));
      if (v2->album != NULL)
        rb_hash_aset(songHash, rb_str_new2("album-name"),   rb_str_new2(v2->album->p));
      else if (v1 != NULL)
        rb_hash_aset(songHash, rb_str_new2("album-name"),   rb_str_new2(v1->album));
    }
    if (v1 != NULL)
    {
      if(v1->comment[28] == 0 && v1->comment[29] != 0)
        rb_hash_aset(songHash, rb_str_new2("track-num"),    INT2NUM((unsigned char)v1->comment[29]));
    }
  }

  rb_hash_aset(rbHash, rb_str_new2("song"), songHash);

  return rbHash;
}

void testprogram(void);

void Init_mpg123_ruby(void) {

  // init local things

  // connect to ruby class
  rubyClassMpg123 = rb_define_class("Mpg123_ruby", rb_cObject);

  rb_define_alloc_func(rubyClassMpg123, mpg123Ruby_alloc);
  rb_define_method(rubyClassMpg123, "load",   mpg123Ruby_load, 1);
  rb_define_method(rubyClassMpg123, "pause",  mpg123Ruby_pause, 0);
  //rb_define_method(rubyClassMpg123, "stop",   mpg123Ruby_stop, 0);
  //rb_define_method(rubyClassMpg123, "play",   mpg123Ruby_play, 0);

  rb_define_method(rubyClassMpg123, "info",   mpg123Ruby_info, 0);
  rb_define_method(rubyClassMpg123, "frame",  mpg123Ruby_frame, -1);
  //rb_define_method(rubyClassMpg123, "equalizer",   mpg123Ruby_equalizer, 0);

  rb_define_method(rubyClassMpg123, "volume", mpg123Ruby_volume, -1);
  rb_define_method(rubyClassMpg123, "mute",   mpg123Ruby_mute, 0);

  rb_define_method(rubyClassMpg123, "shuffle",  mpg123Ruby_shuffle, 1);
  rb_define_method(rubyClassMpg123, "loopsong", mpg123Ruby_loopsong, 0);
  rb_define_method(rubyClassMpg123, "looplist", mpg123Ruby_looplist, 0);

  // plus we'll need tracknum and next, previous track in the playlist
  testprogram();
}


/**
*!
*!  pause()           pause/resume
*!  stop()            stop.  not resumable
*!  tell()            return where we are (sample and num samples)
*!  seek(frame)       go that spot or relative frame or jump
*!  volume[new]       set volume if given; return volume
*!  mute()            silence the audio  - set volume 0 or does mute toggle?
*!  load(uri)         load file or url start playing
*!  eq(...)           set equalier, return current?
*!  tags()            return tags for this file
*!  info()
*!  format()          return sample rate in hz and channel
*!  state()           return 0, 1, 2.  stopped, playing, paused
*!
**/

extern FILE* aux_out; /* Output for interesting information, normally on stdout to be parseable. */
/* File-global storage of command line arguments.
   They may be needed for cleanup after charset conversion. */
static char **argv = NULL;
static int    argc = 0;
/* Cleanup marker to know that we intiialized libmpg123 already. */
static int cleanup_mpg123 = FALSE;
topt opts[] = {
   {0, 0, 0, 0, 0, 0}
};
extern char *prgName;

void usage(int param)
{
  printf("Usage: good luck!\r\n");
}

static void print_title(FILE *o)
{
  fprintf(o, "High Performance MPEG 1.0/2.0/2.5 Audio Player for Layers 1, 2 and 3\n");
  fprintf(o, "\tversion %s; written and copyright by Michael Hipp and others\n", PACKAGE_VERSION);
  fprintf(o, "\tfree software (LGPL) without any warranty but with best wishes\n");
}


void testprogram(void)
{
int sys_argc = 1;
char *arrrg = "testprogram";
char ** sys_argv = &arrrg;
  int result;
  char end_of_files = FALSE;
  long parr;
  //char *fname;
  int libpar = 0;
  mpg123_pars *mp;
#if !defined(WIN32) && !defined(GENERIC)
  struct timeval start_time;
#endif
  aux_out = stdout; /* Need to initialize here because stdout is not a constant?! */
#if defined (WANT_WIN32_UNICODE)
  if(win32_cmdline_utf8(&argc, &argv) != 0)
  {
    error("Cannot convert command line to UTF8!");
    safe_exit(76);
  }
#else
  argv = sys_argv;
  argc = sys_argc;
#endif
#if defined (WANT_WIN32_SOCKETS)
  win32_net_init();
#endif

  /* Extract binary and path, take stuff before/after last / or \ . */
  if((prgName = strrchr(argv[0], '/')) || (prgName = strrchr(argv[0], '\\')))
  {
    /* There is some explicit path. */
    prgName[0] = 0; /* End byte for path. */
    prgName++;
    binpath = argv[0];
  }
  else
  {
    prgName = argv[0]; /* No path separators there. */
    binpath = NULL; /* No path at all. */
  }

  /* Need to initialize mpg123 lib here for default parameter values. */

  result = mpg123_init();
  if(result != MPG123_OK)
  {
    error1("Cannot initialize mpg123 library: %s", mpg123_plain_strerror(result));
    safe_exit(77);
  }
  cleanup_mpg123 = TRUE;

  mp = mpg123_new_pars(&result); /* This may get leaked on premature exit(), which is mainly a cosmetic issue... */
  if(mp == NULL)
  {
    error1("Crap! Cannot get mpg123 parameters: %s", mpg123_plain_strerror(result));
    safe_exit(78);
  }

  /* get default values */
  mpg123_getpar(mp, MPG123_DOWN_SAMPLE, &parr, NULL);
  param.down_sample = (int) parr;
  mpg123_getpar(mp, MPG123_RVA, &param.rva, NULL);
  mpg123_getpar(mp, MPG123_DOWNSPEED, &param.halfspeed, NULL);
  mpg123_getpar(mp, MPG123_UPSPEED, &param.doublespeed, NULL);
  mpg123_getpar(mp, MPG123_OUTSCALE, &param.outscale, NULL);
  mpg123_getpar(mp, MPG123_FLAGS, &parr, NULL);
  mpg123_getpar(mp, MPG123_INDEX_SIZE, &param.index_size, NULL);
  param.flags = (int) parr;
  param.flags |= MPG123_SEEKBUFFER; /* Default on, for HTTP streams. */
  mpg123_getpar(mp, MPG123_RESYNC_LIMIT, &param.resync_limit, NULL);
  mpg123_getpar(mp, MPG123_PREFRAMES, &param.preframes, NULL);

#ifdef OS2
        _wildcard(&argc,&argv);
#endif

  while ((result = getlopt(argc, argv, opts)))
  switch (result) {
    case GLO_UNKNOWN:
      fprintf (stderr, "%s: Unknown option \"%s\".\n", 
        prgName, loptarg);
      usage(1);
    case GLO_NOARG:
      fprintf (stderr, "%s: Missing argument for option \"%s\".\n",
        prgName, loptarg);
      usage(1);
  }
  /* Do this _after_ parameter parsing. */
  check_locale(); /* Check/set locale; store if it uses UTF-8. */

  if(param.list_cpu)
  {
    const char **all_dec = mpg123_decoders();
    printf("Builtin decoders:");
    while(*all_dec != NULL){ printf(" %s", *all_dec); ++all_dec; }
    printf("\n");
    mpg123_delete_pars(mp);
    return;
  }
  if(param.test_cpu)
  {
    const char **all_dec = mpg123_supported_decoders();
    printf("Supported decoders:");
    while(*all_dec != NULL){ printf(" %s", *all_dec); ++all_dec; }
    printf("\n");
    mpg123_delete_pars(mp);
    return;
  }
  if(param.gain != -1)
  {
      warning("The parameter -g is deprecated and may be removed in the future.");
  }

  if (loptind >= argc && !param.listname && !param.remote) usage(1);
  /* Init audio as early as possible.
     If there is the buffer process to be spawned, it shouldn't carry the mpg123_handle with it. */
  bufferblock = mpg123_safe_buffer(); /* Can call that before mpg123_init(), it's stateless. */
  if(init_output(&ao) < 0)
  {
    error("Failed to initialize output, goodbye.");
    mpg123_delete_pars(mp);
    return; // 99; /* It's safe here... nothing nasty happened yet. */
  }
  have_output = TRUE;

  /* ========================================================================================================= */
  /* Enterning the leaking zone... we start messing with stuff here that should be taken care of when leaving. */
  /* Don't just exit() or return out...                                                                        */
  /* ========================================================================================================= */

  httpdata_init(&htd);

#if !defined(WIN32) && !defined(GENERIC)
  if (param.remote)
  {
    param.verbose = 0;
    param.quiet = 1;
    param.flags |= MPG123_QUIET;
  }
#endif

  /* Set the frame parameters from command line options */
  if(param.quiet) param.flags |= MPG123_QUIET;

#ifdef OPT_3DNOW
  //if(dnow != 0) param.cpu = (dnow == SET_3DNOW) ? "3dnow" : "i586";
  param.cpu = "i586";
#endif
  if(param.cpu != NULL && (!strcmp(param.cpu, "auto") || !strcmp(param.cpu, ""))) param.cpu = NULL;
  if(!(  MPG123_OK == (result = mpg123_par(mp, MPG123_VERBOSE, param.verbose, 0))
      && ++libpar
      && MPG123_OK == (result = mpg123_par(mp, MPG123_FLAGS, param.flags, 0))
      && ++libpar
      && MPG123_OK == (result = mpg123_par(mp, MPG123_DOWN_SAMPLE, param.down_sample, 0))
      && ++libpar
      && MPG123_OK == (result = mpg123_par(mp, MPG123_RVA, param.rva, 0))
      && ++libpar
      && MPG123_OK == (result = mpg123_par(mp, MPG123_FORCE_RATE, param.force_rate, 0))
      && ++libpar
      && MPG123_OK == (result = mpg123_par(mp, MPG123_DOWNSPEED, param.halfspeed, 0))
      && ++libpar
      && MPG123_OK == (result = mpg123_par(mp, MPG123_UPSPEED, param.doublespeed, 0))
      && ++libpar
      && MPG123_OK == (result = mpg123_par(mp, MPG123_ICY_INTERVAL, 0, 0))
      && ++libpar
      && MPG123_OK == (result = mpg123_par(mp, MPG123_RESYNC_LIMIT, param.resync_limit, 0))
      && ++libpar
      && MPG123_OK == (result = mpg123_par(mp, MPG123_TIMEOUT, param.timeout, 0))
      && ++libpar
      && MPG123_OK == (result = mpg123_par(mp, MPG123_OUTSCALE, param.outscale, 0))
      && ++libpar
      && MPG123_OK == (result = mpg123_par(mp, MPG123_PREFRAMES, param.preframes, 0))
      ))
  {
    error2("Cannot set library parameter %i: %s", libpar, mpg123_plain_strerror(result));
    return; //safe_exit(45);
  }
  if (!(param.listentry < 0) && !param.quiet) print_title(stderr); /* do not pollute stdout! */

  {
    long default_index;
    mpg123_getpar(mp, MPG123_INDEX_SIZE, &default_index, NULL);
    if( param.index_size != default_index && (result = mpg123_par(mp, MPG123_INDEX_SIZE, param.index_size, 0.)) != MPG123_OK )
    error1("Setting of frame index size failed: %s", mpg123_plain_strerror(result));
  }

  if(param.force_rate && param.down_sample)
  {
    error("Down sampling and fixed rate options not allowed together!");
    return; //safe_exit(1);
  }

  /* Now actually get an mpg123_handle. */
  mh = mpg123_parnew(mp, param.cpu, &result);
  if(mh == NULL)
  {
    error1("Crap! Cannot get a mpg123 handle: %s", mpg123_plain_strerror(result));
    return; //safe_exit(77);
  }
  mpg123_delete_pars(mp); /* Don't need the parameters anymore ,they're in the handle now. */

  /* Prepare stream dumping, possibly replacing mpg123 reader. */
  if(dump_open(mh) != 0) return; //safe_exit(78);

  /* Now either check caps myself or query buffer for that. */
  audio_capabilities(ao, mh);

  load_equalizer(mh);

}