#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>
#include <iostream>

using boost::asio::deadline_timer;
using boost::asio::ip::tcp;

class Client {
 public:
  Client(boost::asio::io_service& io_service)
    : stopped_(false),
      socket_(io_service),
      deadline_(io_service),
      heartbeat_timer_(io_service) {
  }

  void Start(tcp::resolver::iterator endpoint_iter) {
    StartConnect(endpoint_iter);
    deadline_.async_wait(boost::bind(&Client::CheckDeadline, this));
  }

  void Stop() {
    stopped_ = true;
    boost::system::error_code ignored_ec;
    socket_.close(ignored_ec);
    deadline_.cancel();
    heartbeat_timer_.cancel();
  }

 private:
  void StartConnect(tcp::resolver::iterator endpoint_iter) {
    if (endpoint_iter != tcp::resolver::iterator()) {
      std::cout << "Trying " << endpoint_iter->endpoint() << "...\n";

      deadline_.expires_from_now(boost::posix_time::seconds(60));

      socket_.async_connect(endpoint_iter->endpoint(),
                            boost::bind(&Client::HandleConnect,
                                        this, _1, endpoint_iter));
    } else {
      Stop();
    }
  }

  void HandleConnect(const boost::system::error_code& ec,
      tcp::resolver::iterator endpoint_iter) {
    if (stopped_)
      return;

    if (!socket_.is_open()) {
      std::cout << "Connect timed out\n";
      StartConnect(++endpoint_iter);
    } else if (ec) {
      std::cout << "Connect error: " << ec.message() << "\n";
      socket_.close();

      StartConnect(++endpoint_iter);
    } else {
      std::cout << "Connected to " << endpoint_iter->endpoint() << "\n";

      StartRead();
      StartWrite();
    }
  }

  void StartRead() {
    deadline_.expires_from_now(boost::posix_time::seconds(30));
    boost::asio::async_read_until(socket_, input_buffer_, '\n',
        boost::bind(&Client::HandleRead, this, _1));
  }

  void HandleRead(const boost::system::error_code& ec) {
    if (stopped_)
      return;

    if (!ec) {
      std::string line;
      std::istream is(&input_buffer_);
      std::getline(is, line);

      if (!line.empty())
        std::cout << "Received: " << line << "\n";

      StartRead();
    } else {
      std::cout << "Error on receive: " << ec.message() << "\n";
      Stop();
    }
  }

  void StartWrite() {
    if (stopped_)
      return;

    boost::asio::async_write(socket_, boost::asio::buffer("\n", 1),
        boost::bind(&Client::HandleWrite, this, _1));
  }

  void HandleWrite(const boost::system::error_code& ec) {
    if (stopped_)
      return;

    if (!ec) {
      heartbeat_timer_.expires_from_now(boost::posix_time::seconds(10));
      heartbeat_timer_.async_wait(boost::bind(&Client::StartWrite, this));
    }
    else {
      std::cout << "Error on heartbeat: " << ec.message() << "\n";
      Stop();
    }
  }

  void CheckDeadline() {
    if (stopped_)
      return;

    if (deadline_.expires_at() <= deadline_timer::traits_type::now()) {
      socket_.close();
      deadline_.expires_at(boost::posix_time::pos_infin);
    }

    deadline_.async_wait(boost::bind(&Client::CheckDeadline, this));
  }

private:
  bool stopped_;
  tcp::socket socket_;
  boost::asio::streambuf input_buffer_;
  deadline_timer deadline_;
  deadline_timer heartbeat_timer_;
};

int main(int argc, char* argv[]) {
  try {
    if (argc != 3) {
      std::cerr << "Usage: client <host> <port>\n";
      return 1;
    }

    boost::asio::io_service io_service;
    tcp::resolver r(io_service);
    Client c(io_service);

    c.Start(r.resolve(tcp::resolver::query(argv[1], argv[2])));
    io_service.run();
  }
  catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
