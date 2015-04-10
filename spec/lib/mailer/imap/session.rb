
class Mailer::IMAP::Session

  def initialize(host, port, debug = false)
    @socket, @debug = TCPSocket.new(host, port), debug
    read_line '*', 'OK'
  end

  def login(username, password)
    write_line 'command_01', "login #{username} #{password}"
    begin
      read_line 'command_01', 'OK'
    rescue
      raise 'invalid password'
    end
  end

  def logout
    write_line 'command_50', 'logout'
    read_line '*', 'BYE'
    read_line 'command_50', 'OK'
    @socket.close
  end

  def select(name)
    write_line 'command_02', "select #{name}"
    read_line '*', 'FLAGS'
    read_line '*', 'OK'
    existing_mails = read_count_line '*'
    recent_mails = read_count_line '*'
    read_tagged_line 'command_02'
    {
      existing_mails: existing_mails,
      recent_mails: recent_mails
    }
  end

  def fetch_mail(number)
    write_line 'command_03', "fetch #{number} body.peek[]"
    read_line '*', "#{number}"
    mail = ''
    read_lines do |line|
      raise 'broken stream' unless line
      if line.strip == ')'
        true
      else
        mail += line
        false
      end
    end
    read_line 'command_03', 'OK'
    mail
  end

  def fetch_mail_with_attachment(number)
    write_line 'command_04', "fetch #{number} body.peek[]"
    read_line '*', "#{number}"
    line_number = 0
    header = ''
    received_attachment_size = 0
    read_lines do |line|
      if line_number < 20
        header += line
      elsif line =~ /^A/
        chunk = Base64.decode64 line
        received_attachment_size += chunk.bytesize
      end
      line_number += 1
      line =~ /^command_04\sOK/i
    end
    [ /plain(.*)---separator---/m.match(header)[1].strip, received_attachment_size ]
  end

  def fetch_mail_parts(ids)
    ids.map do |id|
      write_line 'command_05', "fetch #{id} body.peek[2.mime]"
      part = ''
      read_lines do |line|
        puts line
        part += line
        line =~ /^command_05\sOK/i
      end
      [ part, part.size ]
    end.compact
  end

  def sort(field, reverse)
    write_line 'command_06', "sort (#{reverse ? 'reverse ' : ''}#{field}) us-ascii all"
    response = read_line '*', 'SORT'
    ids = response.sub(/^\*\sSORT/, '').strip.split(' ')
    read_line 'command_06', 'OK'
    ids
  end

  def search(options = { })
    parameter = if options[:keyword]
                  "keyword \"#{options[:keyword]}\""
                elsif options[:seen]
                  'seen'
                else
                  'all'
                end
    parameter = "not #{parameter}" if options[:not]

    write_line 'command_07', "search #{parameter}"
    response = read_line '*', 'SEARCH'
    ids = response.sub(/^\*\sSEARCH/, '').strip.split(' ')
    read_lines do |line|
      line =~ /^command_07\sOK/i
    end
    ids
  end

  def fetch_mail_headers(ids)
    responses = { }
    write_line 'command_08', "fetch #{ids.join(',')} (UID RFC822.SIZE FLAGS INTERNALDATE BODY.PEEK[HEADER.FIELDS (DATE FROM TO SUBJECT CONTENT-TYPE CC REPLY-TO LIST-POST DISPOSITION-NOTIFICATION-TO X-PRIORITY)])"
    ids.length.times do
      id = read_line('*').sub(/^\*\s(\d+).*/, '\1').strip
      response = ''
      read_lines do |line|
        if line.strip == ')'
          true
        else
          response += line
          false
        end
      end
      responses[id] = response
    end
    read_line 'command_08', 'OK'

    ids.map{ |id| responses[id] }
  end

  def store(ids, flags)
    write_line 'command_11', "store #{ids.join(',')} +flags (#{flags.join(' ')})"
    read_tagged_line 'command_11'
  end

  private

  def parse_ids(line)
    blocks = line.split ','
    blocks.map do |block|
      if block =~ /\d+:\d+/
        from = block.sub(/(\d+):\d+/, '\1').to_i
        to = block.sub(/\d+:(\d+)/, '\1').to_i
        (from .. to).to_a
      else
        block
      end
    end.flatten
  end

  def read_count_line(expected_id = nil)
    line = read_line expected_id
    count = /.+\s(\d+)\s/.match(line)[1].strip
    count.to_i
  end

  def read_tagged_line(tag)
    read_lines do |line|
      line =~ /^#{tag}/
    end
  end

  def read_lines
    line = @socket.gets
    until yield(line)
      line = @socket.gets
    end
    line
  end

  def read_line(expected_id = nil, expected_status = nil)
    line = @socket.gets || ''

    puts "S: #{line}" if @debug

    if expected_id
      id = line.sub(/\s.*$/, '').chop
      raise "unexpected id [#{id}]: #{line}" unless expected_id == id
    end

    if expected_status
      status = line.sub(/^\S+\s(\S+).*$/, '\1').chop
      raise "unexpected status [#{status}]: #{line}" unless expected_status == status
    end

    line
  end

  def write_line(id, line)
    @socket.puts "#{id} #{line}"
    puts "C: #{id} #{line}" if @debug
  end

end
