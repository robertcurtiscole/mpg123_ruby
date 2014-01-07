require 'mkmf'

extension_name = 'mpg123_ruby'
include_dirs = ['$(srcdir)']
dir_config(extension_name, include_dirs)

unless have_header('mpg123.h')
  puts "please install mpg123 headers"
  exit
end

unless find_header('mpg123app.h')
  puts "please install mpg123 extended headers"
  puts "rake build # then gem install"
  puts "gem install path-to-file.gem -- --with-mpg123_ruby-include=/path-to-gemsource/ext/mpg123_ruby/targetinc"
  exit
end

unless have_library('mpg123')
  puts "please install mpg123 lib"
  exit
end

unless have_library('ltdl')
  puts "please install ltdl lib"
  exit
end

#CFLAGS for LINUX
$CFLAGS << ' -DPKGLIBDIR="\"/usr/local/lib/mpg123\"" -DOPT_MULTI -DOPT_GENERIC -DOPT_GENERIC_DITHER -DOPT_I386 -DOPT_I586 -DOPT_I586_DITHER -DOPT_MMX -DOPT_3DNOW -DOPT_3DNOW_VINTAGE -DOPT_3DNOWEXT -DOPT_3DNOWEXT_VINTAGE -DOPT_SSE -DOPT_SSE_VINTAGE -DREAL_IS_FLOAT -DNEWOLD_WRITE_SAMPLE   -O2 -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math '
#CFLAGS for MACOS
#$CFLAGS << ' -DHAVE_CONFIG_H -DPKGLIBDIR="\"/usr/local/lib/mpg123\"" -DOPT_MULTI -DOPT_X86_64 -DOPT_GENERIC -DOPT_GENERIC_DITHER -DREAL_IS_FLOAT   -O2 -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math '
#CFLAGS for ARM
#$CFLAGS << ' -DHAVE_CONFIG_H -DPKGLIBDIR="\"/usr/local/lib/mpg123\"" -DOPT_ARM -DOPT -DREAL_IS_FIXED   -O2 -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math '
$LDFLAGS << ' -O2 -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math '
create_makefile(extension_name)    # mpg123_ruby/mpg123_ruby