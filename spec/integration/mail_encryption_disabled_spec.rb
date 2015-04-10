require File.expand_path('../helper', File.dirname(__FILE__))
require 'socket'
require 'tempfile'

describe 'Mail encryption disabled' do

  before :all do
    @database = Database.new
    @mailer = Mailer.new
    @storage = Storage.new
    @administrator = Administrator.new 'test'

    @database.clear_users
    @database.clear_keys
    @database.insert_user 1, 'test', 'testPassword'
    @database.insert_key 1, false
  end

  after :all do
    @mailer.close
  end

  context 'of a standard mail' do

    before :each do
      @mailer.deliver 'test message one', 'test'
    end

    after :each do
      @storage.clear
    end

    it 'should response the meta-data and mail' do
      mails = @mailer.receive
      mails.length.should == 1
      mails[0].should =~ /test message one/
    end

    context 'if encrypt after reception' do

      before :each do
        @administrator.encrypt 'testPassword'
      end

      after :each do
        @administrator.clear
      end

      it 'should allow the administrator to access the mail using the valid password' do
        mails = @administrator.fetch 'testPassword'
        mails.length.should == 1
        mails[0].should =~ /test message one/
      end

      it 'should deny the administrator to access the mail using an invalid password' do
        lambda do
          @administrator.fetch 'invalid'
        end.should raise_error('invalid password')
      end

    end

  end

  context 'of a mail with an even number of chunks' do

    before :all do
      @mailer.deliver_file File.expand_path('../fixtures/mail-4.eml', File.dirname(__FILE__)), 'test'
    end

    after :all do
      @storage.clear
    end

    context 'if encrypt after reception' do

      before :each do
        @administrator.encrypt 'testPassword'
      end

      after :each do
        @administrator.clear
      end

      it 'should allow the administrator to access the mail using the valid password' do
        mails = @administrator.fetch_header 'testPassword'
        mails.length.should == 1
      end

    end

  end

end
