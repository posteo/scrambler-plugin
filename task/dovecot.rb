require 'erb'
require 'facter'
require File.join(File.dirname(__FILE__), 'lib', 'context')

base_directory = File.expand_path '..', File.dirname(__FILE__)
sudo_password_filename = File.join base_directory, 'sudo.password'

namespace :dovecot do

  dovecot_directory = File.join base_directory, 'dovecot'
  source_directory = File.join dovecot_directory, 'source'
  target_directory = File.join dovecot_directory, 'target'
  configuration_directory = File.join dovecot_directory, 'configuration'
  run_directory = File.join dovecot_directory, 'run'

  conf_filename = File.join configuration_directory, 'dovecot.conf'
  pid_filename = File.join run_directory, 'master.pid'

  desc 'configures the dovecot sources'
  task :configure do
    generate_configuration_files configuration_directory, Context.new(dovecot_directory)
  end

  desc 'starts dovecot'
  task :start => %w{dovecot:configure} do
    binary_filename = File.join target_directory, 'sbin', 'dovecot'
    raise 'dovecot seems not to be installed. please run dovecot:install' unless File.exists?(binary_filename)

    system "rm -rf #{run_directory}"
    system unlimited_files("#{binary_filename} -c #{conf_filename}")
  end

  desc 'stops dovecot'
  task :stop do
    binary_filename = File.join target_directory, 'sbin', 'dovecot'
    raise 'dovecot seems not to be installed. please run dovecot:install' unless File.exists?(binary_filename)

    system "kill `cat #{pid_filename}`"
  end

  def generate_configuration_files(directory, context)
    Dir[ File.join(directory, '**', '*.erb') ].each do |template_filename|
      filename = File.join File.dirname(template_filename), File.basename(template_filename, '.erb')
      template = ERB.new File.read(template_filename)
      File.open filename, 'w' do |file|
        file.write template.result(context.get_binding)
      end
      puts "generated file #{filename}"
    end
  end

  def unlimited_files(command)
    "export GDB=1 && ulimit -c 0 && #{command}"
  end

end
