#include "modules/fht/window.hpp"

#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <spdlog/spdlog.h>

#include "util/rewrite_string.hpp"
#include "util/sanitize_str.hpp"

namespace waybar::modules::fht {

Window::Window(const std::string &id, const Bar &bar, const Json::Value &config)
    : AAppIconLabel(config, "window", id, "{title}", 0, true), bar_(bar) {
  if (!gIPC) gIPC = std::make_unique<IPC>();

  gIPC->registerForIPC("windows", this);
  gIPC->registerForIPC("window-changed", this);
  gIPC->registerForIPC("window-closed", this);
  gIPC->registerForIPC("window-focused", this);
  gIPC->registerForIPC("workspace-changed", this);

  dp.emit();
}

Window::~Window() { gIPC->unregisterForIPC(this); }

void Window::onEvent(const Json::Value &ev) { dp.emit(); }

void Window::doUpdate() {
  auto ipcLock = gIPC->lockData();

  const auto &windows = gIPC->windows();
  const auto &workspaces = gIPC->workspaces();

  const auto separateOutputs = config_["separate-outputs"].asBool();
  const auto ws_it = std::find_if(workspaces.cbegin(), workspaces.cend(), [&](const auto &ws) {
    if (separateOutputs) {
      return ws["is_active"].asBool() && ws["output"].asString() == bar_.output->name;
    }

    return ws["is_focused"].asBool();
  });

  std::vector<Json::Value>::const_iterator it = windows.cend();
  if (ws_it != workspaces.cend() && !(*ws_it)["active_window_idx"].isNull()) {
    const auto activeWindowIdx = (*ws_it)["active_window_idx"].asInt();
    const auto &wsWindows = (*ws_it)["windows"];
    
    if (activeWindowIdx >= 0 && activeWindowIdx < static_cast<int>(wsWindows.size())) {
      const auto windowId = wsWindows[activeWindowIdx].asInt();
      it = std::find_if(windows.cbegin(), windows.cend(),
                        [windowId](const auto &win) { return win["id"].asInt() == windowId; });
    }
  }

  setClass("empty", it == windows.cend());

  if (it != windows.cend()) {
    const auto &window = *it;

    const auto title = window["title"].isString() ? window["title"].asString() : "";
    const auto appId = window["app_id"].isString() ? window["app_id"].asString() : "";
    const auto sanitizedTitle = waybar::util::sanitize_string(title);
    const auto sanitizedAppId = waybar::util::sanitize_string(appId);

    label_.show();
    label_.set_markup(waybar::util::rewriteString(
        fmt::format(fmt::runtime(format_), fmt::arg("title", sanitizedTitle),
                    fmt::arg("app_id", sanitizedAppId)),
        config_["rewrite"]));

    updateAppIconName(appId, "");

    if (tooltipEnabled()) label_.set_tooltip_text(title);

    const auto id = window["id"].asInt();
    const auto workspaceId = window["workspace_id"].asInt();
    const auto isSolo = std::none_of(windows.cbegin(), windows.cend(), [&](const auto &win) {
      return win["id"].asInt() != id && win["workspace_id"].asInt() == workspaceId;
    });
    setClass("solo", isSolo);
    if (!appId.empty()) setClass(appId, isSolo);

    if (oldAppId_ != appId) {
      if (!oldAppId_.empty()) setClass(oldAppId_, false);
      oldAppId_ = appId;
    }
  } else {
    label_.hide();
    updateAppIconName("", "");
    setClass("solo", false);
    if (!oldAppId_.empty()) setClass(oldAppId_, false);
    oldAppId_.clear();
  }
}

void Window::update() {
  doUpdate();
  AAppIconLabel::update();
}

void Window::setClass(const std::string &className, bool enable) {
  auto styleContext = bar_.window.get_style_context();
  if (enable) {
    if (!styleContext->has_class(className)) {
      styleContext->add_class(className);
    }
  } else {
    styleContext->remove_class(className);
  }
}

}  // namespace waybar::modules::fht
