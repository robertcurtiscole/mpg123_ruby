// for a ruby gem
#include <ruby.h>

// for our C code
#include <stdio.h>
#include <strings.h>
#include <mpg123.h>

#include "pthread.h"

#define MODE_STOPPED 0
#define MODE_PLAYING 1
#define MODE_PAUSED 2

void generic_sendmsg (const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
}


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

/* increment x to 100 */
  sMPG123Globals   *p123Globals = (sMPG123Globals *) gPtr;
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
  int             result;
  VALUE           obj;

	printf("new!\n");
  // wrap the C structure in Ruby
  obj = Data_Make_Struct(klass, sMPG123Globals, 0, mpg123Ruby_free, p123Globals);

  // start a thread
  /* create a second thread which executes inc_x(&x) */
  if(pthread_create(&p123Globals->playerThread, NULL, PlayerThread, p123Globals)) {
    fprintf(stderr, "Error creating thread\n");
  }

  result = mpg123_init();
  if(result == MPG123_OK)
  {
    //if(init_output(&pMPG123Globals->ao) >= 0)

      //mh = mpg123_parnew(mp, param.cpu, &result);
      p123Globals->mh = mpg123_parnew(NULL, NULL, &result);
      if(p123Globals->mh != NULL)
      {

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