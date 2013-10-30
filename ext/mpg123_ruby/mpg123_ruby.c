// for a ruby gem
#include <ruby.h>

// for our C code
#include <stdio.h>
#include <strings.h>
#include <mpg123.h>

// the ruby class that we live in.
static VALUE rubyClassMpg123;

static void mpg123Ruby_free( void *p)
{
  printf("mpg123Ruby_free\n");
  // close and free hte handle?  I think this is mh
}
VALUE mpg123Ruby_alloc(VALUE klass) {
  mpg123_handle   *mh = NULL;
  //audio_output_t  *ao = NULL;
  int result;
  VALUE           obj;

	printf("new!\n");

#if 0

  result = mpg123_init();
  if(result != MPG123_OK)
  {
    error1("Cannot initialize mpg123 library: %s", mpg123_plain_strerror(result));
    safe_exit(77);
  }

  if(init_output(&ao) < 0)
  {
    error("Failed to initialize output, goodbye.");
    mpg123_delete_pars(mp);
    return 99; /* It's safe here... nothing nasty happened yet. */
  }

  //mh = mpg123_parnew(mp, param.cpu, &result);
  mh = mpg123_parnew(NULL, NULL, &result);
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
#endif

  // wrap the C structure in Ruby
  obj = Data_Wrap_Struct(klass, 0, mpg123Ruby_free, mh);
  return obj;
}


VALUE mpg123Ruby_load(VALUE self, VALUE url)
{
  mpg123_handle   *mh = NULL;

  // get the handle
  Data_Get_Struct(self, mpg123_handle, mh);

  printf("call mpg123_load %s\n", (char*) RSTRING_PTR(url));
  Check_Type(url, T_STRING);
  //INT2FIX(mpg123_load(DATA_PTR(self), (char*) RSTRING_PTR(url)));
  return Qnil;
}

void Init_mpg123_ruby(void) {

  // init local things

  // connect to ruby class
  rubyClassMpg123 = rb_define_class("Mpg123_ruby", rb_cObject);

  rb_define_alloc_func(rubyClassMpg123, mpg123Ruby_alloc);
  rb_define_method(rubyClassMpg123, "load", mpg123Ruby_load, 1);
}