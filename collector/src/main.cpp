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
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <poll.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

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

struct ContextSnapshot {
  std::uint64_t sample_monotonic_us = 0;
  std::uint64_t request_start_us = 0;
  std::uint64_t request_end_us = 0;
  std::uint64_t request_latency_us = 0;
  std::string sample_status;
  bool has_cursor = false;
  double cursor_x = 0;
  double cursor_y = 0;
  int monitor_id = -1;
  std::string monitor_name;
  int workspace_id = 0;
  std::string workspace_name;
  std::string active_window_address;
  std::string active_app;
  std::string active_window_class;
};

class ContextQueue {
 public:
  explicit ContextQueue(std::size_t capacity) : capacity_(capacity) {}

  void push(ContextSnapshot snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshots_.size() >= capacity_) {
      snapshots_.pop_front();
      ++dropped_;
    }
    snapshots_.push_back(std::move(snapshot));
  }

  bool pop(ContextSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshots_.empty()) return false;
    snapshot = std::move(snapshots_.front());
    snapshots_.pop_front();
    return true;
  }

  std::uint64_t dropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
  }

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::deque<ContextSnapshot> snapshots_;
  std::uint64_t dropped_ = 0;
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
              "device_name TEXT NOT NULL)")) {
      return false;
    }
    if (!migrate_devices()) {
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
    if (!exec("CREATE TABLE IF NOT EXISTS pointer_context ("
              "context_id INTEGER PRIMARY KEY,"
              "run_id INTEGER NOT NULL REFERENCES collector_runs(run_id),"
              "sample_monotonic_us INTEGER NOT NULL,"
              "request_start_us INTEGER NOT NULL,"
              "request_end_us INTEGER NOT NULL,"
              "request_latency_us INTEGER NOT NULL,"
              "sample_status TEXT NOT NULL,"
              "cursor_x REAL,"
              "cursor_y REAL,"
              "monitor_id INTEGER,"
              "monitor_name TEXT,"
              "workspace_id INTEGER,"
              "workspace_name TEXT,"
              "active_window_address TEXT,"
              "active_app TEXT,"
              "active_window_class TEXT)")) {
      return false;
    }
    if (!exec("CREATE TABLE IF NOT EXISTS input_context_matches ("
              "match_id INTEGER PRIMARY KEY,"
              "run_id INTEGER NOT NULL REFERENCES collector_runs(run_id),"
              "raw_event_id INTEGER NOT NULL REFERENCES raw_input_events(event_id),"
              "context_id INTEGER REFERENCES pointer_context(context_id),"
              "match_status TEXT NOT NULL,"
              "context_delta_us INTEGER,"
              "absolute_delta_us INTEGER,"
              "tolerance_us INTEGER NOT NULL)")) {
      return false;
    }

    if (sqlite3_prepare_v2(db_,
                           "INSERT INTO collector_runs "
                           "(started_wallclock_us, started_monotonic_us) VALUES (?, ?)",
                           -1, &run_statement_, nullptr) != SQLITE_OK ||
        sqlite3_prepare_v2(
            db_,
            "INSERT INTO devices "
            "(device_id, device_name) VALUES (?, ?) "
            "ON CONFLICT(device_id) DO UPDATE SET device_name=excluded.device_name",
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
    if (sqlite3_prepare_v2(
            db_,
            "INSERT INTO pointer_context "
            "(run_id, sample_monotonic_us, request_start_us, request_end_us, "
            "request_latency_us, sample_status, cursor_x, cursor_y, monitor_id, "
            "monitor_name, workspace_id, workspace_name, active_window_address, "
            "active_app, active_window_class) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            -1, &context_statement_, nullptr) != SQLITE_OK ||
        sqlite3_prepare_v2(
            db_,
            "INSERT INTO input_context_matches "
            "(run_id, raw_event_id, context_id, match_status, context_delta_us, "
            "absolute_delta_us, tolerance_us) VALUES (?, ?, ?, ?, ?, ?, ?)",
            -1, &match_statement_, nullptr) != SQLITE_OK) {
      disable(last_error("could not prepare context statements"));
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

  void record_device(libinput_device* device) {
    if (!enabled_) return;
    if (!begin_batch()) return;
    const std::string id = device_id(device);
    const std::string name = device_name(device);
    sqlite3_bind_text(device_statement_, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(device_statement_, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(device_statement_) != SQLITE_DONE) {
      disable(last_error("could not persist device"));
      return;
    }
    sqlite3_reset(device_statement_);
    sqlite3_clear_bindings(device_statement_);
    ++pending_rows_;
    commit_if_full();
  }

  sqlite3_int64 record_event(const RawEvent& event) {
    if (!enabled_) return 0;
    if (!begin_batch()) return 0;
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
      return 0;
    }
    const sqlite3_int64 event_id = sqlite3_last_insert_rowid(db_);
    sqlite3_reset(event_statement_);
    sqlite3_clear_bindings(event_statement_);
    ++pending_rows_;
    commit_if_full();
    return event_id;
  }

  sqlite3_int64 record_context(const ContextSnapshot& context) {
    if (!enabled_) return 0;
    if (!begin_batch()) return 0;
    sqlite3_bind_int64(context_statement_, 1, run_id_);
    sqlite3_bind_int64(context_statement_, 2,
                       static_cast<sqlite3_int64>(context.sample_monotonic_us));
    sqlite3_bind_int64(context_statement_, 3,
                       static_cast<sqlite3_int64>(context.request_start_us));
    sqlite3_bind_int64(context_statement_, 4,
                       static_cast<sqlite3_int64>(context.request_end_us));
    sqlite3_bind_int64(context_statement_, 5,
                       static_cast<sqlite3_int64>(context.request_latency_us));
    sqlite3_bind_text(context_statement_, 6, context.sample_status.c_str(), -1, SQLITE_TRANSIENT);
    if (context.has_cursor) {
      sqlite3_bind_double(context_statement_, 7, context.cursor_x);
      sqlite3_bind_double(context_statement_, 8, context.cursor_y);
    } else {
      sqlite3_bind_null(context_statement_, 7);
      sqlite3_bind_null(context_statement_, 8);
    }
    bind_int(context_statement_, 9, context.monitor_id >= 0, context.monitor_id);
    bind_text(context_statement_, 10, context.monitor_name);
    bind_int(context_statement_, 11, context.workspace_id != 0, context.workspace_id);
    bind_text(context_statement_, 12, context.workspace_name);
    bind_text(context_statement_, 13, context.active_window_address);
    bind_text(context_statement_, 14, context.active_app);
    bind_text(context_statement_, 15, context.active_window_class);
    if (sqlite3_step(context_statement_) != SQLITE_DONE) {
      disable(last_error("could not persist compositor context"));
      return 0;
    }
    const sqlite3_int64 context_id = sqlite3_last_insert_rowid(db_);
    sqlite3_reset(context_statement_);
    sqlite3_clear_bindings(context_statement_);
    ++pending_rows_;
    commit_if_full();
    return context_id;
  }

  void record_match(sqlite3_int64 raw_event_id, sqlite3_int64 context_id,
                    const std::string& status, std::int64_t delta_us,
                    std::uint64_t tolerance_us) {
    if (!enabled_ || raw_event_id == 0) return;
    if (!begin_batch()) return;
    sqlite3_bind_int64(match_statement_, 1, run_id_);
    sqlite3_bind_int64(match_statement_, 2, raw_event_id);
    if (context_id != 0) {
      sqlite3_bind_int64(match_statement_, 3, context_id);
    } else {
      sqlite3_bind_null(match_statement_, 3);
    }
    sqlite3_bind_text(match_statement_, 4, status.c_str(), -1, SQLITE_TRANSIENT);
    if (context_id != 0) {
      sqlite3_bind_int64(match_statement_, 5, delta_us);
      sqlite3_bind_int64(match_statement_, 6,
                         static_cast<sqlite3_int64>(delta_us < 0 ? -delta_us : delta_us));
    } else {
      sqlite3_bind_null(match_statement_, 5);
      sqlite3_bind_null(match_statement_, 6);
    }
    sqlite3_bind_int64(match_statement_, 7, static_cast<sqlite3_int64>(tolerance_us));
    if (sqlite3_step(match_statement_) != SQLITE_DONE) {
      disable(last_error("could not persist input/context match"));
      return;
    }
    sqlite3_reset(match_statement_);
    sqlite3_clear_bindings(match_statement_);
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
    if (context_statement_) sqlite3_finalize(context_statement_);
    if (match_statement_) sqlite3_finalize(match_statement_);
    if (db_) sqlite3_close(db_);
    run_statement_ = nullptr;
    device_statement_ = nullptr;
    event_statement_ = nullptr;
    context_statement_ = nullptr;
    match_statement_ = nullptr;
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

  static void bind_int(sqlite3_stmt* statement, int index, bool present, int value) {
    if (present) {
      sqlite3_bind_int(statement, index, value);
    } else {
      sqlite3_bind_null(statement, index);
    }
  }

  static void bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
    if (value.empty()) {
      sqlite3_bind_null(statement, index);
    } else {
      sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
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

  bool has_column(const char* table, const char* column, bool& present) {
    sqlite3_stmt* statement = nullptr;
    const std::string sql = std::string("PRAGMA table_info(") + table + ")";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
      disable(last_error("could not inspect database schema"));
      return false;
    }
    present = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
      const unsigned char* name = sqlite3_column_text(statement, 1);
      if (name && std::strcmp(reinterpret_cast<const char*>(name), column) == 0) {
        present = true;
        break;
      }
    }
    sqlite3_finalize(statement);
    return true;
  }

  bool migrate_devices() {
    bool has_first_seen = false;
    bool has_last_seen = false;
    if (!has_column("devices", "first_seen_sequence", has_first_seen) ||
        !has_column("devices", "last_seen_sequence", has_last_seen)) {
      return false;
    }
    if (!has_first_seen && !has_last_seen) return true;

    // Older Slice 2 databases stored run-local sequence values in this global
    // identity table. Keep the identity and move lifecycle meaning to the raw
    // DEVICE_ADDED/DEVICE_REMOVED rows, which are run-scoped.
    return exec("BEGIN TRANSACTION") && exec("DROP TABLE IF EXISTS devices_migrated") &&
           exec("CREATE TABLE devices_migrated ("
                "device_id TEXT PRIMARY KEY,"
                "device_name TEXT NOT NULL)") &&
           exec("INSERT OR IGNORE INTO devices_migrated (device_id, device_name) "
                "SELECT device_id, device_name FROM devices") &&
           exec("DROP TABLE devices") && exec("ALTER TABLE devices_migrated RENAME TO devices") &&
           exec("COMMIT");
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
    if (db_) {
      sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    pending_rows_ = 0;
    enabled_ = false;
  }

  sqlite3* db_ = nullptr;
  sqlite3_stmt* run_statement_ = nullptr;
  sqlite3_stmt* device_statement_ = nullptr;
  sqlite3_stmt* event_statement_ = nullptr;
  sqlite3_stmt* context_statement_ = nullptr;
  sqlite3_stmt* match_statement_ = nullptr;
  sqlite3_int64 run_id_ = 0;
  std::size_t pending_rows_ = 0;
  const std::size_t batch_size_ = 128;
  bool enabled_ = false;
  std::string path_;
};

struct MonitorState {
  int id = -1;
  std::string name;
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
  int workspace_id = 0;
  std::string workspace_name;
};

class HyprlandJsonReader {
 public:
  explicit HyprlandJsonReader(const std::string& text) : text_(text) {}

  bool cursor(double& x, double& y) {
    bool has_x = false;
    bool has_y = false;
    if (!object([&](const std::string& key, bool& known) {
          if (key == "x") {
            known = true;
            has_x = number(x);
            return has_x;
          }
          if (key == "y") {
            known = true;
            has_y = number(y);
            return has_y;
          }
          return true;
        })) {
      return false;
    }
    return has_x && has_y && finished();
  }

  bool active_window(std::string& address, std::string& window_class) {
    bool has_address = false;
    bool has_class = false;
    if (!object([&](const std::string& key, bool& known) {
          if (key == "address") {
            known = true;
            has_address = string(address);
            return has_address;
          }
          if (key == "class") {
            known = true;
            has_class = string(window_class);
            return has_class;
          }
          return true;
        })) {
      return false;
    }
    return has_address && has_class && finished();
  }

  bool active_workspace(int& id, std::string& name) {
    bool has_id = false;
    bool has_name = false;
    if (!workspace_object(id, name, has_id, has_name)) return false;
    return has_id && has_name && finished();
  }

  bool monitors(std::vector<MonitorState>& result) {
    if (!consume('[')) return false;
    skip_whitespace();
    if (consume(']')) return finished();
    while (true) {
      MonitorState monitor;
      bool has_id = false;
      bool has_name = false;
      bool has_x = false;
      bool has_y = false;
      bool has_width = false;
      bool has_height = false;
      if (!object([&](const std::string& key, bool& known) {
            if (key == "id") {
              known = true;
              has_id = integer(monitor.id);
              return has_id;
            }
            if (key == "name") {
              known = true;
              has_name = string(monitor.name);
              return has_name;
            }
            if (key == "x") {
              known = true;
              has_x = number(monitor.x);
              return has_x;
            }
            if (key == "y") {
              known = true;
              has_y = number(monitor.y);
              return has_y;
            }
            if (key == "width") {
              known = true;
              has_width = number(monitor.width);
              return has_width;
            }
            if (key == "height") {
              known = true;
              has_height = number(monitor.height);
              return has_height;
            }
            if (key == "activeWorkspace") {
              known = true;
              bool workspace_id = false;
              bool workspace_name = false;
              return workspace_object(monitor.workspace_id, monitor.workspace_name,
                                      workspace_id, workspace_name);
            }
            return true;
          })) {
        return false;
      }
      if (!has_id || !has_name || !has_x || !has_y || !has_width || !has_height) return false;
      result.push_back(std::move(monitor));
      skip_whitespace();
      if (consume(']')) return finished();
      if (!consume(',')) return false;
    }
  }

 private:
  static bool is_hex(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
  }

  static std::uint32_t hex_value(char value) {
    if (value >= '0' && value <= '9') return static_cast<std::uint32_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<std::uint32_t>(value - 'a' + 10);
    return static_cast<std::uint32_t>(value - 'A' + 10);
  }

  static void append_utf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7f) {
      output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
      output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
      output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
      output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
  }

  bool string(std::string& output) {
    if (!consume('"')) return false;
    output.clear();
    while (position_ < text_.size()) {
      const char value = text_[position_++];
      if (value == '"') return true;
      if (static_cast<unsigned char>(value) < 0x20) return false;
      if (value != '\\') {
        output.push_back(value);
        continue;
      }
      if (position_ >= text_.size()) return false;
      const char escape = text_[position_++];
      switch (escape) {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case 'u': {
          if (position_ + 4 > text_.size()) return false;
          std::uint32_t codepoint = 0;
          for (int index = 0; index < 4; ++index) {
            if (!is_hex(text_[position_])) return false;
            codepoint = (codepoint << 4) | hex_value(text_[position_++]);
          }
          if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (position_ + 6 > text_.size() || text_[position_] != '\\' ||
                text_[position_ + 1] != 'u') return false;
            position_ += 2;
            std::uint32_t low = 0;
            for (int index = 0; index < 4; ++index) {
              if (!is_hex(text_[position_])) return false;
              low = (low << 4) | hex_value(text_[position_++]);
            }
            if (low < 0xdc00 || low > 0xdfff) return false;
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
          } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            return false;
          }
          append_utf8(output, codepoint);
          break;
        }
        default: return false;
      }
    }
    return false;
  }

  bool number(double& output) {
    skip_whitespace();
    if (position_ >= text_.size() || (text_[position_] != '-' &&
                                      (text_[position_] < '0' || text_[position_] > '9'))) {
      return false;
    }
    char* end = nullptr;
    errno = 0;
    output = std::strtod(text_.c_str() + position_, &end);
    if (end == text_.c_str() + position_ || errno == ERANGE || !std::isfinite(output)) {
      return false;
    }
    position_ = static_cast<std::size_t>(end - text_.c_str());
    return true;
  }

  bool integer(int& output) {
    double value = 0;
    if (!number(value) || std::trunc(value) != value || value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
      return false;
    }
    output = static_cast<int>(value);
    return true;
  }

  bool workspace_object(int& id, std::string& name, bool& has_id, bool& has_name) {
    has_id = false;
    has_name = false;
    return object([&](const std::string& key, bool& known) {
      if (key == "id") {
        known = true;
        has_id = integer(id);
        return has_id;
      }
      if (key == "name") {
        known = true;
        has_name = string(name);
        return has_name;
      }
      return true;
    });
  }

  template <typename Handler>
  bool object(Handler handler) {
    if (!consume('{')) return false;
    skip_whitespace();
    if (consume('}')) return true;
    while (true) {
      std::string key;
      if (!string(key) || !consume(':')) return false;
      bool known = false;
      if (!handler(key, known)) return false;
      if (!known && !skip_value()) return false;
      skip_whitespace();
      if (consume('}')) return true;
      if (!consume(',')) return false;
    }
  }

  bool skip_value() {
    skip_whitespace();
    if (position_ >= text_.size()) return false;
    if (text_[position_] == '"') {
      std::string ignored;
      return string(ignored);
    }
    if (text_[position_] == '{') {
      return object([](const std::string&, bool& known) {
        known = false;
        return true;
      });
    }
    if (text_[position_] == '[') {
      ++position_;
      skip_whitespace();
      if (consume(']')) return true;
      while (true) {
        if (!skip_value()) return false;
        skip_whitespace();
        if (consume(']')) return true;
        if (!consume(',')) return false;
      }
    }
    if (text_.compare(position_, 4, "true") == 0) {
      position_ += 4;
      return true;
    }
    if (text_.compare(position_, 5, "false") == 0) {
      position_ += 5;
      return true;
    }
    if (text_.compare(position_, 4, "null") == 0) {
      position_ += 4;
      return true;
    }
    double ignored = 0;
    return number(ignored);
  }

  void skip_whitespace() {
    while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\n' ||
                                       text_[position_] == '\r' || text_[position_] == '\t')) {
      ++position_;
    }
  }

  bool consume(char expected) {
    skip_whitespace();
    if (position_ >= text_.size() || text_[position_] != expected) return false;
    ++position_;
    return true;
  }

  bool finished() {
    skip_whitespace();
    return position_ == text_.size();
  }

  const std::string& text_;
  std::size_t position_ = 0;
};

class HyprlandContextBridge {
 public:
  static constexpr std::uint64_t sample_period_us = 16667;
  static constexpr std::uint64_t request_timeout_ms = 100;

  HyprlandContextBridge(std::string request_socket, std::string event_socket,
                        ContextQueue& queue)
      : request_socket_(std::move(request_socket)),
        event_socket_(std::move(event_socket)),
        queue_(queue) {}

  ~HyprlandContextBridge() {
    stop();
  }

  void start() {
    thread_ = std::thread(&HyprlandContextBridge::run, this);
  }

  void stop() {
    stop_requested_ = true;
    if (thread_.joinable()) thread_.join();
  }

  std::uint64_t samples_attempted() const { return samples_attempted_; }
  std::uint64_t samples_succeeded() const { return samples_succeeded_; }
  std::uint64_t events_seen() const { return events_seen_; }
  std::uint64_t latency_total_us() const { return latency_total_us_; }
  std::uint64_t latency_min_us() const { return latency_min_us_; }
  std::uint64_t latency_max_us() const { return latency_max_us_; }

 private:
  static int connect_socket(const std::string& path) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
      close(fd);
      return -1;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
      close(fd);
      return -1;
    }
    return fd;
  }

  bool request_json(const std::string& request, std::string& response,
                    std::uint64_t& start_us, std::uint64_t& end_us) {
    start_us = monotonic_us();
    const int fd = connect_socket(request_socket_);
    if (fd < 0) {
      end_us = monotonic_us();
      return false;
    }
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = static_cast<suseconds_t>(request_timeout_ms * 1000);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    const char* data = request.data();
    std::size_t remaining = request.size();
    while (remaining > 0) {
      const ssize_t written = send(fd, data, remaining, MSG_NOSIGNAL);
      if (written <= 0) {
        close(fd);
        end_us = monotonic_us();
        return false;
      }
      data += written;
      remaining -= static_cast<std::size_t>(written);
    }

    response.clear();
    char buffer[4096];
    while (true) {
      const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
      if (count == 0) break;
      if (count < 0) {
        close(fd);
        end_us = monotonic_us();
        return false;
      }
      response.append(buffer, static_cast<std::size_t>(count));
    }
    close(fd);
    end_us = monotonic_us();
    return true;
  }

  bool refresh_monitors(const std::string& response) {
    HyprlandJsonReader parser(response);
    std::vector<MonitorState> next;
    if (!parser.monitors(next)) return false;
    monitors_ = std::move(next);
    return true;
  }

  bool refresh_active_window(const std::string& response) {
    HyprlandJsonReader parser(response);
    if (!parser.active_window(active_window_address_, active_window_class_)) return false;
    active_app_ = active_window_class_;
    return true;
  }

  bool refresh_active_workspace(const std::string& response) {
    HyprlandJsonReader parser(response);
    return parser.active_workspace(focused_workspace_id_, focused_workspace_name_);
  }

  bool refresh_state() {
    bool success = true;
    std::string response;
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    if (!request_json("j/monitors", response, start, end) || !refresh_monitors(response)) {
      success = false;
    }
    if (!request_json("j/activewindow", response, start, end) ||
        !refresh_active_window(response)) {
      success = false;
    }
    if (!request_json("j/activeworkspace", response, start, end) ||
        !refresh_active_workspace(response)) {
      success = false;
    }
    state_dirty_ = !success;
    next_state_refresh_us_ = success ? 0 : monotonic_us() + 1000000;
    return success;
  }

  void read_event_socket() {
    char buffer[4096];
    while (event_fd_ >= 0) {
      const ssize_t count = recv(event_fd_, buffer, sizeof(buffer), 0);
      if (count == 0) {
        close(event_fd_);
        event_fd_ = -1;
        return;
      }
      if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        close(event_fd_);
        event_fd_ = -1;
        return;
      }
      event_buffer_.append(buffer, static_cast<std::size_t>(count));
      std::size_t newline = 0;
      while ((newline = event_buffer_.find('\n')) != std::string::npos) {
        const std::string line = event_buffer_.substr(0, newline);
        event_buffer_.erase(0, newline + 1);
        ++events_seen_;
        const std::size_t separator = line.find(">>");
        if (separator == std::string::npos) continue;
        const std::string name = line.substr(0, separator);
        if (name == "configreloaded" || name == "monitoraddedv2" ||
            name == "monitorremoved" || name == "createworkspacev2" ||
            name == "destroyworkspacev2" || name == "focusedmon" ||
            name == "workspacev2" || name == "moveworkspacev2" ||
            name == "renameworkspace" || name == "openwindow" ||
            name == "closewindow" || name == "movewindowv2" ||
            name == "activewindowv2") {
          state_dirty_ = true;
        }
      }
    }
  }

  void sample_cursor() {
    ++samples_attempted_;
    ContextSnapshot snapshot;
    std::string response;
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    const bool request_ok = request_json("j/cursorpos", response, start, end);
    bool cursor_success = false;
    snapshot.request_start_us = start;
    snapshot.request_end_us = end;
    snapshot.request_latency_us = end >= start ? end - start : 0;
    snapshot.sample_monotonic_us = start + snapshot.request_latency_us / 2;
    snapshot.sample_status = request_ok ? (state_dirty_ ? "cursor_ok_state_stale" : "ok")
                                        : "cursor_request_failed";
    snapshot.monitor_id = -1;

    if (request_ok) {
      HyprlandJsonReader parser(response);
      if (parser.cursor(snapshot.cursor_x, snapshot.cursor_y)) {
        snapshot.has_cursor = true;
        cursor_success = true;
      } else {
        snapshot.sample_status = "cursor_response_invalid";
      }
    }

    if (snapshot.has_cursor) {
      for (const MonitorState& monitor : monitors_) {
        if (snapshot.cursor_x >= monitor.x && snapshot.cursor_x < monitor.x + monitor.width &&
            snapshot.cursor_y >= monitor.y && snapshot.cursor_y < monitor.y + monitor.height) {
          snapshot.monitor_id = monitor.id;
          snapshot.monitor_name = monitor.name;
          snapshot.workspace_id = monitor.workspace_id;
          snapshot.workspace_name = monitor.workspace_name;
          break;
        }
      }
    }
    snapshot.active_window_address = active_window_address_;
    snapshot.active_app = active_app_;
    snapshot.active_window_class = active_window_class_;
    queue_.push(std::move(snapshot));
    if (cursor_success) {
      ++samples_succeeded_;
      latency_total_us_ += end - start;
      latency_min_us_ = std::min(latency_min_us_, end - start);
      latency_max_us_ = std::max(latency_max_us_, end - start);
    }
    if (!cursor_success && !cursor_warning_emitted_) {
      std::cerr << "mouseprint-collector: warning: Hyprland cursor context request unavailable\n";
      cursor_warning_emitted_ = true;
    }
  }

  void run() {
    try {
      event_fd_ = connect_socket(event_socket_);
      if (event_fd_ < 0 && !event_warning_emitted_) {
        std::cerr << "mouseprint-collector: warning: Hyprland event socket unavailable\n";
        event_warning_emitted_ = true;
      }
      if (event_fd_ >= 0) {
        const int flags = fcntl(event_fd_, F_GETFL, 0);
        fcntl(event_fd_, F_SETFL, flags | O_NONBLOCK);
      }
      state_dirty_ = true;
      refresh_state();
      std::uint64_t next_sample = monotonic_us();
      std::uint64_t next_event_retry = next_sample;
      while (!stop_requested_) {
        const std::uint64_t now = monotonic_us();
        if (event_fd_ < 0 && now >= next_event_retry) {
          event_fd_ = connect_socket(event_socket_);
          if (event_fd_ >= 0) {
            const int flags = fcntl(event_fd_, F_GETFL, 0);
            fcntl(event_fd_, F_SETFL, flags | O_NONBLOCK);
          }
          next_event_retry = now + 1000000;
        }
        if (state_dirty_ && monotonic_us() >= next_state_refresh_us_) refresh_state();
        if (now >= next_sample) {
          sample_cursor();
          next_sample = monotonic_us() + sample_period_us;
        }

        const std::uint64_t current = monotonic_us();
        int timeout_ms = static_cast<int>(
            std::min<std::uint64_t>(1000, next_sample > current ?
                                                    (next_sample - current + 999) / 1000 : 0));
        if (event_fd_ >= 0) {
          pollfd descriptor{event_fd_, POLLIN, 0};
          const int result = poll(&descriptor, 1, timeout_ms);
          if (result > 0 && (descriptor.revents & (POLLIN | POLLHUP | POLLERR))) {
            read_event_socket();
          }
        } else {
          usleep(static_cast<useconds_t>(std::min(timeout_ms, 50) * 1000));
        }
      }
      if (event_fd_ >= 0) close(event_fd_);
      event_fd_ = -1;
    } catch (const std::exception& error) {
      std::cerr << "mouseprint-collector: warning: Hyprland context bridge stopped: "
                << error.what() << "\n";
      if (event_fd_ >= 0) close(event_fd_);
      event_fd_ = -1;
    }
  }

  const std::string request_socket_;
  const std::string event_socket_;
  ContextQueue& queue_;
  std::atomic<bool> stop_requested_{false};
  std::thread thread_;
  int event_fd_ = -1;
  std::string event_buffer_;
  std::vector<MonitorState> monitors_;
  int focused_workspace_id_ = 0;
  std::string focused_workspace_name_;
  std::string active_window_address_;
  std::string active_app_;
  std::string active_window_class_;
  bool state_dirty_ = true;
  std::uint64_t next_state_refresh_us_ = 0;
  std::uint64_t samples_attempted_ = 0;
  std::uint64_t samples_succeeded_ = 0;
  std::uint64_t events_seen_ = 0;
  std::uint64_t latency_total_us_ = 0;
  std::uint64_t latency_min_us_ = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t latency_max_us_ = 0;
  bool event_warning_emitted_ = false;
  bool cursor_warning_emitted_ = false;
};

class ContextCorrelator {
 public:
  static constexpr std::uint64_t retention_us = 2000000;

  explicit ContextCorrelator(std::uint64_t tolerance_us) : tolerance_us_(tolerance_us) {}

  void add_context(Database& database, const ContextSnapshot& snapshot) {
    const sqlite3_int64 context_id = database.record_context(snapshot);
    if (context_id == 0) return;
    all_contexts_.push_back({context_id, snapshot.sample_monotonic_us, snapshot.has_cursor});
    if (snapshot.has_cursor) {
      contexts_.push_back({context_id, snapshot.sample_monotonic_us});
    }
    while (!all_contexts_.empty() &&
           all_contexts_.front().sample_time_us + retention_us < snapshot.sample_monotonic_us) {
      all_contexts_.pop_front();
    }
    while (!contexts_.empty() &&
           contexts_.front().sample_time_us + retention_us < snapshot.sample_monotonic_us) {
      contexts_.pop_front();
    }
    finalize_ready(database, snapshot.sample_monotonic_us);
  }

  void add_input(Database& database, sqlite3_int64 raw_event_id, std::uint64_t source_time_us) {
    if (raw_event_id == 0) return;
    pending_.push_back({raw_event_id, source_time_us});
    if (!all_contexts_.empty()) finalize_ready(database, all_contexts_.back().sample_time_us);
  }

  void finish(Database& database) {
    while (!pending_.empty()) {
      finalize_one(database, pending_.front());
      pending_.pop_front();
    }
  }

 private:
  struct ContextRef {
    sqlite3_int64 id;
    std::uint64_t sample_time_us;
  };

  struct ContextObservation {
    sqlite3_int64 id;
    std::uint64_t sample_time_us;
    bool valid;
  };

  struct PendingInput {
    sqlite3_int64 raw_event_id;
    std::uint64_t source_time_us;
  };

  void finalize_ready(Database& database, std::uint64_t newest_context_time_us) {
    while (!pending_.empty() &&
           newest_context_time_us >= pending_.front().source_time_us + tolerance_us_) {
      finalize_one(database, pending_.front());
      pending_.pop_front();
    }
  }

  void finalize_one(Database& database, const PendingInput& input) {
    if (contexts_.empty()) {
      std::uint64_t failed_absolute = 0;
      const ContextObservation* failed = nearest_failed(input, failed_absolute);
      if (failed) {
        database.record_match(input.raw_event_id, failed->id, "unmatched_context_error",
                              signed_delta(failed->sample_time_us, input.source_time_us),
                              tolerance_us_);
        return;
      }
      database.record_match(input.raw_event_id, 0,
                            all_contexts_.empty() ? "unmatched_no_context"
                                                  : "unmatched_outside_tolerance",
                            0, tolerance_us_);
      return;
    }
    const ContextRef* nearest = &contexts_.front();
    std::uint64_t nearest_absolute = absolute_delta(input.source_time_us, nearest->sample_time_us);
    for (const ContextRef& context : contexts_) {
      const std::uint64_t candidate = absolute_delta(input.source_time_us, context.sample_time_us);
      if (candidate < nearest_absolute) {
        nearest = &context;
        nearest_absolute = candidate;
      }
    }
    if (nearest_absolute <= tolerance_us_) {
      const std::int64_t delta = signed_delta(nearest->sample_time_us, input.source_time_us);
      database.record_match(input.raw_event_id, nearest->id, "matched", delta, tolerance_us_);
      return;
    }

    std::uint64_t failed_absolute = 0;
    const ContextObservation* failed = nearest_failed(input, failed_absolute);
    if (failed) {
      database.record_match(input.raw_event_id, failed->id, "unmatched_context_error",
                            signed_delta(failed->sample_time_us, input.source_time_us),
                            tolerance_us_);
    } else {
      database.record_match(input.raw_event_id, nearest->id, "unmatched_outside_tolerance",
                            signed_delta(nearest->sample_time_us, input.source_time_us),
                            tolerance_us_);
    }
  }

  const ContextObservation* nearest_failed(const PendingInput& input,
                                            std::uint64_t& nearest_absolute) const {
    const ContextObservation* nearest = nullptr;
    nearest_absolute = 0;
    for (const ContextObservation& observation : all_contexts_) {
      if (observation.valid) continue;
      const std::uint64_t candidate = absolute_delta(input.source_time_us,
                                                     observation.sample_time_us);
      if (candidate > tolerance_us_ || (nearest && candidate >= nearest_absolute)) continue;
      nearest = &observation;
      nearest_absolute = candidate;
    }
    return nearest;
  }

  static std::uint64_t absolute_delta(std::uint64_t left, std::uint64_t right) {
    return left >= right ? left - right : right - left;
  }

  static std::int64_t signed_delta(std::uint64_t context_time_us,
                                   std::uint64_t input_time_us) {
    if (context_time_us >= input_time_us) {
      return static_cast<std::int64_t>(context_time_us - input_time_us);
    }
    return -static_cast<std::int64_t>(input_time_us - context_time_us);
  }

  const std::uint64_t tolerance_us_;
  std::deque<ContextRef> contexts_;
  std::deque<ContextObservation> all_contexts_;
  std::deque<PendingInput> pending_;
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

double scroll_value(libinput_event_pointer* pointer, libinput_pointer_axis axis) {
  return libinput_event_pointer_has_axis(pointer, axis)
             ? libinput_event_pointer_get_scroll_value(pointer, axis)
             : 0;
}

void print_device_event(Database& database, const char* kind, libinput_device* device,
                        std::uint64_t sequence) {
  RawEvent event;
  event.sequence = sequence;
  event.device_id = device_id(device);
  event.event_type = std::strcmp(kind, "ADD") == 0 ? "DEVICE_ADDED" : "DEVICE_REMOVED";
  database.record_device(device);
  database.record_event(event);
  std::cout << "DEVICE_" << kind << " seq=" << sequence
            << " id=" << device_id(device)
            << " name=\"" << device_name(device) << "\""
            << " pointer=true\n";
  std::cout.flush();
}

void print_pointer_event(Database& database, ContextCorrelator& correlator, libinput_event* base,
                         std::uint64_t sequence) {
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
      event.scroll_x = scroll_value(pointer, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
      event.scroll_y = scroll_value(pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
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

  const sqlite3_int64 event_id = database.record_event(event);
  correlator.add_input(database, event_id, event.source_time_us);
  std::cout << "\n";
  std::cout.flush();
}

void drain_events(libinput* context, Database& database, ContextCorrelator& correlator,
                  std::uint64_t& sequence) {
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
      print_pointer_event(database, correlator, event, sequence);
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

void drain_context_queue(ContextQueue& queue, Database& database, ContextCorrelator& correlator) {
  ContextSnapshot snapshot;
  while (queue.pop(snapshot)) {
    correlator.add_context(database, snapshot);
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, stop_handler);
  std::signal(SIGTERM, stop_handler);

  std::string database_path;
  if (!parse_arguments(argc, argv, database_path)) {
    return argc > 1 && std::string(argv[1]) == "--help" ? 0 : 1;
  }

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

  // Do not create a collector run until the input observer is initialized.
  // This prevents startup failures from looking like interrupted recordings.
  Database database;
  const bool database_enabled = database.open(database_path);
  const std::uint64_t correlation_tolerance_us = 25000;
  const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
  const char* instance = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
  std::string hyprland_dir;
  if (instance && runtime_dir) {
    hyprland_dir = std::string(runtime_dir) + "/hypr/" + instance;
  } else if (instance) {
    hyprland_dir = std::string("/tmp/hypr/") + instance;
  }
  ContextQueue context_queue(256);
  HyprlandContextBridge context_bridge(
      hyprland_dir.empty() ? std::string() : hyprland_dir + "/.socket.sock",
      hyprland_dir.empty() ? std::string() : hyprland_dir + "/.socket2.sock", context_queue);
  ContextCorrelator correlator(correlation_tolerance_us);
  context_bridge.start();

  std::cout << "MOUSEPRINT_READY seat=seat0 input_fd=" << input_fd
            << " mode=non-exclusive-pointer-observer"
            << " persistence=" << (database_enabled ? "enabled" : "disabled")
            << " database=\"" << database_path << "\""
            << " hyprland_context=threaded sampling_hz=60 tolerance_us="
            << correlation_tolerance_us << "\n";
  std::cout.flush();

  std::uint64_t sequence = 0;
  if (libinput_dispatch(context) != 0) {
    context_bridge.stop();
    database.finish(sequence);
    libinput_unref(context);
    udev_unref(udev_context);
    return fail("initial libinput dispatch failed");
  }
  drain_events(context, database, correlator, sequence);
  drain_context_queue(context_queue, database, correlator);

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
      drain_events(context, database, correlator, sequence);
    }
    drain_context_queue(context_queue, database, correlator);
  }

  context_bridge.stop();
  drain_context_queue(context_queue, database, correlator);
  correlator.finish(database);
  const std::uint64_t attempted = context_bridge.samples_attempted();
  const std::uint64_t succeeded = context_bridge.samples_succeeded();
  const std::uint64_t average_latency =
      succeeded == 0 ? 0 : context_bridge.latency_total_us() / succeeded;
  std::cout << "MOUSEPRINT_STOP events_seen=" << sequence << "\n";
  std::cout << "HYPR_CONTEXT samples_attempted=" << attempted
            << " samples_succeeded=" << succeeded
            << " success_rate=" << std::setprecision(3)
            << (attempted == 0 ? 0.0 : static_cast<double>(succeeded) / attempted)
            << " request_latency_us_avg=" << average_latency
            << " request_latency_us_min="
            << (succeeded == 0 ? 0 : context_bridge.latency_min_us())
            << " request_latency_us_max=" << context_bridge.latency_max_us()
            << " event_count=" << context_bridge.events_seen()
            << " queue_dropped=" << context_queue.dropped() << "\n";
  std::cout.flush();
  database.finish(sequence);
  libinput_unref(context);
  udev_unref(udev_context);
  return 0;
}
