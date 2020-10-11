//
// Created by rfriedma on 11/10/2020.
//

#include <fcntl.h>
#include <poll.h>
#include <sys/un.h>

#include <algorithm>
#include <iostream>
#include <string_view>

//#include "test_events.h"
#include "te.h"

#define dout std::cout
#define dendl "\n"

//#include "common/admin_socket.h"
//#include "common/admin_socket_client.h"
//#include "common/dout.h"
//#include "common/errno.h"
//#include "common/safe_io.h"
//#include "common/Thread.h"
//#include "common/version.h"
//#include "common/ceph_mutex.h"

// re-include our assert to clobber the system one; fix dout:
//#include "include/ceph_assert.h"
//#include "include/compat.h"
//#include "include/sock_compat.h"

#define dout_subsys ceph_subsys_asok
#undef dout_prefix
#define dout_prefix *_dout << "tpipe(" << (void*)m_cct << ") "

using namespace std::literals;
namespace fs = std::filesystem;

#if 0
void TestEventsHook::call_async(
  std::string_view prefix,
  const cmdmap_t& cmdmap,
  Formatter* f,
  const bufferlist& inbl,
  std::function<void(int, const std::string&, bufferlist&)> on_finish) override
{
try {
osd->asok_command(prefix, cmdmap, f, inbl, on_finish);
} catch (const TOPNSPC::common::bad_cmd_get& e) {
bufferlist empty;
on_finish(-EINVAL, e.what(), empty);
}
}
#endif


// -------------------------------------------------------------------------

/*
How do we maintain the clients?
for now - a vector of them.


*/

///

EventsPipe::EventsPipe(const std::filesystem::path& pipepath,
		       maybe_pip_token client_token)
    : m_token{client_token.value_or(make_token())}
    //  if a token was not provided - create a random one
    , m_path{pipepath}
{
  //  if a token was not provided - create a random one

  if (!path_seems_safe(pipepath)) {
    // dout << "rejecting incoming path. Not printed." << dendl;
  }

  // keep it open for output only. Will signal if the client is not maintaining an
  // active reader
  m_fd = open(pipepath.c_str(), O_APPEND | O_RDONLY);
}

EventsPipe::~EventsPipe()
{
  if (m_fd >= 0)
    close(m_fd);
}



/*
 for now - just verify that it's only printables, it is in /tmp and no '..'. This is
 absolutely not even close to the real thing. So many known security issues not handled.
*/
bool EventsPipe::path_seems_safe(const std::filesystem::path& filepath)
{
  // check the path string
  if (std::find_if(filepath.string().begin(), filepath.string().end(),
		   [](char ch) { return !isprint(ch); }) != filepath.string().end()) {
    dout << "Incoming path contains non-printable chars. Rejected." << dendl;
    return false;
  }

  if (filepath.string().find("..") != std::string::npos) {
    dout << "Incoming path contains '..'. Rejected." << dendl;
    return false;
  }

  // check the actual file at the end of the path
  std::error_code ec;
  auto s = fs::status(filepath, ec);
  if (ec || fs::is_regular_file(s) || fs::is_directory(s) || fs::is_block_file(s) ||
      !fs::exists(s)) {
    dout << "Incoming path does not name a pipe. Rejected." << dendl;
    return false;
  }

  return true;
}

// demo implementation!
pip_token_t EventsPipe::make_token()
{
  static pip_token_t handout{1000};
  return ++handout;
}


// -------------------------------------------------------------------------

bool Testevents::register_for_events(pip_token_t client_tok,
				     event_req_id req_id,
				     const event_id_t& ev_id)
{
  //  locate the client by its token
  auto client_en = m_clients.find(client_tok);
  if (client_en == m_clients.end()) {
    dout << "Events-pipe request from an unknown client." << dendl;
    return false;
  }

  // if this specific group is already requested with this req-id, the request is
  // understood as an edit of the existing one

  std::unique_lock lk(m_regis_lock);
  auto req =
    find_if(m_registrations.begin(), m_registrations.end(),
	    [req_id](const TesteventRegistration& e) { return req_id == e.m_req_id; });

  if (req != m_registrations.end()) {

    if (req->m_out_pipe->get_token() != client_tok) {
      dout << "Events-pipe w same req ID registered by another client" << dendl;
      return false;
    }

    // but make sure that the req_id indeed specifies the same M*G
    auto& existing_id = req->m_event_id;

    if (!existing_id.same_dg(ev_id)) {
      dout << "Events-pipe w/ mismatched req_id to previous domain/group" << dendl;
      return false;
    }

    existing_id.m_event = ev_id.m_event;
  } else {
    m_registrations.emplace_back(*client_en->second, req_id, ev_id);
  }
  return true;
}

bool Testevents::unregister_events(pip_token_t client_tok, event_req_id req_id)
{
  std::unique_lock lk(m_regis_lock);
  auto req =
    find_if(m_registrations.begin(), m_registrations.end(),
	    [req_id](const TesteventRegistration& e) { return req_id == e.m_req_id; });

  if (req == m_registrations.end()) {
    return false;
  }

  if (req->m_out_pipe->get_token() != client_tok) {
    dout << "Events-pipe w same req ID registered by another client" << dendl;
    return false;
  }

  m_registrations.erase(req);
  return true;
}


bool Testevents::should_post(const event_id_t& evnt)
{
  std::unique_lock lk(m_regis_lock);

  // search for registrations that match domainXgroup

  return (
    std::any_of(m_registrations.begin(), m_registrations.end(), [evnt](const auto& r) {
      return (evnt.m_domain == r.m_event_id.m_domain &&
	      evnt.m_group == r.m_event_id.m_group &&
	      (r.m_event_id.m_event & evnt.m_event).any());
    }));
}



void Testevents::post_event(OutBuf bf, const event_id_t& evnt)
{
  std::unique_lock lk(m_regis_lock);

  // search for registrations that match domainXgroup

  std::for_each(m_registrations.begin(), m_registrations.end(), [&](const auto& r) {
    if (evnt.m_domain == r.m_event_id.m_domain && evnt.m_group == r.m_event_id.m_group &&
	(r.m_event_id.m_event & evnt.m_event).any()) {

      // we have a client!
      r.m_out_pipe->send_event(r.m_req_id, bf);
    }
  });
}
