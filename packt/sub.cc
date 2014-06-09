#ifdef WIN32
#define _WIN32_WINNT 0x0501
#include <stdio.h>
#endif

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

using namespace boost::asio;
io_service service;

#define MEM_FN(x)       boost::bind(&self_type::x, shared_from_this())
#define MEM_FN1(x,y)    boost::bind(&self_type::x, shared_from_this(),y)
#define MEM_FN2(x,y,z)  boost::bind(&self_type::x, shared_from_this(),y,z)

class talk_to_svr : public boost::enable_shared_from_this<talk_to_svr>
                  , boost::noncopyable {
    typedef talk_to_svr self_type;
    talk_to_svr(const std::string &topic)
      : sock_(service), started_(true), topic_(topic), timer_(service) {}
    void start(ip::tcp::endpoint ep) {
        sock_.async_connect(ep, MEM_FN1(on_connect,_1));
    }

public:
    typedef boost::system::error_code error_code;
    typedef boost::shared_ptr<talk_to_svr> ptr;

    static ptr start(ip::tcp::endpoint ep, const std::string& topic) {
        std::cout << "start()\n";
        ptr new_(new talk_to_svr(topic));
        new_->start(ep);
        return new_;
    }

    void stop() {
        if ( !started_) return;
        std::cout << "stopping " << this << std::endl;
        started_ = false;
        sock_.close();
    }

    bool started() { return started_; }

private:
    void on_connect(const error_code & err) {
        std::cout << "on_connect()\n";
        if (!err)      do_write("subscribe " + topic_ + "\n");
        else            stop();
    }

    void on_read(const error_code & err, size_t bytes) {
        if (err) stop();
        if (!started() ) return;

        std::cout << "on_read()\n";

        // process the msg
        std::string msg(read_buffer_, bytes);
        if (msg.find("subscribed") == 0) on_subscribed();
        //else if (msg.find("ping") == 1) on_ping(msg);
        else if (msg.find("msg") == 0) on_msg(msg);
        else std::cerr << "invalid msg " << msg << std::endl;
    }

    void on_subscribed() {
        std::cout << "on_subscribed()\n";
        std::cout << topic_ << " subscribed" << std::endl;
        //do_ping();
    }

    /*void on_ping(const std::string & msg) {
        postpone_ping();
    }*/

    void on_msg(const std::string& msg) {
      std::cout << "on_msg(" << msg << ")\n";
      std::istringstream iss(msg);
      std::string data, topic;
      iss >> topic >> topic;
      std::getline(iss, data);
      std::cout << topic << ":" << data << std::endl;
      do_write("recv\n");
    }

    /*void do_ping() {
        std::cout << "ping\n";
        do_write("ping\n");
    }

    void postpone_ping() {
        // note: even though the server wants a ping every 5 secs, we randomly
        // don't ping that fast - so that the server will randomly disconnect us
        int millis = 1000;
        //int millis = rand() % 7000;
        std::cout << this << " postponing ping " << millis
                  << " millis" << std::endl;
        timer_.expires_from_now(boost::posix_time::millisec(millis));
        timer_.async_wait(MEM_FN(do_ping));
    }*/

    void on_write(const error_code & err, size_t bytes) {
        std::cout << "on_write()\n";
        do_read();
    }

    void do_read() {
        std::cout << "do_read()\n";
        async_read(sock_, buffer(read_buffer_),
                   MEM_FN2(read_complete,_1,_2), MEM_FN2(on_read,_1,_2));
    }

    void do_write(const std::string & msg) {
        if (!started() ) return;
        std::cout << "do_write: " << msg << std::endl;
        std::copy(msg.begin(), msg.end(), write_buffer_);
        sock_.async_write_some( buffer(write_buffer_, msg.size()),
                                MEM_FN2(on_write,_1,_2));
    }

    size_t read_complete(const boost::system::error_code & err, size_t bytes) {
        if (err) return 0;
        std::cout << "read_complete()\n";
        bool found = std::find(read_buffer_, read_buffer_ + bytes, '\n') < read_buffer_ + bytes;
        // we read one-by-one until we get to enter, no buffering
        return found ? 0 : 1;
    }

private:
    ip::tcp::socket sock_;
    enum { max_msg = 1024 };
    char read_buffer_[max_msg];
    char write_buffer_[max_msg];
    bool started_;
    std::string topic_;
    deadline_timer timer_;
};

int main(int argc, char* argv[]) {
    // connect several clients
    ip::tcp::endpoint ep( ip::address::from_string("127.0.0.1"), 8001);
    char* topics[] = { "data", "data", "data", 0 };

    for ( char ** topic = topics; *topic; ++topic) {
        talk_to_svr::start(ep, *topic);
        boost::this_thread::sleep(boost::posix_time::millisec(100));
    }

    service.run();
}
