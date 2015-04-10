require File.expand_path('../helper', File.dirname(__FILE__))
require 'socket'
require 'tempfile'

describe 'Mail encryption missing' do

  before :all do
    @database = Database.new
    @mailer = Mailer.new
    @storage = Storage.new
    @administrator = Administrator.new 'test'

    @database.clear_users
    @database.clear_keys
    @database.insert_user 1, 'test', 'testPassword'
  end

  after :all do
    @mailer.close
  end

  context 'of a standard mail' do

    before :all do
      @mailer.deliver 'test message one', 'test'
    end

    after :all do
      @storage.clear
    end

    it 'should allow the administrator to access the mail' do
      mails = @administrator.fetch 'testPassword'
      mails.length.should == 1
      mails[0].should =~ /test message one/
    end

    it 'should respond the meta-data and mail' do
      mails = @mailer.receive
      mails.length.should == 1
      mails[0].should =~ /test message one/
    end

  end

end
