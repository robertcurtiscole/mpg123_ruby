require "bundler/gem_tasks"
require 'rake/extensiontask'

spec = Gem::Specification.load('mpg123_ruby.gemspec')

Rake::ExtensionTask.new('mpg123_ruby', spec)
#Rake::ExtensionTask.new do |ext|
#	ext.name = "mpg123_ruby"
#	ext.gem_spec = spec
#end
