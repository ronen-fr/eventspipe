#pragma once

#include <bitset>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <vector>
#include <mutex>
#include <memory>
#include <algorithm> // or just at the .c
//#include <boost/container/flat_set.hpp>
#include <map>

#if 0
class TestEventsHook : public AdminSocketHook {
  OSD* osd;

public:
  explicit TestEventsHook(OSD* o) : osd(o) {}
  int call(std::string_view prefix,
           const cmdmap_t& cmdmap,
           Formatter* f,
           std::ostream& ss,
           bufferlist& out) override
  {
    ceph_abort("should use async hook");
  }
  void call_async(
    std::string_view prefix,
    const cmdmap_t& cmdmap,
    Formatter* f,
    const bufferlist& inbl,
    std::function<void(int, const std::string&, bufferlist&)> on_finish) override;
};
#endif

// remember to have a count of lost messages in the reports

using pip_token_t = uint64_t;
using maybe_pip_token = std::optional<pip_token_t>;
using domain_t = uint_fast16_t;
using group_t = uint_fast16_t;
using events_t = std::bitset<32>;
struct event_id_t {
  domain_t m_domain;
  group_t m_group;
  events_t m_event;
  [[nodiscard]] bool same_dg(const event_id_t& o) const
  {
    return m_domain == o.m_domain && m_group == o.m_group;
  }
};
using event_req_id = uint16_t;

using Formatter = std::string;

using OutBuf = std::string_view;


/*
	we need:
- the ID of the channel
- the fd of the pipe

*/
// the pipe of one client
// MUST BE REF-COUNTED!!!!
class EventsPipe {
public:
  EventsPipe(const std::filesystem::path& pipepath, maybe_pip_token client_token);
  ~EventsPipe();

  void discard_pipe(pip_token_t client_token);
  // int get_fd() const { return m_fd; }
  void send_event(event_req_id, OutBuf buffer);

  maybe_pip_token get_token() const { return (m_fd >= 0) ? maybe_pip_token{m_token} : maybe_pip_token{}; }

private:
  pip_token_t m_token;
  int m_fd{-1};
  std::filesystem::path m_path;

  static pip_token_t make_token();
  [[nodiscard]] static bool path_seems_safe(const std::filesystem::path& filepath);
};

class TesteventRegistration {
public:
  TesteventRegistration(EventsPipe& ev_pipe, event_req_id req_id, const event_id_t& ev_id);

  // events_t m_mask;
  event_id_t m_event_id;
  event_req_id m_req_id;
  EventsPipe* m_out_pipe;  // should be a weak pointer.
  bool m_obsolete{false};
};

class TesteventsDB {
public:
  TesteventsDB() = default;

  [[nodiscard]] maybe_pip_token client_registration(const std::filesystem::path& pipepath, maybe_pip_token client_token);
  void client_unregistration(pip_token_t client_token);


  [[nodiscard]] bool register_for_events(pip_token_t client_tok,
                                         event_req_id req_id,
                                         const event_id_t& ev_id);

  bool unregister_events(pip_token_t client_tok, event_req_id req_id);

public:  // interface for event posters
  //bool post_event(OutBuf bf, domain_t domain, group_t grp, events_t evnt);
  void post_event(OutBuf bf, const event_id_t& evnt);
  bool should_post(const event_id_t& evnt); // so that the client will only prepare the output stream if needed

private:
  /*ceph::*/ Formatter m_formatter;

  std::mutex m_pipestbl_lock;
  std::map<pip_token_t, std::unique_ptr<EventsPipe>> m_clients;
  std::mutex m_regis_lock;
  std::vector<TesteventRegistration> m_registrations;
};
