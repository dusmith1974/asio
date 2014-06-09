#ifdef WIN32
#define _WIN32_WINNT 0x0501
#include <stdio.h>
#endif

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
using namespace boost::asio;
using namespace boost::posix_time;
io_service service;

class talk_to_client;
typedef boost::shared_ptr<talk_to_client> client_ptr;
typedef std::vector<client_ptr> array;
array clients;

#define MEM_FN(x)       boost::bind(&self_type::x, shared_from_this())
#define MEM_FN1(x,y)    boost::bind(&self_type::x, shared_from_this(),y)
#define MEM_FN2(x,y,z)  boost::bind(&self_type::x, shared_from_this(),y,z)


void update_clients_changed();

class talk_to_client : public boost::enable_shared_from_this<talk_to_client>
                     , boost::noncopyable {
    typedef talk_to_client self_type;
    talk_to_client() : sock_(service), started_(false),
                       timer_(service)/*, clients_changed_(false)*/ {
    }
public:
    typedef boost::system::error_code error_code;
    typedef boost::shared_ptr<talk_to_client> ptr;

    void start() {
        started_ = true;
        clients.push_back(shared_from_this());
        do_read(); // wait for a subscribtion.
    }

    static ptr new_() {
        ptr new_(new talk_to_client);
        return new_;
    }

    void stop() {
        if ( !started_) return;
        started_ = false;
        sock_.close();

        ptr self = shared_from_this();
        array::iterator it = std::find(clients.begin(), clients.end(), self);
        clients.erase(it);
    }

    bool started() const { return started_; }
    ip::tcp::socket& sock() { return sock_;}
    std::string topic() const { return topic_; }

private:
    void on_read(const error_code & err, size_t bytes) {
        if (err) stop();
        if (!started() ) return;

        // process the msg
        std::string msg(read_buffer_, bytes);
        if (msg.find("subscribe") == 0) on_subscribe(msg);
        else if (msg.find("ping") == 0) on_ping();
        else std::cerr << "invalid msg " << msg << std::endl;
    }

    void on_subscribe(const std::string & msg) {
        // store remote hostname as user_
        std::istringstream in(msg);
        in >> topic_ >> topic_;
        std::cout << topic_ << " subscribed" << std::endl;
        do_write("subscribed ok\n");
    }

    void on_ping() {
        std::cout << "ping ok\n";
        do_write("ping ok\n");
    }

    void on_check_ping(const boost::system::error_code& err) {
      if (err) return; // operation_aborted when expire_from_now reset

      std::cout << "ocp " << this << " " << boost::posix_time::microsec_clock::local_time().time_of_day().total_milliseconds() << "\n";
      std::cout << "stopping " << " - no ping in time" << std::endl;
      stop();
    }

    void post_check_ping() {
      std::cout << "pcp " << this << " " << boost::posix_time::microsec_clock::local_time().time_of_day().total_milliseconds() << "\n";
      timer_.expires_from_now(boost::posix_time::millisec(5000));
      timer_.async_wait(MEM_FN1(on_check_ping, _1));
    }

    void on_write(const error_code & err, size_t bytes) {
        do_read();
    }

    // start a read and set a check ping in 5s.
    void do_read() {
        async_read(sock_, buffer(read_buffer_),
                   MEM_FN2(read_complete,_1,_2), MEM_FN2(on_read,_1,_2));
        post_check_ping();
    }

    size_t read_complete(const boost::system::error_code & err, size_t bytes) {
        if ( err) return 0;
        bool found = std::find(read_buffer_, read_buffer_ + bytes, '\n') < read_buffer_ + bytes;
        // we read one-by-one until we get to enter, no buffering
        return found ? 0 : 1;
    }

private:
    ip::tcp::socket sock_;
    enum { max_msg = 1024 };
    char read_buffer_[max_msg];
    char write_buffer_[max_msg];
    void do_write(const std::string & msg) {
        if (!started() ) return;

        std::copy(msg.begin(), msg.end(), write_buffer_);
        sock_.async_write_some(buffer(write_buffer_, msg.size()),
                                MEM_FN2(on_write,_1,_2));
    }
    bool started_;

    std::string topic_;
    deadline_timer timer_;
    //bool clients_changed_;
};

// topics_changed?
/*void update_clients_changed() {
    for( array::iterator b = clients.begin(), e = clients.end(); b != e; ++b)
        (*b)->set_clients_changed();
}*/

ip::tcp::acceptor acceptor(service, ip::tcp::endpoint(ip::tcp::v4(), 8001));

void handle_accept(talk_to_client::ptr client, const boost::system::error_code & err) {
    client->start();
    talk_to_client::ptr new_client = talk_to_client::new_();
    acceptor.async_accept(new_client->sock(), boost::bind(handle_accept,new_client,_1));
}

int main(int argc, char* argv[]) {
  talk_to_client::ptr client = talk_to_client::new_();
  acceptor.async_accept(client->sock(), boost::bind(handle_accept,client,_1));
  service.run();
}
