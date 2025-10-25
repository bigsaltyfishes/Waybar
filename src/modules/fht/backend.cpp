#include "modules/fht/backend.hpp"

#include <netdb.h>
#include <netinet/in.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <thread>

#include "giomm/datainputstream.h"
#include "giomm/dataoutputstream.h"
#include "giomm/unixinputstream.h"
#include "giomm/unixoutputstream.h"

namespace waybar::modules::fht {

int IPC::connectToSocket() {
  const char *socket_path = getenv("FHTC_SOCKET_PATH");

  if (socket_path == nullptr) {
    spdlog::warn("Fht is not running, fht IPC will not be available.");
    return -1;
  }

  struct sockaddr_un addr;
  int socketfd = socket(AF_UNIX, SOCK_STREAM, 0);

  if (socketfd == -1) {
    throw std::runtime_error("socketfd failed");
  }

  addr.sun_family = AF_UNIX;

  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

  addr.sun_path[sizeof(addr.sun_path) - 1] = 0;

  int l = sizeof(struct sockaddr_un);

  if (connect(socketfd, (struct sockaddr *)&addr, l) == -1) {
    close(socketfd);
    throw std::runtime_error("unable to connect");
  }

  return socketfd;
}

void IPC::startIPC() {
  // will start IPC and relay events to parseIPC

  std::thread([&]() {
    int socketfd;
    try {
      socketfd = connectToSocket();
    } catch (std::exception &e) {
      spdlog::error("Fht IPC: failed to start, reason: {}", e.what());
      return;
    }
    if (socketfd == -1) return;

    spdlog::info("Fht IPC starting");

    auto unix_istream = Gio::UnixInputStream::create(socketfd, true);
    auto unix_ostream = Gio::UnixOutputStream::create(socketfd, false);
    auto istream = Gio::DataInputStream::create(unix_istream);
    auto ostream = Gio::DataOutputStream::create(unix_ostream);

    // Subscribe to events
    Json::Value subscribeRequest;
    subscribeRequest = "subscribe";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    std::ostringstream oss;
    writer->write(subscribeRequest, &oss);
    oss << '\n';

    if (!ostream->put_string(oss.str()) || !ostream->flush()) {
      spdlog::error("Fht IPC: failed to start event stream");
      return;
    }

    // Fht compositor starts sending events immediately after subscription,
    // no acknowledgement message is sent
    spdlog::info("Fht IPC: subscription sent, listening for events");

    std::string line;
    while (istream->read_line(line)) {
      spdlog::debug("Fht IPC: received {}", line);

      try {
        parseIPC(line);
      } catch (std::exception &e) {
        spdlog::warn("Failed to parse IPC message: {}, reason: {}", line, e.what());
      } catch (...) {
        throw;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }).detach();
}

void IPC::parseIPC(const std::string &line) {
  const auto ev = parser_.parse(line);

  // Fht events have format: {"event":"event-name","data":{...}}
  if (!ev.isMember("event") || !ev.isMember("data")) {
    spdlog::warn("Fht IPC: invalid event format: {}", line);
    return;
  }

  const auto eventName = ev["event"].asString();
  const auto &payload = ev["data"];

  // Handle the event based on its type
  {
    auto lock = lockData();

    // Workspaces event
    if (eventName == "workspaces") {
      workspaces_.clear();
      
      // Extract workspaces from the Workspaces map
      for (const auto &member : payload.getMemberNames()) {
        const auto &ws = payload[member];
        Json::Value workspace(Json::objectValue);
        workspace["id"] = std::stoi(member);
        workspace["output"] = ws["output"];
        workspace["windows"] = ws["windows"];
        workspace["active_window_idx"] = ws["active-window-idx"];
        workspace["fullscreen_window_idx"] = ws["fullscreen-window-idx"];
        workspace["mwfact"] = ws["mwfact"];
        workspace["nmaster"] = ws["nmaster"];
        
        workspaces_.push_back(workspace);
      }

      // Sort workspaces by id
      std::sort(workspaces_.begin(), workspaces_.end(), [](const auto &a, const auto &b) {
        return a["id"].asInt() < b["id"].asInt();
      });
    } else if (eventName == "active-workspace-changed") {
      // Mark the workspace as active
      const auto id = payload["id"].asInt();
      for (auto &ws : workspaces_) {
        ws["is_active"] = (ws["id"].asInt() == id);
        ws["is_focused"] = (ws["id"].asInt() == id);
      }
    } else if (eventName == "workspace-changed") {
      // Update or add the workspace
      const auto id = payload["id"].asInt();
      auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
                             [id](const auto &ws) { return ws["id"].asInt() == id; });
      
      if (it != workspaces_.end()) {
        // Update existing workspace
        (*it)["output"] = payload["output"];
        (*it)["windows"] = payload["windows"];
        (*it)["active_window_idx"] = payload["active-window-idx"];
        (*it)["fullscreen_window_idx"] = payload["fullscreen-window-idx"];
        (*it)["mwfact"] = payload["mwfact"];
        (*it)["nmaster"] = payload["nmaster"];
      } else {
        // Add new workspace
        Json::Value workspace(Json::objectValue);
        workspace["id"] = id;
        workspace["output"] = payload["output"];
        workspace["windows"] = payload["windows"];
        workspace["active_window_idx"] = payload["active-window-idx"];
        workspace["fullscreen_window_idx"] = payload["fullscreen-window-idx"];
        workspace["mwfact"] = payload["mwfact"];
        workspace["nmaster"] = payload["nmaster"];
        workspaces_.push_back(workspace);

        // Re-sort
        std::sort(workspaces_.begin(), workspaces_.end(), [](const auto &a, const auto &b) {
          return a["id"].asInt() < b["id"].asInt();
        });
      }
    } else if (eventName == "workspace-removed") {
      const auto id = payload["id"].asInt();
      auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
                             [id](const auto &ws) { return ws["id"].asInt() == id; });
      if (it != workspaces_.end()) {
        workspaces_.erase(it);
      }
    } else if (eventName == "windows") {
      // Update all windows
      windows_.clear();
      
      // Extract windows from the Windows map
      for (const auto &member : payload.getMemberNames()) {
        const auto &win = payload[member];
        Json::Value window(Json::objectValue);
        window["id"] = std::stoi(member);
        window["title"] = win["title"];
        window["app_id"] = win["app-id"];
        window["workspace_id"] = win["workspace-id"];
        window["size"] = win["size"];
        window["location"] = win["location"];
        window["fullscreened"] = win["fullscreened"];
        window["maximized"] = win["maximized"];
        window["tiled"] = win["tiled"];
        window["activated"] = win["activated"];
        window["focused"] = win["focused"];
        
        windows_.push_back(window);
      }
    } else if (eventName == "window-changed") {
      // Update or add the window
      const auto id = payload["id"].asInt();
      auto it = std::find_if(windows_.begin(), windows_.end(),
                             [id](const auto &win) { return win["id"].asInt() == id; });
      
      if (it != windows_.end()) {
        // Copy all fields with kebab-case conversion
        (*it)["id"] = payload["id"];
        (*it)["title"] = payload["title"];
        (*it)["app_id"] = payload["app-id"];
        (*it)["workspace_id"] = payload["workspace-id"];
        (*it)["size"] = payload["size"];
        (*it)["location"] = payload["location"];
        (*it)["fullscreened"] = payload["fullscreened"];
        (*it)["maximized"] = payload["maximized"];
        (*it)["tiled"] = payload["tiled"];
        (*it)["activated"] = payload["activated"];
        (*it)["focused"] = payload["focused"];
      } else {
        Json::Value window(Json::objectValue);
        window["id"] = payload["id"];
        window["title"] = payload["title"];
        window["app_id"] = payload["app-id"];
        window["workspace_id"] = payload["workspace-id"];
        window["size"] = payload["size"];
        window["location"] = payload["location"];
        window["fullscreened"] = payload["fullscreened"];
        window["maximized"] = payload["maximized"];
        window["tiled"] = payload["tiled"];
        window["activated"] = payload["activated"];
        window["focused"] = payload["focused"];
        windows_.push_back(window);
      }
    } else if (eventName == "window-closed") {
      const auto id = payload["id"].asInt();
      auto it = std::find_if(windows_.begin(), windows_.end(),
                             [id](const auto &win) { return win["id"].asInt() == id; });
      if (it != windows_.end()) {
        windows_.erase(it);
      }
    } else if (eventName == "window-focused") {
      const auto id = payload["id"];
      const auto focused = !id.isNull();
      for (auto &win : windows_) {
        if (focused) {
          win["focused"] = (win["id"].asInt() == id.asInt());
        } else {
          win["focused"] = false;
        }
      }
    }
  }

  // Notify event handlers
  std::unique_lock lock(callbackMutex_);

  for (auto &[name, handler] : callbacks_) {
    if (name == eventName) {
      handler->onEvent(ev);
    }
  }
}

void IPC::registerForIPC(const std::string &ev, EventHandler *ev_handler) {
  if (ev_handler == nullptr) {
    return;
  }

  std::unique_lock lock(callbackMutex_);
  callbacks_.emplace_back(ev, ev_handler);
}

void IPC::unregisterForIPC(EventHandler *ev_handler) {
  if (ev_handler == nullptr) {
    return;
  }

  std::unique_lock lock(callbackMutex_);

  for (auto it = callbacks_.begin(); it != callbacks_.end();) {
    auto &[eventname, handler] = *it;
    if (handler == ev_handler) {
      it = callbacks_.erase(it);
    } else {
      ++it;
    }
  }
}

Json::Value IPC::send(const Json::Value &request) {
  int socketfd = connectToSocket();
  if (socketfd == -1) throw std::runtime_error("Fht is not running");

  auto unix_istream = Gio::UnixInputStream::create(socketfd, true);
  auto unix_ostream = Gio::UnixOutputStream::create(socketfd, false);
  auto istream = Gio::DataInputStream::create(unix_istream);
  auto ostream = Gio::DataOutputStream::create(unix_ostream);

  // Fht needs the request on a single line.
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
  std::ostringstream oss;
  writer->write(request, &oss);
  oss << '\n';

  if (!ostream->put_string(oss.str()) || !ostream->flush())
    throw std::runtime_error("error writing to fht socket");

  std::string line;
  if (!istream->read_line(line)) throw std::runtime_error("error reading from fht socket");

  std::istringstream iss(std::move(line));
  Json::Value response;
  iss >> response;
  return response;
}

}  // namespace waybar::modules::fht
