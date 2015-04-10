require 'rspec'

base_directory = File.expand_path '..', File.dirname(__FILE__)

namespace :spec do

  desc 'run the integration specs'
  task :integration do
    begin
      Rake::Task['dovecot:start'].invoke

      specs = Dir[ File.expand_path('spec/integration/**/*_spec.rb', base_directory) ]
      RSpec::Core::CommandLine.new(specs).run $stderr, $stdout
    ensure
      Rake::Task['dovecot:stop'].invoke
    end
  end

  namespace :integration do

    desc 'run the focused integration specs'
    task :focus do
      begin
        Rake::Task['dovecot:start'].invoke

        RSpec.configure do |configuration|
          configuration.filter_run :focus => true
        end

        specs = Dir[ File.expand_path('spec/integration/**/*_spec.rb', base_directory) ]
        RSpec::Core::CommandLine.new(specs).run $stderr, $stdout
      ensure
        Rake::Task['dovecot:stop'].invoke
      end
    end

  end

end
