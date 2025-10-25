#include "modules/fht/workspaces.hpp"

#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <spdlog/spdlog.h>

namespace waybar::modules::fht {

Workspaces::Workspaces(const std::string &id, const Bar &bar, const Json::Value &config)
    : AModule(config, "workspaces", id, false, false), bar_(bar), box_(bar.orientation, 0) {
  box_.set_name("workspaces");
  if (!id.empty()) {
    box_.get_style_context()->add_class(id);
  }
  box_.get_style_context()->add_class(MODULE_CLASS);
  event_box_.add(box_);

  if (!gIPC) gIPC = std::make_unique<IPC>();

  gIPC->registerForIPC("workspaces", this);
  gIPC->registerForIPC("active-workspace-changed", this);
  gIPC->registerForIPC("workspace-changed", this);
  gIPC->registerForIPC("workspace-removed", this);

  dp.emit();
}

Workspaces::~Workspaces() { gIPC->unregisterForIPC(this); }

void Workspaces::onEvent(const Json::Value &ev) { dp.emit(); }

void Workspaces::doUpdate() {
  auto ipcLock = gIPC->lockData();

  const auto alloutputs = config_["all-outputs"].asBool();
  std::vector<Json::Value> my_workspaces;
  const auto &workspaces = gIPC->workspaces();
  std::copy_if(workspaces.cbegin(), workspaces.cend(), std::back_inserter(my_workspaces),
               [&](const auto &ws) {
                 if (alloutputs) return true;
                 return ws["output"].asString() == bar_.output->name;
               });

  // Remove buttons for removed workspaces.
  for (auto it = buttons_.begin(); it != buttons_.end();) {
    auto ws = std::find_if(my_workspaces.begin(), my_workspaces.end(),
                           [it](const auto &ws) { return ws["id"].asInt() == it->first; });
    if (ws == my_workspaces.end()) {
      it = buttons_.erase(it);
    } else {
      ++it;
    }
  }

  // Add buttons for new workspaces, update existing ones.
  for (const auto &ws : my_workspaces) {
    auto bit = buttons_.find(ws["id"].asInt());
    auto &button = bit == buttons_.end() ? addButton(ws) : bit->second;
    auto style_context = button.get_style_context();

    if (ws["is_focused"].asBool())
      style_context->add_class("focused");
    else
      style_context->remove_class("focused");

    if (ws["is_active"].asBool())
      style_context->add_class("active");
    else
      style_context->remove_class("active");

    if (ws["output"]) {
      if (ws["output"].asString() == bar_.output->name)
        style_context->add_class("current_output");
      else
        style_context->remove_class("current_output");
    } else {
      style_context->remove_class("current_output");
    }

    if (ws["windows"].empty())
      style_context->add_class("empty");
    else
      style_context->remove_class("empty");

    std::string name = std::to_string(ws["id"].asInt());
    button.set_name("fht-workspace-" + name);

    if (config_["format"].isString()) {
      auto format = config_["format"].asString();
      name = fmt::format(fmt::runtime(format), fmt::arg("icon", getIcon(name, ws)),
                         fmt::arg("value", name), fmt::arg("id", ws["id"].asInt()),
                         fmt::arg("output", ws["output"].asString()));
    }
    if (!config_["disable-markup"].asBool()) {
      static_cast<Gtk::Label *>(button.get_children()[0])->set_markup(name);
    } else {
      button.set_label(name);
    }

    if (config_["current-only"].asBool()) {
      const auto *property = alloutputs ? "is_focused" : "is_active";
      if (ws[property].asBool())
        button.show();
      else
        button.hide();
    } else {
      button.show();
    }
  }

  // Refresh the button order.
  for (size_t i = 0; i < my_workspaces.size(); ++i) {
    const auto &ws = my_workspaces[i];
    auto &button = buttons_[ws["id"].asInt()];
    box_.reorder_child(button, i);
  }
}

void Workspaces::update() {
  doUpdate();
  AModule::update();
}

Gtk::Button &Workspaces::addButton(const Json::Value &ws) {
  std::string name = std::to_string(ws["id"].asInt());

  auto pair = buttons_.emplace(ws["id"].asInt(), name);
  auto &&button = pair.first->second;
  box_.pack_start(button, false, false, 0);
  button.set_relief(Gtk::RELIEF_NONE);
  if (!config_["disable-click"].asBool()) {
    const auto id = ws["id"].asInt();
    button.signal_pressed().connect([=] {
      try {
        // {"action":{"focus-workspace":{"workspace-id":1}}}
        Json::Value request(Json::objectValue);
        auto &action = (request["action"] = Json::Value(Json::objectValue));
        auto &focusWorkspace = (action["focus-workspace"] = Json::Value(Json::objectValue));
        focusWorkspace["workspace-id"] = id;

        IPC::send(request);
      } catch (const std::exception &e) {
        spdlog::error("Error switching workspace: {}", e.what());
      }
    });
  }
  return button;
}

std::string Workspaces::getIcon(const std::string &value, const Json::Value &ws) {
  const auto &icons = config_["format-icons"];
  if (!icons) return value;

  if (ws["windows"].empty() && icons["empty"]) return icons["empty"].asString();

  if (ws["is_focused"].asBool() && icons["focused"]) return icons["focused"].asString();

  if (ws["is_active"].asBool() && icons["active"]) return icons["active"].asString();

  const auto id = std::to_string(ws["id"].asInt());
  if (icons[id]) return icons[id].asString();

  if (icons["default"]) return icons["default"].asString();

  return value;
}

}  // namespace waybar::modules::fht
