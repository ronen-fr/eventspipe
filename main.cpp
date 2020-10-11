
#define CATCH_CONFIG_MAIN
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <fcntl.h>
#include "unistd.h"


#include "catch.hpp"

using namespace std;
namespace fs = std::filesystem;

#include "te.h"

int main_17()
{

  std::cout << "Hello, World!" << std::endl;
  return 0;
}

#include <sys/stat.h>
#include <sys/types.h>

#include <atomic>
#include <thread>

static void read_till(std::atomic<bool>* should_term, const fs::path& fn)
{
  int fd = open(fn.c_str(), O_RDWR);

  auto thrd = new std::thread([=, gatep = should_term]() {
    while (!gatep->load()) {
      int c;
      read(fd, &c, 1);
      write(2, &c, 1);
    };
    gatep->store(false);
  });

  thrd->detach();
}


// create a named pipe, and a process to keep reading it
void create_n_hold(std::atomic<bool>* should_term, fs::path fn, string_view dbg_nm)
{
  // verify the path is absolute, and does not exist
  auto fd0 = mkfifo(fn.c_str(), 0644);
  // ifstream fps(fd);

  //int fd = open(fn.c_str(), O_RDONLY);

  read_till(should_term, fn);
}

static string good_fname1 =
  "/tmp/tpipe_1";  // NOTE!!! make sure someone is already reading from this pipe
static string bad_fname1 = "/tmp/notthere";


TEST_CASE("Reg_unreg_clients", "[clients]")
{
  TesteventsDB db;

  fs::path fpt1{good_fname1};
  auto client1 = db.client_registration(fpt1, {});
  cout << "Client1 registration: " << client1.value_or(9999999);
  REQUIRE(client1.has_value());

  fs::path fpt2{bad_fname1};
  auto client2 = db.client_registration(fpt2, {});
  cout << "Client2 registration: " << client1.value_or(9999999);
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

TEST_CASE("our own pipe", "[clients]")
{
  TesteventsDB db;

  fs::path fpt1{good_fname3};
  std::atomic<bool> should_term{false};

  create_n_hold(&should_term, fpt1, "fpt1");

  auto client1 = db.client_registration(fpt1, {});
  cout << "Client1 registration: " << client1.value_or(9999999);
  REQUIRE(client1.has_value());

  should_term.store(true);
  while (should_term.load())
    sleep(1);

  unlink(fpt1.c_str());
}


#if 0
TEST_CASE("reg_unreg_events", "[events]")
{
  TesteventsDB db;

  fs::path fpt1{good_fname1};
  auto client1 = db.client_registration(fpt1, {});
  cout << "Client1 registration: " << client1.value_or(9999999);
  REQUIRE(client1.has_value());

  fs::path fpt2{bad_fname1};
  auto client2 = db.client_registration(fpt2, {});
  cout << "Client2 registration: " << client1.value_or(9999999);
  REQUIRE(!client2.has_value());

  SECTION("removing client1")
#endif
