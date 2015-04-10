
class Context

  def initialize(dovecot_directory)
    @dovecot_directory = dovecot_directory
  end

  def with_base_directory(path)
    File.join @dovecot_directory, path
  end

  def current_user
    ENV['USER']
  end

  def get_binding
    binding
  end

end
