require File.expand_path('../helper', File.dirname(__FILE__))
require 'socket'
require 'tempfile'

describe 'Mail encryption enabled' do

  before :all do
    @database = Database.new
    @mailer = Mailer.new
    @storage = Storage.new
    @administrator = Administrator.new 'test'

    @database.clear_users
    @database.clear_keys
    @database.insert_user 1, 'test', 'testPassword'
    @database.insert_key 1, true
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

    it 'should deny the administrator to access the mail using no password' do
      lambda do
        @administrator.fetch
      end.should raise_error('invalid password')
    end

    it 'should decrypt the meta-data and mail during reception' do
      mails = @mailer.receive
      mails.length.should == 1
      mails[0].should =~ /test message one/
    end

    it 'should fail if an invalid password is given' do
      expect{
        begin
          @mailer.password = 'invalid'
          @mailer.receive
        ensure
          @mailer.password = 'testPassword'
        end
      }.to raise_error('invalid password')
    end

    context 'if disabled after reception' do

      before :each do
        @database.update_key 1, false
      end

      after :each do
        @database.update_key 1, true
      end

      it 'should decrypt the meta-data and mail' do
        mails = @mailer.receive
        mails.length.should == 1
        mails[0].should =~ /test message one/
      end

    end

    context 'if missing after reception' do

      before :each do
        @database.clear_keys
      end

      after :each do
        @database.insert_key 1, true
      end

      it 'should brake the connection' do
        lambda do
          @mailer.receive
        end.should raise_error('broken stream')
      end

    end

    context 'if decrypted after reception' do

      after :each do
        @administrator.clear
      end

      it 'should response the meta-data and mail' do
        @administrator.decrypt 'testPassword'
        ids = @mailer.search
        ids.length.should == 1
      end

      it 'should fail if decrypted with an invalid password' do
        lambda do
          @administrator.decrypt 'invalid'
        end.should raise_error('invalid password')
      end

    end

  end

  context 'of multiple standard mails' do

    before :all do
      deliver_test_messages @mailer, MULTIPLE_MAILS_COUNT, 'test'
    end

    after :all do
      @storage.clear
    end

    it 'should decrypt the meta-data and mail' do
      mails = @mailer.receive
      mails.length.should == MULTIPLE_MAILS_COUNT
      MULTIPLE_MAILS_COUNT.times do |index|
        mails[index].should =~ /test message #{index}/
      end
    end

    it 'should list all received mail headers' do
      headers = @mailer.receive_headers 'date', true
      headers.length.should == MULTIPLE_MAILS_COUNT
      MULTIPLE_MAILS_COUNT.times do |index|
        headers[index].should =~ /Subject: test #{MULTIPLE_MAILS_COUNT - index - 1}/
      end
    end

  end

  context 'of a large mail' do

    before :all do
      @mailer.deliver 'test message two', 'test', LARGE_ATTACHMENT_FILE_SIZE
    end

    after :all do
      @storage.clear
    end

    it 'should decrypt the meta-data and mail' do
      mails = @mailer.receive LARGE_ATTACHMENT_FILE_SIZE
      mails.length.should == 1
      mails[0][0].should =~ /test message two/
      mails[0][1].should == LARGE_ATTACHMENT_FILE_SIZE
    end

  end

  context 'of a real mail with attachment' do

    before :all do
      @mailer.deliver_file File.expand_path('../fixtures/mail-1.eml', File.dirname(__FILE__)), 'test'
    end

    after :all do
      @storage.clear
    end

    it 'should decrypt only a part if requested' do
      mails = @mailer.receive_part
      mails.length.should == 1
    end

  end

  context 'of a chunk-size mail' do

    before :all do
      @mailer.deliver_file File.expand_path('../fixtures/mail-2.eml', File.dirname(__FILE__)), 'test'
    end

    after :all do
      @storage.clear
    end

    it 'should decrypt only a part if requested' do
      mails = @mailer.receive_part
      mails.length.should == 1
    end

  end

  context 'of a real mail of odd size' do

    before :all do
      @mailer.deliver_file File.expand_path('../fixtures/mail-3.eml', File.dirname(__FILE__)), 'test'
    end

    after :all do
      @storage.clear
    end

    it 'should decrypt only a part if requested' do
      mails = @mailer.receive_part
      mails.length.should == 1
    end

    context 'if decrypted after reception' do

      after :each do
        @administrator.clear
      end

      it 'should response the meta-data and mail' do
        @administrator.decrypt 'testPassword'
        ids = @mailer.search
        ids.length.should == 1
      end

      it 'should fail if decrypted with an invalid password' do
        lambda do
          @administrator.decrypt 'invalid'
        end.should raise_error('invalid password')
      end

    end

  end

end
