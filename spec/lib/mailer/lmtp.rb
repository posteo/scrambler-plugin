require 'base64'

class Mailer::LMTP

  def initialize(host, port, domain, from)
    @domain, @from = domain, from
    @socket = TCPSocket.new host, port
    read_line 220
  end

  def deliver(message, to, attachment_size = nil)
    deliver_parts to do |handler|
      if attachment_size
        handler.call "Date: #{Time.now.strftime('%a, %d %b %Y %H:%M:%S %z')}"
        handler.call 'Subject: test multipart'
        handler.call 'MIME-Version: 1.0'
        handler.call 'Content-Type: multipart/mixed; boundary="---separator---"'
        handler.call ''
        handler.call 'This message has multiple parts'
        handler.call ''
        handler.call '---separator---'
        handler.call 'Content-Type: text/plain'
        handler.call ''
        handler.call message
        handler.call '---separator---'
        handler.call 'Content-Type: application/octet-stream'
        handler.call 'Content-Transfer-Encoding: base64'
        handler.call ''
        size = 0
        while size < attachment_size do
          left = attachment_size - size
          chunk_size = left > Mailer::ATTACHMENT_BYTES_PER_LINE ?
              Mailer::ATTACHMENT_BYTES_PER_LINE :
              left

          handler.call Base64.strict_encode64(0.chr * chunk_size)
          size += chunk_size
        end
        handler.call '---separator---'
      else
        handler.call message
      end
    end
  end

  def deliver_file(path, to)
    deliver_parts to do |handler|
      File.foreach path do |line|
        handler.call line
      end
    end
  end

  def close
    write_line 'QUIT'
    read_line 221

    @socket.close
  end

  private

  def deliver_parts(to)
    write_line "LHLO #{@domain}"
    read_line 250
    read_line 250
    read_line 250
    read_line 250

    write_line "MAIL FROM:<#{@from}>"
    read_line 250

    write_line "RCPT TO:<#{to}>"
    read_line 250

    write_line 'DATA'
    read_line 354

    size = 0
    yield lambda { |part|
      size += part.bytesize
      write_line part
    }
    write_line '.'
    read_line 250

    size
  end

  def read_line(expected_status_code = nil)
    line = @socket.gets
    if expected_status_code
      status_code = line.sub(/\s.*$/, '').to_i
      raise "expected status code: #{line}" unless expected_status_code == status_code
    end
    line
  end

  def write_line(line)
    @socket.puts line
  end

end
