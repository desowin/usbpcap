# frozen_string_literal: true

Gem::Specification.new do |spec|
  spec.name          = "USBPCAP"
  spec.version       = "0.1.0"
  spec.authors       = ["desowin", "gpotter2"]
  spec.email         = [""]

  spec.summary       = "Gem used by the USBPcap website"
  spec.homepage      = "https://desowin.org/usbpcap/"
  spec.license       = "MIT"

  spec.files = `git ls-files -z`.split("\x0").select do |f|
        f.match(%r{^(_(includes|layouts|sass)/|(LICENSE|README)((\.(txt|md|markdown)|$)))}i)
  end

  spec.add_runtime_dependency "jekyll", "~> 3.8"

  spec.add_development_dependency "bundler", "~> 1.16"
  spec.add_development_dependency "rake", "~> 12.0"
end
