#include <thread>
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <set>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

using boost::asio::deadline_timer;
using boost::asio::ip::tcp;

class Subscriber {
 public:
  virtual ~Subscriber() {}
  virtual void Deliver(const std::string& msg) = 0;
};

class TcpSession;
typedef boost::shared_ptr<Subscriber> SubscriberPtr;
typedef boost::shared_ptr<TcpSession> TcpSessionPtr;

class Channel {
 public:
  void Join(SubscriberPtr subscriber) {
    subscribers_.insert(subscriber);
  }

  void Leave(SubscriberPtr subscriber) {
    subscribers_.erase(subscriber);
  }

  void Deliver(const std::string& msg) {
    std::for_each(subscribers_.begin(), subscribers_.end(),
        boost::bind(&Subscriber::Deliver, _1, boost::ref(msg)));
  }

 private:
  std::set<SubscriberPtr> subscribers_;
};

class TcpSession
  : public Subscriber,
    public boost::enable_shared_from_this<TcpSession> {
 public:
  TcpSession(boost::asio::io_service& io_service, Channel& ch)
    : channel_(ch),
      socket_(io_service),
      input_deadline_(io_service),
      non_empty_output_queue_(io_service),
      output_deadline_(io_service) {
    input_deadline_.expires_at(boost::posix_time::pos_infin);
    output_deadline_.expires_at(boost::posix_time::pos_infin);
    non_empty_output_queue_.expires_at(boost::posix_time::pos_infin);
  }

  void Start() {
    channel_.Join(shared_from_this());

    StartRead();
    input_deadline_.async_wait(boost::bind(&TcpSession::CheckDeadline,
                                           shared_from_this(),
                                           &input_deadline_));
    AwaitOutput();
    output_deadline_.async_wait(boost::bind(&TcpSession::CheckDeadline,
                                            shared_from_this(),
                                            &output_deadline_));
  }

  tcp::socket& socket() {
    return socket_;
  }

  void Deliver(const std::string& msg) {
    output_queue_.push_back(msg + "\n");
    non_empty_output_queue_.expires_at(boost::posix_time::neg_infin);
  }

 private:
  void Stop() {
    channel_.Leave(shared_from_this());

    boost::system::error_code ignored_ec;
    socket_.close(ignored_ec);
    input_deadline_.cancel();
    non_empty_output_queue_.cancel();
    output_deadline_.cancel();
  }

  bool Stopped() const {
    return !socket_.is_open();
  }

  void StartRead() {
    input_deadline_.expires_from_now(boost::posix_time::seconds(30));
    boost::asio::async_read_until(socket_, input_buffer_, '\n',
                                  boost::bind(&TcpSession::HandleRead,
                                              shared_from_this(), _1));
  }

  void HandleRead(const boost::system::error_code& ec) {
    if (Stopped()) return;

    if (ec) {
      Stop();
    } else {
      std::string msg;
      std::istream is(&input_buffer_);
      std::getline(is, msg);

      if (!msg.empty()) {
        channel_.Deliver(msg);
      }
      else {
        if (output_queue_.empty()) {
          output_queue_.push_back("\n");  // Return heartbeat if idle.
          non_empty_output_queue_.expires_at(boost::posix_time::neg_infin);
        }
      }

      StartRead();
    }
  }

  void AwaitOutput() {
    if (Stopped()) return;

    if (output_queue_.empty()) {
      non_empty_output_queue_.expires_at(boost::posix_time::pos_infin);
      non_empty_output_queue_.async_wait(boost::bind(&TcpSession::AwaitOutput,
                                                     shared_from_this()));
    } else {
      StartWrite();
    }
  }

  void StartWrite() {
    output_deadline_.expires_from_now(boost::posix_time::seconds(30));
    boost::asio::async_write(socket_, boost::asio::buffer(output_queue_.front()),
                             boost::bind(&TcpSession::HandleWrite,
                                         shared_from_this(), _1));
  }

  void HandleWrite(const boost::system::error_code& ec) {
    if (Stopped()) return;

    if (!ec) {
      output_queue_.pop_front();
      AwaitOutput();
    } else {
      Stop();
    }
  }

  void CheckDeadline(deadline_timer* deadline) {
    if (Stopped()) return;

    if (deadline->expires_at() <= deadline_timer::traits_type::now())
      Stop();
    else
      deadline->async_wait(boost::bind(&TcpSession::CheckDeadline,
                           shared_from_this(), deadline));
  }

  Channel& channel_;
  tcp::socket socket_;
  boost::asio::streambuf input_buffer_;
  deadline_timer input_deadline_;
  std::deque<std::string> output_queue_;
  deadline_timer non_empty_output_queue_;
  deadline_timer output_deadline_;
};

class Server {
 public:
  Server(boost::asio::io_service& io_service,
         const tcp::endpoint& listen_endpoint)
    : io_service_(io_service),
      acceptor_(io_service, listen_endpoint) {
    StartAccept();
  }

  void StartAccept() {
    TcpSessionPtr new_session(new TcpSession(io_service_, channel_));

    acceptor_.async_accept(new_session->socket(),
        boost::bind(&Server::HandleAccept, this, new_session, _1));
  }

  void HandleAccept(TcpSessionPtr session, const boost::system::error_code& ec) {
    if (!ec) {
      for (const auto& msg : cache_) { // TODO(ds) use container adapter
        session->Deliver(msg.second);
      }
      session->Start();
    }

    StartAccept();
  }

  // TODO(ds) cache msgs (map) for catch-up on new client connect.
  void PublishMessage(const std::string& msg) {
    cache_[cache_.size() + 1] = msg;
    channel_.Deliver(msg);
  }

 private:
  boost::asio::io_service& io_service_;
  tcp::acceptor acceptor_;
  Channel channel_;

  std::map<long, std::string> cache_;
};

int main(int argc, char* argv[]) {
  try {
    using namespace std;

    if (argc != 2) {
      std::cerr << "Usage: server <listen_port>\n";
      return 1;
    }

    boost::asio::io_service io_service;
    tcp::endpoint listen_endpoint(tcp::v4(), atoi(argv[1]));

    Server s(io_service, listen_endpoint);
    io_service.post(boost::bind(&Server::PublishMessage, &s, "000"));

    std::thread t([&](){ io_service.run(); });
    std::string abc("abc");
    for (;;) {
      io_service.post(boost::bind(&Server::PublishMessage, &s, abc));
      sleep(1);
    }

    t.join();
  }
  catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
