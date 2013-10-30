require 'mkmf'

unless have_header('mpg123.h')
  puts "please install mpg123 headers"
  exit
end

unless have_library('mpg123')
  puts "please install mpg123 lib"
  exit
end

extension_name = 'mpg123_ruby'
dir_config(extension_name)
create_makefile(extension_name)    # mpg123_ruby/mpg123_ruby