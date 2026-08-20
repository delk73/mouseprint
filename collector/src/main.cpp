#include <cerrno>
#include <csignal>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <poll.h>
#include <string>
#include <unistd.h>

#include <libinput.h>
#include <libudev.h>
#include <sqlite3.h>

namespace {

volatile std::sig_atomic_t running = 1;

void stop_handler(int) {
  running = 0;
}

int open_restricted(const char* path, int flags, void*) {
  const int fd = open(path, flags | O_CLOEXEC);
  return fd >= 0 ? fd : -errno;
}

void close_restricted(int fd, void*) {
  close(fd);
}

const libinput_interface interface = {
    open_restricted,
    close_restricted,
};

std::string device_id(libinput_device* device) {
  const char* sysname = libinput_device_get_sysname(device);
  return sysname ? sysname : "unknown";
}

std::string device_name(libinput_device* device) {
  const char* name = libinput_device_get_name(device);
  return name ? name : "unknown";
}

bool is_pointer_device(libinput_device* device) {
  return libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_POINTER) != 0;
}

std::uint64_t wallclock_us() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

std::uint64_t monotonic_us() {
  timespec value{};
  clock_gettime(CLOCK_MONOTONIC, &value);
  return static_cast<std::uint64_t>(value.tv_sec) * 1000000ULL +
         static_cast<std::uint64_t>(value.tv_nsec) / 1000ULL;
}

struct RawEvent {
  std::uint64_t sequence = 0;
  std::string device_id;
  std::string event_type;
  bool has_source_time = false;
  std::uint64_t source_time_us = 0;
  bool has_accelerated = false;
  double dx_accelerated = 0;
  double dy_accelerated = 0;
  bool has_unaccelerated = false;
  double dx_unaccelerated = 0;
  double dy_unaccelerated = 0;
  bool has_button = false;
  std::uint32_t button = 0;
  std::string button_state;
  bool has_scroll = false;
  std::string scroll_source;
  double scroll_x = 0;
  double scroll_y = 0;
};

class Database {
 public:
  ~Database() {
    close();
  }

  bool open(const std::string& path) {
    path_ = path;
    try {
      const std::filesystem::path database_path(path);
      if (database_path.has_parent_path()) {
        std::filesystem::create_directories(database_path.parent_path());
      }
    } catch (const std::filesystem::filesystem_error& error) {
      disable("could not create database directory: " + std::string(error.what()));
      return false;
    }

    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
      disable(last_error("could not open database"));
      return false;
    }

    sqlite3_busy_timeout(db_, 1000);
    if (!exec("PRAGMA journal_mode=WAL") || !exec("PRAGMA synchronous=NORMAL") ||
        !exec("PRAGMA foreign_keys=ON") ||
        !exec("CREATE TABLE IF NOT EXISTS collector_runs ("
              "run_id INTEGER PRIMARY KEY,"
              "started_wallclock_us INTEGER NOT NULL,"
              "started_monotonic_us INTEGER NOT NULL,"
              "ended_wallclock_us INTEGER,"
              "events_seen INTEGER)")) {
      return false;
    }
    if (!exec("CREATE TABLE IF NOT EXISTS devices ("
              "device_id TEXT PRIMARY KEY,"
              "device_name TEXT NOT NULL,"
              "first_seen_sequence INTEGER NOT NULL,"
              "last_seen_sequence INTEGER NOT NULL)")) {
      return false;
    }
    if (!exec("CREATE TABLE IF NOT EXISTS raw_input_events ("
              "event_id INTEGER PRIMARY KEY,"
              "run_id INTEGER NOT NULL REFERENCES collector_runs(run_id),"
              "receive_sequence INTEGER NOT NULL,"
              "source_time_us INTEGER,"
              "device_id TEXT NOT NULL,"
              "event_type TEXT NOT NULL,"
              "dx_accelerated_collector REAL,"
              "dy_accelerated_collector REAL,"
              "dx_unaccelerated REAL,"
              "dy_unaccelerated REAL,"
              "button INTEGER,"
              "button_state TEXT,"
              "scroll_source TEXT,"
              "scroll_x REAL,"
              "scroll_y REAL)")) {
      return false;
    }

    if (sqlite3_prepare_v2(db_,
                           "INSERT INTO collector_runs "
                           "(started_wallclock_us, started_monotonic_us) VALUES (?, ?)",
                           -1, &run_statement_, nullptr) != SQLITE_OK ||
        sqlite3_prepare_v2(
            db_,
            "INSERT INTO devices "
            "(device_id, device_name, first_seen_sequence, last_seen_sequence) "
            "VALUES (?, ?, ?, ?) "
            "ON CONFLICT(device_id) DO UPDATE SET "
            "device_name=excluded.device_name, last_seen_sequence=excluded.last_seen_sequence",
            -1, &device_statement_, nullptr) != SQLITE_OK ||
        sqlite3_prepare_v2(
            db_,
            "INSERT INTO raw_input_events "
            "(run_id, receive_sequence, source_time_us, device_id, event_type, "
            "dx_accelerated_collector, dy_accelerated_collector, dx_unaccelerated, "
            "dy_unaccelerated, button, button_state, scroll_source, scroll_x, scroll_y) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            -1, &event_statement_, nullptr) != SQLITE_OK) {
      disable(last_error("could not prepare database statements"));
      return false;
    }

    sqlite3_bind_int64(run_statement_, 1, static_cast<sqlite3_int64>(wallclock_us()));
    sqlite3_bind_int64(run_statement_, 2, static_cast<sqlite3_int64>(monotonic_us()));
    if (sqlite3_step(run_statement_) != SQLITE_DONE) {
      disable(last_error("could not create collector run"));
      return false;
    }
    run_id_ = sqlite3_last_insert_rowid(db_);
    sqlite3_reset(run_statement_);
    sqlite3_clear_bindings(run_statement_);
    enabled_ = true;
    return true;
  }

  bool enabled() const {
    return enabled_;
  }

  const std::string& path() const {
    return path_;
  }

  void record_device(libinput_device* device, std::uint64_t sequence) {
    if (!enabled_) return;
    if (!begin_batch()) return;
    const std::string id = device_id(device);
    const std::string name = device_name(device);
    sqlite3_bind_text(device_statement_, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(device_statement_, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(device_statement_, 3, static_cast<sqlite3_int64>(sequence));
    sqlite3_bind_int64(device_statement_, 4, static_cast<sqlite3_int64>(sequence));
    if (sqlite3_step(device_statement_) != SQLITE_DONE) {
      disable(last_error("could not persist device"));
      return;
    }
    sqlite3_reset(device_statement_);
    sqlite3_clear_bindings(device_statement_);
    ++pending_rows_;
    commit_if_full();
  }

  void record_event(const RawEvent& event) {
    if (!enabled_) return;
    if (!begin_batch()) return;
    sqlite3_bind_int64(event_statement_, 1, run_id_);
    sqlite3_bind_int64(event_statement_, 2, static_cast<sqlite3_int64>(event.sequence));
    if (event.has_source_time) {
      sqlite3_bind_int64(event_statement_, 3,
                         static_cast<sqlite3_int64>(event.source_time_us));
    } else {
      sqlite3_bind_null(event_statement_, 3);
    }
    sqlite3_bind_text(event_statement_, 4, event.device_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(event_statement_, 5, event.event_type.c_str(), -1, SQLITE_TRANSIENT);
    bind_real(event_statement_, 6, event.has_accelerated, event.dx_accelerated);
    bind_real(event_statement_, 7, event.has_accelerated, event.dy_accelerated);
    bind_real(event_statement_, 8, event.has_unaccelerated, event.dx_unaccelerated);
    bind_real(event_statement_, 9, event.has_unaccelerated, event.dy_unaccelerated);
    if (event.has_button) {
      sqlite3_bind_int(event_statement_, 10, static_cast<int>(event.button));
      sqlite3_bind_text(event_statement_, 11, event.button_state.c_str(), -1, SQLITE_TRANSIENT);
    } else {
      sqlite3_bind_null(event_statement_, 10);
      sqlite3_bind_null(event_statement_, 11);
    }
    if (event.has_scroll) {
      sqlite3_bind_text(event_statement_, 12, event.scroll_source.c_str(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_double(event_statement_, 13, event.scroll_x);
      sqlite3_bind_double(event_statement_, 14, event.scroll_y);
    } else {
      sqlite3_bind_null(event_statement_, 12);
      sqlite3_bind_null(event_statement_, 13);
      sqlite3_bind_null(event_statement_, 14);
    }
    if (sqlite3_step(event_statement_) != SQLITE_DONE) {
      disable(last_error("could not persist input event"));
      return;
    }
    sqlite3_reset(event_statement_);
    sqlite3_clear_bindings(event_statement_);
    ++pending_rows_;
    commit_if_full();
  }

  void finish(std::uint64_t events_seen) {
    if (!db_) return;
    if (enabled_ && pending_rows_ > 0 && !commit_batch()) return;
    if (enabled_) {
      sqlite3_stmt* statement = nullptr;
      if (sqlite3_prepare_v2(db_,
                             "UPDATE collector_runs SET ended_wallclock_us=?, events_seen=? "
                             "WHERE run_id=?",
                             -1, &statement, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(wallclock_us()));
        sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(events_seen));
        sqlite3_bind_int64(statement, 3, run_id_);
        if (sqlite3_step(statement) != SQLITE_DONE) {
          disable(last_error("could not finalize collector run"));
        }
        sqlite3_finalize(statement);
      } else {
        disable(last_error("could not prepare run finalization"));
      }
    }
  }

  void close() {
    if (run_statement_) sqlite3_finalize(run_statement_);
    if (device_statement_) sqlite3_finalize(device_statement_);
    if (event_statement_) sqlite3_finalize(event_statement_);
    if (db_) sqlite3_close(db_);
    run_statement_ = nullptr;
    device_statement_ = nullptr;
    event_statement_ = nullptr;
    db_ = nullptr;
  }

 private:
  static void bind_real(sqlite3_stmt* statement, int index, bool present, double value) {
    if (present) {
      sqlite3_bind_double(statement, index, value);
    } else {
      sqlite3_bind_null(statement, index);
    }
  }

  bool exec(const char* sql) {
    char* error_message = nullptr;
    const int result = sqlite3_exec(db_, sql, nullptr, nullptr, &error_message);
    if (result != SQLITE_OK) {
      const std::string detail = error_message ? error_message : sqlite3_errmsg(db_);
      sqlite3_free(error_message);
      disable("database SQL failed: " + detail);
      return false;
    }
    return true;
  }

  bool begin_batch() {
    if (pending_rows_ != 0) return true;
    return exec("BEGIN TRANSACTION");
  }

  void commit_if_full() {
    if (pending_rows_ >= batch_size_) commit_batch();
  }

  bool commit_batch() {
    if (pending_rows_ == 0) return true;
    if (!exec("COMMIT")) return false;
    pending_rows_ = 0;
    return true;
  }

  std::string last_error(const std::string& prefix) const {
    return prefix + ": " + (db_ ? sqlite3_errmsg(db_) : "no database");
  }

  void disable(const std::string& message) {
    if (!message.empty()) {
      std::cerr << "mouseprint-collector: warning: " << message << "\n";
    }
    if (db_ && pending_rows_ > 0) {
      sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    pending_rows_ = 0;
    enabled_ = false;
  }

  sqlite3* db_ = nullptr;
  sqlite3_stmt* run_statement_ = nullptr;
  sqlite3_stmt* device_statement_ = nullptr;
  sqlite3_stmt* event_statement_ = nullptr;
  sqlite3_int64 run_id_ = 0;
  std::size_t pending_rows_ = 0;
  const std::size_t batch_size_ = 128;
  bool enabled_ = false;
  std::string path_;
};

const char* scroll_type_name(libinput_event_type type) {
  switch (type) {
    case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
      return "wheel";
    case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
      return "finger";
    case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS:
      return "continuous";
    default:
      return "unknown";
  }
}

void print_device_event(Database& database, const char* kind, libinput_device* device,
                        std::uint64_t sequence) {
  RawEvent event;
  event.sequence = sequence;
  event.device_id = device_id(device);
  event.event_type = kind == std::string("ADD") ? "DEVICE_ADDED" : "DEVICE_REMOVED";
  database.record_device(device, sequence);
  database.record_event(event);
  std::cout << "DEVICE_" << kind << " seq=" << sequence
            << " id=" << device_id(device)
            << " name=\"" << device_name(device) << "\""
            << " pointer=true\n";
  std::cout.flush();
}

void print_pointer_event(Database& database, libinput_event* base, std::uint64_t sequence) {
  libinput_device* device = libinput_event_get_device(base);
  libinput_event_pointer* pointer = libinput_event_get_pointer_event(base);
  if (!device || !pointer || !is_pointer_device(device)) {
    return;
  }

  const auto type = libinput_event_get_type(base);
  const std::uint64_t source_time_us = libinput_event_pointer_get_time_usec(pointer);
  RawEvent event;
  event.sequence = sequence;
  event.device_id = device_id(device);
  event.has_source_time = true;
  event.source_time_us = source_time_us;
  std::cout << std::setprecision(6) << std::fixed
            << "INPUT seq=" << sequence
            << " source_time_us=" << source_time_us
            << " id=" << device_id(device);

  switch (type) {
    case LIBINPUT_EVENT_POINTER_MOTION:
      event.event_type = "MOTION";
      event.has_accelerated = true;
      event.dx_accelerated = libinput_event_pointer_get_dx(pointer);
      event.dy_accelerated = libinput_event_pointer_get_dy(pointer);
      event.has_unaccelerated = true;
      event.dx_unaccelerated = libinput_event_pointer_get_dx_unaccelerated(pointer);
      event.dy_unaccelerated = libinput_event_pointer_get_dy_unaccelerated(pointer);
      std::cout << " type=MOTION"
                << " dx_accelerated_collector=" << event.dx_accelerated
                << " dy_accelerated_collector=" << event.dy_accelerated
                << " dx_unaccelerated=" << event.dx_unaccelerated
                << " dy_unaccelerated=" << event.dy_unaccelerated;
      break;

    case LIBINPUT_EVENT_POINTER_BUTTON:
      event.event_type = libinput_event_pointer_get_button_state(pointer) ==
                                 LIBINPUT_BUTTON_STATE_PRESSED
                             ? "BUTTON_DOWN"
                             : "BUTTON_UP";
      event.has_button = true;
      event.button = libinput_event_pointer_get_button(pointer);
      event.button_state = event.event_type == "BUTTON_DOWN" ? "down" : "up";
      std::cout << " type=BUTTON_"
                << (libinput_event_pointer_get_button_state(pointer) ==
                            LIBINPUT_BUTTON_STATE_PRESSED
                        ? "DOWN"
                        : "UP")
                << " button=" << libinput_event_pointer_get_button(pointer);
      break;

    case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
    case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
    case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS:
      event.event_type = "SCROLL";
      event.has_scroll = true;
      event.scroll_source = scroll_type_name(type);
      event.scroll_x = libinput_event_pointer_get_scroll_value(
          pointer, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
      event.scroll_y = libinput_event_pointer_get_scroll_value(
          pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
      std::cout << " type=SCROLL"
                << " source=" << event.scroll_source
                << " scroll_x=" << event.scroll_x
                << " scroll_y=" << event.scroll_y;
      break;

    // Ignore the deprecated axis event because newer libinput emits it in
    // addition to the source-specific scroll event.
    default:
      return;
  }

  database.record_event(event);
  std::cout << "\n";
  std::cout.flush();
}

void drain_events(libinput* context, Database& database, std::uint64_t& sequence) {
  while (libinput_event* event = libinput_get_event(context)) {
    ++sequence;
    const auto type = libinput_event_get_type(event);
    libinput_device* device = libinput_event_get_device(event);

    if (device && (type == LIBINPUT_EVENT_DEVICE_ADDED ||
                   type == LIBINPUT_EVENT_DEVICE_REMOVED)) {
      if (is_pointer_device(device)) {
        print_device_event(database,
                           type == LIBINPUT_EVENT_DEVICE_ADDED ? "ADD" : "REMOVE", device,
                           sequence);
      }
    } else if (type == LIBINPUT_EVENT_POINTER_MOTION ||
               type == LIBINPUT_EVENT_POINTER_BUTTON ||
               type == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL ||
               type == LIBINPUT_EVENT_POINTER_SCROLL_FINGER ||
               type == LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS) {
      print_pointer_event(database, event, sequence);
    }

    libinput_event_destroy(event);
  }
}

int fail(const std::string& message) {
  std::cerr << "mouseprint-collector: error: " << message << "\n";
  return 1;
}

std::string default_database_path() {
  const char* state_home = std::getenv("XDG_STATE_HOME");
  if (state_home && *state_home) {
    return std::string(state_home) + "/mouseprint/mouseprint.sqlite3";
  }
  const char* home = std::getenv("HOME");
  if (home && *home) {
    return std::string(home) + "/.local/state/mouseprint/mouseprint.sqlite3";
  }
  return "mouseprint.sqlite3";
}

bool parse_arguments(int argc, char** argv, std::string& database_path) {
  database_path = default_database_path();
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help") {
      std::cout << "Usage: mouseprint-collector [--database PATH]\n";
      return false;
    }
    if (argument == "--database" && index + 1 < argc) {
      database_path = argv[++index];
      continue;
    }
    std::cerr << "mouseprint-collector: error: unknown or incomplete option '" << argument
              << "'\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, stop_handler);
  std::signal(SIGTERM, stop_handler);

  std::string database_path;
  if (!parse_arguments(argc, argv, database_path)) {
    return argc > 1 && std::string(argv[1]) == "--help" ? 0 : 1;
  }

  Database database;
  const bool database_enabled = database.open(database_path);

  udev* udev_context = udev_new();
  if (!udev_context) {
    return fail("could not initialize udev");
  }

  libinput* context = libinput_udev_create_context(&interface, nullptr, udev_context);
  if (!context) {
    udev_unref(udev_context);
    return fail("could not initialize libinput");
  }

  libinput_log_set_priority(context, LIBINPUT_LOG_PRIORITY_ERROR);
  if (libinput_udev_assign_seat(context, "seat0") != 0) {
    libinput_unref(context);
    udev_unref(udev_context);
    return fail("could not assign libinput context to seat0");
  }

  const int input_fd = libinput_get_fd(context);
  if (input_fd < 0) {
    libinput_unref(context);
    udev_unref(udev_context);
    return fail("could not obtain libinput event fd");
  }

  std::cout << "MOUSEPRINT_READY seat=seat0 input_fd=" << input_fd
            << " mode=non-exclusive-pointer-observer"
            << " persistence=" << (database_enabled ? "enabled" : "disabled")
            << " database=\"" << database_path << "\"\n";
  std::cout.flush();

  std::uint64_t sequence = 0;
  if (libinput_dispatch(context) != 0) {
    libinput_unref(context);
    udev_unref(udev_context);
    return fail("initial libinput dispatch failed");
  }
  drain_events(context, database, sequence);

  while (running) {
    pollfd descriptor = {input_fd, POLLIN, 0};
    const int result = poll(&descriptor, 1, 250);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::cerr << "mouseprint-collector: poll failed: " << std::strerror(errno)
                << "\n";
      break;
    }

    if (result > 0 && (descriptor.revents & (POLLIN | POLLERR | POLLHUP))) {
      if (libinput_dispatch(context) != 0) {
        std::cerr << "mouseprint-collector: libinput dispatch failed\n";
        break;
      }
      drain_events(context, database, sequence);
    }
  }

  std::cout << "MOUSEPRINT_STOP events_seen=" << sequence << "\n";
  std::cout.flush();
  database.finish(sequence);
  libinput_unref(context);
  udev_unref(udev_context);
  return 0;
}
