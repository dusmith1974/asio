#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <set>
#include <thread>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>

using boost::asio::deadline_timer;
using boost::asio::ip::tcp;
using boost::bind;
using boost::shared_ptr;
using boost::system::error_code;

namespace asio = boost::asio;
namespace posix_time = boost::posix_time;

class Subscriber {
 public:
  virtual ~Subscriber() {}
  virtual void Deliver(const std::string& msg) = 0;
};

typedef shared_ptr<Subscriber> SubscriberPtr;

class TcpSession;
typedef shared_ptr<TcpSession> TcpSessionPtr;

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
        bind(&Subscriber::Deliver, _1, boost::ref(msg)));
  }

 private:
  std::set<SubscriberPtr> subscribers_;
};

class TcpSession
  : public Subscriber,
    public boost::enable_shared_from_this<TcpSession> {
 public:
  TcpSession(asio::io_service& io_service, Channel& ch)
    : channel_(ch),
      socket_(io_service),
      input_deadline_(io_service),
      non_empty_output_queue_(io_service),
      output_deadline_(io_service) {
    input_deadline_.expires_at(posix_time::pos_infin);
    output_deadline_.expires_at(posix_time::pos_infin);
    non_empty_output_queue_.expires_at(posix_time::pos_infin);
  }

  void Start() {
    channel_.Join(shared_from_this());

    StartRead();
    input_deadline_.async_wait(bind(&TcpSession::CheckDeadline,
                                    shared_from_this(),
                                    &input_deadline_));
    AwaitOutput();
    output_deadline_.async_wait(bind(&TcpSession::CheckDeadline,
                                     shared_from_this(),
                                     &output_deadline_));
  }

  tcp::socket& socket() {
    return socket_;
  }

  void Deliver(const std::string& msg) {
    output_queue_.push_back(msg + "\n");
    non_empty_output_queue_.expires_at(posix_time::neg_infin);
  }

 private:
  void Stop() {
    channel_.Leave(shared_from_this());

    error_code ignored_ec;
    socket_.close(ignored_ec);
    input_deadline_.cancel();
    non_empty_output_queue_.cancel();
    output_deadline_.cancel();
  }

  bool Stopped() const {
    return !socket_.is_open();
  }

  void StartRead() {
    input_deadline_.expires_from_now(posix_time::seconds(30));
    asio::async_read_until(socket_, input_buffer_, '\n',
                           bind(&TcpSession::HandleRead,
                                shared_from_this(), _1));
  }

  void HandleRead(const error_code& ec) {
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
          non_empty_output_queue_.expires_at(posix_time::neg_infin);
        }
      }

      StartRead();
    }
  }

  void AwaitOutput() {
    if (Stopped()) return;

    if (output_queue_.empty()) {
      non_empty_output_queue_.expires_at(posix_time::pos_infin);
      non_empty_output_queue_.async_wait(bind(&TcpSession::AwaitOutput,
                                              shared_from_this()));
    } else {
      StartWrite();
    }
  }

  void StartWrite() {
    output_deadline_.expires_from_now(posix_time::seconds(30));
    asio::async_write(socket_, asio::buffer(output_queue_.front()),
                      bind(&TcpSession::HandleWrite, shared_from_this(), _1));
  }

  void HandleWrite(const error_code& ec) {
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
      deadline->async_wait(bind(&TcpSession::CheckDeadline,
                                shared_from_this(), deadline));
  }

  Channel& channel_;
  tcp::socket socket_;
  asio::streambuf input_buffer_;
  deadline_timer input_deadline_;
  std::deque<std::string> output_queue_;
  deadline_timer non_empty_output_queue_;
  deadline_timer output_deadline_;
};

class Server {
 public:
  Server(asio::io_service& io_service,
         const tcp::endpoint& listen_endpoint)
    : io_service_(io_service),
      acceptor_(io_service, listen_endpoint) {
    StartAccept();
  }

  void StartAccept() {
    TcpSessionPtr new_session(new TcpSession(io_service_, channel_));

    acceptor_.async_accept(new_session->socket(),
        bind(&Server::HandleAccept, this, new_session, _1));
  }

  void HandleAccept(TcpSessionPtr session, const error_code& ec) {
    if (!ec) {
      for (const auto& msg : cache_) { // TODO(ds) use container adapter
        session->Deliver(msg.second);
      }
      session->Start();
    }

    StartAccept();
  }

  void PublishMessage(const std::string& msg) {
    cache_[cache_.size() + 1] = msg;
    channel_.Deliver(msg);
  }

 private:
  asio::io_service& io_service_;
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

    asio::io_service io_service;
    tcp::endpoint listen_endpoint(tcp::v4(), atoi(argv[1]));

    Server server(io_service, listen_endpoint);
    io_service.post(bind(&Server::PublishMessage, &server, "000"));

    std::thread t([&](){ io_service.run(); });
    std::string abc("abc");
    for (;;) {
      io_service.post(bind(&Server::PublishMessage, &server, abc));
      sleep(1);
    }

    t.join();
  }
  catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
