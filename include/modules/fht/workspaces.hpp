#pragma once

#include <gtkmm/button.h>
#include <json/value.h>

#include "AModule.hpp"
#include "bar.hpp"
#include "modules/fht/backend.hpp"

namespace waybar::modules::fht {

class Workspaces : public AModule, public EventHandler {
 public:
  Workspaces(const std::string &, const Bar &, const Json::Value &);
  ~Workspaces() override;
  void update() override;

 private:
  void onEvent(const Json::Value &ev) override;
  void doUpdate();
  Gtk::Button &addButton(const Json::Value &ws);
  std::string getIcon(const std::string &value, const Json::Value &ws);

  const Bar &bar_;
  Gtk::Box box_;
  // Map from fht workspace id to button.
  std::unordered_map<int, Gtk::Button> buttons_;
};

}  // namespace waybar::modules::fht
