
#define CATCH_CONFIG_MAIN
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "catch.hpp"

using namespace std;
namespace fs = std::filesystem;

#include "te.h"

int main_17()
{

  std::cout << "Hello, World!" << std::endl;
  return 0;
}

static string good_fname1 = "/tmp/tpipe_1";
static string bad_fname1 = "/tmp/notthere";


TEST_CASE("Registering an event", "[Testevents]")
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

}
