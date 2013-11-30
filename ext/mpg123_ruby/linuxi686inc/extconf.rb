require 'mkmf'

extension_name = 'mpg123_ruby'
include_dirs = ['./include']
dir_config(extension_name, include_dirs)

unless have_header('mpg123.h')
  puts "please install mpg123 headers"
  exit
end

unless have_header('mpg123app.h')
  puts "please install mpg123 headers"
  exit
end

unless have_library('mpg123')
  puts "please install mpg123 lib"
  exit
end

#unless find_header('include/compat.h')
#	put "can't find compat.h"
#	exit
#end


create_makefile(extension_name)    # mpg123_ruby/mpg123_ruby