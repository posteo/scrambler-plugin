require 'base64'

class Mailer::IMAP

  autoload :Session, File.expand_path('imap/session.rb', File.dirname(__FILE__))

  attr_writer :password

  def initialize(host, port, username, password)
    @host, @port, @username, @password = host, port, username, password
  end

  def receive(attachment_size = nil)
    with_session do |session|
      result = [ ]
      inbox = session.select 'inbox'
      inbox[:existing_mails].times do |index|
        result << (attachment_size ?
            session.fetch_mail_with_attachment(index + 1) :
            session.fetch_mail(index + 1))
      end
      result
    end
  end

  def receive_headers(sort_by, reverse)
    within 'inbox' do |session|
      ids = session.sort sort_by, reverse
      session.fetch_mail_headers ids
    end
  end

  def receive_part
    within 'inbox' do |session|
      ids = session.search
      session.fetch_mail_parts ids
    end
  end

  def search(options = { })
    within 'inbox' do |session|
      session.search options
    end
  end

  def store(*flags)
    within 'inbox' do |session|
      ids = session.search
      session.store ids, flags
    end
  end

  def multiple_receive_cycles(count)
    with_session do |session|
      inbox = session.select 'inbox'
      inbox[:existing_mails].times do |index|
        session.fetch_mail index + 1
      end

      rss = imap_process_rss

      count.times do
        inbox[:existing_mails].times do |index|
          session.fetch_mail index + 1
        end
      end

      imap_process_rss - rss
    end
  end

  private

  def within(mailbox_name)
    with_session do |session|
      session.select mailbox_name
      yield session
    end
  end

  def with_session
    session = Session.new @host, @port
    session.login @username, @password
    result = yield session
    session.logout
    result
  end

  def imap_process_rss
    line = `ps ax -o command,rss | grep "imap"`.split("\n").select{ |line| !(line =~ /^grep/) }.first
    line.split(' ').last.to_i
  end

end
