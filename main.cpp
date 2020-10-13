
#define CATCH_CONFIG_MAIN
#include <fcntl.h>
#include <sys/epoll.h>

#include <array>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "catch.hpp"
#include "unistd.h"

using namespace std;
namespace fs = std::filesystem;

#include <sys/stat.h>
#include <sys/types.h>

#include <atomic>
#include <thread>

#include "te.h"



class PipeClientWrap {
 public:
  PipeClientWrap(const fs::path& fn, string_view dbg_nm) : fn_{fn}, name_{dbg_nm}
  {
    if (!verify_name(fn)) {
      return;
    }

    if (mkfifo(fn.c_str(), 0644) == 0) {
      valid_ = true;
      poll_until();
    }
  }

  ~PipeClientWrap()
  {
    std::cout << __func__ << "\n";
    if (do_stop()) {
      unlink(fn_.c_str());
    }
  }

  [[nodiscard]] bool is_valid() const { return valid_; }

  bool do_stop()
  {
    bool was_valid = valid_;
    if (valid_) {
      valid_ = false;
      done_.store(true);
      while (done_.load())
	usleep(100'000);
    }
    return was_valid;
  }
  /// an 'expected' buffer, where we collect the event reports and compare
  /// against the expected output.

  struct Expected : public ut_out_if {
    string buf_;

    void push(string sv) final { buf_ = buf_.append(sv); }

    bool vs_expected(string_view sv)  // clears upon failure
    {
      // we do not have starts_with() yet,,,
      if (buf_.substr(0, sv.length()) != sv) {
	std::cout << "Expected failure. Have: " << buf_ << " vs " << sv << "\n";
	return false;
      }
      buf_.erase(0, sv.length());
      return true;
    }

    [[nodiscard]] bool is_empty() const { return buf_.empty(); }
    void clear() { buf_.clear(); }
  };

  Expected expect_;

 private:
  fs::path fn_;
  string_view name_;
  std::thread* thrd_{nullptr};
  bool valid_{false};  // should be made atomic

  std::atomic<bool> done_{false};
  std::atomic<bool> go_{false};

  static bool verify_name(fs::path fn);

  void poll_until()
  {
    int fd = open(fn_.c_str(), O_RDWR);

    if (fd <= 0) {
      cout << "Reader cannot open " << fn_.c_str() << "\n";
      valid_ = false;
      return;
    }

    cout << "reader opnd " << fn_.c_str() << endl;

    struct epoll_event ev;

    auto epoll_fd = epoll_create(1);
    ev.events = EPOLLIN;
    ev.data.fd = fd;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);

    thrd_ = new std::thread([this, epoll_fd, fd, gatep = &done_]() {
      struct epoll_event events[4];
      while (!gatep->load()) {

	auto nfds = epoll_wait(epoll_fd, events, 1, 100);
	// for now - testing one event
	if (events[0].data.fd == fd) {
	  char bf[128];
	  auto n = read(fd, bf, sizeof(bf));
	  write(1, bf, n);
	}
      };
      cout << "reader done " << endl;
      close(fd);
      close(epoll_fd);
      valid_ = false;
      gatep->store(false);
    });

    thrd_->detach();
  }

  void read_till()
  {
    int fd = open(fn_.c_str(), O_RDWR);

    if (fd <= 0) {
      cout << "Reader cannot open " << fn_.c_str() << "\n";
      valid_ = false;
      return;
    }

    cout << "reader opnd " << fn_.c_str() << endl;

    thrd_ = new std::thread([this, fd, gatep = &done_]() {
      while (!gatep->load()) {
	char bf[128];
	auto n = read(fd, bf, sizeof(bf));
	write(2, bf, n);
      };
      cout << "reader done " << endl;
      valid_ = false;
      gatep->store(false);
    });

    thrd_->detach();
    // while (!go_.load())
    //  sleep(1);
  }
};

bool PipeClientWrap::verify_name(fs::path filepath)
{
  std::string fn = filepath.string();

  if (fn.length() < 4) {
    cout << "Incoming path is too short. Rejected.\n";
    return false;
  }

  if (std::find_if(fn.begin(), fn.end(), [](char ch) { return !isprint(ch); }) !=
      fn.end()) {
    cout << "Incoming path contains non-printable chars. Rejected.\n";
    return false;
  }

  if (fn.find("..") != std::string::npos) {
    cout << "Incoming path contains '..'. Rejected.\n";
    return false;
  }

  // check the actual file at the end of the path
  std::error_code ec;
  auto s = fs::status(filepath, ec);
  if (!ec && fs::exists(s)) {
    cout << "File already exists\n";
    return false;
  }

  return true;
}

static string good_fname1 =
  "/tmp/tpipe_1";  // NOTE!!! make sure someone is already reading from this pipe
static string bad_fname1 = "/tmp/notthere";


TEST_CASE("Reg_unreg_clients", "[clients]")
{
  TesteventsDB db;

  fs::path fpt1{good_fname1};
  PipeClientWrap reader{fpt1, "Reg_unreg_clients"};
  REQUIRE(reader.is_valid());
  auto client1 = db.client_registration(fpt1, {});
  cout << "Client1 registration: " << client1.value_or(9999999) << "\n";
  REQUIRE(client1.has_value());

  fs::path fpt2{bad_fname1};
  auto client2 = db.client_registration(fpt2, {});
  cout << "Client2 registration: " << client1.value_or(9999999) << "\n";
  REQUIRE(!client2.has_value());

  SECTION("removing client1")
  {

    // try to create a duplicate reservation. That should fail
    auto client1dup = db.client_registration(fpt1, client1);
    REQUIRE(!client1dup.has_value());

    db.client_unregistration(*client1);
    client1dup = db.client_registration(fpt1, client1);
    REQUIRE(client1dup.has_value());
  }
}

static string good_fname3 = "/tmp/we_will_create";

TEST_CASE("our_own_pipe", "[clients]")
{
  TesteventsDB db;
  fs::path fpt{good_fname3};
  PipeClientWrap reader{fpt, "our_own_pipe"};
  REQUIRE(reader.is_valid());
  auto client1 = db.client_registration(fpt, {});
  cout << "Client1 registration: " << client1.value_or(9999999);
  REQUIRE(client1.has_value());
}

// event-groups for registration
static constexpr const event_id_t evm1{0x400, 0x0001, 0x00000002};
static constexpr const event_id_t evm1v2{0x400, 0x0001, 0x00000007};
static constexpr const event_id_t evm2{0x400, 0x0002, 0x0ff0};
static constexpr const event_id_t evmH{0x400, 0x0003, 0xf000};
static constexpr const event_id_t evmL{0x400, 0x0003, 0x0f00};

// "actual" specific events
static constexpr const event_id_t ev_1{0x400, 0x0001, 0x0001};
static constexpr const event_id_t ev_2{0x400, 0x0001, 0x00f0};
static constexpr const event_id_t ev_3{0x400, 0x0002, 0x0100};
static constexpr const event_id_t ev_4{0x400, 0x0002, 0x0002};
static constexpr const event_id_t ev_5{0xf90, 0x0002, 0x0002};
static constexpr const event_id_t ev_H{0x400, 0x0003, 0x1000};
static constexpr const event_id_t ev_6{0x400, 0x0002, 0x0080};


TEST_CASE("reg_unreg_events", "[events]")
{
  TesteventsDB db;
  fs::path fpt{good_fname3};
  PipeClientWrap reader{fpt, "reg_unreg_events"};
  REQUIRE(reader.is_valid());
  auto client1 = db.client_registration(fpt, {});
  cout << "Client1 registration: " << client1.value_or(9999999);
  REQUIRE(client1.has_value());

  REQUIRE(db.register_for_events(*client1, 101, evm1));
  REQUIRE(!db.register_for_events(*client1, 101, evm2));
  REQUIRE(db.register_for_events(*client1, 102, evm2));
  REQUIRE(db.register_for_events(*client1, 101, evm1v2));

  SECTION("ev_reg_1")
  {
    // verify that the registrations are there

    REQUIRE(db.should_post(ev_1));
    REQUIRE(!db.should_post(ev_2));
    REQUIRE(db.should_post(ev_3));
    REQUIRE(!db.should_post(ev_4));
    REQUIRE(!db.should_post(ev_5));
  }
  SECTION("ev_unreg_2")
  {
    // client exists, req-id OK
    REQUIRE(db.should_post(ev_3));
    REQUIRE(db.unregister_events(*client1, 102));
    REQUIRE(!db.should_post(ev_3));

    // client exists, req-id already removed
    REQUIRE(!db.unregister_events(*client1, 102));

    // client exists, req-id wrong
    REQUIRE(!db.unregister_events(*client1, 199));
    REQUIRE(db.should_post(ev_1));
  }

  SECTION("basic_posting")
  {
    OutBuf line1{"hello"};
    db.post_event(line1, ev_1);
  }
}

static string clnt1_path = "/tmp/cpipe1";
static string clnt2_path = "/tmp/cpipe2";


TEST_CASE("client_unreg", "[clients]")
{
  TesteventsDB db;

  // register client 1
  fs::path fpt1{clnt1_path};
  PipeClientWrap reader1{fpt1, "client_unreg 1"};
  REQUIRE(reader1.is_valid());
  auto client1 = db.client_registration(fpt1, pip_token_t{21});
  cout << "Client1 registration: " << client1.value_or(9999999);
  REQUIRE(client1.has_value());

  // register client 2
  fs::path fpt2{clnt2_path};
  PipeClientWrap reader2{fpt2, "client_unreg 2"};
  REQUIRE(reader2.is_valid());
  auto client2 = db.client_registration(fpt2, pip_token_t{21});	 // same token!
  cout << "Client2 registration (should have failed): " << client2.value_or(9999999);
  REQUIRE(!client2.has_value());

  // trying again to register client 2
  client2 = db.client_registration(fpt2, pip_token_t{22});  // same token!
  cout << "Client2 registration: " << client2.value_or(9999999);
  REQUIRE(client2.has_value());


  REQUIRE(db.register_for_events(*client1, 104, evmH));
  REQUIRE(db.register_for_events(*client2, 105, evmL));
  REQUIRE(db.register_for_events(*client1, 106, evm2));
  REQUIRE(db.register_for_events(*client2, 107, evm2));

  // now - remove client 1
  REQUIRE(db.should_post(ev_H));
  OutBuf line1{"hello"};
  db.post_event(line1, ev_H);
  db.client_unregistration(*client1);
  db.post_event(line1, ev_H);
  REQUIRE(!db.should_post(ev_H));
  REQUIRE(db.should_post(ev_6));
  db.post_event(line1, ev_6);
}

#define GET_CLIENT(N, PIPE)                             \
  fs::path fpath##N{(PIPE)};                            \
  PipeClientWrap rdr##N{fpath##N, "Rdr_" #N};           \
  REQUIRE(rdr##N.is_valid());                           \
  auto clnt_##N = db.client_registration(fpath##N, {}); \
  REQUIRE(clnt_##N.has_value());                        \
  db.add_test_sink(*clnt_##N, &rdr##N.expect_)


// test actual events

TEST_CASE("report1", "[reports]")
{
  TesteventsDB db;

  // register client 1
  GET_CLIENT(2, clnt2_path);
  REQUIRE(db.register_for_events(*clnt_2, 200, evmL));
  REQUIRE(db.register_for_events(*clnt_2, 201, evmH));
  REQUIRE(db.should_post(ev_H));

  db.post_event("Z", ev_H);
  REQUIRE(rdr2.expect_.vs_expected(from_ev(ev_H, *clnt_2, 201, "Z")));
  REQUIRE(rdr2.expect_.is_empty());
}
