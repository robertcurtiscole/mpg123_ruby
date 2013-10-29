# coding: utf-8
lib = File.expand_path('../lib', __FILE__)
$LOAD_PATH.unshift(lib) unless $LOAD_PATH.include?(lib)
require 'mpg123_ruby/version'

Gem::Specification.new do |spec|
  spec.name          = "mpg123-ruby"
  spec.version       = Mpg123Ruby::VERSION
  spec.authors       = ["Curtis Cole"]
  spec.email         = ["RCurtisCole@gmail.com"]
  spec.description   = "api to mpg123 library"
  spec.summary       = "api to mpg123 library"
  spec.homepage      = "http://www.github.com/robertcurtiscole/mpg123ruby"
  spec.license       = "MIT"

  spec.files         = ['lib/mpg123-ruby.rb'] + Dir.glob("ext/**/*.{c,rb}")
  spec.executables   = spec.files.grep(%r{^bin/}) { |f| File.basename(f) }
  spec.test_files    = spec.files.grep(%r{^(test|spec|features)/})
  spec.require_paths = ["lib"]

  spec.extensions << "ext/mpg123_ruby/extconf.rb"
  spec.add_development_dependency "bundler", "~> 1.3"
  spec.add_development_dependency "rake"
  spec.add_development_dependency "rake-compiler"
end
