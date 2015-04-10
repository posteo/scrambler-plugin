require 'base64'

class Mailer

  autoload :LMTP, File.expand_path('mailer/lmtp', File.dirname(__FILE__))
  autoload :IMAP, File.expand_path('mailer/imap', File.dirname(__FILE__))

  ATTACHMENT_BYTES_PER_LINE = 45

  def initialize(domain = 'test.com', from = 'sender@test.com', username = 'test', password = 'testPassword')
    @lmtp = LMTP.new '127.0.0.1', 6060, domain, from
    @imap = IMAP.new '127.0.0.1', 6070, username, password
  end

  def password=(value)
    @imap.password = value
  end

  def deliver(message, to, attachment_size = nil)
    @lmtp.deliver message, to, attachment_size
  end

  def deliver_file(path, to)
    @lmtp.deliver_file path, to
  end

  def receive(attachment_size = nil)
    @imap.receive attachment_size
  end

  def receive_headers(sort_by, reverse = false)
    @imap.receive_headers sort_by, reverse
  end

  def receive_part
    @imap.receive_part
  end

  def search(options = { })
    @imap.search options
  end

  def store(*flags)
    @imap.store *flags
  end

  def multiple_receive_cycles(count)
    @imap.multiple_receive_cycles count
  end

  def close
    @lmtp.close
  end

end
