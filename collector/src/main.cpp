#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <poll.h>
#include <string>
#include <unistd.h>

#include <libinput.h>
#include <libudev.h>

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

void print_device_event(const char* kind, libinput_device* device, std::uint64_t sequence) {
  std::cout << "DEVICE_" << kind << " seq=" << sequence
            << " id=" << device_id(device)
            << " name=\"" << device_name(device) << "\""
            << " pointer=true\n";
  std::cout.flush();
}

void print_pointer_event(libinput_event* base, std::uint64_t sequence) {
  libinput_device* device = libinput_event_get_device(base);
  libinput_event_pointer* pointer = libinput_event_get_pointer_event(base);
  if (!device || !pointer || !is_pointer_device(device)) {
    return;
  }

  const auto type = libinput_event_get_type(base);
  const std::uint64_t source_time_us = libinput_event_pointer_get_time_usec(pointer);
  std::cout << std::setprecision(6) << std::fixed
            << "INPUT seq=" << sequence
            << " source_time_us=" << source_time_us
            << " id=" << device_id(device);

  switch (type) {
    case LIBINPUT_EVENT_POINTER_MOTION:
      std::cout << " type=MOTION"
                << " dx_accelerated_collector=" << libinput_event_pointer_get_dx(pointer)
                << " dy_accelerated_collector=" << libinput_event_pointer_get_dy(pointer)
                << " dx_unaccelerated="
                << libinput_event_pointer_get_dx_unaccelerated(pointer)
                << " dy_unaccelerated="
                << libinput_event_pointer_get_dy_unaccelerated(pointer);
      break;

    case LIBINPUT_EVENT_POINTER_BUTTON:
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
      std::cout << " type=SCROLL"
                << " source=" << scroll_type_name(type)
                << " scroll_x="
                << libinput_event_pointer_get_scroll_value(
                       pointer, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)
                << " scroll_y="
                << libinput_event_pointer_get_scroll_value(
                       pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
      break;

    // Ignore the deprecated axis event because newer libinput emits it in
    // addition to the source-specific scroll event.
    default:
      return;
  }

  std::cout << "\n";
  std::cout.flush();
}

void drain_events(libinput* context, std::uint64_t& sequence) {
  while (libinput_event* event = libinput_get_event(context)) {
    ++sequence;
    const auto type = libinput_event_get_type(event);
    libinput_device* device = libinput_event_get_device(event);

    if (device && (type == LIBINPUT_EVENT_DEVICE_ADDED ||
                   type == LIBINPUT_EVENT_DEVICE_REMOVED)) {
      if (is_pointer_device(device)) {
        print_device_event(type == LIBINPUT_EVENT_DEVICE_ADDED ? "ADD" : "REMOVE",
                           device, sequence);
      }
    } else if (type == LIBINPUT_EVENT_POINTER_MOTION ||
               type == LIBINPUT_EVENT_POINTER_BUTTON ||
               type == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL ||
               type == LIBINPUT_EVENT_POINTER_SCROLL_FINGER ||
               type == LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS) {
      print_pointer_event(event, sequence);
    }

    libinput_event_destroy(event);
  }
}

int fail(const std::string& message) {
  std::cerr << "mouseprint-collector: error: " << message << "\n";
  return 1;
}

}  // namespace

int main() {
  std::signal(SIGINT, stop_handler);
  std::signal(SIGTERM, stop_handler);

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
            << " mode=non-exclusive-pointer-observer\n";
  std::cout.flush();

  std::uint64_t sequence = 0;
  if (libinput_dispatch(context) != 0) {
    libinput_unref(context);
    udev_unref(udev_context);
    return fail("initial libinput dispatch failed");
  }
  drain_events(context, sequence);

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
      drain_events(context, sequence);
    }
  }

  std::cout << "MOUSEPRINT_STOP events_seen=" << sequence << "\n";
  std::cout.flush();
  libinput_unref(context);
  udev_unref(udev_context);
  return 0;
}
