require 'fileutils'

class Storage

  DIRECTORY = File.expand_path '../../dovecot/home', File.dirname(__FILE__)

  def initialize(user = 'test')
    @directory = File.join DIRECTORY, user, 'mail'
  end

  def find_mail_delivered_to(user)
    find_mail_with /Delivered-To: <#{user}>/
  end

  def find_mail_with(pattern)
    mails.select do |mail|
      mail =~ pattern
    end
  end

  def clear
    FileUtils.rm_rf @directory
    @mails = nil
  end

  private

  def mails
    @mails ||= begin
      result = [ ]

      Dir[ File.join(@directory, 'storage', 'm.*') ].each do |filename|
        content = File.read filename

        from = 0
        while from = content.index('Return-Path:', from + 1)
          to = content.index "\n\n"
          result << content.slice(from, to - from)
        end
      end

      result
    end
  end

end
