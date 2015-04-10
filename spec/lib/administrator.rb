
class Administrator

  BASE_PATH = File.expand_path '../..', File.dirname(__FILE__)
  DOVECOT_PATH = File.expand_path 'dovecot', BASE_PATH
  DOVEADM_PATH = File.expand_path 'target/bin/doveadm', DOVECOT_PATH
  CONF_PATH = File.expand_path 'configuration/dovecot.conf', DOVECOT_PATH
  PASSWORD_FILE_PATH = File.expand_path 'sudo.password', BASE_PATH

  def initialize(username)
    @username = username
  end

  def home_mail_path
    File.expand_path "home/#{@username}/mail", DOVECOT_PATH
  end

  def fetch(password = nil)
    output = doveadm password, 'fetch', '-u', @username, 'text', 'mailbox', 'inbox'
    parse_doveadm_fetch_text_output output
  end

  def fetch_header(password = nil)
    output = doveadm password, 'fetch', '-u', @username, 'hdr', 'mailbox', 'inbox'
    parse_doveadm_fetch_header_output output
  end

  def decrypt(password = nil)
    doveadm password, '-o', 'plugin/scrambler_enabled=0', 'sync', '-u', @username, 'mdbox:/tmp/test'
    system "rm -rf #{home_mail_path}"
    system "mv /tmp/test #{home_mail_path}"
  end

  def encrypt(password = nil)
    doveadm password, '-o', 'plugin/scrambler_enabled=1', 'sync', '-u', @username, 'mdbox:/tmp/test'
    system "rm -rf #{home_mail_path}"
    system "mv /tmp/test #{home_mail_path}"
  end

  def doveadm(password, *arguments)
    passwordReader, passwordWriter = IO.pipe
    outputReader, outputWriter = IO.pipe

    command = "#{DOVEADM_PATH} -c #{CONF_PATH} -D "
    command += "-o plugin/scrambler_plain_password_fd=#{passwordReader.fileno} " if password
    command += arguments.join ' '
    pid = spawn command, passwordReader => passwordReader, :out => outputWriter
    outputWriter.close

    passwordWriter.write "#{password}\n"
    passwordWriter.write "#{password}\n"
    passwordWriter.close

    Process.wait pid
    raise 'invalid password' if $?.exitstatus.to_i == 75

    outputReader.read
  end

  def clear
    system 'rm -rf /tmp/test'
  end

  private

  def parse_doveadm_fetch_text_output(output)
    parts = output.split "\f\n"
    parts.map do |part|
      part.sub /^test:/, ''
    end
  end

  def parse_doveadm_fetch_header_output(output)
    output.split "\f\n"
  end

end
