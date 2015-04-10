require 'sqlite3'
require 'openssl'
require 'bcrypt'

class Database

  def initialize(filename = File.expand_path('../../dovecot/configuration/mailservice.sqlite', File.dirname(__FILE__)))
    @database = SQLite3::Database.new filename
  end

  def insert_user(id, name, password)
    @database.execute(
        'INSERT INTO users (id, username, crypted_password, persistence_token) VALUES (?, ?, ?, "dummy")',
        [ id, name, password ])
  end

  def insert_key(id, enabled)
    @database.execute(
        'INSERT INTO keys (id, enabled, public_key, private_key, private_key_salt, private_key_iterations) VALUES (?, ?, ?, ?, ?, ?)',
        [ id, (enabled ? 1 : 0), default_public_key, default_private_key, default_private_key_salt, default_private_key_iterations ])
  end

  def update_key(id, enabled)
    @database.execute 'UPDATE keys SET enabled = ? WHERE id = ?', [ (enabled ? 1 : 0), id ]
  end

  def fetch_users
    result = [ ]
    @database.execute('select id, username, crypted_password from users;') do |row|
      result << {
          id: row[0],
          name: row[1],
          password: row[2]
      }
    end
    result
  end

  def fetch_keys
    result = [ ]
    @database.execute(
        'select id, enabled, public_key, private_key, private_key_salt, private_key_iterations from keys;'
    ) do |row|
      result << {
          id: row[0],
          enabled: row[1] == 1,
          public_key: row[2],
          private_key: row[3],
          private_key_salt: row[4],
          private_key_iterations: row[5]
      }
    end
    result
  end

  def print_users
    print 'users', fetch_users
  end

  def print_keys
    print 'keys', fetch_keys
  end

  def print(name, items)
    if items.empty?
      puts "no #{name} in database"
    else
      puts "#{name}:"
      items.each do |item|
        puts "  #{item.inspect}"
      end
    end
  end

  def clear_users
    @database.execute('DELETE FROM users;')
  end

  def clear_keys
    @database.execute('DELETE FROM keys;')
  end

  private

  def default_public_key
    escape_pem default_key_pair.public_key.to_pem
  end

  def default_private_key
    settings = '$2a$%02u$%22s' % [ default_private_key_iterations, default_private_key_salt ]
    hashed_password = BCrypt::Engine.hash_secret default_private_key_password, settings
    escape_pem default_key_pair.to_pem(OpenSSL::Cipher.new('aes-256-cbc'), hashed_password)
  end

  def default_private_key_password
    'testPassword'
  end

  def default_private_key_salt
    @salt ||= Base64.strict_encode64(random.bytes(16)).gsub('+', '.').slice 0, 22
  end

  def default_private_key_iterations
    12
  end

  def default_key_pair
    @key_pair ||= OpenSSL::PKey::RSA.new 2048
  end

  def escape_pem(pem)
    pem.gsub /\n/, '_'
  end

  def random
    @random ||= Random.new
  end

end
