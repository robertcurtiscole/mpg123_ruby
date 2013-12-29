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
} sMPG123Globals;


/* this function is run by the second thread */
void *PlayerThread(void *gPtr)
{
  int             result;
  sMPG123Globals   *p123Globals = (sMPG123Globals *) gPtr;
  int i = 0;

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
      p123Globals->mode = &playerMode;
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
            return 99; /* It's safe here... nothing nasty happened yet. */  // TODO don't return
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
          control_generic(mh);
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
  int             returnCode = 0;

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

VALUE mpg123Ruby_pause(VALUE self, VALUE url)
{
  sMPG123Globals  *p123Globals = NULL;
  int             returnCode = 0;

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL)
    return Qnil;

  printf("mpg123_pause before %d\n", *p123Globals->mode);
  printf("p123Globals = 0x%08x\n", (unsigned int) p123Globals);

  // if not playing, start
  if(*p123Globals->mode != MODE_STOPPED)
  { 
    if (*p123Globals->mode == MODE_PLAYING) {
      *p123Globals->mode = MODE_PAUSED;
      if(param.usebuffer) buffer_stop();
      generic_sendmsg("P 1");
    } else {
      *p123Globals->mode = MODE_PLAYING;
      if(param.usebuffer) buffer_start();
      // set simple command to run
      pthread_mutex_trylock(&new_cmd_ready);
      new_cmd = NULL;   // just wake up
      pthread_mutex_unlock(&new_cmd_ready);

      generic_sendmsg("P 2");
    }
  }

  return INT2NUM(*p123Globals->mode);
}

VALUE mpg123Ruby_getvolume(VALUE self)
{
  double  v;
  sMPG123Globals  *p123Globals = NULL;

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL)
    return Qnil;

  mpg123_getvolume(p123Globals->mh, &v, NULL, NULL); /* Necessary? */
  //generic_sendmsg("V %f%%", v * 100);
  return DBL2NUM(v*100);
}


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
    mpg123_volume(p123Globals->mh, v/100);
  }

  mpg123_getvolume(p123Globals->mh, &v, NULL, NULL); /* Necessary? */
  return DBL2NUM(v*100);
}

/*
 *  This can be a ruby function
 */
VALUE mpg123Ruby_state(VALUE self)
{
  double  v;
  sMPG123Globals  *p123Globals = NULL;

  // get the handle
  Data_Get_Struct(self, sMPG123Globals, p123Globals);

  if (p123Globals == NULL)
    return Qnil;

  return INT2NUM(p123Globals->mode);
}

void testprogram(void);

void Init_mpg123_ruby(void) {

  // init local things

  // connect to ruby class
  rubyClassMpg123 = rb_define_class("Mpg123_ruby", rb_cObject);

  rb_define_alloc_func(rubyClassMpg123, mpg123Ruby_alloc);
  rb_define_method(rubyClassMpg123, "load", mpg123Ruby_load, 1);
  rb_define_method(rubyClassMpg123, "pause", mpg123Ruby_pause, 0);
  rb_define_method(rubyClassMpg123, "getvolume", mpg123Ruby_getvolume, 0);
  rb_define_method(rubyClassMpg123, "volume", mpg123Ruby_volume, -1);
  rb_define_method(rubyClassMpg123, "state", mpg123Ruby_state, 0);

  testprogram();
}

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
  char *fname;
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
    return 0;
  }
  if(param.test_cpu)
  {
    const char **all_dec = mpg123_supported_decoders();
    printf("Supported decoders:");
    while(*all_dec != NULL){ printf(" %s", *all_dec); ++all_dec; }
    printf("\n");
    mpg123_delete_pars(mp);
    return 0;
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
    return 99; /* It's safe here... nothing nasty happened yet. */
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
  if(dnow != 0) param.cpu = (dnow == SET_3DNOW) ? "3dnow" : "i586";
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
    safe_exit(45);
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
    safe_exit(1);
  }

  /* Now actually get an mpg123_handle. */
  mh = mpg123_parnew(mp, param.cpu, &result);
  if(mh == NULL)
  {
    error1("Crap! Cannot get a mpg123 handle: %s", mpg123_plain_strerror(result));
    safe_exit(77);
  }
  mpg123_delete_pars(mp); /* Don't need the parameters anymore ,they're in the handle now. */

  /* Prepare stream dumping, possibly replacing mpg123 reader. */
  if(dump_open(mh) != 0) safe_exit(78);

  /* Now either check caps myself or query buffer for that. */
  audio_capabilities(ao, mh);

  load_equalizer(mh);

}