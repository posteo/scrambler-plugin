require File.expand_path('lib/administrator', File.dirname(__FILE__))
require File.expand_path('lib/database', File.dirname(__FILE__))
require File.expand_path('lib/mailer', File.dirname(__FILE__))
require File.expand_path('lib/storage', File.dirname(__FILE__))

LARGE_ATTACHMENT_FILE_SIZE = 2 * 1024 * 1024
MULTIPLE_MAILS_COUNT = 5

def test_message(number = 0)
  "Date: #{Time.new(2000, 1, 1, 1, number, 0).strftime('%a, %d %b %Y %H:%M:%S %z')}\n" +
  "Subject: test #{number}\n" +
  "\n" +
  "test message #{number}"
end

def deliver_test_message(mailer, number, to)
  mailer.deliver test_message(number), to
end

def deliver_test_messages(mailer, count, to)
  count.times do |index|
    deliver_test_message mailer, index, to
  end
end
